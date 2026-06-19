// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/FxOversampler2xTest.cpp — acceptance tests for the dedicated
// post-voice FX Drive 2x up/down halfband pair (task 090).
//
// Test-case names begin with "fxos" so `ctest -R fxos` selects them (silent-pass
// rule; AGENTS.md). Each case maps to a `## Acceptance criteria` checkbox in
// plan/backlog/090-fxoversampler2x-dedicated-post-voice-2x.md.
//
// Covered:
//   * up->down round trip of a band-limited signal reconstructs within tolerance
//     (no gross level/aliasing error) [docs/design/07 §4.1].
//   * a tone near the original Nyquist is attenuated by the halfband on downsample
//     (anti-alias decimation) [ADR-017 L2; docs/research/10 §5].
//   * latencySamples() returns a fixed nonzero value, constant across calls and
//     independent of input [ADR-017 L2].
//   * prepare/reset/process/latencySamples perform no heap allocation and take no
//     locks [ADR-017 L10; docs/design/00 §9.1 RT-6].
//   * the FX oversampler is a DISTINCT type from the per-voice voice oversampler
//     [ADR-017 §A2, L9].

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <complex>
#include <cstring>
#include <type_traits>
#include <vector>

#include "dsp/fx/FxOversampler2x.h"
#include "dsp/Oversampler.h"
#include "calibration/FxOversampler2xConstants.h"
#include "../invariants/AudioThreadGuard.h"

using mw::fx::FxOversampler2x;
using mw::test::AudioThreadGuard;

namespace {
constexpr double kPi = 3.14159265358979323846;

// Run a base-rate signal through up->down and return the reconstructed block.
std::vector<float> roundTrip(FxOversampler2x& os, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) {
        const float* hi = os.upsampleSample(in[n]);
        out[n] = os.downsampleSample(hi);
    }
    return out;
}

// Steady-state RMS over [start, N) of a buffer.
double rms(const std::vector<float>& x, int start) {
    double acc = 0.0;
    int n = 0;
    for (int i = start; i < static_cast<int>(x.size()); ++i) {
        const double v = x[static_cast<std::size_t>(i)];
        acc += v * v;
        ++n;
    }
    return std::sqrt(acc / static_cast<double>(n));
}
} // namespace

// --- Acceptance 1: round-trip reconstruction within tolerance --------------------
TEST_CASE("fxos: up-then-down round trip reconstructs a band-limited signal within tolerance",
          "[fxos]") {
    constexpr double fs = 48000.0;
    FxOversampler2x os;
    os.prepare(/*maxBlockSize=*/512);

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
        const double ratio = rms(out, 4000) / rms(in, 4000);
        INFO("f0 = " << f0 << " Hz, amplitude ratio = " << ratio);
        REQUIRE(ratio == Catch::Approx(1.0).margin(0.01));   // within 1% across passband
    }
}

// --- Acceptance 1 (oracle/control): a constant DC level passes through at unity ---
TEST_CASE("fxos: a DC level round-trips at unity gain (level oracle)", "[fxos]") {
    FxOversampler2x os;
    os.prepare(256);
    os.reset();

    // Settle the IIR, then check steady-state output equals the DC input.
    float last = 0.0f;
    for (int n = 0; n < 8000; ++n) {
        const float* hi = os.upsampleSample(0.4f);
        last = os.downsampleSample(hi);
    }
    INFO("steady-state DC output = " << last);
    REQUIRE(last == Catch::Approx(0.4f).margin(1.0e-4));
}

// --- Acceptance 2: a tone near the original Nyquist is attenuated on downsample ---
// The downsampler is an anti-alias decimation halfband: capture its prototype
// magnitude response and confirm a tone near the original (base-rate) Nyquist —
// i.e. up at the halfband transition/stopband — is attenuated below the design
// bound. We measure the halfband prototype from the upsampler's interleaved impulse
// response (the up and down halfbands share the same prototype), then evaluate
// |H(e^jw)| at the stopband (w near the high-rate Nyquist).
TEST_CASE("fxos: a tone near the original Nyquist is attenuated by the halfband decimation",
          "[fxos]") {
    FxOversampler2x os;
    os.prepare(/*maxBlockSize=*/8192);
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

    // Passband reference at DC: the upsampler's interleaved convention has +6 dB
    // gain (the downsampler's 0.5 factor compensates). Normalize the stopband to it.
    const double ref = magDb(1.0e-4);
    INFO("passband (DC) reference = " << ref << " dB");
    REQUIRE(ref == Catch::Approx(6.0206).margin(0.05));   // 20*log10(2)

    // Stopband: w from just past the transition (0.52*pi) to the high-rate Nyquist
    // (pi). A base-rate signal at the original Nyquist (Fs/2) maps to w = pi/2 of
    // the high-rate response; everything in (pi/2, pi] is the image band the
    // decimation halfband must reject. Check the rejection past the transition.
    double worst = -1000.0;
    const int steps = 400;
    for (int i = 0; i <= steps; ++i) {
        const double w = (0.52 + (1.0 - 0.52) * (static_cast<double>(i) / steps)) * kPi;
        const double atten = magDb(w) - ref;   // normalized to passband
        if (atten > worst) worst = atten;
    }
    INFO("worst-case normalized stopband attenuation = " << worst << " dB");
    REQUIRE(worst <= mw::cal::fxos::kStopbandBoundDb);   // <= -90 dB design bound

    // And a direct time-domain check: a high tone whose IMAGE lands deep in the
    // decimation stopband must be heavily attenuated through the downsampler.
    // Feed a high-rate tone at w = 0.85*pi (well into the stopband) into the
    // downsampler directly and confirm the decimated output RMS is tiny vs input.
    constexpr double fs = 48000.0;
    os.reset();
    const int N = 16000;
    double inEnergy = 0.0, outEnergy = 0.0;
    // High-rate frequency 0.85*pi rad/sample at 2*fs == 0.85 * fs (near 2*fs Nyquist
    // = fs). This is an image well above the base Nyquist; it must be rejected.
    const double wHi = 0.85 * kPi;
    for (int n = 0; n < N; ++n) {
        const float s0 = static_cast<float>(std::sin(wHi * (2 * n)));
        const float s1 = static_cast<float>(std::sin(wHi * (2 * n + 1)));
        const float pair[2] = {s0, s1};
        const float y = os.downsampleSample(pair);
        if (n >= 4000) {
            inEnergy += (static_cast<double>(s0) * s0 + static_cast<double>(s1) * s1);
            outEnergy += static_cast<double>(y) * y;
        }
    }
    const double rejDb = 10.0 * std::log10((outEnergy + 1e-30) / (inEnergy + 1e-30));
    INFO("decimation rejection of a stopband image tone = " << rejDb << " dB");
    (void) fs;
    REQUIRE(rejDb < -40.0);   // image tone strongly rejected by the decimator
}

// --- Acceptance 3: latencySamples() fixed, nonzero, constant, input-independent ---
TEST_CASE("fxos: latencySamples returns a fixed nonzero value constant and input-independent",
          "[fxos]") {
    FxOversampler2x a;
    a.prepare(512);
    const int la = a.latencySamples();

    // Nonzero so the host actually compensates the FX Drive group delay [ADR-017 L2].
    REQUIRE(la > 0);
    // Equals the frozen reported constant (measured == declared).
    REQUIRE(la == mw::cal::fxos::kReportedLatencySamples);

    // Constant across repeated calls.
    REQUIRE(a.latencySamples() == la);
    REQUIRE(a.latencySamples() == la);

    // Independent of input: pushing arbitrary audio through never changes it.
    for (int n = 0; n < 5000; ++n) {
        const float x = 0.9f * static_cast<float>(std::sin(0.013 * n));
        const float* hi = a.upsampleSample(x);
        (void) a.downsampleSample(hi);
    }
    REQUIRE(a.latencySamples() == la);
    a.reset();
    REQUIRE(a.latencySamples() == la);   // reset does not change reported latency

    // Independent of block size: a second instance prepared at a different max block
    // size reports the same fixed latency (it is a property of the halfband, not the
    // buffer) [ADR-017 L5, L10].
    FxOversampler2x b;
    b.prepare(64);
    REQUIRE(b.latencySamples() == la);
    FxOversampler2x c;
    c.prepare(4096);
    REQUIRE(c.latencySamples() == la);
}

// --- Acceptance 3 (oracle): the measured round-trip group delay matches the report --
// The reported latency is the *measured* group delay of the up->down round trip.
// Cross-correlate the round-trip impulse response against the input to find the lag
// of peak alignment; it must equal latencySamples() (within rounding).
TEST_CASE("fxos: the reported latency equals the measured round-trip group delay",
          "[fxos]") {
    FxOversampler2x os;
    os.prepare(4096);
    os.reset();

    // Round-trip impulse response.
    const int L = 2048;
    std::vector<float> imp(static_cast<std::size_t>(L), 0.0f);
    imp[0] = 1.0f;
    const std::vector<float> resp = roundTrip(os, imp);

    // Energy-weighted centroid (group-delay proxy) of the round-trip response.
    double num = 0.0, den = 0.0;
    for (int n = 0; n < L; ++n) {
        const double e = static_cast<double>(resp[static_cast<std::size_t>(n)])
                       * static_cast<double>(resp[static_cast<std::size_t>(n)]);
        num += e * n;
        den += e;
    }
    const double centroid = num / den;
    INFO("round-trip group-delay centroid = " << centroid
         << " samples, reported = " << os.latencySamples());
    // The reported integer latency is the round-trip group delay rounded to samples.
    REQUIRE(std::lround(centroid) == os.latencySamples());
}

// --- Acceptance 4: no-alloc / no-lock; prepare is the only allocator -------------
TEST_CASE("fxos: process and latency query allocate no heap and take no locks",
          "[fxos][rt]") {
    FxOversampler2x os;
    os.prepare(/*maxBlockSize=*/512);   // the only allocator
    os.reset();

    // Pre-touch a result buffer so its storage exists before the armed scope.
    std::vector<float> sink(2048, 0.0f);

    AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    const int lat = os.latencySamples();   // query on the hot path: no alloc
    for (int n = 0; n < 1024; ++n) {
        const float x = 0.3f * static_cast<float>(std::sin(0.01 * n));
        const float* hi = os.upsampleSample(x);
        const float y = os.downsampleSample(hi);
        sink[static_cast<std::size_t>(n)] = y;
        acc += y;
    }
    os.reset();   // reset is also on the hot path: no alloc
    g.disarm();

    INFO("acc = " << acc << ", lat = " << lat);   // keep the loop from being elided
    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}

// --- Acceptance 4 (positive control): prepare DOES allocate (guard is live here) --
TEST_CASE("fxos: prepare is the allocator (positive control trips the guard)",
          "[fxos][rt]") {
    FxOversampler2x os;
    AudioThreadGuard g;
    g.arm();
    os.prepare(512);   // the legitimate allocation site, deliberately inside arm
    g.disarm();
    // Proves the guard is live in this fixture: if prepare allocated nothing the
    // no-alloc test above would be vacuous.
    REQUIRE(g.violated());
}

// --- Distinct-instance contract: FX oversampler != per-voice voice oversampler ----
TEST_CASE("fxos: the FX oversampler is a distinct type from the per-voice voice oversampler",
          "[fxos]") {
    // ADR-017 §A2 / L9: the FX Drive oversampler is a separate instance and type
    // from the per-voice voice oversampler. They must NOT be the same type.
    REQUIRE_FALSE((std::is_same<mw::fx::FxOversampler2x, mw::dsp::Oversampler>::value));
}

// --- Determinism: bit-identical kernel output across repeated runs (frozen coeffs) -
TEST_CASE("fxos: kernel output is bit-identical across repeated runs (frozen coeffs)",
          "[fxos]") {
    constexpr double fs = 48000.0;
    const int N = 4000;
    std::vector<float> in(static_cast<std::size_t>(N));
    for (int n = 0; n < N; ++n)
        in[static_cast<std::size_t>(n)] =
            0.7f * static_cast<float>(std::sin(2.0 * kPi * 997.0 * n / fs))
            + 0.2f * static_cast<float>(std::sin(2.0 * kPi * 5003.0 * n / fs));

    FxOversampler2x a;
    a.prepare(N);
    a.reset();
    const std::vector<float> outA = roundTrip(a, in);

    FxOversampler2x b;
    b.prepare(N);
    b.reset();
    const std::vector<float> outB = roundTrip(b, in);

    REQUIRE(outA.size() == outB.size());
    REQUIRE(std::memcmp(outA.data(), outB.data(),
                        outA.size() * sizeof(float)) == 0);
}
