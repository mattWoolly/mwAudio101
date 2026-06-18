// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/Xorshift128p.h — POD xorshift128+ PRNG + Gaussian/cubic shaping
// helpers and the deterministic per-voice seed derivation for the drift subsystem
// (task 063).
//
// Realizes docs/design/08 §3.1 (this file's responsibility), §6 (Tier-3 slop:
// gaussian()/cubic()), §8.2 (seedFromInstance = splitmix64(instance ^ goldenMix
// (voice)), bit-identical determinism) and ADR-009 §Decision 5 / VV-17.
//
// Class discipline (docs/design/08 §3.1, §12.4):
//   - header-only, constexpr-friendly *integer* core (next / splitmix64 /
//     goldenMix / seedFromInstance), no std::random, no heap, no locks;
//   - the raw 64-bit stream is pure fixed-width unsigned integer arithmetic
//     (wraparound is defined), so the sequence is bit-identical run-to-run and
//     across the macOS arm64 bless gate / Linux x64 [ADR-009 VV-17, §12.7].
//   - gaussian()/cubic() are floating-point shapers (transcendental Box-Muller),
//     so they are noexcept but not constexpr; they are drawn at note-on, never per
//     sample [docs/design/08 §6, §12.3].
//
// All numeric figures are TUNABLE DEFAULTS / reproducibility anchors, NOT measured
// SH-101 specs.

#pragma once

#include <cstdint>

namespace mw::dsp::drift {

// SplitMix64 finalizer (Steele/Vigna). A high-quality 64-bit avalanche mixer used
// to (a) expand a single 64-bit seed into the two non-zero xorshift128+ words and
// (b) derive per-voice seeds deterministically [docs/design/08 §8.2]. Pure
// fixed-width integer arithmetic => bit-identical everywhere, constexpr-evaluable.
constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Per-voice index mixer: multiply by an odd 64-bit golden-ratio constant so
// adjacent voice indices are spread far apart before being XORed into the instance
// seed, decorrelating per-voice streams [docs/design/08 §8.2]. constexpr, integer.
constexpr std::uint64_t goldenMix(int voiceIndex) noexcept {
    // 2^64 / golden-ratio, forced odd — the standard Fibonacci-hashing multiplier.
    constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ULL;
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(voiceIndex)) * kGolden;
}

// Deterministic per-voice seed: seed = splitmix64(instanceSeed ^ goldenMix(voice))
// [docs/design/08 §8.2; ADR-009 §Decision 5, VV-17]. Same input => same seed =>
// bit-identical stream; distinct voices decorrelate.
constexpr std::uint64_t seedFromInstance(std::uint64_t instanceSeed, int voiceIndex) noexcept {
    return splitmix64(instanceSeed ^ goldenMix(voiceIndex));
}

// xorshift128+ (Vigna 2014): POD state, no std::random, no heap, no locks. The raw
// next() stream is pure unsigned integer arithmetic, hence bit-exact and
// constexpr-friendly. gaussian()/cubic() add FP shaping for Tier-3 slop.
//
// Trivially-copyable / standard-layout POD so it lives by value inside the
// pre-allocated DriftState[kMaxVoices] array [docs/design/08 §8.1, §12.1].
struct Xorshift128p {
    std::uint64_t s0 = 0;
    std::uint64_t s1 = 0;

    // Default state is a fixed non-zero seed so an un-seeded PRNG never degenerates
    // to the forbidden all-zero state (xorshift128+ requires (s0,s1) != (0,0)).
    constexpr Xorshift128p() noexcept { seed(0x853C49E6748FEA9BULL); }
    constexpr explicit Xorshift128p(std::uint64_t seedValue) noexcept { seed(seedValue); }

    // Expand one 64-bit seed into two non-zero state words via SplitMix64; OR-in a
    // guard bit so the (0,0) degenerate state is impossible for any input seed.
    constexpr void seed(std::uint64_t seedValue) noexcept {
        std::uint64_t z = seedValue;
        s0 = splitmix64(z) | 1ULL;          // guaranteed non-zero
        z += 0x9E3779B97F4A7C15ULL;         // advance SplitMix sequence for word 2
        s1 = splitmix64(z);
    }

    // Next raw 64-bit draw (xorshift128+). Defined unsigned wraparound => identical
    // on every conforming platform; constexpr-evaluable [docs/design/08 §12.7].
    constexpr std::uint64_t next() noexcept {
        std::uint64_t x = s0;
        const std::uint64_t y = s1;
        s0 = y;
        x ^= x << 23;
        s1 = x ^ y ^ (x >> 17) ^ (y >> 26);
        return s1 + y;
    }

    // Uniform float in the half-open interval [0,1) from the top 24 bits (the float
    // mantissa width), so every representable value is reachable and < 1.0.
    constexpr float nextFloat01() noexcept {
        // 24 high bits -> [0, 2^24) -> scale by 2^-24 => [0, 1).
        return static_cast<float>(static_cast<std::uint32_t>(next() >> 40))
             * (1.0f / 16777216.0f); // 2^-24
    }

    // Uniform double in [0,1) from the top 53 bits (used internally by gaussian()).
    constexpr double nextDouble01() noexcept {
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); // 2^-53
    }

    // N(0,1) via the basic (polar-free) Box-Muller transform [docs/design/08 §6].
    // Two uniforms -> one standard-normal sample (the second is discarded; we draw
    // fresh each call — this runs at note-on, not per sample). u1 is nudged off zero
    // so log(0) can never produce -inf. noexcept; NOT constexpr (uses std libm).
    float gaussian() noexcept {
        // Pull u1 into (0,1] so std::log(u1) is finite and negative.
        double u1 = nextDouble01();
        const double u2 = nextDouble01();
        if (u1 <= 0.0) u1 = kTinyU1;
        const double r = mwSqrt(-2.0 * mwLog(u1));
        return static_cast<float>(r * mwCos(kTwoPi * u2));
    }

    // Cubic shaper (2u-1)^3 mapping u in [0,1) -> [-1, 1) [docs/design/08 §6]. A
    // labelled taste alternative to gaussian() (kSlopShape). Soft near 0, reaches
    // the rails; symmetric => zero mean. constexpr-evaluable (no transcendentals).
    constexpr float cubic() noexcept {
        const float t = 2.0f * nextFloat01() - 1.0f; // [-1, 1)
        return t * t * t;
    }

private:
    static constexpr double kTwoPi  = 6.283185307179586476925286766559; // 2*pi
    static constexpr double kTinyU1 = 2.3283064365386963e-10;           // ~2^-32 floor

    // Thin libm wrappers kept local so the header has no <cmath> in the constexpr
    // path; the FP shapers are the only callers and they are runtime-only.
    static double mwSqrt(double v) noexcept;
    static double mwLog(double v) noexcept;
    static double mwCos(double v) noexcept;
};

} // namespace mw::dsp::drift

// Out-of-line libm wrappers (header-only via inline). Kept after the struct so the
// constexpr integer core above pulls in no <cmath>.
#include <cmath>

namespace mw::dsp::drift {

inline double Xorshift128p::mwSqrt(double v) noexcept { return std::sqrt(v); }
inline double Xorshift128p::mwLog(double v) noexcept { return std::log(v); }
inline double Xorshift128p::mwCos(double v) noexcept { return std::cos(v); }

} // namespace mw::dsp::drift
