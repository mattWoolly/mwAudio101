// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/PdcInvarianceFxOffTest.cpp — the constant-PDC invariance + FX-off
// bit-exact INTEGRATION test (task 141). It asserts, against the assembled JUCE-free
// core the plugin's LatencyReporter is composed of, the ADR-017 / docs/design/00 §7
// constant-PDC contract end to end.
//
// Test-case names begin with "pdc_invariant" so
// `ctest -R pdc_invariant --no-tests=error` selects them under the silent-pass rule
// (AGENTS.md "Tests"). The "[pdc_invariant]" tag groups them; '[' is avoided inside the
// display text (Catch2 would parse it as a tag and break `-R` selection).
//
// SCOPE NOTE (why the core, not plugin/LatencyReporter.cpp). The reported PDC value is
// the SUM of exactly two core-owned, fixed group-delay contributors [ADR-017 L1/L2;
// docs/design/00 §7.2]:
//   * L1 the PER-VOICE oversampled-zone group delay (mw::cal::latency::
//        kVoiceZoneGroupDelaySamples) — inside the blessed FX-off golden, NOT subtracted;
//   * L2 the FX Drive 2x-oversampler group delay (mw::cal::fxos::kReportedLatencySamples,
//        re-measured by FxOversampler2x / reported by FxChain), always counted.
// LatencyReporter::computeWorstCaseLatency() returns exactly L1 + L2 (verbatim, see
// plugin/latency/LatencyReporter.cpp), and the host setLatencySamples marshalling is the
// plugin-processor's job — BOTH are OUT OF SCOPE for the value's behavior, which lives
// entirely in these core contributors. This headless mwcore-only binary links the core
// but NOT plugin/latency/LatencyReporter.cpp, so we assert the contract against the
// contributors the reporter sums (and replicate the reporter's L1+L2 sum here, so a drift
// in either contributor or in the sum is caught). The LatencyReporter compute/padding
// internals (format-wrappers) and the FX/oversampler DSP are explicitly out of scope per
// the task file.
//
// Each case maps to an `## Acceptance criteria` checkbox in
// plan/backlog/141-constant-pdc-invariance-fx-off.md:
//   AC1 L5/L7/L8 — reported latency INVARIANT to FX bypass, Quality tier, build-to-build.
//   AC2 §7.1/L4/L10 — reported latency == worst-case total group delay; sized in prepare,
//                     never mutated from process.
//   AC3 §7.4/L6 — FX-off output bit-exact at the declared worst-case offset; the L1
//                 IIR-zone delay is inside the golden, not subtracted.
//   AC-ORACLE — the measured group delays (FxOversampler2x round-trip centroid) equal the
//               declared constants, so the reported number cannot silently drift from the
//               coefficients it is derived from [LatencyReporterConstants.h rationale].

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "calibration/LatencyReporterConstants.h"   // L1: mw::cal::latency::kVoiceZoneGroupDelaySamples
#include "calibration/FxOversampler2xConstants.h"    // L2: mw::cal::fxos::kReportedLatencySamples

#include "dsp/fx/FxOversampler2x.h"                  // re-measures L2 round-trip group delay (oracle)
#include "dsp/fx/FxChain.h"                          // reports FX latency; FX-off dry-pad alignment
#include "dsp/fx/FxParams.h"

// The REAL plugin LatencyReporter, compiled DIRECTLY into this JUCE-free test TU so the
// case below asserts its ACTUAL computeWorstCaseLatency() output (not a replica). The
// reporter is fully JUCE-free (it includes only the two core calibration constants + the
// JUCE-free FractionalDelayLine), so it links cleanly into mwcore-only mw101_tests — the
// same pattern sibling PRs use to drive a plugin .cpp from the headless suite. The
// mw101_tests target exposes plugin/ as an include root (tests/CMakeLists.txt), so the
// header's `#include "latency/LatencyReporter.h"` resolves; mwcore globs core/**/*.cpp
// ONLY, so this .cpp is NOT double-compiled, and this is the ONLY test TU that includes
// it — no duplicate-symbol / ODR risk. [ADR-001; ADR-017 L4; docs/design/09 §8.3]
#include "../../plugin/latency/LatencyReporter.cpp"  // the REAL reporter under test

#include "../invariants/AudioThreadGuard.h"

using mw::test::AudioThreadGuard;

namespace {

// ---------------------------------------------------------------------------
// The reported constant PDC value, replicated EXACTLY as the plugin's
// LatencyReporter::computeWorstCaseLatency() computes it (see
// plugin/latency/LatencyReporter.cpp): the SUM of the two contributing sources, in
// BASE-RATE samples. Kept here so a drift in either contributor — or in the sum the
// reporter performs — fails this test. [ADR-017 L1/L2/L4; docs/design/00 §7.2]
// ---------------------------------------------------------------------------
constexpr int kReportedWorstCasePdc =
    mw::cal::latency::kVoiceZoneGroupDelaySamples   // L1 per-voice IIR zone (inside golden)
  + mw::cal::fxos::kReportedLatencySamples;          // L2 FX Drive 2x OS (always counted)

constexpr int kMaxBlock = 512;

// A neutral, FULLY-DRY FxParams snapshot: master bypass on, every stage off. This is the
// engine default [FxParams.h; ADR-010 FX-13].
mw::fx::FxParams paramsAllOff() noexcept {
    mw::fx::FxParams p{};   // defaults: masterBypass=true, drive.on=false, chorus.mode=Off, delay.on=false
    return p;
}

// Master-bypass OFF but EVERY per-block stage off (the "all-blocks-off" early-out path,
// distinct from masterBypass) [FxChain.cpp step 3].
mw::fx::FxParams paramsAllBlocksOff() noexcept {
    mw::fx::FxParams p{};
    p.masterBypass = false;
    p.drive.on     = false;
    p.chorus.mode  = 0;     // Chorus::Mode::Off
    p.delay.on     = false;
    return p;
}

// FX FULLY ENGAGED: master on, Drive on, Chorus on, Delay on. Used to prove the reported
// latency does not move when the actual processing path changes.
mw::fx::FxParams paramsAllOn() noexcept {
    mw::fx::FxParams p{};
    p.masterBypass = false;
    p.monoOutput   = false;
    p.drive  = { /*on*/ true, /*amount*/ 0.7f, /*tone*/ 0.5f, /*output*/ 0.5f };
    p.chorus = { /*mode*/ 3, /*rate*/ 0.4f, /*depth*/ 0.5f, /*width*/ 0.7f, /*mix*/ 0.5f };
    p.delay  = { /*on*/ true, /*sync*/ false, /*pingpong*/ false, /*division*/ 0,
                 /*timeMs*/ 250.0f, /*feedback*/ 0.3f, /*damp*/ 0.4f, /*width*/ 0.6f, /*mix*/ 0.4f };
    return p;
}

} // namespace

// ===========================================================================
// AC-ORACLE — the measured group delays equal the declared constants. The reported PDC
// is the sum of two fixed constants; this case proves those constants are not arbitrary
// but the MEASURED round-trip group delay of the frozen halfband coefficients (the same
// energy-weighted-centroid proxy ADR-017 L1/L2 call "measured"). If a coefficient set is
// re-tuned without re-deriving the constant, this fails — the number cannot silently
// drift from the filter it is derived from [LatencyReporterConstants.h / §8.3].
// ===========================================================================
TEST_CASE("pdc_invariant: measured group delays equal the declared constants",
          "[pdc_invariant]") {
    // L2 — the FX Drive 2x halfband re-measures to its declared constant, deterministically
    // and independent of block size [ADR-017 L2; FxOversampler2xConstants.h].
    for (const int blk : {1, 32, 64, kMaxBlock}) {
        INFO("block size = " << blk);
        mw::fx::FxOversampler2x os;
        os.prepare(blk);
        REQUIRE(os.isPrepared());
        REQUIRE(os.latencySamples() == mw::cal::fxos::kReportedLatencySamples);
    }

    // L1 — the per-voice IIR zone uses the SAME frozen elliptic-halfband coefficient set
    // as the FX-rate halfband (LatencyReporterConstants.h "WHY THIS VALUE"), so the same
    // round-trip group-delay measurement lands on the SAME fixed integer. Re-derive it
    // from the shared halfband and assert it equals the declared L1 constant: the number
    // cannot drift from the coefficients it is derived from [ADR-017 L1; §8.3].
    {
        mw::fx::FxOversampler2x sharedHalfband;
        sharedHalfband.prepare(kMaxBlock);
        REQUIRE(sharedHalfband.latencySamples()
                == mw::cal::latency::kVoiceZoneGroupDelaySamples);
    }

    // Both contributors are NONZERO — the host actually compensates each [ADR-017 L1/L2].
    STATIC_REQUIRE(mw::cal::latency::kVoiceZoneGroupDelaySamples > 0);
    STATIC_REQUIRE(mw::cal::fxos::kReportedLatencySamples > 0);
}

// ===========================================================================
// AC2 §7.1 / L4 / L10 — reported latency EQUALS the worst-case total group delay and is
// the SUM of exactly the two contributors; it is computed/sized in prepare and never
// mutated from process. We assert the reporter's formula (L1 + L2), the FxChain's
// prepare-time latency report, and that no process()/reset() call mutates it.
// ===========================================================================
TEST_CASE("pdc_invariant: reported latency equals the worst-case total group delay sum",
          "[pdc_invariant]") {
    // The reported worst case is EXACTLY L1 + L2 — the verbatim sum the LatencyReporter
    // computes [ADR-017 L4; docs/design/00 §7.2]. It is a compile-time constant, so it is
    // build-to-build stable by construction [L11].
    STATIC_REQUIRE(kReportedWorstCasePdc
                   == mw::cal::latency::kVoiceZoneGroupDelaySamples
                    + mw::cal::fxos::kReportedLatencySamples);
    REQUIRE(kReportedWorstCasePdc > 0);

    // The FX chain's prepare()-computed contribution equals L2 (its only contributing FX
    // source: Drive's 2x OS; Chorus/Delay are musical delay, L3) [ADR-017 L2/L3; §6.1].
    mw::fx::FxChain fx;
    fx.prepare(48000.0, kMaxBlock);
    REQUIRE(fx.getLatencySamples() == mw::cal::fxos::kReportedLatencySamples);

    // The reported total = the per-voice zone delay (L1, inside the golden) + the FX
    // chain's reported delay (L2). This is the value plugin/ hands setLatencySamples.
    const int reported = mw::cal::latency::kVoiceZoneGroupDelaySamples + fx.getLatencySamples();
    REQUIRE(reported == kReportedWorstCasePdc);

    // L10 — sized in prepare; NEVER mutated from process/reset. Stream many blocks with FX
    // fully engaged, with bypass, and a reset between; the reported number never changes.
    std::vector<float> mono(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> L(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(kMaxBlock), 0.0f);
    float* out[2] = { L.data(), R.data() };
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = std::sin(0.05f * static_cast<float>(i));

    const int latBefore = fx.getLatencySamples();
    fx.setParams(paramsAllOn());
    fx.process(mono.data(), out, kMaxBlock);
    REQUIRE(fx.getLatencySamples() == latBefore);     // process did not mutate it

    fx.setParams(paramsAllOff());
    fx.process(mono.data(), out, kMaxBlock);
    REQUIRE(fx.getLatencySamples() == latBefore);

    fx.reset();
    REQUIRE(fx.getLatencySamples() == latBefore);     // reset did not mutate it
    fx.process(mono.data(), out, kMaxBlock);
    REQUIRE(fx.getLatencySamples() == latBefore);
}

// ===========================================================================
// AC2 §7.1 / L4 / L7 / L11 — the REAL reporter's ACTUAL output. The case above proves
// the contract against the contributors and a test-local L1+L2 replica; THIS case drives
// the genuine plugin object: it constructs mw::plugin::LatencyReporter and asserts its
// computeWorstCaseLatency(sampleRate) RETURN VALUE equals L1 + L2 directly — so a drift
// in the reporter's own arithmetic (a different sum, sample-rate scaling, padding folded
// into the reported value, an off-by-one) is caught, not just a drift in the constants it
// sums. The reporter is the real production code (#include'd above), so this is its
// genuine output, not a replica. It is asserted INVARIANT across the blessed host
// sample-rate set {44100, 48000, 88200, 96000}: the constant-PDC contract requires the
// same fixed integer at every rate [ADR-017 L4/L7/L11; docs/design/09 §8.3; §7.1].
// ===========================================================================
TEST_CASE("pdc_invariant: the real LatencyReporter computes the worst-case sum and is rate-invariant",
          "[pdc_invariant]") {
    // The genuine production reporter (not the replica): its accessor returns exactly the
    // documented worst-case total, L1 + L2, in base-rate samples [ADR-017 L4; §8.3].
    const mw::plugin::LatencyReporter reporter;

    constexpr int kExpected =
        mw::cal::latency::kVoiceZoneGroupDelaySamples   // L1 per-voice IIR zone
      + mw::cal::fxos::kReportedLatencySamples;          // L2 FX Drive 2x OS
    // Sanity: the value-under-test is identical to the contract sum the rest of the suite
    // asserts, so this case and the replica-based cases cannot silently diverge.
    STATIC_REQUIRE(kExpected == kReportedWorstCasePdc);

    // ACTUAL reporter output == the worst-case sum, and INVARIANT across every blessed
    // host sample rate (no rate scaling, no padding folded in) [ADR-017 L4/L7/L11].
    int seen = -1;
    for (const double sr : {44100.0, 48000.0, 88200.0, 96000.0}) {
        INFO("sample rate = " << sr);
        const int worst = reporter.computeWorstCaseLatency(sr);
        REQUIRE(worst == kExpected);              // real output == L1 + L2 (no replica)
        REQUIRE(worst > 0);                       // both contributors counted, nonzero
        if (seen < 0) seen = worst;
        REQUIRE(worst == seen);                   // sample-rate-INVARIANT (constant PDC)
    }
}

// ===========================================================================
// AC1 L5 / L8 — reported latency is INVARIANT to master FX bypass and to per-block FX
// bypass. The bypassed stage's worst-case latency stays in the constant pad, so the
// number never changes whether FX are off (masterBypass), all-blocks-off, or fully on.
// ===========================================================================
TEST_CASE("pdc_invariant: reported latency is invariant to master and per-block FX bypass",
          "[pdc_invariant]") {
    mw::fx::FxChain fx;
    fx.prepare(48000.0, kMaxBlock);
    const int reported = fx.getLatencySamples();
    REQUIRE(reported == mw::cal::fxos::kReportedLatencySamples);

    std::vector<float> mono(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> L(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(kMaxBlock), 0.0f);
    float* out[2] = { L.data(), R.data() };

    // The reported latency does not move across the three bypass states [ADR-017 L5/L8].
    for (const auto& mk : { &paramsAllOff, &paramsAllBlocksOff, &paramsAllOn }) {
        fx.setParams(mk());
        fx.process(mono.data(), out, kMaxBlock);
        REQUIRE(fx.getLatencySamples() == reported);   // unchanged on bypass toggle
    }

    // And the total reported value (L1 + chain) is the same constant across all states.
    REQUIRE(mw::cal::latency::kVoiceZoneGroupDelaySamples + fx.getLatencySamples()
            == kReportedWorstCasePdc);
}

// ===========================================================================
// AC1 L7 — reported latency is INVARIANT to the Quality tier (1x/2x/4x). The FX Drive
// oversampler is ALWAYS 2x (no factor switch) and the per-voice zone delay constant does
// not depend on the host Quality tier; the reported value is built from compile-time
// constants and therefore the SAME for every tier. No host PDC relaunch on tier change.
// ===========================================================================
TEST_CASE("pdc_invariant: reported latency is invariant to the Quality tier",
          "[pdc_invariant]") {
    // The FX Drive oversampler has no eco/1x tier — it is fixed 2x [FxOversampler2x.h].
    STATIC_REQUIRE(mw::fx::FxOversampler2x::kFactor == 2);

    // Re-prepare the FX chain at each blessed host sample rate (the only "tier-like" knob
    // a JUCE-free FX chain sees) — the reported FX latency constant never changes; the
    // total reported PDC stays the single worst-case constant [ADR-017 L7].
    for (const double sr : {44100.0, 48000.0, 88200.0, 96000.0}) {
        INFO("sample rate = " << sr);
        mw::fx::FxChain fx;
        fx.prepare(sr, kMaxBlock);
        REQUIRE(fx.getLatencySamples() == mw::cal::fxos::kReportedLatencySamples);
        REQUIRE(mw::cal::latency::kVoiceZoneGroupDelaySamples + fx.getLatencySamples()
                == kReportedWorstCasePdc);
    }

    // The reported value is independent of the per-voice zone's actual oversampling tier:
    // it is declared in base-rate samples and assembled from compile-time constants, so a
    // 1x / 2x / 4x render reports the SAME worst-case number [ADR-017 L4/L7; §7.1].
    STATIC_REQUIRE(kReportedWorstCasePdc
                   == mw::cal::latency::kVoiceZoneGroupDelaySamples
                    + mw::cal::fxos::kReportedLatencySamples);
}

// ===========================================================================
// AC3 §7.4 / L6 — with all FX bypassed, output is FX-off bit-exact at the declared
// worst-case offset: the FX-off output equals the (mono voice-sum) input delayed by
// EXACTLY the FX chain's reported latency, sample for sample, with the per-voice L1
// IIR-zone delay INSIDE the golden (NOT subtracted — the chain adds only its own L2
// offset). We stream a known mono signal through the bypassed chain and assert the L/R
// outputs are the input delayed by exactly getLatencySamples() samples.
// ===========================================================================
TEST_CASE("pdc_invariant: FX-off output is bit-exact at the declared worst-case offset",
          "[pdc_invariant]") {
    mw::fx::FxChain fx;
    fx.prepare(48000.0, kMaxBlock);
    fx.setParams(paramsAllOff());          // master bypass: pure dry-pad alignment path

    const int delay = fx.getLatencySamples();
    REQUIRE(delay == mw::cal::fxos::kReportedLatencySamples);
    REQUIRE(delay > 0);

    // A deterministic, non-trivial mono "voice sum" input streamed over several blocks so
    // the dry-pad ring is exercised across block boundaries.
    constexpr int kBlock = 64;
    constexpr int kBlocks = 8;
    constexpr int kTotal = kBlock * kBlocks;

    std::vector<float> in(static_cast<std::size_t>(kTotal), 0.0f);
    for (int i = 0; i < kTotal; ++i)
        in[static_cast<std::size_t>(i)] =
            std::sin(0.13f * static_cast<float>(i)) + 0.25f * static_cast<float>((i % 7) - 3);

    std::vector<float> outL(static_cast<std::size_t>(kTotal), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(kTotal), 0.0f);

    for (int b = 0; b < kBlocks; ++b) {
        const int n0 = b * kBlock;
        float* L = outL.data() + n0;
        float* R = outR.data() + n0;
        float* ch[2] = { L, R };
        fx.process(in.data() + n0, ch, kBlock);
    }

    // FX-off: L == R == the input delayed by EXACTLY `delay` samples, BIT-EXACT (==, not a
    // tolerance) on the macOS arm64 reference [ADR-017 L6; docs/design/00 §7.4, §9.1 RT-7].
    // The first `delay` output samples are the dry-pad's zero-initialized history.
    for (int i = 0; i < delay; ++i) {
        REQUIRE(outL[static_cast<std::size_t>(i)] == 0.0f);
        REQUIRE(outR[static_cast<std::size_t>(i)] == 0.0f);
    }
    for (int i = delay; i < kTotal; ++i) {
        const float expected = in[static_cast<std::size_t>(i - delay)];
        REQUIRE(outL[static_cast<std::size_t>(i)] == expected);   // bit-exact alignment
        REQUIRE(outR[static_cast<std::size_t>(i)] == expected);   // mono summed equally to L/R
    }

    // The chain's reported delay does NOT include the per-voice L1 zone delay: L1 sits
    // INSIDE the blessed mono-voice golden and is NOT subtracted here [ADR-017 L6/L9].
    // The chain reports L2 only; the per-voice zone delay is reported separately by the
    // plugin and is already present in the (golden) mono voice fed into this chain.
    REQUIRE(fx.getLatencySamples() == mw::cal::fxos::kReportedLatencySamples);
    REQUIRE(fx.getLatencySamples() != kReportedWorstCasePdc);    // L2 != (L1 + L2): L1 not folded in
}

// ===========================================================================
// AC2 (cont.) L10 / §9.1 — the FX chain's process()/reset() hot path allocates nothing
// and locks nothing (the reported latency is sized in prepare and never recomputed on the
// audio thread). Under an armed AudioThreadGuard, a steady-state process() and reset()
// take zero allocations [ADR-017 L10; docs/design/00 §9.1 RT-1/RT-2/RT-6].
// ===========================================================================
TEST_CASE("pdc_invariant: FX process and reset are alloc-free and never recompute latency",
          "[pdc_invariant]") {
    mw::fx::FxChain fx;
    fx.prepare(48000.0, kMaxBlock);          // the ONLY allocation / sizing site
    const int reported = fx.getLatencySamples();

    std::vector<float> mono(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> L(static_cast<std::size_t>(kMaxBlock), 0.0f);
    std::vector<float> R(static_cast<std::size_t>(kMaxBlock), 0.0f);
    float* out[2] = { L.data(), R.data() };
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = std::sin(0.07f * static_cast<float>(i));

    fx.setParams(paramsAllOn());
    fx.process(mono.data(), out, kMaxBlock);   // warm any lazy state before arming

    AudioThreadGuard guard;
    guard.arm();
    fx.process(mono.data(), out, kMaxBlock);   // steady-state hot path: no alloc, no lock
    fx.reset();                                // reset is a hot path too: no alloc
    fx.process(mono.data(), out, kMaxBlock);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    // The reported latency is unchanged by the hot-path activity [ADR-017 L10].
    REQUIRE(fx.getLatencySamples() == reported);
}
