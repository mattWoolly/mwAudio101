// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/OversamplerTest.cpp — acceptance tests for the realtime polyphase IIR
// halfband up/downsampler (task 036). Test-case names begin with "os-iir" so
// `ctest -R os-iir` selects them (silent-pass rule; AGENTS.md). Each case maps to a
// `## Acceptance criteria` checkbox in plan/backlog/036-oversampler.md.
//
// Covered:
//   * round-trip up->down reconstruction within tolerance + halfband stopband
//     attenuation meets the design bound (ADR-004 C4).
//   * no heap alloc / no lock in upsample/downsample/setFactor; prepare is the only
//     allocator (ADR-004 C15; docs/design/00 §9.1 RT-6).
//   * bit-identical kernel output across repeated runs for fixed input/coeffs
//     (frozen constants, ADR-004 C14).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include "dsp/Oversampler.h"
#include "calibration/OversamplerConstants.h"
#include "../invariants/AudioThreadGuard.h"

using mw::dsp::Oversampler;
using mw::test::AudioThreadGuard;

namespace {
constexpr double kPi = 3.14159265358979323846;

// Run a base-rate signal through up->down at 2x and return the reconstructed block.
std::vector<float> roundTrip(Oversampler& os, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) {
        const float* hi = os.upsampleSample(in[n]);
        out[n] = os.downsampleSample(hi);
    }
    return out;
}
} // namespace

// --- Acceptance 1a: round-trip reconstruction within tolerance -------------------
TEST_CASE("os-iir: round-trip up->down reconstructs a band-limited tone within tolerance",
          "[os-iir]") {
    constexpr double fs = 48000.0;
    Oversampler os;
    os.prepare(/*maxBlockSize=*/512, /*maxFactor=*/2);
    REQUIRE(os.factor() == 2);   // blessed 2x default after prepare [ADR-004 C10]

    // Band-limited tones well inside the halfband passband (<= Fs/8).
    const int N = 16000;
    for (double f0 : {200.0, 1000.0, 3000.0, 6000.0}) {
        os.reset();
        std::vector<float> in(static_cast<std::size_t>(N));
        for (int n = 0; n < N; ++n)
            in[static_cast<std::size_t>(n)] =
                0.5f * static_cast<float>(std::sin(2.0 * kPi * f0 * n / fs));

        const std::vector<float> out = roundTrip(os, in);

        // Steady-state energy ratio (skip the IIR start-up transient). A correct
        // crossed-branch round trip reconstructs to near unity in the passband.
        double rmsIn = 0.0, rmsOut = 0.0;
        for (int n = 4000; n < N; ++n) {
            const double a = in[static_cast<std::size_t>(n)];
            const double b = out[static_cast<std::size_t>(n)];
            rmsIn += a * a;
            rmsOut += b * b;
        }
        const double ratio = std::sqrt(rmsOut / rmsIn);
        INFO("f0 = " << f0 << " Hz, amplitude ratio = " << ratio);
        REQUIRE(ratio == Catch::Approx(1.0).margin(0.01));   // within 1% across passband
    }
}

// --- Acceptance 1a (negative control): 1x is an exact pass-through ---------------
TEST_CASE("os-iir: at 1x the resampler is an exact identity pass-through (control)",
          "[os-iir]") {
    Oversampler os;
    os.prepare(256, 2);
    os.setFactor(1);
    REQUIRE(os.factor() == 1);

    for (float x : {-0.9f, -0.1f, 0.0f, 0.25f, 0.8f}) {
        const float* hi = os.upsampleSample(x);
        REQUIRE(os.downsampleSample(hi) == x);   // bit-exact identity, not "close"
    }
}

// --- Acceptance 1b: halfband stopband attenuation meets the design bound ---------
TEST_CASE("os-iir: halfband stopband attenuation meets the design bound", "[os-iir]") {
    // Capture the halfband prototype impulse response from the UPSAMPLER: feeding a
    // unit impulse then zeros yields the interleaved high-rate impulse response of
    // H(z); its magnitude over w in [0, pi] (w = pi at the high-rate Nyquist) is the
    // halfband response. We then evaluate |H(e^jw)| at stopband frequencies by a
    // direct DFT sum (no FFT dependency) and confirm attenuation <= the bound.
    Oversampler os;
    os.prepare(/*maxBlockSize=*/8192, /*maxFactor=*/2);
    os.reset();

    const int L = 4096;                       // base-rate impulse length
    std::vector<double> h;                    // interleaved high-rate impulse response
    h.reserve(static_cast<std::size_t>(2 * L));
    for (int n = 0; n < L; ++n) {
        const float x = (n == 0) ? 1.0f : 0.0f;
        const float* hi = os.upsampleSample(x);
        h.push_back(static_cast<double>(hi[0]));
        h.push_back(static_cast<double>(hi[1]));
    }

    auto magDb = [&](double w) {
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t k = 0; k < h.size(); ++k)
            acc += h[k] * std::exp(std::complex<double>(0.0, -w * static_cast<double>(k)));
        return 20.0 * std::log10(std::abs(acc) + 1.0e-30);
    };

    // Passband reference at DC: the upsampler's interleaved convention has +6 dB gain
    // (the downsampler's 0.5 factor compensates). Normalize the stopband to this.
    const double ref = magDb(1.0e-4);
    INFO("passband (DC) reference = " << ref << " dB");
    REQUIRE(ref == Catch::Approx(6.0206).margin(0.05));   // 20*log10(2)

    // Stopband: w from just past the transition (0.52*pi) to the Nyquist (pi).
    double worst = -1000.0;
    const int steps = 400;
    for (int i = 0; i <= steps; ++i) {
        const double w = (0.52 + (1.0 - 0.52) * (static_cast<double>(i) / steps)) * kPi;
        const double atten = magDb(w) - ref;   // normalized to passband
        if (atten > worst) worst = atten;
    }
    INFO("worst-case normalized stopband attenuation = " << worst << " dB");
    REQUIRE(worst <= mw::cal::osiir::kStopbandBoundDb);   // <= -90 dB design bound
}

// --- Acceptance 2: no-alloc / no-lock; prepare is the only allocator -------------
TEST_CASE("os-iir: upsample/downsample and factor change allocate no heap (RT)",
          "[os-iir][rt]") {
    Oversampler os;
    os.prepare(/*maxBlockSize=*/512, /*maxFactor=*/2);   // the only allocator
    os.reset();

    // Pre-touch a result buffer so its storage exists before the armed scope.
    std::vector<float> sink(2048, 0.0f);

    AudioThreadGuard g;
    g.arm();
    // A factor change must vary stride only, never allocate [ADR-004 C15].
    os.setFactor(1);
    os.setFactor(2);
    float acc = 0.0f;
    for (int n = 0; n < 1024; ++n) {
        const float x = 0.3f * static_cast<float>(std::sin(0.01 * n));
        const float* hi = os.upsampleSample(x);
        const float y = os.downsampleSample(hi);
        sink[static_cast<std::size_t>(n)] = y;
        acc += y;
    }
    os.reset();   // reset is also on the hot path: no alloc [ADR-004 C15]
    g.disarm();

    INFO("acc = " << acc);   // keep the loop from being optimized away
    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}

// --- Acceptance 2 (positive control): prepare DOES allocate (guard works here) ---
TEST_CASE("os-iir: prepare is the allocator (positive control trips the guard)",
          "[os-iir][rt]") {
    Oversampler os;
    AudioThreadGuard g;
    g.arm();
    os.prepare(512, 2);   // the legitimate allocation site, deliberately inside arm
    g.disarm();
    // Proves the guard is live in this fixture: if prepare allocated nothing the
    // no-alloc test above would be vacuous.
    REQUIRE(g.violated());
}

// --- Acceptance 3: bit-identical kernel output across repeated runs --------------
TEST_CASE("os-iir: kernel output is bit-identical across repeated runs (frozen coeffs)",
          "[os-iir]") {
    constexpr double fs = 48000.0;
    const int N = 4000;
    std::vector<float> in(static_cast<std::size_t>(N));
    for (int n = 0; n < N; ++n)
        in[static_cast<std::size_t>(n)] =
            0.7f * static_cast<float>(std::sin(2.0 * kPi * 997.0 * n / fs))
            + 0.2f * static_cast<float>(std::sin(2.0 * kPi * 5003.0 * n / fs));

    Oversampler a;
    a.prepare(N, 2);
    a.reset();
    const std::vector<float> outA = roundTrip(a, in);

    Oversampler b;
    b.prepare(N, 2);
    b.reset();
    const std::vector<float> outB = roundTrip(b, in);

    // Byte-for-byte identical: frozen coefficients + fixed evaluation order, no
    // fast-math reassociation [ADR-004 C14; docs/design/00 §9.1 RT-7].
    REQUIRE(outA.size() == outB.size());
    REQUIRE(std::memcmp(outA.data(), outB.data(),
                        outA.size() * sizeof(float)) == 0);

    // Also assert the high-rate interleaved samples themselves are bit-identical run
    // to run (not just the round-trip), to lock the kernel output directly.
    Oversampler c, d;
    c.prepare(64, 2); c.reset();
    d.prepare(64, 2); d.reset();
    for (int n = 0; n < N; ++n) {
        const float* hc = c.upsampleSample(in[static_cast<std::size_t>(n)]);
        const float* hd = d.upsampleSample(in[static_cast<std::size_t>(n)]);
        REQUIRE(std::memcmp(hc, hd, 2 * sizeof(float)) == 0);
        (void) c.downsampleSample(hc);
        (void) d.downsampleSample(hd);
    }
}
