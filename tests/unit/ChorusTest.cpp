// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the Juno-style BBD Chorus stage (task 092). Test-case names
// begin with "fxchorus" so `ctest -R fxchorus` selects them (silent-pass rule,
// AGENTS.md). The names avoid '[' in the display text so the tag does not break
// ctest -R selection. Covers each acceptance criterion in plan/backlog/092:
//   - width=0 yields out[L]==out[R] (centered mono collapse) [§5.1.3; ADR-010 FX-6]
//   - width=1 produces anti-phase L/R modulation (the two taps differ; LFO phases are
//     0.5 cycle apart) [§5.1.3]
//   - latencySamples() returns 0 (intended musical delay) [§5.1.3; ADR-017 L3]
//   - prepare/reset/process/setParams perform no heap allocation, take no locks
//     [ADR-010 FX-10]
//
// Plus oracle / circuit-behavior checks:
//   - the anti-phase LFO is 0.5 cycle apart -> the L/R modulated read offsets are
//     mirror images about the base delay over an LFO cycle [§5.1.2, §5.1.3]
//   - mode presets select the documented (PI) rate/depth pairs [§5.1.4]

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "dsp/fx/Chorus.h"
#include "dsp/fx/FxParams.h"
#include "calibration/ChorusConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::fx::Chorus;
using mw::fx::FxParams;

namespace {

// Build a ChorusP snapshot. mode is the enum int; rate<=0 means "use mode default".
FxParams::ChorusP makeParams(int mode, float rate, float depth, float width, float mix) {
    FxParams::ChorusP p{};
    p.mode  = mode;
    p.rate  = rate;
    p.depth = depth;
    p.width = width;
    p.mix   = mix;
    return p;
}

// Run the chorus over a block: caller seeds L/R with the dry (= mono) signal, the
// chorus mixes wet stereo in-place. Returns nothing; L/R are mutated.
void runBlock(Chorus& ch, const std::vector<float>& mono,
              std::vector<float>& L, std::vector<float>& R) {
    const int n = static_cast<int>(mono.size());
    L = mono; // dry already in L/R per §3.4 step 6
    R = mono;
    ch.process(mono.data(), L.data(), R.data(), n);
}

} // namespace

TEST_CASE("fxchorus: width=0 yields out L equals out R (centered mono collapse)", "[fxchorus]") {
    // §5.1.3 / ADR-010 FX-6: width=0 sums the two anti-phase taps to the center and
    // adds them equally to L and R -> a true centered mono collapse, so out[L]==out[R]
    // for every sample even though the chorus is fully wet and modulating.
    Chorus ch;
    ch.prepare(48000.0, 256);
    // Fully wet, full depth, Mode I, WIDTH = 0.
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I),
                            /*rate=*/0.0f, /*depth=*/1.0f, /*width=*/0.0f, /*mix=*/1.0f));
    ch.reset();
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I),
                            0.0f, 1.0f, 0.0f, 1.0f)); // re-arm targets after reset snap

    // A non-trivial input so any L/R difference would show.
    constexpr int kN = 2048;
    std::vector<float> mono(kN);
    for (int i = 0; i < kN; ++i)
        mono[i] = std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / 48000.0f);

    std::vector<float> L, R;
    runBlock(ch, mono, L, R);

    for (int i = 0; i < kN; ++i)
        REQUIRE(L[i] == Catch::Approx(R[i]).margin(1e-6f));
}

TEST_CASE("fxchorus: width=1 produces anti-phase L/R modulation (the two taps differ)", "[fxchorus]") {
    // §5.1.3: width=1 is the full hard-panned anti-phase image. Because the two LFOs
    // are 0.5 cycle apart, the L and R read offsets differ, so the wet content (and
    // therefore the output) differs between channels for at least some samples.
    Chorus ch;
    ch.prepare(48000.0, 256);
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I),
                            0.0f, /*depth=*/1.0f, /*width=*/1.0f, /*mix=*/1.0f));
    ch.reset();
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I),
                            0.0f, 1.0f, 1.0f, 1.0f));

    constexpr int kN = 4096;
    std::vector<float> mono(kN);
    for (int i = 0; i < kN; ++i)
        mono[i] = std::sin(2.0f * 3.14159265f * 330.0f * static_cast<float>(i) / 48000.0f);

    std::vector<float> L, R;
    runBlock(ch, mono, L, R);

    // POSITIVE: the channels must differ somewhere — the anti-phase modulation widens.
    float maxDiff = 0.0f;
    for (int i = 0; i < kN; ++i)
        maxDiff = std::max(maxDiff, std::fabs(L[i] - R[i]));
    REQUIRE(maxDiff > 1e-3f);
}

TEST_CASE("fxchorus: the two LFOs are 0.5 cycle apart (anti-phase modulation oracle)", "[fxchorus]") {
    // Circuit-behavior oracle for §5.1.2/§5.1.3: the right line runs 0.5 cycle apart
    // from the left, and the LFO is a triangle, which is ODD-symmetric about the
    // half-period. So lfoWave(p + 0.5) == -lfoWave(p) for every phase p. Because each
    // line's read offset is base + depth*excursion*lfoWave(phase), this means the L
    // and R modulated delay offsets are exact mirror images about the base delay at
    // every instant — the defining anti-phase widening property [ADR-010 FX-6].
    //
    // Direct oracle on the modeled waveform (no fragile peak-finding):
    for (int i = 0; i < 64; ++i) {
        const float p = static_cast<float>(i) / 64.0f; // phases across one cycle
        const float left  = Chorus::lfoWave(p);
        const float right = Chorus::lfoWave(p + 0.5f); // the R line, 0.5 cycle ahead
        REQUIRE(right == Catch::Approx(-left).margin(1e-6f));
    }
    // Sanity on the triangle's shape / range: it is bipolar in [-1,1] with the
    // documented breakpoints (0 at phase 0/0.5, +1 at 0.25, -1 at 0.75).
    REQUIRE(Chorus::lfoWave(0.0f)  == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(Chorus::lfoWave(0.25f) == Catch::Approx(1.0f).margin(1e-6f));
    REQUIRE(Chorus::lfoWave(0.5f)  == Catch::Approx(0.0f).margin(1e-6f));
    REQUIRE(Chorus::lfoWave(0.75f) == Catch::Approx(-1.0f).margin(1e-6f));

    // And the two lines are seeded 0.5 cycle apart from a fresh prepare/reset: feed an
    // impulse and confirm the channels are NOT identical (the offsets genuinely differ
    // because of the anti-phase seeding), with width=1, mix=1.
    Chorus ch;
    ch.prepare(48000.0, 64);
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I), 5.0f, 1.0f, 1.0f, 1.0f));
    ch.reset();
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I), 5.0f, 1.0f, 1.0f, 1.0f));
    constexpr int kN = 1024;
    std::vector<float> mono(kN, 0.0f); mono[0] = 1.0f;
    std::vector<float> L(kN, 0.0f), R(kN, 0.0f);
    ch.process(mono.data(), L.data(), R.data(), kN);
    bool differ = false;
    for (int i = 0; i < kN; ++i)
        if (std::fabs(L[i] - R[i]) > 1e-4f) { differ = true; break; }
    REQUIRE(differ);
}

TEST_CASE("fxchorus: latencySamples returns 0 (intended musical delay)", "[fxchorus]") {
    // §5.1.3 / ADR-017 L3: the Chorus modulation delay is intended musical delay and
    // is NOT reported as PDC.
    Chorus ch;
    ch.prepare(48000.0, 256);
    REQUIRE(ch.latencySamples() == 0);
    // Invariant across modes/params (latency is structural, not parameter-dependent).
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::IandII), 0.0f, 1.0f, 1.0f, 1.0f));
    REQUIRE(ch.latencySamples() == 0);
}

TEST_CASE("fxchorus: mode presets select the documented rate and depth pairs", "[fxchorus]") {
    // §5.1.4: Mode I -> 0.5 Hz / depth scalar 0.6; Mode II -> 0.83 Hz / 1.0. We verify
    // the depth scalar's effect indirectly: at depth=1 the modulation excursion scales
    // with the mode depth scalar, so Mode II (deeper) widens more than Mode I for the
    // same input. The (PI) constants themselves are pinned here as a static contract.
    STATIC_REQUIRE(mw::cal::chorus::kChorusModeIRateHz == 0.5f);
    STATIC_REQUIRE(mw::cal::chorus::kChorusModeIIRateHz == 0.83f);
    STATIC_REQUIRE(mw::cal::chorus::kChorusModeIDepth == 0.6f);
    STATIC_REQUIRE(mw::cal::chorus::kChorusModeIIDepth == 1.0f);
    STATIC_REQUIRE(mw::cal::chorus::kChorusBaseDelayMs == 7.5f);
    STATIC_REQUIRE(mw::cal::chorus::kChorusDepthMs == 4.0f);

    // Behavioral oracle on the DELAY EXCURSION (the thing the depth scalar controls):
    // feed a unit-slope ramp with mix=1, width=1 and seed dry=0, so wetL[n] == the
    // value the ramp held delL samples ago == (n - delL) once the line has filled.
    // Therefore (n - wetL[n]) == delL == base + depthScalar*depthMs*lfoWave(phaseL).
    // The peak-to-peak of that recovered offset over a full LFO cycle equals
    // 2*depthScalar*depthMs — directly proportional to the mode's depth scalar. So
    // Mode II (1.0) must show a LARGER excursion than Mode I (0.6).
    auto excursion = [](Chorus::Mode m) {
        const double sr = 48000.0;
        Chorus ch;
        ch.prepare(sr, 256);
        // Same explicit (slow) rate for both modes so ONLY the depth scalar differs;
        // slow enough that one cycle fits in the capture window.
        ch.setParams(makeParams(static_cast<int>(m), 2.0f, 1.0f, 1.0f, 1.0f));
        ch.reset();
        ch.setParams(makeParams(static_cast<int>(m), 2.0f, 1.0f, 1.0f, 1.0f));
        // One LFO period at 2 Hz == 24000 samples; capture a bit more than a period.
        constexpr int kN = 30000;
        std::vector<float> mono(kN);
        for (int i = 0; i < kN; ++i) mono[i] = static_cast<float>(i); // unit-slope ramp
        std::vector<float> L(kN, 0.0f), R(kN, 0.0f); // dry seeded 0 -> L holds wet only
        ch.process(mono.data(), L.data(), R.data(), kN);
        // Recover the per-sample L offset and take its peak-to-peak over the window
        // (skip the first samples while the line/smoother settle).
        float lo = 1e30f, hi = -1e30f;
        for (int i = 2000; i < kN; ++i) {
            const float offset = static_cast<float>(i) - L[i]; // == delL
            lo = std::min(lo, offset);
            hi = std::max(hi, offset);
        }
        return hi - lo; // ~= 2 * depthScalar * depthMs(samples)
    };
    REQUIRE(excursion(Chorus::Mode::II) > excursion(Chorus::Mode::I));
}

TEST_CASE("fxchorus: prepare/reset/process/setParams perform no heap allocation", "[fxchorus]") {
    // ADR-010 FX-10: all storage is allocated once in prepare(); the hot paths only
    // move indices and read/write the preallocated rings. Arm the alloc sentinel AFTER
    // prepare() and assert a clean scope across reset/setParams/process.
    Chorus ch;
    ch.prepare(48000.0, 512); // allocation allowed here, before arming
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::IandII), 0.0f, 1.0f, 1.0f, 0.5f));
    ch.reset();

    // Pre-size scratch so its std::vector storage exists before arming.
    constexpr int kN = 256;
    std::vector<float> mono(kN, 0.0f), L(kN, 0.0f), R(kN, 0.0f);
    for (int i = 0; i < kN; ++i) mono[i] = 0.01f * static_cast<float>(i % 32);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    ch.reset();
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::I), 0.0f, 0.8f, 0.7f, 0.6f));
    for (int i = 0; i < kN; ++i) { L[i] = mono[i]; R[i] = mono[i]; }
    ch.process(mono.data(), L.data(), R.data(), kN);
    (void) ch.latencySamples();
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

TEST_CASE("fxchorus: mix=0 leaves the dry L/R unchanged (chorus adds mix*wet)", "[fxchorus]") {
    // §5.1.3: Chorus adds mix*wet; with mix=0 the wet contributes nothing, so L/R
    // (which already hold the dry mono) are returned bit-for-bit unchanged. Negative
    // control proving the stage is additive on the dry, not a dry/wet replace.
    Chorus ch;
    ch.prepare(48000.0, 256);
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::II), 0.0f, 1.0f, 1.0f, /*mix=*/0.0f));
    ch.reset();
    ch.setParams(makeParams(static_cast<int>(Chorus::Mode::II), 0.0f, 1.0f, 1.0f, 0.0f));

    constexpr int kN = 512;
    std::vector<float> mono(kN);
    for (int i = 0; i < kN; ++i) mono[i] = 0.5f * std::sin(0.05f * static_cast<float>(i));
    std::vector<float> L = mono, R = mono;
    ch.process(mono.data(), L.data(), R.data(), kN);
    for (int i = 0; i < kN; ++i) {
        REQUIRE(L[i] == Catch::Approx(mono[i]).margin(1e-6f));
        REQUIRE(R[i] == Catch::Approx(mono[i]).margin(1e-6f));
    }
}
