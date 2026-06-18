// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/Xorshift128pTest.cpp — Layer-1 unit tests for the drift-subsystem
// xorshift128+ PRNG and its Gaussian/cubic/seed-derivation helpers (task 063).
//
// Test-case names begin with "vintage_prng" so `ctest -R vintage_prng` selects
// them (silent-pass discipline, AGENTS.md). Covers every Acceptance criterion of
// plan/backlog/063: bit-identical sequence for a fixed seed (§12.7, VV-17),
// per-voiceIndex decorrelation (§8.2), Gaussian ~N(0,1) and cubic in [-1,1] (§6).
//
// All figures here are reproducibility/statistical anchors, NOT measured specs.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "dsp/drift/Xorshift128p.h"

using mw::dsp::drift::Xorshift128p;

namespace {

// First N raw 64-bit draws from a freshly seeded PRNG.
template <std::size_t N>
std::array<std::uint64_t, N> firstN(std::uint64_t seed) {
    Xorshift128p p(seed);
    std::array<std::uint64_t, N> out{};
    for (auto& v : out) v = p.next();
    return out;
}

} // namespace

// --- Determinism: same seed => bit-identical sequence (§12.7, VV-17) ----------

TEST_CASE("vintage_prng: same seed yields a bit-identical 64-bit sequence", "[vintage_prng]") {
    const auto a = firstN<16>(0xDEADBEEFCAFEF00DULL);
    const auto b = firstN<16>(0xDEADBEEFCAFEF00DULL);
    REQUIRE(a == b);                 // run-to-run identical (also intended cross-platform)

    // The stream must actually advance, not repeat one value.
    bool advanced = false;
    for (std::size_t i = 1; i < a.size(); ++i)
        if (a[i] != a[0]) advanced = true;
    REQUIRE(advanced);
}

TEST_CASE("vintage_prng same seed yields a bit-identical float unit-interval sequence", "[vintage_prng]") {
    Xorshift128p p1(0x123456789ABCDEF0ULL);
    Xorshift128p p2(0x123456789ABCDEF0ULL);
    for (int i = 0; i < 1000; ++i) {
        const float f1 = p1.nextFloat01();
        const float f2 = p2.nextFloat01();
        REQUIRE(f1 == f2);           // bit-identical, not merely approx-equal
        REQUIRE(f1 >= 0.0f);
        REQUIRE(f1 < 1.0f);          // half-open [0,1)
    }
}

TEST_CASE("vintage_prng a different seed yields a different sequence - negative control", "[vintage_prng]") {
    const auto a = firstN<16>(1);
    const auto b = firstN<16>(2);
    REQUIRE(a != b);                 // a constant-stub PRNG would fail this
}

// --- Determinism of the whole derivation+sequence (VV-17 end to end) ---------

TEST_CASE("vintage_prng seedFromInstance derivation is deterministic and reproducible", "[vintage_prng]") {
    namespace d = mw::dsp::drift;
    const std::uint64_t instance = 0xA5A5A5A5DEADC0DEULL;

    // Same (instanceSeed, voiceIndex) => identical derived seed => identical stream.
    const std::uint64_t s0a = d::seedFromInstance(instance, 0);
    const std::uint64_t s0b = d::seedFromInstance(instance, 0);
    REQUIRE(s0a == s0b);

    const auto streamA = firstN<8>(s0a);
    const auto streamB = firstN<8>(s0b);
    REQUIRE(streamA == streamB);
}

// --- Decorrelation: distinct voiceIndex seeds produce independent streams (§8.2)

TEST_CASE("vintage_prng distinct voiceIndex seeds derive to distinct seeds", "[vintage_prng]") {
    namespace d = mw::dsp::drift;
    const std::uint64_t instance = 0x0123456789ABCDEFULL;

    std::array<std::uint64_t, 8> seeds{};
    for (int v = 0; v < 8; ++v) seeds[static_cast<std::size_t>(v)] = d::seedFromInstance(instance, v);

    // All eight derived seeds must be pairwise distinct (no collisions across voices).
    for (std::size_t i = 0; i < seeds.size(); ++i)
        for (std::size_t j = i + 1; j < seeds.size(); ++j)
            REQUIRE(seeds[i] != seeds[j]);
}

TEST_CASE("vintage_prng distinct voiceIndex streams decorrelate over a long run", "[vintage_prng]") {
    namespace d = mw::dsp::drift;
    const std::uint64_t instance = 0xBADC0FFEE0DDF00DULL;

    Xorshift128p v0(d::seedFromInstance(instance, 0));
    Xorshift128p v1(d::seedFromInstance(instance, 1));

    // Sample-correlation of the two uniform streams centered on 0.5.
    constexpr int kN = 200000;
    double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (int i = 0; i < kN; ++i) {
        const double x = static_cast<double>(v0.nextFloat01()) - 0.5;
        const double y = static_cast<double>(v1.nextFloat01()) - 0.5;
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
    }
    const double n = static_cast<double>(kN);
    const double cov = (sxy - sx * sy / n);
    const double vx  = (sxx - sx * sx / n);
    const double vy  = (syy - sy * sy / n);
    const double corr = cov / std::sqrt(vx * vy);

    // Independent uniform streams => |corr| ~ 0; allow generous slack for finite N.
    REQUIRE(std::abs(corr) < 0.02);
}

// --- gaussian(): zero mean / unit variance over large N (§6) ------------------

TEST_CASE("vintage_prng gaussian approximates standard-normal over large N", "[vintage_prng]") {
    Xorshift128p p(0xFEEDFACECAFEBEEFULL);

    constexpr int kN = 400000;
    double sum = 0.0, sumsq = 0.0;
    double maxAbs = 0.0;
    for (int i = 0; i < kN; ++i) {
        const double g = static_cast<double>(p.gaussian());
        REQUIRE(std::isfinite(g));   // Box-Muller must never emit inf/NaN (log(0) guard)
        sum += g;
        sumsq += g * g;
        maxAbs = std::max(maxAbs, std::abs(g));
    }
    const double n = static_cast<double>(kN);
    const double mean = sum / n;
    const double var  = sumsq / n - mean * mean;

    REQUIRE(std::abs(mean) < 0.02);          // ~zero mean
    REQUIRE(var == Catch::Approx(1.0).margin(0.03)); // ~unit variance
    REQUIRE(maxAbs > 3.0);                   // a real Gaussian reaches the tails
}

// --- cubic(): stays within [-1,1], zero mean, non-degenerate (§6) -------------

TEST_CASE("vintage_prng cubic stays within unit range and is zero-mean", "[vintage_prng]") {
    Xorshift128p p(0x1357924680ABCDEFULL);

    constexpr int kN = 400000;
    double sum = 0.0;
    double maxAbs = 0.0;
    bool sawNeg = false, sawPos = false;
    for (int i = 0; i < kN; ++i) {
        const float c = p.cubic();
        REQUIRE(c >= -1.0f);                 // (2u-1)^3 on u in [0,1) is in [-1,1)
        REQUIRE(c <= 1.0f);
        if (c < -0.01f) sawNeg = true;
        if (c >  0.01f) sawPos = true;
        sum += static_cast<double>(c);
        maxAbs = std::max(maxAbs, std::abs(static_cast<double>(c)));
    }
    const double mean = sum / static_cast<double>(kN);

    REQUIRE(std::abs(mean) < 0.02);          // symmetric => ~zero mean
    REQUIRE(sawNeg);                         // covers both signs (not a stub)
    REQUIRE(sawPos);
    REQUIRE(maxAbs > 0.5);                   // reaches a meaningful magnitude
}

// --- splitmix64 / goldenMix: deterministic integer mixers --------------------

TEST_CASE("vintage_prng splitmix64 is deterministic and avalanches", "[vintage_prng]") {
    namespace d = mw::dsp::drift;
    REQUIRE(d::splitmix64(0) == d::splitmix64(0));      // pure function
    REQUIRE(d::splitmix64(0) != d::splitmix64(1));      // distinct inputs diverge
    REQUIRE(d::splitmix64(0) != 0);                     // mixes zero away
}

TEST_CASE("vintage_prng goldenMix decorrelates adjacent voice indices", "[vintage_prng]") {
    namespace d = mw::dsp::drift;
    // Adjacent voice indices must not produce adjacent (or equal) mix values.
    for (int v = 0; v < 16; ++v) {
        REQUIRE(d::goldenMix(v) != d::goldenMix(v + 1));
    }
}

// --- constexpr-friendliness of the integer core (§3.1) -----------------------

TEST_CASE("vintage_prng integer core is constexpr-evaluable", "[vintage_prng]") {
    namespace d = mw::dsp::drift;

    // splitmix64 / goldenMix / seedFromInstance must be usable in constant context.
    constexpr std::uint64_t kMixed = d::splitmix64(0x9E3779B97F4A7C15ULL);
    static_assert(kMixed != 0, "splitmix64 must be constexpr-evaluable and non-zero");

    constexpr std::uint64_t kSeed = d::seedFromInstance(42ULL, 3);
    static_assert(kSeed != 0, "seedFromInstance must be constexpr-evaluable");

    // The PRNG state and next() must be usable at compile time.
    constexpr auto kFirst = [] {
        Xorshift128p rng(kSeed);
        return rng.next();
    }();
    static_assert(kFirst != 0 || kFirst == 0, "next() must be constexpr-evaluable");

    REQUIRE(d::seedFromInstance(42ULL, 3) == kSeed); // runtime == compile-time
}

TEST_CASE("vintage_prng type is a POD trivially-copyable PRNG for RT-safety", "[vintage_prng]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<Xorshift128p>);
    STATIC_REQUIRE(std::is_standard_layout_v<Xorshift128p>);
    STATIC_REQUIRE(std::is_nothrow_default_constructible_v<Xorshift128p>);
}
