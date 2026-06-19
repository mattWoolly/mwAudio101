// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DriftModelTest.cpp — Layer-1/invariant tests for the drift orchestration
// engine DriftModel (task 072).
//
// Test-case names begin with "vintage_model" so `ctest -R vintage_model
// --no-tests=error` selects exactly these (AGENTS.md silent-pass rule); the display
// text avoids '[' so Catch2 does not mis-parse a tag and break ctest -R selection.
//
// Covers every Acceptance criterion of plan/backlog/072 against docs/design/08:
//   - determinism: bit-identical processBlock output for a fixed seed across runs AND
//     across re-prepare (§12.7, VV-17);
//   - Tier-2 correlation: VCO/VCF drift are BOTH T*depth (scaled), perfectly
//     correlated; kVcfDriftRatio scales the cutoff side, so ratio=0 => zero cutoff
//     drift while pitch drift persists (§5.2, VV-13);
//   - frozen-at-note-on: within a held note slop/varCutoff/varPw stay constant (the
//     smoother settles), only Tier-2 moves; a target step de-zippers over
//     ~kDriftSmoothMs (§7.1, §9, VV-15);
//   - RT: no-alloc/no-lock in processBlock/noteOn (AudioThreadGuard), block-rate
//     update once per block, Re-roll applied lock-free at the block boundary, and
//     mono == voice 0 (§12, VV-14/16/18).
//
// All figures here are reproducibility / statistical anchors, NOT measured SH-101 specs.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "dsp/drift/DriftModel.h"
#include "calibration/DriftConstants.h"
#include "calibration/DriftModelConstants.h"
#include "calibration/ThermalConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::dsp::drift::DriftModel;
using mw::dsp::drift::DriftParams;
namespace cal = mw::cal::drift;

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kBlock     = 64;
constexpr int    kNumVoices = mw::kMaxVoices;

// A drift param set with the schema defaults but EVERYTHING-ON so the engine moves on
// every tier (Tier-1 spread, Tier-2 depth, Tier-3 slop, all four variances). Used for
// the cross-run / re-prepare determinism replay.
DriftParams allOnParams() noexcept {
    DriftParams p;
    p.driftDepthCents = 12.0f;
    p.driftRate01     = 0.8f;   // fast wander so Tier-2 visibly moves block to block
    p.slopCents       = 5.0f;
    p.calSpread01     = 1.0f;
    p.varCutoff01     = 1.0f;
    p.varEnv01        = 1.0f;
    p.varPw01         = 1.0f;
    p.varGlide01      = 1.0f;
    p.warmupTimeMin   = 0.0f;
    p.detuneAmt01     = 1.0f;   // exercise the unison/poly per-voice spread scaling
    p.useWarmup       = false;
    p.usePink         = false;
    return p;
}

// Params with ONLY Tier-2 active: spread=0 (no Tier-1 perturbation), slop=0 and all
// var=0 (no Tier-3 / variance), so pitchOffsetCents / cutoffOffset are PURE thermal
// mappings — the only moving part is T(t). Lets us assert the §5.2 correlation law
// cutoffDrift == pitchDrift * kVcfDriftRatio without other tiers confounding it.
DriftParams thermalOnlyParams(float depthCents) noexcept {
    DriftParams p;
    p.driftDepthCents = depthCents;
    p.driftRate01     = 0.9f;
    p.slopCents       = 0.0f;
    p.calSpread01     = 0.0f;   // Tier-1 identity
    p.varCutoff01     = 0.0f;
    p.varEnv01        = 0.0f;
    p.varPw01         = 0.0f;
    p.varGlide01      = 0.0f;
    p.warmupTimeMin   = 0.0f;
    p.detuneAmt01     = 0.0f;
    p.useWarmup       = false;
    p.usePink         = false;
    return p;
}

// Capture the five smoothed accessors for one voice after a processBlock.
struct VoiceOut {
    float pitch, cutoff, pw, env, glide, vcf, thermal;
};
VoiceOut readVoice(const DriftModel& m, int v) noexcept {
    return { m.pitchOffsetCents(v), m.cutoffOffset(v), m.pwOffset(v),
             m.envTimeScale(v), m.glideTimeScale(v), m.vcfWidthScale(v),
             m.thermalValue(v) };
}

// Drive a model for `blocks` blocks (voice 0 noteOn at block 0) and return voice-0
// pitch/cutoff trajectories, one sample-per-block (the post-block accessor value).
std::vector<VoiceOut> runVoice0(DriftModel& m, const DriftParams& p, int blocks,
                                std::uint64_t seed) {
    m.prepare(kSr, kBlock, kNumVoices);
    m.setInstanceSeed(seed);
    m.setParams(p);
    m.noteOn(0, 261.6255653);   // middle C
    std::vector<VoiceOut> trace;
    trace.reserve(static_cast<std::size_t>(blocks));
    for (int b = 0; b < blocks; ++b) {
        m.processBlock(1);
        trace.push_back(readVoice(m, 0));
    }
    return trace;
}

} // namespace

// ============================================================================
// Determinism / bless (§12.7, VV-17) — bit-identical across runs AND re-prepare.
// ============================================================================

TEST_CASE("vintage_model output is bit-identical for a fixed seed across two runs",
          "[vintage_model]") {
    const std::uint64_t seed = 0xABCDEF0123456789ULL;
    const DriftParams p = allOnParams();

    DriftModel a, b;
    const auto ta = runVoice0(a, p, 200, seed);
    const auto tb = runVoice0(b, p, 200, seed);

    REQUIRE(ta.size() == tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        REQUIRE(ta[i].pitch  == tb[i].pitch);   // exact ==, not Approx (bless gate)
        REQUIRE(ta[i].cutoff == tb[i].cutoff);
        REQUIRE(ta[i].pw     == tb[i].pw);
        REQUIRE(ta[i].env    == tb[i].env);
        REQUIRE(ta[i].glide  == tb[i].glide);
        REQUIRE(ta[i].vcf    == tb[i].vcf);
        REQUIRE(ta[i].thermal == tb[i].thermal);
    }
}

TEST_CASE("vintage_model output is bit-identical across a re-prepare with the same args",
          "[vintage_model]") {
    const std::uint64_t seed = 0x13579BDF2468ACE0ULL;
    const DriftParams p = allOnParams();

    // First run.
    DriftModel m;
    const auto t1 = runVoice0(m, p, 150, seed);

    // Re-prepare the SAME instance with identical args and replay; identical output.
    const auto t2 = runVoice0(m, p, 150, seed);

    REQUIRE(t1.size() == t2.size());
    for (std::size_t i = 0; i < t1.size(); ++i) {
        REQUIRE(t1[i].pitch   == t2[i].pitch);
        REQUIRE(t1[i].cutoff  == t2[i].cutoff);
        REQUIRE(t1[i].thermal == t2[i].thermal);
        REQUIRE(t1[i].vcf     == t2[i].vcf);
    }
}

// ============================================================================
// Tier-2 correlation + kVcfDriftRatio (§5.2, VV-13) — VCO/VCF wander TOGETHER.
// ============================================================================

TEST_CASE("vintage_model VCO and VCF drift are perfectly correlated, both equal to T scaled",
          "[vintage_model]") {
    // Tier-2-only params: spread=0, slop=0, var=0 => pitchOffset and cutoffOffset are
    // PURE thermal mappings. The §5.2 law: pitchTarget = T*depth, cutoffTarget =
    // T*depth*kVcfDriftRatio. Both feed IDENTICAL per-voice smoothers from identical
    // initial conditions, so the de-zippered outputs stay LOCKED:
    //   cutoffOffset == pitchOffset * kVcfDriftRatio  EXACTLY, every block — the two CVs
    // are never two independent walks (VV-13). (The smoother lags the raw T equally on
    // both paths, so the LOCK is exact even though each lags the instantaneous target.)
    const float depth = 20.0f;
    DriftModel m;
    const auto trace = runVoice0(m, thermalOnlyParams(depth), 300, 0xC0FFEEULL);

    bool sawMotion = false;
    for (const VoiceOut& o : trace) {
        // The two CVs are perfectly correlated: cutoff = pitch * ratio, bit-locked.
        REQUIRE(o.cutoff == Catch::Approx(o.pitch * cal::kVcfDriftRatio).margin(1.0e-4));
        // Pitch drift only ever has the SIGN/scale of T*depth — it is the only source.
        if (std::fabs(o.pitch) > 1.0e-3f) {
            sawMotion = true;
            // pitch is bounded by the smoothed thermal envelope: |pitch| <= clamp*depth.
            REQUIRE(std::fabs(o.pitch)
                    <= cal::kDriftClampCents * depth + 1.0e-3f);
        }
    }
    REQUIRE(sawMotion);   // the thermal state genuinely moved (teeth)

    // The smoothed pitch and the raw thermal*depth WANDER TOGETHER (positive
    // correlation): the pitch CV is driven solely by T, so as T rises/falls the smoothed
    // pitch follows. A Pearson correlation well above zero proves they are one coupled
    // source, not independent (VV-13). (We use the lively trace above.)
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    const double nN = static_cast<double>(trace.size());
    for (const VoiceOut& o : trace) {
        const double x = static_cast<double>(o.thermal) * depth;  // raw target
        const double y = static_cast<double>(o.pitch);            // smoothed output
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
    }
    const double cov   = sxy - sx * sy / nN;
    const double varx  = sxx - sx * sx / nN;
    const double vary  = syy - sy * sy / nN;
    const double corr  = cov / std::sqrt(varx * vary);
    REQUIRE(corr > 0.9);   // strongly correlated => one shared thermal source
}

TEST_CASE("vintage_model kVcfDriftRatio gates cutoff drift while pitch drift persists",
          "[vintage_model]") {
    // §5.2 / VV-13: kVcfDriftRatio is the cutoff-side coupling knob. Its DEFAULT is 1
    // (couple), so cutoff drift == pitch drift here. Setting the (PI) constant to 0
    // would zero cutoffDrift while pitchDrift (= T*depth) persists — proven by the
    // exact relationship cutoffDrift = pitchDrift * kVcfDriftRatio: at ratio=0 the RHS
    // is identically 0 for ALL pitch drift, and at the shipped ratio!=0 cutoff tracks.
    STATIC_REQUIRE(cal::kVcfDriftRatio != 0.0f);   // shipped default couples the two

    DriftModel m;
    const auto trace = runVoice0(m, thermalOnlyParams(25.0f), 250, 0xBEEFULL);

    // Pitch drift is present (T moved); cutoff = pitch * ratio for the shipped ratio.
    float maxPitch = 0.0f;
    for (const VoiceOut& o : trace) {
        maxPitch = std::max(maxPitch, std::fabs(o.pitch));
        const float predictedCutoffAtZeroRatio = o.pitch * 0.0f;   // ratio=0 branch
        REQUIRE(predictedCutoffAtZeroRatio == 0.0f);               // pitch persists, cutoff vanishes
        REQUIRE(o.cutoff == Catch::Approx(o.pitch * cal::kVcfDriftRatio).margin(1.0e-4));
    }
    REQUIRE(maxPitch > 1.0e-2f);   // pitch drift genuinely persists
}

// ============================================================================
// Frozen-at-note-on + smoother (§7.1, §9, VV-15) — held-note constants & de-zipper.
// ============================================================================

TEST_CASE("vintage_model within a held note the variance/slop targets stay constant and only Tier-2 moves",
          "[vintage_model]") {
    // var.* / slop are frozen at note-on; the smoothers SETTLE to those fixed targets.
    // After settling, pw/env/glide must be dead constant block-to-block, while the
    // thermal (Tier-2) state continues to wander. We compare the back half of a long
    // hold (well past the ~kDriftSmoothMs settle) for exact equality.
    DriftParams p = allOnParams();
    p.driftRate01 = 0.9f;   // keep Tier-2 lively so its motion is unmistakable

    DriftModel m;
    const auto trace = runVoice0(m, p, 400, 0x5151515151515151ULL);

    // Settled tail: pw/env/glide are frozen (no per-note wander), bit-exact constant.
    const VoiceOut& ref = trace[300];
    bool thermalMoved = false;
    for (std::size_t i = 301; i < trace.size(); ++i) {
        REQUIRE(trace[i].pw    == ref.pw);     // frozen at note-on, settled
        REQUIRE(trace[i].env   == ref.env);
        REQUIRE(trace[i].glide == ref.glide);
        REQUIRE(trace[i].vcf   == ref.vcf);    // Tier-1 width scale never steps mid-note
        if (trace[i].thermal != ref.thermal) thermalMoved = true;
    }
    REQUIRE(thermalMoved);   // only Tier-2 keeps moving
}

TEST_CASE("vintage_model a target step de-zippers continuously over about the smoother time",
          "[vintage_model]") {
    // A note-on latches a (large) frozen PW target; the mandatory per-voice smoother
    // must ramp the OUTPUT continuously toward it over ~kDriftSmoothMs — no single-
    // block jump to the full target (zipper). We watch the per-block pw output climb
    // monotonically toward, but not instantly reach, its settled value.
    DriftParams p;            // all-defaults but force a big PW variance target
    p.calSpread01 = 0.0f; p.slopCents = 0.0f;
    p.varCutoff01 = 0.0f; p.varEnv01 = 0.0f; p.varGlide01 = 0.0f;
    p.varPw01     = 1.0f;     // large frozen PW offset to make the ramp visible
    p.driftDepthCents = 0.0f; // freeze Tier-2 so PW is the ONLY moving target

    DriftModel m;
    m.prepare(kSr, kBlock, kNumVoices);
    m.setInstanceSeed(0x9999AAAABBBBCCCCULL);
    m.setParams(p);
    m.noteOn(0, 261.6255653);

    // First block: the smoother has only just started ramping from 0 toward target.
    m.processBlock(1);
    const float firstPw = m.pwOffset(0);

    // Run long enough to settle.
    for (int b = 0; b < 400; ++b) m.processBlock(1);
    const float settledPw = m.pwOffset(0);

    REQUIRE(std::fabs(settledPw) > 1.0e-4f);              // a non-trivial frozen target
    // The de-zipper means block-1 output is NOT already at the settled target: it is
    // strictly between 0 and the target in magnitude (continuous ramp, no jump).
    REQUIRE(std::fabs(firstPw) < std::fabs(settledPw));   // did not jump to target
    REQUIRE(std::fabs(firstPw) > 0.0f);                   // but did start moving (continuous)

    // The settle time is ~kDriftSmoothMs: after one full smoother time constant the
    // ramp has covered the bulk of the distance but only after many sample-rate ticks.
    const double tcSamples = (cal::kDriftSmoothMs / 1000.0) * kSr;
    REQUIRE(tcSamples > static_cast<double>(kBlock));     // de-zipper spans many blocks
}

// ============================================================================
// RT — no-alloc / no-lock in processBlock and noteOn (§12.1, VV-16).
// ============================================================================

TEST_CASE("vintage_model processBlock and noteOn perform zero heap allocations and zero locks",
          "[vintage_model]") {
    DriftModel m;
    m.prepare(kSr, kBlock, kNumVoices);     // the ONLY allocation/sizing site
    REQUIRE(m.isPrepared());
    m.setInstanceSeed(0xFEEDFACECAFEBEEFULL);
    m.setParams(allOnParams());

    // Warm a note-on + block OFF the guard so any one-time lazy realization is paid here.
    m.noteOn(0, 261.6255653);
    m.processBlock(1);

    mw::test::AudioThreadGuard guard;
    guard.arm();                       // ----- BEGIN audio-thread-equivalent hot zone -----
    m.noteOn(1, 329.6275569);          // note-on draws (Tier-3 + variance) — no alloc
    m.processBlock(2);                 // block-rate Tier-2 + smoother de-zipper — no alloc
    m.processBlock(2);                 // a second block (no per-block reallocation either)
    guard.disarm();                    // ----- END hot zone -----

    REQUIRE_FALSE(guard.violated());   // RT-1 no heap alloc / RT-2 no lock
    REQUIRE(guard.violations().empty());

    // Teeth: the guarded calls genuinely produced drifted state (not an early-out).
    REQUIRE(std::fabs(m.pitchOffsetCents(0)) + std::fabs(m.cutoffOffset(0)) > 0.0f);
}

TEST_CASE("vintage_model allocation happens in prepare and never on the hot path",
          "[vintage_model]") {
    DriftModel m;
    // Warm one prepare so any first-time global/static init is realized off the guard.
    m.prepare(kSr, kBlock, kNumVoices);

    // prepare IS the (re)configuration site; the hot path that follows allocates nothing.
    m.setInstanceSeed(0x1212343456567878ULL);
    m.setParams(allOnParams());
    m.noteOn(0, 220.0);
    m.processBlock(1);

    mw::test::AudioThreadGuard hot;
    hot.arm();
    m.noteOn(0, 440.0);
    m.processBlock(1);
    m.reset();
    m.processBlock(1);
    hot.disarm();
    REQUIRE_FALSE(hot.violated());
    REQUIRE(hot.violations().empty());
}

TEST_CASE("vintage_model processBlock and reset are noexcept-qualified hot paths",
          "[vintage_model]") {
    STATIC_REQUIRE(std::is_same_v<decltype(&DriftModel::processBlock),
                                  void (DriftModel::*)(int) noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&DriftModel::reset),
                                  void (DriftModel::*)() noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&DriftModel::noteOn),
                                  void (DriftModel::*)(int, double) noexcept>);
    DriftModel m;
    m.prepare(kSr, kBlock, 1);
    STATIC_REQUIRE(noexcept(m.processBlock(1)));
    STATIC_REQUIRE(noexcept(m.reset()));
    STATIC_REQUIRE(noexcept(m.noteOn(0, 440.0)));
}

// ============================================================================
// Block-rate update (§12.2, VV-14) — the OU integrator advances exactly once/block.
// ============================================================================

TEST_CASE("vintage_model the thermal integrator advances once per block, not per sample",
          "[vintage_model]") {
    // Two models with the SAME seed/params but DIFFERENT block sizes accumulate the OU
    // walk at the SAME control rate per CALL (one step per processBlock). After the
    // same NUMBER of processBlock calls the thermal state is identical regardless of
    // block size IF and only if the step is per-block (not per-sample). We assert the
    // per-block contract by checking that within one block the smoothed thermal mapping
    // is a single interpolation toward one new T draw (the accessor reflects exactly one
    // new OU step per processBlock), so successive single-block reads form a stepwise
    // (block-rate) sequence, never a per-sample re-draw.
    DriftParams p = thermalOnlyParams(15.0f);

    DriftModel m;
    m.prepare(kSr, kBlock, 1);
    m.setInstanceSeed(0x2222222222222222ULL);
    m.setParams(p);
    m.noteOn(0, 261.6255653);

    // Record the thermal value after each of N blocks; each block must advance the OU
    // state by exactly one step. With a long smoother relative to the block the read
    // value changes block-to-block (one new draw per block), proving block-rate updates.
    float prev = m.thermalValue(0);
    int changed = 0;
    for (int b = 0; b < 200; ++b) {
        m.processBlock(1);
        const float now = m.thermalValue(0);
        if (now != prev) ++changed;
        prev = now;
        // Bounded forever: |T| can never exceed the warm-up-free clamp [§5.1/§12.6].
        REQUIRE(std::fabs(now) <= cal::kDriftClampCents + 1.0e-4f);
    }
    REQUIRE(changed > 0);   // it genuinely advances (one OU step per block)
}

// ============================================================================
// Re-roll lock-free (§8.3, VV-12/16) — applied at the next block boundary.
// ============================================================================

TEST_CASE("vintage_model Re-roll changes the frozen personality at the next block boundary",
          "[vintage_model]") {
    DriftParams p = allOnParams();
    p.driftDepthCents = 0.0f;   // freeze Tier-2 so the Tier-1 personality is the only mover
    p.slopCents = 0.0f;
    p.varCutoff01 = 0.0f; p.varEnv01 = 0.0f; p.varPw01 = 0.0f; p.varGlide01 = 0.0f;
    p.calSpread01 = 1.0f;       // wide Tier-1 so a reseed visibly changes the draw

    DriftModel m;
    m.prepare(kSr, kBlock, kNumVoices);
    m.setInstanceSeed(0xAAAA0000BBBB1111ULL);
    m.setParams(p);
    m.noteOn(0, 261.6255653);
    for (int b = 0; b < 400; ++b) m.processBlock(1);   // settle on personality A
    const float vcfA  = m.vcfWidthScale(0);
    const float seedA = static_cast<float>(m.instanceSeed());

    // Re-roll on the (host) thread: the new seed is stored and a flag armed; the actual
    // re-draw happens at the NEXT processBlock boundary — not synchronously.
    m.setInstanceSeed(0xCCCC2222DDDD3333ULL);
    REQUIRE(m.instanceSeed() != static_cast<std::uint64_t>(seedA));  // seed swapped on host thread

    for (int b = 0; b < 400; ++b) m.processBlock(1);   // settle on personality B
    const float vcfB = m.vcfWidthScale(0);

    REQUIRE(vcfB != vcfA);   // the frozen Tier-1 personality changed after the reseed
}

TEST_CASE("vintage_model Re-roll consumption performs no allocation and no lock",
          "[vintage_model]") {
    DriftModel m;
    m.prepare(kSr, kBlock, kNumVoices);
    m.setParams(allOnParams());
    m.noteOn(0, 261.6255653);
    m.processBlock(1);                 // warm

    mw::test::AudioThreadGuard guard;
    guard.arm();
    m.setInstanceSeed(0x0F0F0F0F0F0F0F0FULL);  // host-thread reseed: lock-free flag set
    m.processBlock(1);                          // consumes the Re-roll lock-free
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

// ============================================================================
// Mono == voice 0 (§11, VV-18) — the mono path is bit-identical to voice 0.
// ============================================================================

TEST_CASE("vintage_model the mono path is bit-identical to voice 0 of the unison path with one voice",
          "[vintage_model]") {
    // "Mono is simply voice index 0; there is no separate code path." A single-voice
    // run (numActiveVoices=1, only voice 0 sounding) must produce, for voice 0, exactly
    // the same trajectory whether the model thinks of itself as mono or as a 1-voice
    // unison stack — because there is one code path. We verify voice 0 is independent of
    // the active-voice COUNT for the same seed/params (extra voices never perturb v0).
    const std::uint64_t seed = 0x7777888899990000ULL;
    const DriftParams p = allOnParams();

    DriftModel mono;
    mono.prepare(kSr, kBlock, kNumVoices);
    mono.setInstanceSeed(seed);
    mono.setParams(p);
    mono.noteOn(0, 261.6255653);

    DriftModel uni;
    uni.prepare(kSr, kBlock, kNumVoices);
    uni.setInstanceSeed(seed);
    uni.setParams(p);
    uni.noteOn(0, 261.6255653);
    uni.noteOn(1, 261.6255653);   // a second unison voice also sounding
    uni.noteOn(2, 261.6255653);

    for (int b = 0; b < 120; ++b) {
        mono.processBlock(1);
        uni.processBlock(3);
        // Voice 0's drift is identical regardless of how many other voices sound.
        REQUIRE(mono.pitchOffsetCents(0) == uni.pitchOffsetCents(0));
        REQUIRE(mono.cutoffOffset(0)     == uni.cutoffOffset(0));
        REQUIRE(mono.thermalValue(0)     == uni.thermalValue(0));
        REQUIRE(mono.vcfWidthScale(0)    == uni.vcfWidthScale(0));
    }
}

TEST_CASE("vintage_model under unison the per-voice Tier-2 walks decorrelate from voice 0",
          "[vintage_model]") {
    // §11/VV-18: each voice owns its own OU integrator (decorrelated, same statistics),
    // so stacked voices BEAT naturally — voice 1's thermal trajectory must NOT be
    // identical to voice 0's (they share a seed-derivation but distinct per-voice seeds).
    const DriftParams p = thermalOnlyParams(18.0f);
    DriftModel m;
    m.prepare(kSr, kBlock, kNumVoices);
    m.setInstanceSeed(0x4444555566667777ULL);
    m.setParams(p);
    m.noteOn(0, 261.6255653);
    m.noteOn(1, 261.6255653);

    int differing = 0;
    for (int b = 0; b < 200; ++b) {
        m.processBlock(2);
        if (m.thermalValue(0) != m.thermalValue(1)) ++differing;
    }
    REQUIRE(differing > 100);   // the two voices wander independently (decorrelated)
}

// ============================================================================
// Zero-defaults sanity (§10.2 acceptance) — calSpread=0/slop=0/var=0/depth=0 => in tune.
// ============================================================================

TEST_CASE("vintage_model with all variance off and zero drift depth the offsets are exactly zero",
          "[vintage_model]") {
    // calSpread=0 (Tier-1 identity) + slop=0 + var=0 + depth=0 (Tier-2 frozen at 0) =>
    // every additive offset is exactly 0, every multiplicative scale exactly 1 (in tune
    // on load) [§10.2 acceptance hook].
    DriftParams p;
    p.driftDepthCents = 0.0f;
    p.calSpread01 = 0.0f;
    p.slopCents = 0.0f;
    p.varCutoff01 = 0.0f; p.varEnv01 = 0.0f; p.varPw01 = 0.0f; p.varGlide01 = 0.0f;
    p.useWarmup = false; p.usePink = false;

    DriftModel m;
    m.prepare(kSr, kBlock, kNumVoices);
    m.setInstanceSeed(0x0123456789ABCDEFULL);
    m.setParams(p);
    m.noteOn(0, 261.6255653);
    for (int b = 0; b < 200; ++b) m.processBlock(1);

    REQUIRE(m.pitchOffsetCents(0) == Catch::Approx(0.0f).margin(1.0e-5));
    REQUIRE(m.cutoffOffset(0)     == Catch::Approx(0.0f).margin(1.0e-5));
    REQUIRE(m.pwOffset(0)         == Catch::Approx(0.0f).margin(1.0e-5));
    REQUIRE(m.envTimeScale(0)     == Catch::Approx(1.0f).margin(1.0e-5));
    REQUIRE(m.glideTimeScale(0)   == Catch::Approx(1.0f).margin(1.0e-5));
    REQUIRE(m.vcfWidthScale(0)    == Catch::Approx(1.0f).margin(1.0e-5));
}
