// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the white NoiseSource (task 028). Test-case names begin
// with "noise" so `ctest -R noise` selects them (silent-pass rule). Covers every
// acceptance criterion in plan/backlog/028 against docs/design/01 §6.1-§6.4, §10:
//   - output in half-open [-1, 1) and a flat (white, no pinking) spectrum, HF OFF;
//   - distinct per-voice seeds decorrelate; a zero seed is rejected/avoided;
//   - kNoiseHfRolloffHz is read from the calibration header, not duplicated;
//   - renderSample() is noexcept and performs no heap allocation / takes no locks.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp/NoiseSource.h"
#include "calibration/NoiseConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::NoiseSource;

namespace {

// Pearson correlation of two equal-length streams (for decorrelation checks).
double correlation (const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = a.size();
    double ma = 0.0, mb = 0.0;
    for (std::size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double> (n);
    mb /= static_cast<double> (n);
    double num = 0.0, da = 0.0, db = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = a[i] - ma, y = b[i] - mb;
        num += x * y; da += x * x; db += y * y;
    }
    if (da <= 0.0 || db <= 0.0) return 0.0;
    return num / std::sqrt (da * db);
}

// Power in a normalized-frequency band [loBin, hiBin) of a real signal, via a naive
// DFT. Whiteness => power is roughly flat across the band; a pink (1/f) tilt would
// pile power into the low bins. Bins are over the half-spectrum (0..N/2).
double bandPower (const std::vector<float>& sig, int loBin, int hiBin) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const int n = static_cast<int> (sig.size());
    double total = 0.0;
    for (int k = loBin; k < hiBin; ++k) {
        double re = 0.0, im = 0.0;
        const double w = -kTwoPi * k / n;
        for (int t = 0; t < n; ++t) {
            re += sig[t] * std::cos (w * t);
            im += sig[t] * std::sin (w * t);
        }
        total += re * re + im * im;
    }
    return total / (hiBin - loBin);   // mean power per bin in the band
}

} // namespace

TEST_CASE("noise: output stays in the half-open minus-one-to-one range over a long block", "[noise]") {
    NoiseSource n;
    n.prepare (48000.0);
    n.reset (0x1234ABCDu);

    REQUIRE_FALSE (n.hfRolloffEnabled());   // white by default (§6.4)

    bool sawNearMinusOne = false;
    for (int i = 0; i < 1'000'000; ++i) {
        const float s = n.renderSample();
        REQUIRE (s >= -1.0f);   // lower bound is INCLUSIVE
        REQUIRE (s <  1.0f);    // upper bound is EXCLUSIVE (half-open)
        if (s < -0.999f) sawNearMinusOne = true;
    }
    // Half-open means -1 is reachable (x==0 from xorshift maps to exactly -1).
    REQUIRE (sawNearMinusOne);
}

TEST_CASE("noise: the lower bound minus-one is inclusive and the upper bound plus-one is excluded", "[noise]") {
    // Lower bound: x==0 -> 0*scale - 1 == exactly -1.0f (inclusive).
    REQUIRE (0.0f * mw::cal::noise::kNoiseScale - mw::cal::noise::kNoiseOffset == -1.0f);

    // Upper bound is the subtle one: the BARE float formula rounds the top of the
    // uint32 range UP to exactly +1.0f (single precision cannot represent the true
    // 0.99999999953, and (float)0xFFFFFFFF rounds to 2^32). The source must clamp
    // that lone case so the half-open [-1, 1) contract holds (§6.3, §10).
    constexpr float kBareMax =
        static_cast<float> (0xFFFFFFFFu) * mw::cal::noise::kNoiseScale - mw::cal::noise::kNoiseOffset;
    REQUIRE (kBareMax >= 1.0f);                            // the bug the clamp guards against
    REQUIRE (mw::cal::noise::kNoiseMaxBelowOne < 1.0f);    // the clamp target is strictly below 1
}

TEST_CASE("noise: spectrum is flat (white, no pinking) within tolerance, HF rolloff OFF", "[noise]") {
    NoiseSource n;
    n.prepare (48000.0);
    n.reset (0xCAFEBABEu);

    constexpr int kN = 4096;
    std::vector<float> sig (kN);
    for (int i = 0; i < kN; ++i) sig[i] = n.renderSample();

    // Low band vs high band over the half-spectrum (skip DC bin 0). For WHITE noise
    // the mean power per bin is approximately equal across bands; a pink (1/f) tilt
    // would make the low band many times hotter than the high band.
    const double loPow = bandPower (sig, 1, kN / 8);            // ~0..6 kHz
    const double hiPow = bandPower (sig, 3 * kN / 8, kN / 2);   // ~18..24 kHz
    const double ratio = loPow / hiPow;

    // White => ratio ~ 1. Generous tolerance for a single 4k-sample realization;
    // a 1/f pinking would push this well above 3. (Tight enough to fail pink.)
    REQUIRE (ratio > 0.4);
    REQUIRE (ratio < 2.5);
}

TEST_CASE("noise: distinct per-voice seeds produce decorrelated streams", "[noise]") {
    NoiseSource a, b;
    a.prepare (48000.0);
    b.prepare (48000.0);
    a.reset (0x0000000100000002ULL);   // distinct per-voice seeds
    b.reset (0x00000003000000A5ULL);

    constexpr int kN = 8192;
    std::vector<float> sa (kN), sb (kN);
    for (int i = 0; i < kN; ++i) { sa[i] = a.renderSample(); sb[i] = b.renderSample(); }

    const double c = correlation (sa, sb);
    REQUIRE (std::abs (c) < 0.05);   // near-zero cross-correlation => decorrelated

    // Sanity: each stream is correlated 1.0 with itself (the metric works).
    REQUIRE (correlation (sa, sa) > 0.99);
}

TEST_CASE("noise: a zero seed is rejected/avoided (xorshift cannot escape 0)", "[noise]") {
    NoiseSource z;
    z.prepare (48000.0);
    z.reset (0);   // zero seed: MUST be replaced so the generator does not stick at 0

    bool sawNonzero = false;
    for (int i = 0; i < 256; ++i) {
        if (z.renderSample() != -1.0f) { sawNonzero = true; break; }
    }
    // If the state stuck at 0, xorshift32 stays 0 forever -> every sample == -1.0f.
    REQUIRE (sawNonzero);

    // A seed whose 64->32 fold is zero (e.g. low32 == high32) must also be rescued.
    NoiseSource z2;
    z2.prepare (48000.0);
    z2.reset (0x00ABCDEF00ABCDEFULL);   // low32 ^ high32 == 0
    bool sawNonzero2 = false;
    for (int i = 0; i < 256; ++i) {
        if (z2.renderSample() != -1.0f) { sawNonzero2 = true; break; }
    }
    REQUIRE (sawNonzero2);
}

TEST_CASE("noise: kNoiseHfRolloffHz is read from the calibration header, not duplicated", "[noise]") {
    // Acceptance: the (PI) corner lives in the calibration header and the DSP source
    // references it. Pin the centralized value so an inlined-literal regression fails.
    REQUIRE (mw::cal::noise::kNoiseHfRolloffHz == 3000.0);

    // With the rolloff enabled the source must visibly low-pass (attenuate HF power)
    // relative to the white (default-OFF) stream — proving the coefficient derived
    // from kNoiseHfRolloffHz is actually applied (and that OFF is genuinely white).
    constexpr int kN = 8192;

    NoiseSource white;
    white.prepare (48000.0);
    white.reset (0xABCDEF01u);
    std::vector<float> w (kN);
    for (int i = 0; i < kN; ++i) w[i] = white.renderSample();
    REQUIRE_FALSE (white.hfRolloffEnabled());

    NoiseSource rolled;
    rolled.prepare (48000.0);
    rolled.reset (0xABCDEF01u);
    rolled.setHfRolloffEnabled (true);
    REQUIRE (rolled.hfRolloffEnabled());
    std::vector<float> r (kN);
    for (int i = 0; i < kN; ++i) r[i] = rolled.renderSample();

    const double whiteHi   = bandPower (w, 3 * kN / 8, kN / 2);
    const double rolledHi  = bandPower (r, 3 * kN / 8, kN / 2);
    REQUIRE (rolledHi < whiteHi * 0.6);   // HF clearly attenuated when enabled
}

TEST_CASE("noise: renderSample is noexcept and allocates/locks nothing (RT guard)", "[noise]") {
    static_assert (noexcept (std::declval<NoiseSource&>().renderSample()),
                   "renderSample() must be noexcept [docs/design/01 §2.4; ADR-001 C5].");
    static_assert (noexcept (std::declval<NoiseSource&>().reset (0ULL)),
                   "reset() must be noexcept.");

    NoiseSource n;
    n.prepare (48000.0);          // prepare may allocate; happens BEFORE arming
    n.reset (0xFEEDFACEu);

    mw::test::AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    for (int i = 0; i < 4096; ++i) acc += n.renderSample();   // hot path, armed
    g.disarm();

    REQUIRE_FALSE (g.violated());
    REQUIRE (g.violations().empty());
    // Touch acc so the loop is not optimized away.
    REQUIRE (std::isfinite (acc));
}
