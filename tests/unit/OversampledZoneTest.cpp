// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/OversampledZoneTest.cpp — acceptance tests for the per-voice
// oversampled-zone WRAPPER (task 047, core-filter-8). Test-case names begin with
// "os-zone" so `ctest -R os-zone --no-tests=error` selects them (silent-pass rule;
// AGENTS.md, docs/design/11 §8.2). AVOID '[' in display names.
//
// Each case maps to an `## Acceptance criteria` checkbox in
// plan/backlog/047-oversampler-zone-wrapper.md and to a normative row of ADR-004's
// Contract and docs/design/00 §8.5:
//
//   * exactly one up + one down per voice block; 1x bypasses the resamplers
//     (ADR-004 C8, Contract row 8; §10 F-09).
//   * at 2x, alias products from a full-drive self-oscillating LadderFilter run sit
//     below a higher-oversampled reference of the SAME engine (ADR-004 C12, §10 F-09)
//     — self-referential floor, no physical oracle.
//   * factor clamps to 1x when factor*fs > OS_CEILING_HZ and the clamp is recorded
//     (docs/design/00 §8.5 V15/V16; ADR-023 V15/V16).
//   * AudioThreadGuard confirms stride/factor changes + the wrapped process allocate
//     no heap and take no lock (ADR-004 C15; docs/design/00 §9.1 RT-6).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/OversampledZone.h"
#include "dsp/LadderFilter.h"
#include "dsp/FirOversampler.h"
#include "calibration/OversampledZoneConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::dsp::LadderFilter;
using mw::dsp::OversampledZone;
using mw::test::AudioThreadGuard;

namespace {

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Magnitude (linear) of the DFT bin of a real signal at frequency f (Hz), sampling
// rate fs. Naive O(N) projection — no FFT dependency.
double dftMag(const std::vector<float>& x, double f, double fs) {
    std::complex<double> acc{0.0, 0.0};
    const double w = kTwoPi * f / fs;
    for (std::size_t n = 0; n < x.size(); ++n)
        acc += static_cast<double>(x[n]) * std::exp(std::complex<double>(0.0, -w * static_cast<double>(n)));
    return std::abs(acc) / static_cast<double>(x.size());
}

} // namespace

// ---------------------------------------------------------------------------------
// Acceptance 1: exactly one up + one down per voice block; 1x bypasses the resamplers
// (ADR-004 C8, Contract row 8; docs/design/02 §10 F-09).
// ---------------------------------------------------------------------------------
TEST_CASE("os-zone: exactly one upsample and one downsample bound the zone per block",
          "[os-zone]") {
    OversampledZone zone;
    zone.prepare(/*hostFsHz=*/48000.0, /*maxBlockSize=*/256, /*factor=*/2);
    REQUIRE(zone.factor() == 2);

    std::vector<float> block(128, 0.0f);
    for (int n = 0; n < 128; ++n)
        block[static_cast<std::size_t>(n)] =
            0.3f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * n / 48000.0));

    // The callback receives the HIGH-RATE buffer (factor*numFrames samples). It must
    // be invoked exactly once per block — never once per sample, never split.
    int callbackInvocations = 0;
    int highRateSamplesSeen = 0;
    zone.process(block.data(), 128, [&](float* hi, int numHi) noexcept {
        ++callbackInvocations;
        highRateSamplesSeen = numHi;
        // identity nonlinearity for this structural check
        (void) hi;
    });

    REQUIRE(callbackInvocations == 1);
    REQUIRE(highRateSamplesSeen == 128 * 2);          // factor * numFrames
    REQUIRE(zone.upsampleCount() == 1);               // exactly one up per block
    REQUIRE(zone.downsampleCount() == 1);             // exactly one down per block
    REQUIRE(zone.lastBlockUpsamples() == 1);
    REQUIRE(zone.lastBlockDownsamples() == 1);
}

TEST_CASE("os-zone: 1x stride bypasses the resamplers and the callback runs at base rate",
          "[os-zone]") {
    OversampledZone zone;
    zone.prepare(/*hostFsHz=*/48000.0, /*maxBlockSize=*/256, /*factor=*/1);
    REQUIRE(zone.factor() == 1);
    REQUIRE_FALSE(zone.resamplersActive());           // 1x => resamplers bypassed

    std::vector<float> in(64);
    for (int n = 0; n < 64; ++n)
        in[static_cast<std::size_t>(n)] = 0.5f * static_cast<float>(std::sin(0.07 * n));
    const std::vector<float> orig = in;

    int numHiSeen = 0;
    zone.process(in.data(), 64, [&](float* hi, int numHi) noexcept {
        numHiSeen = numHi;
        (void) hi;
    });

    // At 1x the high-rate buffer IS the base-rate buffer (no stuffing/decimation).
    REQUIRE(numHiSeen == 64);
    REQUIRE(zone.resamplersActive() == false);

    // An identity callback at 1x must round-trip BIT-EXACT (the resamplers never ran,
    // so there is no reconstruction error to tolerate) [ADR-004 §10/11].
    for (std::size_t n = 0; n < orig.size(); ++n)
        REQUIRE(in[n] == orig[n]);
}

// ---------------------------------------------------------------------------------
// Acceptance 2: at 2x, alias products from a full-drive self-oscillating LadderFilter
// sit below a higher-oversampled reference of the SAME engine (ADR-004 C12; §10 F-09).
// Self-referential floor: the 2x zone vs a 4x-equivalent (FIR up to 4x, same ladder).
// ---------------------------------------------------------------------------------
TEST_CASE("os-zone: at 2x alias products sit below a higher-oversampled reference of the same engine",
          "[os-zone]") {
    // Worst case per ADR-004: full drive + self-oscillation. We push a hot saw-ish
    // base tone whose self-oscillating ladder generates broadband harmonics; aliasing
    // shows up as inharmonic energy folded below the fundamental. We compare the 2x
    // zone output against a higher-oversampled (4x) run of the SAME ladder engine; the
    // 4x run is the (more) alias-free reference because it pushes images far higher
    // before folding [ADR-004 item 5, C12: self-referential, no physical oracle].
    constexpr double hostFs = 48000.0;
    const int        N      = 8192;

    // A fundamental high enough that its low harmonics already exceed Nyquist, so any
    // aliasing folds into a CLEAN low region we can measure against.
    const double f0 = 7000.0;

    // Drive a hot input so the ladder's tanh stages generate strong harmonics, with
    // the filter self-oscillating (reso01 == 1) at a high cutoff — the §4.4 worst case.
    auto makeInput = [&](double fs) {
        std::vector<float> in(static_cast<std::size_t>(N), 0.0f);
        for (int n = 0; n < N; ++n)
            in[static_cast<std::size_t>(n)] =
                1.5f * static_cast<float>(std::sin(kTwoPi * f0 * n / fs));   // hot, clips the tanh
        return in;
    };

    // --- 2x path through the zone wrapper -----------------------------------------
    LadderFilter ladder2x;
    OversampledZone zone;
    zone.prepare(hostFs, /*maxBlockSize=*/N, /*factor=*/2);
    ladder2x.prepare(hostFs * 2.0, N * 2);
    ladder2x.setCutoffHz(9000.0f);
    ladder2x.setResonance(1.0f);                 // self-oscillation, worst-case alias exposure
    ladder2x.reset();

    std::vector<float> out2x = makeInput(hostFs);
    zone.process(out2x.data(), N, [&](float* hi, int numHi) noexcept {
        ladder2x.processBlock(hi, numHi);
    });

    // --- 4x reference: same ladder, run at 4x the host rate via a linear-phase FIR
    // chained 2x + 2x. This is the "higher-oversampled run of the same engine". ------
    LadderFilter ladder4x;
    ladder4x.prepare(hostFs * 4.0, N * 4);
    ladder4x.setCutoffHz(9000.0f);
    ladder4x.setResonance(1.0f);
    ladder4x.reset();

    mw::dsp::FirOversampler upA, upB, downA, downB;
    upA.prepare(N);
    upB.prepare(N * 2);
    downA.prepare(N * 2);
    downB.prepare(N);
    upA.reset(); upB.reset(); downA.reset(); downB.reset();

    std::vector<float> base = makeInput(hostFs);
    std::vector<float> hi2(static_cast<std::size_t>(N * 2));
    upA.upsampleBlock(base.data(), N, hi2.data());
    std::vector<float> hi4(static_cast<std::size_t>(N * 4));
    upB.upsampleBlock(hi2.data(), N * 2, hi4.data());
    ladder4x.processBlock(hi4.data(), N * 4);
    std::vector<float> back2(static_cast<std::size_t>(N * 2));
    downA.downsampleBlock(hi4.data(), N * 2, back2.data());
    std::vector<float> out4x(static_cast<std::size_t>(N));
    downB.downsampleBlock(back2.data(), N, out4x.data());

    // Measure inharmonic (alias) energy in a quiet low band, away from the fundamental
    // and its (aliased-down) harmonic partials. We skip the startup transient.
    const std::size_t skip = 2048;
    std::vector<float> tail2x(out2x.begin() + static_cast<long>(skip), out2x.end());
    std::vector<float> tail4x(out4x.begin() + static_cast<long>(skip), out4x.end());

    // Reference level = the fundamental magnitude in the 4x (cleaner) run.
    const double ref = dftMag(tail4x, f0, hostFs) + 1e-30;

    // Probe a set of low, non-harmonic alias frequencies. f0=7k harmonics land at
    // 14k(->fold 48-2*7=34k? no: 14k<24k in-band), so choose probes that are NOT
    // integer multiples of f0 and below f0: pure aliasing/quantization noise lives
    // here. The 2x zone should keep these alias products well below the fundamental;
    // the 2x floor must be no worse (within a small margin) than the 4x reference.
    double worst2xDb = -1000.0;
    double worst4xDb = -1000.0;
    for (double fp : {1234.0, 2345.0, 3456.0, 4567.0, 5678.0}) {
        const double m2 = dftMag(tail2x, fp, hostFs);
        const double m4 = dftMag(tail4x, fp, hostFs);
        const double d2 = 20.0 * std::log10((m2 + 1e-30) / ref);
        const double d4 = 20.0 * std::log10((m4 + 1e-30) / ref);
        if (d2 > worst2xDb) worst2xDb = d2;
        if (d4 > worst4xDb) worst4xDb = d4;
    }

    INFO("worst 2x alias = " << worst2xDb << " dBc, worst 4x (reference) alias = "
                             << worst4xDb << " dBc");

    // Self-referential floor: the 2x zone's worst inharmonic alias product must be
    // comfortably below the fundamental (a real anti-aliased path), AND no more than a
    // small margin above the higher-oversampled reference floor [ADR-004 C12]. We do
    // NOT assert a physical-unit number; the 4x run is the oracle of "good enough".
    REQUIRE(worst2xDb < -60.0);                       // alias products well below fundamental
    REQUIRE(worst2xDb <= worst4xDb + 12.0);           // within margin of the cleaner reference
}

// ---------------------------------------------------------------------------------
// Acceptance 2 (negative control): WITHOUT the zone (no oversampling), the same hot
// self-oscillating ladder aliases far worse, proving the 2x zone is doing real work.
// ---------------------------------------------------------------------------------
TEST_CASE("os-zone: a 1x (no-oversampling) run aliases worse than the 2x zone (control)",
          "[os-zone]") {
    constexpr double hostFs = 48000.0;
    const int        N      = 8192;
    const double     f0     = 7000.0;

    auto makeInput = [&]() {
        std::vector<float> in(static_cast<std::size_t>(N), 0.0f);
        for (int n = 0; n < N; ++n)
            in[static_cast<std::size_t>(n)] =
                1.5f * static_cast<float>(std::sin(kTwoPi * f0 * n / hostFs));
        return in;
    };

    // 1x: ladder runs straight at host rate inside a 1x zone (resamplers bypassed).
    LadderFilter ladder1x;
    OversampledZone zone1;
    zone1.prepare(hostFs, N, 1);
    ladder1x.prepare(hostFs, N);
    ladder1x.setCutoffHz(9000.0f);
    ladder1x.setResonance(1.0f);
    ladder1x.reset();
    std::vector<float> out1x = makeInput();
    zone1.process(out1x.data(), N, [&](float* hi, int numHi) noexcept {
        ladder1x.processBlock(hi, numHi);
    });

    // 2x zone, same engine.
    LadderFilter ladder2x;
    OversampledZone zone2;
    zone2.prepare(hostFs, N, 2);
    ladder2x.prepare(hostFs * 2.0, N * 2);
    ladder2x.setCutoffHz(9000.0f);
    ladder2x.setResonance(1.0f);
    ladder2x.reset();
    std::vector<float> out2x = makeInput();
    zone2.process(out2x.data(), N, [&](float* hi, int numHi) noexcept {
        ladder2x.processBlock(hi, numHi);
    });

    const std::size_t skip = 2048;
    std::vector<float> tail1x(out1x.begin() + static_cast<long>(skip), out1x.end());
    std::vector<float> tail2x(out2x.begin() + static_cast<long>(skip), out2x.end());

    const double ref1 = dftMag(tail1x, f0, hostFs) + 1e-30;
    const double ref2 = dftMag(tail2x, f0, hostFs) + 1e-30;

    double worst1xDb = -1000.0, worst2xDb = -1000.0;
    for (double fp : {1234.0, 2345.0, 3456.0, 4567.0, 5678.0}) {
        const double d1 = 20.0 * std::log10((dftMag(tail1x, fp, hostFs) + 1e-30) / ref1);
        const double d2 = 20.0 * std::log10((dftMag(tail2x, fp, hostFs) + 1e-30) / ref2);
        if (d1 > worst1xDb) worst1xDb = d1;
        if (d2 > worst2xDb) worst2xDb = d2;
    }
    INFO("worst 1x alias = " << worst1xDb << " dBc, worst 2x alias = " << worst2xDb << " dBc");
    REQUIRE(worst2xDb < worst1xDb);   // 2x oversampling lowers the alias floor
}

// ---------------------------------------------------------------------------------
// Acceptance 3: factor clamps to 1x when factor*fs > OS_CEILING_HZ and the clamp is
// recorded for provenance (docs/design/00 §8.5 V15/V16; ADR-023 V15/V16).
// ---------------------------------------------------------------------------------
TEST_CASE("os-zone: factor clamps to 1x above the OS ceiling and records the clamp",
          "[os-zone]") {
    // 192 kHz host * 2x = 384 kHz internal > 192 kHz ceiling => must clamp to 1x.
    OversampledZone zoneHigh;
    zoneHigh.prepare(/*hostFsHz=*/192000.0, /*maxBlockSize=*/64, /*factor=*/2);
    REQUIRE(zoneHigh.requestedFactor() == 2);
    REQUIRE(zoneHigh.factor() == 1);                  // active factor clamped to 1x
    REQUIRE(zoneHigh.clampedToEco());                 // provenance flag set
    REQUIRE_FALSE(zoneHigh.resamplersActive());

    // 176.4 kHz host * 2x = 352.8 kHz > ceiling => clamp.
    OversampledZone zone176;
    zone176.prepare(176400.0, 64, 2);
    REQUIRE(zone176.factor() == 1);
    REQUIRE(zone176.clampedToEco());

    // 96 kHz host * 2x = 192 kHz lands EXACTLY on the ceiling => NOT clamped (allowed).
    OversampledZone zone96;
    zone96.prepare(96000.0, 64, 2);
    REQUIRE(zone96.factor() == 2);
    REQUIRE_FALSE(zone96.clampedToEco());

    // Blessed 48 kHz host * 2x = 96 kHz, well under the ceiling => not clamped.
    OversampledZone zone48;
    zone48.prepare(48000.0, 64, 2);
    REQUIRE(zone48.factor() == 2);
    REQUIRE_FALSE(zone48.clampedToEco());

    // A 1x request is never "clamped" — it is already eco.
    OversampledZone zoneEco;
    zoneEco.prepare(192000.0, 64, 1);
    REQUIRE(zoneEco.factor() == 1);
    REQUIRE_FALSE(zoneEco.clampedToEco());            // 1x requested, not forced down
}

// ---------------------------------------------------------------------------------
// Acceptance 4: AudioThreadGuard confirms stride/factor changes and the wrapped
// process allocate no heap and take no lock (ADR-004 C15; docs/design/00 §9.1 RT-6).
// ---------------------------------------------------------------------------------
TEST_CASE("os-zone: factor change and wrapped process allocate no heap on the audio thread",
          "[os-zone][rt]") {
    OversampledZone zone;
    zone.prepare(/*hostFsHz=*/48000.0, /*maxBlockSize=*/256, /*factor=*/2);   // the only allocator
    zone.reset();

    LadderFilter ladder;
    ladder.prepare(48000.0 * 2.0, 256 * 2);
    ladder.setCutoffHz(1200.0f);
    ladder.setResonance(0.7f);
    ladder.reset();

    std::vector<float> block(256);
    for (int n = 0; n < 256; ++n)
        block[static_cast<std::size_t>(n)] = 0.3f * static_cast<float>(std::sin(0.05 * n));

    AudioThreadGuard g;
    g.arm();
    // Vary the stride (must be allocation-free, stride-only) ...
    zone.setFactor(1);
    zone.setFactor(2);
    // ... and run several wrapped blocks at 2x and 1x.
    for (int rep = 0; rep < 4; ++rep) {
        zone.process(block.data(), 256, [&](float* hi, int numHi) noexcept {
            ladder.processBlock(hi, numHi);
        });
    }
    zone.setFactor(1);
    zone.process(block.data(), 256, [&](float* hi, int numHi) noexcept {
        ladder.processBlock(hi, numHi);
    });
    zone.reset();   // reset is also on the hot path: no alloc
    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}

// Positive control: prepare DOES allocate, proving the guard is live in this fixture.
TEST_CASE("os-zone: prepare is the allocator (positive control trips the guard)",
          "[os-zone][rt]") {
    OversampledZone zone;
    AudioThreadGuard g;
    g.arm();
    zone.prepare(48000.0, 256, 2);   // the legitimate allocation site
    g.disarm();
    REQUIRE(g.violated());
}
