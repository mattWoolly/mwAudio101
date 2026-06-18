// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/FirOversamplerTest.cpp — acceptance tests for the OFFLINE linear-phase
// FIR halfband up/downsampler (task 037, core-filter-7). Test-case names begin with
// "os-fir" so `ctest -R os-fir` selects them (silent-pass rule; AGENTS.md). Each case
// maps to a `## Acceptance criteria` checkbox in plan/backlog/037-oversampler-2.md.
//
// Covered:
//   * FIR halfband is linear-phase (symmetric taps + symmetric round-trip impulse)
//     and round-trip reconstructs a band-limited signal within tolerance (ADR-004 C7).
//   * firLatencySamples() returns the exact constant group delay matching the tap
//     count, AND the round-trip impulse peak lands on that exact integer base-rate
//     sample (C14).
//   * taps are frozen versioned constants; output bit-identical across runs (C14).
//   * halfband structure / stopband attenuation oracle vs the design bound, and a
//     phase-linearity contrast against the realtime IIR path (ADR-004 C7, C16).
//   * no heap alloc / no lock in the kernels; prepare is the only allocator (RT).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include "dsp/FirOversampler.h"
#include "dsp/Oversampler.h"
#include "calibration/OversamplerFirConstants.h"
#include "../invariants/AudioThreadGuard.h"

using mw::dsp::FirOversampler;
using mw::dsp::Oversampler;
using mw::test::AudioThreadGuard;

namespace {
constexpr double kPi = 3.14159265358979323846;

// Run a base-rate signal through up->down at the fixed 2x render ratio.
std::vector<float> roundTrip(FirOversampler& os, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    float hi[2];
    for (std::size_t n = 0; n < in.size(); ++n) {
        os.upsampleSample(in[n], hi);
        out[n] = os.downsampleSample(hi);
    }
    return out;
}

// Round-trip impulse response (base-rate) of length N.
std::vector<float> roundTripImpulse(FirOversampler& os, int N) {
    std::vector<float> out(static_cast<std::size_t>(N), 0.0f);
    float hi[2];
    for (int n = 0; n < N; ++n) {
        const float x = (n == 0) ? 1.0f : 0.0f;
        os.upsampleSample(x, hi);
        out[static_cast<std::size_t>(n)] = os.downsampleSample(hi);
    }
    return out;
}
} // namespace

// --- Acceptance 1a: taps are linear-phase (exactly symmetric) --------------------
TEST_CASE("os-fir: prototype taps are exactly symmetric so the FIR is linear-phase",
          "[os-fir]") {
    const auto& t = mw::cal::osfir::kProtoTaps;
    REQUIRE(t.size() % 2 == 1);                 // Type-I (odd length) linear phase
    for (std::size_t n = 0; n < t.size() / 2; ++n) {
        INFO("tap pair n=" << n);
        REQUIRE(t[n] == t[t.size() - 1 - n]);   // bit-exact symmetry, not "close"
    }
    // Halfband structure: center tap ~ 1/2, every other off-center even-offset tap 0.
    const std::size_t c = mw::cal::osfir::kGroupDelayHi;
    REQUIRE(t[c] == Catch::Approx(0.5).margin(1.0e-5));
    for (std::size_t n = 0; n < t.size(); ++n) {
        const std::ptrdiff_t m =
            static_cast<std::ptrdiff_t>(n) - static_cast<std::ptrdiff_t>(c);
        if (m != 0 && (m % 2 == 0)) {
            INFO("off-center even tap n=" << n);
            REQUIRE(t[n] == 0.0);
        }
    }
}

// --- Acceptance 1b: round-trip reconstructs a band-limited signal within tolerance
TEST_CASE("os-fir: round-trip up then down reconstructs a band-limited tone within tolerance",
          "[os-fir]") {
    constexpr double fs = 48000.0;
    FirOversampler os;
    os.prepare(/*maxBlockSize=*/512);
    REQUIRE(os.isPrepared());

    const int N = 16000;
    for (double f0 : {200.0, 1000.0, 3000.0, 6000.0}) {
        os.reset();
        std::vector<float> in(static_cast<std::size_t>(N));
        for (int n = 0; n < N; ++n)
            in[static_cast<std::size_t>(n)] =
                0.5f * static_cast<float>(std::sin(2.0 * kPi * f0 * n / fs));

        const std::vector<float> out = roundTrip(os, in);

        // Steady-state energy ratio (skip the FIR start-up transient). A correct
        // linear-phase halfband round trip reconstructs to near unity in the passband
        // (phase-robust metric: the round-trip group delay is a constant integer).
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

// --- Acceptance 1c: the round-trip impulse response is SYMMETRIC about its peak ---
// (linear phase => symmetric impulse response; this is the time-domain oracle).
TEST_CASE("os-fir: round-trip impulse response is symmetric about the group-delay center",
          "[os-fir]") {
    FirOversampler os;
    os.prepare(512);
    os.reset();

    const int center = FirOversampler::firLatencySamples();   // group-delay center
    const int span   = 60;                                    // taps to compare around it
    const std::vector<float> h = roundTripImpulse(os, center + span + 8);

    // Peak at the center.
    int pk = 0;
    for (int n = 1; n < static_cast<int>(h.size()); ++n)
        if (std::fabs(h[static_cast<std::size_t>(n)]) > std::fabs(h[static_cast<std::size_t>(pk)]))
            pk = n;
    INFO("impulse peak index = " << pk << " (expected center = " << center << ")");
    REQUIRE(pk == center);

    // Symmetry: h[center - k] == h[center + k] (bit-exact: linear phase, frozen taps).
    for (int k = 1; k <= span; ++k) {
        const float a = h[static_cast<std::size_t>(center - k)];
        const float b = h[static_cast<std::size_t>(center + k)];
        INFO("k = " << k << "  h[c-k]=" << a << "  h[c+k]=" << b);
        REQUIRE(a == b);
    }
}

// --- Acceptance 2: firLatencySamples() == exact constant group delay vs tap count -
TEST_CASE("os-fir: firLatencySamples equals the constant group delay matching the tap count",
          "[os-fir]") {
    // Tap-count relationship: a Type-I FIR of length N has one-direction group delay
    // (N-1)/2 high-rate samples; the up->down round trip is (N-1) high-rate = (N-1)/2
    // base-rate samples [ADR-004 C14].
    const int N = static_cast<int>(FirOversampler::kNumTaps);
    REQUIRE(FirOversampler::firGroupDelayHiSamples() == (N - 1) / 2);
    REQUIRE(FirOversampler::firLatencySamples()     == (N - 1) / 2);
    REQUIRE(FirOversampler::firLatencySamples()
            == static_cast<int>(mw::cal::osfir::kRoundTripLatencyBase));

    // It is a compile-time constant (constexpr) and independent of prepare/state.
    static_assert(FirOversampler::firLatencySamples()
                      == static_cast<int>(mw::cal::osfir::kRoundTripLatencyBase),
                  "firLatencySamples must be a constexpr matching the frozen tap count");

    // And it is the value actually realized by the round-trip impulse peak (the test
    // above already pins the peak; here we cross-check the reported number equals it).
    FirOversampler os;
    os.prepare(256);
    os.reset();
    const std::vector<float> h = roundTripImpulse(os, FirOversampler::firLatencySamples() + 70);
    int pk = 0;
    for (int n = 1; n < static_cast<int>(h.size()); ++n)
        if (std::fabs(h[static_cast<std::size_t>(n)]) > std::fabs(h[static_cast<std::size_t>(pk)]))
            pk = n;
    REQUIRE(pk == FirOversampler::firLatencySamples());
}

// --- Acceptance 3: taps frozen + output bit-identical across runs -----------------
TEST_CASE("os-fir: kernel output is bit-identical across repeated runs (frozen taps)",
          "[os-fir]") {
    constexpr double fs = 48000.0;
    const int N = 4000;
    std::vector<float> in(static_cast<std::size_t>(N));
    for (int n = 0; n < N; ++n)
        in[static_cast<std::size_t>(n)] =
            0.7f * static_cast<float>(std::sin(2.0 * kPi * 997.0 * n / fs))
            + 0.2f * static_cast<float>(std::sin(2.0 * kPi * 5003.0 * n / fs));

    FirOversampler a;
    a.prepare(N);
    a.reset();
    const std::vector<float> outA = roundTrip(a, in);

    FirOversampler b;
    b.prepare(N);
    b.reset();
    const std::vector<float> outB = roundTrip(b, in);

    // Byte-for-byte identical: frozen taps + fixed evaluation order, no fast-math
    // reassociation [ADR-004 C14; docs/design/00 §9.1 RT-7].
    REQUIRE(outA.size() == outB.size());
    REQUIRE(std::memcmp(outA.data(), outB.data(),
                        outA.size() * sizeof(float)) == 0);

    // Also lock the high-rate interleaved samples directly run-to-run.
    FirOversampler c, d;
    c.prepare(64); c.reset();
    d.prepare(64); d.reset();
    float hc[2], hd[2];
    for (int n = 0; n < N; ++n) {
        c.upsampleSample(in[static_cast<std::size_t>(n)], hc);
        d.upsampleSample(in[static_cast<std::size_t>(n)], hd);
        REQUIRE(std::memcmp(hc, hd, 2 * sizeof(float)) == 0);
        (void) c.downsampleSample(hc);
        (void) d.downsampleSample(hd);
    }
}

// --- Acceptance 3 (freeze guard): the taps ARE the versioned renderVersion-1 set --
TEST_CASE("os-fir: taps are the frozen versioned constant set with the documented shape",
          "[os-fir]") {
    // The frozen set is versioned (renderVersion 1) and shaped by the design: length
    // 129, == 1 (mod 4) for integer base-rate latency, DC gain exactly 1 (so the
    // round trip is unity at DC), and a stopband bound at or below the design edge.
    REQUIRE(mw::cal::osfir::kNumTaps == 129);
    REQUIRE(mw::cal::osfir::kNumTaps % 4 == 1);
    REQUIRE(mw::cal::osfir::kGroupDelayHi == 64);
    REQUIRE(mw::cal::osfir::kRoundTripLatencyBase == 64);

    double dc = 0.0;
    for (double t : mw::cal::osfir::kProtoTaps) dc += t;
    INFO("DC gain (sum of taps) = " << dc);
    REQUIRE(dc == Catch::Approx(1.0).margin(1.0e-9));
}

// --- Halfband stopband oracle: linear-phase FIR meets the design bound ------------
TEST_CASE("os-fir: halfband stopband attenuation meets the design bound", "[os-fir]") {
    // Evaluate |H(e^jw)| of the frozen prototype directly (w = pi at the high-rate
    // Nyquist). The stopband (just past the symmetric transition) must reject by at
    // least the design bound, so 2x-downsampled images stay below the aliasing floor.
    const auto& t = mw::cal::osfir::kProtoTaps;
    auto magDb = [&](double w) {
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t k = 0; k < t.size(); ++k)
            acc += t[k] * std::exp(std::complex<double>(0.0, -w * static_cast<double>(k)));
        return 20.0 * std::log10(std::abs(acc) + 1.0e-30);
    };
    // Passband reference at DC is ~0 dB (taps sum to 1).
    const double ref = magDb(1.0e-4);
    INFO("DC reference = " << ref << " dB");
    REQUIRE(ref == Catch::Approx(0.0).margin(0.01));

    double worst = -1000.0;
    const int steps = 400;
    for (int i = 0; i <= steps; ++i) {
        const double w = (0.6 + (1.0 - 0.6) * (static_cast<double>(i) / steps)) * kPi;
        const double atten = magDb(w) - ref;
        if (atten > worst) worst = atten;
    }
    INFO("worst-case stopband attenuation (>=0.6pi) = " << worst << " dB");
    REQUIRE(worst <= mw::cal::osfir::kStopbandBoundDb);   // <= -90 dB design bound
}

// --- Phase-linearity contrast: FIR round-trip is symmetric where IIR is not -------
// (ADR-004 C7/C16: the FIR render path is linear-phase, the realtime IIR path is not.
//  This is the documented audible phase divergence, asserted objectively here.)
TEST_CASE("os-fir: FIR round-trip impulse is phase-symmetric unlike the IIR path",
          "[os-fir]") {
    const int center = FirOversampler::firLatencySamples();
    const int len    = center + 80;

    FirOversampler fir;
    fir.prepare(512); fir.reset();
    const std::vector<float> hFir = roundTripImpulse(fir, len);

    // FIR: symmetric about its peak (already pinned bit-exact elsewhere); confirm the
    // symmetry-energy is essentially zero.
    double firAsym = 0.0;
    for (int k = 1; k <= 40; ++k)
        firAsym += std::fabs(static_cast<double>(hFir[static_cast<std::size_t>(center - k)])
                           - static_cast<double>(hFir[static_cast<std::size_t>(center + k)]));
    INFO("FIR round-trip asymmetry energy = " << firAsym);
    REQUIRE(firAsym == Catch::Approx(0.0).margin(1.0e-7));

    // IIR (realtime path): minimum-phase-ish allpass cascade -> NOT symmetric. Find
    // its round-trip impulse peak and show meaningful asymmetry about it.
    Oversampler iir;
    iir.prepare(512, 2);
    iir.reset();
    std::vector<float> hIir(static_cast<std::size_t>(len), 0.0f);
    for (int n = 0; n < len; ++n) {
        const float x = (n == 0) ? 1.0f : 0.0f;
        const float* hi = iir.upsampleSample(x);
        hIir[static_cast<std::size_t>(n)] = iir.downsampleSample(hi);
    }
    int ipk = 0;
    for (int n = 1; n < len; ++n)
        if (std::fabs(hIir[static_cast<std::size_t>(n)]) > std::fabs(hIir[static_cast<std::size_t>(ipk)]))
            ipk = n;
    double iirAsym = 0.0;
    for (int k = 1; k <= 8 && ipk - k >= 0 && ipk + k < len; ++k)
        iirAsym += std::fabs(static_cast<double>(hIir[static_cast<std::size_t>(ipk - k)])
                           - static_cast<double>(hIir[static_cast<std::size_t>(ipk + k)]));
    INFO("IIR round-trip asymmetry energy about peak " << ipk << " = " << iirAsym);
    REQUIRE(iirAsym > 1.0e-3);   // demonstrably non-linear-phase, unlike the FIR
}

// --- RT: kernels allocate no heap / acquire no lock; prepare is the allocator -----
TEST_CASE("os-fir: up/down kernels and reset allocate no heap (RT discipline)",
          "[os-fir][rt]") {
    FirOversampler os;
    os.prepare(/*maxBlockSize=*/512);   // the only allocator
    os.reset();

    std::vector<float> sink(2048, 0.0f);   // pre-touch storage before the armed scope

    AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    float hi[2];
    for (int n = 0; n < 1024; ++n) {
        const float x = 0.3f * static_cast<float>(std::sin(0.01 * n));
        os.upsampleSample(x, hi);
        const float y = os.downsampleSample(hi);
        sink[static_cast<std::size_t>(n)] = y;
        acc += y;
    }
    os.reset();   // reset must also not allocate [ADR-004 C15]
    g.disarm();

    INFO("acc = " << acc);   // keep the loop from being optimized away
    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}

// --- RT (positive control): prepare DOES allocate (proves the guard is live) ------
TEST_CASE("os-fir: prepare is the allocator (positive control trips the guard)",
          "[os-fir][rt]") {
    FirOversampler os;
    AudioThreadGuard g;
    g.arm();
    os.prepare(512);   // the legitimate allocation site, deliberately inside arm
    g.disarm();
    REQUIRE(g.violated());
}
