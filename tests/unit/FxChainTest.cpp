// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the post-voice FX chain orchestrator FxChain (task 094).
// Test-case names begin with "fxchain" so `ctest -R fxchain` selects them (silent-
// pass rule, AGENTS.md). The names avoid '[' in the display text so the [fxchain]
// tag does not break ctest -R selection. Covers each acceptance criterion in
// plan/backlog/094-fxchain-orchestration.md:
//
//   - masterBypass ON (and separately all three blocks bypassed) => out[L]==out[R]
//     equal to the padded mono dry, FX DSP skipped [§3.3 / ADR-010 FX-1]
//   - chain order is fixed Drive->Chorus->Delay; no path applies Chorus/Delay before
//     Drive [§3.4 / ADR-010 FX-2]
//   - Drive on but Chorus Off and Delay off => out[L]==out[R] (stereo only from
//     Chorus/Delay) [ADR-010 FX-4]
//   - monoOutput=ON with any Chorus/Delay width => out[L]==out[R] at chain output
//     [§3.3 / ADR-010 FX-9]
//   - getLatencySamples() == Drive 2x group delay, invariant to drive.on/masterBypass/
//     per-block bypass; the dryPad keeps FX-off at the constant offset
//     [§6.1/§6.3 / ADR-017 L2/L5/L8]
//   - prepare/reset/process/setParams/getLatencySamples perform no heap allocation and
//     take no locks [§3.2 / ADR-010 FX-10]

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "dsp/fx/FxChain.h"
#include "dsp/fx/Drive.h"
#include "dsp/fx/FxOversampler2x.h"
#include "dsp/fx/FxParams.h"

#include "../invariants/AudioThreadGuard.h"

using mw::fx::FxChain;
using mw::fx::Drive;
using mw::fx::FxOversampler2x;
using mw::fx::FxParams;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// A mono test stimulus: a low-frequency sine plus a transient so any reordered /
// widened stage is observable.
std::vector<float> makeMono(int n) {
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i)
        in[i] = 0.4f * static_cast<float>(std::sin(2.0 * kPi * 220.0 * i / kSr))
              + ((i == n / 3) ? 0.5f : 0.0f);
    return in;
}

// Build an all-default (INIT) FxParams: FX OFF / dry [§7, §8; ADR-010 FX-13].
FxParams initParams() { return FxParams{}; }

// Run `blocks` blocks of `mono` through the chain into freshly-zeroed L/R, returning
// the LAST processed block as a {L,R} pair so smoothers have settled.
struct StereoBlock { std::vector<float> L, R; };

StereoBlock runSettled(FxChain& fx, const std::vector<float>& mono, int blocks = 8) {
    const int n = static_cast<int>(mono.size());
    StereoBlock out{ std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
    float* ch[2] = { out.L.data(), out.R.data() };
    for (int b = 0; b < blocks; ++b) {
        std::fill(out.L.begin(), out.L.end(), 0.0f);
        std::fill(out.R.begin(), out.R.end(), 0.0f);
        fx.process(mono.data(), ch, n);
    }
    return out;
}

bool channelsIdentical(const StereoBlock& s) {
    for (std::size_t i = 0; i < s.L.size(); ++i)
        if (s.L[i] != s.R[i]) return false;
    return true;
}

} // namespace

TEST_CASE("fxchain: masterBypass copies the padded mono dry equally to L and R", "[fxchain]") {
    // §3.3 rule 3 / ADR-010 FX-1: master bypass early-outs — out[L]==out[R]==the dry
    // padded by getLatencySamples(), with no FX DSP applied.
    FxChain fx;
    fx.prepare(kSr, 512);

    FxParams p = initParams();
    p.masterBypass = true;
    // Engage every block to PROVE the early-out skips them: even with hot drive +
    // chorus + delay set, masterBypass must produce the pure padded dry.
    p.drive  = { true, 0.9f, 0.5f, 0.5f };
    p.chorus = { 3 /*I+II*/, 0.5f, 0.8f, 1.0f, 0.8f };
    p.delay  = { true, false, false, 1, 200.0f, 0.5f, 0.5f, 1.0f, 0.5f };
    fx.setParams(p);

    const int N = 256;
    const std::vector<float> in = makeMono(N);
    const StereoBlock out = runSettled(fx, in);

    // Channels are bit-identical (mono).
    REQUIRE(channelsIdentical(out));

    // The output is the input delayed by exactly getLatencySamples() (the dry pad),
    // and nothing else (FX DSP skipped). Steady state after >latency blocks: out[n]
    // == in[n - latency] within the block (block-aligned because N >> latency and we
    // feed the same block repeatedly, so the pad history is the previous identical
    // block's tail).
    const int lat = fx.getLatencySamples();
    REQUIRE(lat > 0);
    for (int n = lat; n < N; ++n)
        REQUIRE(out.L[n] == Catch::Approx(in[n - lat]).margin(1e-7f));
}

TEST_CASE("fxchain: all three blocks bypassed equals master bypass (padded mono dry)", "[fxchain]") {
    // §3.3 rule 3 / ADR-010 FX-1: the all-blocks-off condition (drive.on=false,
    // chorus.mode=Off, delay.on=false) is the SAME early-out as masterBypass: padded
    // mono dry, no FX DSP. Here masterBypass is FALSE but every block is off.
    FxChain fx;
    fx.prepare(kSr, 512);

    FxParams p = initParams();
    p.masterBypass = false;        // master ON, but...
    p.drive.on     = false;        // ...all three blocks are individually off.
    p.chorus.mode  = 0;            // Off
    p.delay.on     = false;
    fx.setParams(p);

    const int N = 256;
    const std::vector<float> in = makeMono(N);
    const StereoBlock out = runSettled(fx, in);

    REQUIRE(channelsIdentical(out));

    const int lat = fx.getLatencySamples();
    REQUIRE(lat > 0);
    for (int n = lat; n < N; ++n)
        REQUIRE(out.L[n] == Catch::Approx(in[n - lat]).margin(1e-7f));

    // Cross-check it matches a master-bypassed chain exactly (both are the early-out).
    FxChain fxMaster;
    fxMaster.prepare(kSr, 512);
    FxParams pm = initParams();
    pm.masterBypass = true;
    fxMaster.setParams(pm);
    const StereoBlock outMaster = runSettled(fxMaster, in);
    for (int n = 0; n < N; ++n) {
        REQUIRE(out.L[n] == Catch::Approx(outMaster.L[n]).margin(1e-7f));
        REQUIRE(out.R[n] == Catch::Approx(outMaster.R[n]).margin(1e-7f));
    }
}

TEST_CASE("fxchain: Drive on with Chorus Off and Delay off stays mono (stereo only from Chorus/Delay)", "[fxchain]") {
    // ADR-010 FX-4 / §3.3 rule 2: Drive is mono-in/mono-out and never widens. With
    // Drive engaged but Chorus Off and Delay off, out[L]==out[R] bit-for-bit, and the
    // signal is NOT the raw dry (Drive actually processed it).
    FxChain fx;
    fx.prepare(kSr, 512);

    FxParams p = initParams();
    p.masterBypass = false;
    p.drive  = { true, 0.85f, 0.5f, 0.5f };  // hot drive, tone flat, unity-ish output
    p.chorus.mode = 0;                       // Off
    p.delay.on    = false;
    fx.setParams(p);

    const int N = 512;
    const std::vector<float> in = makeMono(N);
    const StereoBlock out = runSettled(fx, in, /*blocks=*/16);

    REQUIRE(channelsIdentical(out)); // Drive cannot widen.

    // Sanity: Drive DID change the signal (so this is not a trivial bypass pass).
    // Compare to the master-bypass padded dry; the drive output must differ.
    FxChain fxDry;
    fxDry.prepare(kSr, 512);
    FxParams pd = initParams();
    pd.masterBypass = true;
    fxDry.setParams(pd);
    const StereoBlock dry = runSettled(fxDry, in, /*blocks=*/16);
    double diff = 0.0;
    for (int n = 0; n < N; ++n) diff += std::fabs(out.L[n] - dry.L[n]);
    REQUIRE(diff > 1e-2); // Drive genuinely processed (not a no-op).
}

TEST_CASE("fxchain: chain order is fixed Drive then Chorus then Delay", "[fxchain]") {
    // §3.4 / ADR-010 FX-2: the chain applies Drive FIRST, then Chorus, then Delay; no
    // path applies Chorus/Delay before Drive. We verify behaviorally by an order-
    // sensitive oracle: feed the chain a SINGLE positive impulse and compare the chain
    // output against the reference computed in the documented Drive->Chorus->Delay
    // order using the SAME stage instances. If the chain reordered stages, the byte
    // stream would diverge from the reference.
    const int N = 1024;

    // --- Reference: stages applied explicitly in Drive -> Chorus -> Delay order ----
    auto reference = [&](const std::vector<float>& mono) {
        Drive drive;
        mw::fx::Chorus chorus;
        mw::fx::Delay delay;
        drive.prepare(kSr, N);
        chorus.prepare(kSr, N);
        delay.prepare(kSr, N);

        FxParams::DriveP dp{ true, 0.7f, 0.5f, 0.5f };
        FxParams::ChorusP cp{ 1 /*I*/, 0.5f, 0.5f, 1.0f, 0.7f };
        FxParams::DelayP  ep{ true, false, false, 1, 120.0f, 0.4f, 0.5f, 1.0f, 0.4f };
        drive.setParams(dp);
        chorus.setParams(cp);
        delay.setParams(ep, 120.0);

        // The chain pads the mono dry by getLatencySamples() before any stage (§3.4
        // step 2). The reference must do the same so it lines up with the chain.
        FxOversampler2x os; os.prepare(N);
        const int lat = os.latencySamples();

        std::vector<float> m(N, 0.0f);
        for (int n = 0; n < N; ++n)
            m[n] = (n - lat >= 0) ? mono[n - lat] : 0.0f; // integer dry pad

        // Drive (mono->mono).
        drive.process(m.data(), N);
        // Sum to L/R, then Chorus (adds stereo wet), then Delay (stereo in/out).
        std::vector<float> L(N), R(N);
        for (int n = 0; n < N; ++n) { L[n] = m[n]; R[n] = m[n]; }
        chorus.process(m.data(), L.data(), R.data(), N);
        delay.process(L.data(), R.data(), N);
        return std::pair<std::vector<float>, std::vector<float>>{ L, R };
    };

    // --- Chain under test, same params, FIRST block (no warm-up) so the reference's
    // single-pass stage state matches the chain's single-pass stage state. ---------
    FxChain fx;
    fx.prepare(kSr, N);
    FxParams p = initParams();
    p.masterBypass = false;
    p.hostBpm = 120.0;
    p.drive  = { true, 0.7f, 0.5f, 0.5f };
    p.chorus = { 1 /*I*/, 0.5f, 0.5f, 1.0f, 0.7f };
    p.delay  = { true, false, false, 1, 120.0f, 0.4f, 0.5f, 1.0f, 0.4f };
    fx.setParams(p);

    std::vector<float> in(N, 0.0f);
    in[0] = 1.0f; // single positive impulse

    std::vector<float> L(N, 0.0f), R(N, 0.0f);
    float* ch[2] = { L.data(), R.data() };
    fx.process(in.data(), ch, N);

    const auto ref = reference(in);
    for (int n = 0; n < N; ++n) {
        REQUIRE(L[n] == Catch::Approx(ref.first[n]).margin(1e-6f));
        REQUIRE(R[n] == Catch::Approx(ref.second[n]).margin(1e-6f));
    }
}

TEST_CASE("fxchain: monoOutput forces phase-coherent mono with any Chorus or Delay width", "[fxchain]") {
    // §3.3 rule 5 / ADR-010 FX-9: with monoOutput ON, after the full stereo chain
    // force m=0.5*(L+R); L=R=m, so out[L]==out[R] regardless of Chorus/Delay Width.
    FxChain fx;
    fx.prepare(kSr, 512);

    FxParams p = initParams();
    p.masterBypass = false;
    p.monoOutput   = true;
    p.hostBpm      = 120.0;
    // Maximum width on BOTH stereo stages: without the collapse this would be a wide
    // (and partly anti-phase) image; monoOutput must still force L==R.
    p.drive  = { true, 0.5f, 0.6f, 0.5f };
    p.chorus = { 3 /*I+II*/, 0.5f, 1.0f, 1.0f /*width*/, 1.0f /*mix*/ };
    p.delay  = { true, false, true /*pingpong*/, 1, 130.0f, 0.4f, 0.5f, 1.0f /*width*/, 0.5f };
    fx.setParams(p);

    const int N = 512;
    const std::vector<float> in = makeMono(N);
    const StereoBlock out = runSettled(fx, in, /*blocks=*/16);

    REQUIRE(channelsIdentical(out));

    // Prove the chain was genuinely stereo BEFORE the collapse: the same params with
    // monoOutput OFF must produce L != R somewhere (otherwise the test is vacuous).
    FxChain fxStereo;
    fxStereo.prepare(kSr, 512);
    FxParams ps = p;
    ps.monoOutput = false;
    fxStereo.setParams(ps);
    const StereoBlock stereo = runSettled(fxStereo, in, /*blocks=*/16);
    REQUIRE_FALSE(channelsIdentical(stereo)); // genuinely stereo without the collapse
}

TEST_CASE("fxchain: getLatencySamples equals Drive 2x group delay and is invariant to bypass", "[fxchain]") {
    // §6.1/§6.3 / ADR-017 L2/L5/L8: getLatencySamples() == Drive::latencySamples()
    // (the only contributing FX source) and is CONSTANT regardless of drive.on,
    // masterBypass, and per-block bypass. Chorus/Delay contribute 0.
    FxChain fx;
    fx.prepare(kSr, 256);

    // Oracle: a standalone Drive prepared the same way reports the same fixed delay.
    Drive drive;
    drive.prepare(kSr, 256);
    const int driveLat = drive.latencySamples();
    REQUIRE(driveLat > 0);
    REQUIRE(fx.getLatencySamples() == driveLat);

    // Also equals the raw FxOversampler2x group delay (Drive's only contributing term).
    FxOversampler2x os; os.prepare(256);
    REQUIRE(fx.getLatencySamples() == os.latencySamples());

    const int baseline = fx.getLatencySamples();

    // Invariant to drive.on / masterBypass / per-block bypass — sweep combinations.
    auto withParams = [&](bool master, bool dOn, int cMode, bool delOn) {
        FxParams p = initParams();
        p.masterBypass = master;
        p.drive.on     = dOn;
        p.chorus.mode  = cMode;
        p.delay.on     = delOn;
        fx.setParams(p);
        return fx.getLatencySamples();
    };
    REQUIRE(withParams(true,  false, 0, false) == baseline);
    REQUIRE(withParams(false, false, 0, false) == baseline);
    REQUIRE(withParams(false, true,  0, false) == baseline);
    REQUIRE(withParams(false, true,  3, true)  == baseline);
    REQUIRE(withParams(false, false, 3, true)  == baseline);

    // And invariant after a reset() too (sized once in prepare, never mutated).
    fx.reset();
    REQUIRE(fx.getLatencySamples() == baseline);
}

TEST_CASE("fxchain: defaults are FX OFF and dry on a freshly prepared chain", "[fxchain]") {
    // §8 / ADR-010 FX-13: a freshly-constructed FxChain (INIT FxParams) renders the
    // padded mono dry with no FX. We do NOT call setParams: the engine default
    // (masterBypass=true) must drive the FX-off early-out.
    FxChain fx;
    fx.prepare(kSr, 256);

    const int N = 256;
    const std::vector<float> in = makeMono(N);
    const StereoBlock out = runSettled(fx, in);

    REQUIRE(channelsIdentical(out));
    const int lat = fx.getLatencySamples();
    for (int n = lat; n < N; ++n)
        REQUIRE(out.L[n] == Catch::Approx(in[n - lat]).margin(1e-7f));
}

TEST_CASE("fxchain: prepare/reset/process/setParams/getLatencySamples perform no heap allocation", "[fxchain]") {
    // §3.2 / ADR-010 FX-10 / ADR-017 L10: all ring buffers, oversampler state, scratch
    // and the dry-pad line are allocated in prepare(); the hot paths (process/reset/
    // setParams/getLatencySamples) only move indices and read preallocated state — no
    // heap allocation, no locks. Arm the alloc sentinel AFTER prepare().
    FxChain fx;
    fx.prepare(kSr, 512); // allocation allowed here, before arming.

    // Publish a fully-engaged param set so process() exercises every stage (not the
    // ~0-cost early-out path).
    FxParams p = initParams();
    p.masterBypass = false;
    p.hostBpm      = 120.0;
    p.drive  = { true, 0.7f, 0.5f, 0.5f };
    p.chorus = { 3, 0.5f, 0.8f, 1.0f, 0.7f };
    p.delay  = { true, true, true, 1, 0.0f, 0.5f, 0.5f, 1.0f, 0.5f };
    fx.setParams(p);

    constexpr int kN = 256;
    std::vector<float> mono(kN, 0.0f);
    std::vector<float> L(kN, 0.0f), R(kN, 0.0f);
    for (int i = 0; i < kN; ++i)
        mono[i] = 0.3f * static_cast<float>(std::sin(0.05 * i));
    float* ch[2] = { L.data(), R.data() };
    // Warm once before arming so any lazy state is realized.
    fx.process(mono.data(), ch, kN);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    fx.reset();
    fx.setParams(p);
    fx.process(mono.data(), ch, kN);
    (void) fx.getLatencySamples();
    // Also exercise the bypass early-out path under the guard.
    FxParams pb = initParams();
    pb.masterBypass = true;
    fx.setParams(pb);
    fx.process(mono.data(), ch, kN);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}
