// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/util/Prng.h — fixed-seed integer PRNG (task 009).
//
// Realizes docs/design/11 §5.1 (CLASS-EXACT, PRNG) and docs/design/00 §9.2.
// A reproducible integer stream that is bit-identical run-to-run AND across
// platforms (macOS arm64 / Linux x64) so per-voice "analog" drift stays golden
// bit-stable [ADR-013 C5; ADR-001 C8]. noexcept, no allocation.
//
// Algorithm: PCG-XSH-RR 64/32 — a 64-bit LCG state with an output permutation,
// chosen per §5.1's "prefer 64-bit LCG/PCG". The arithmetic is all fixed-width
// unsigned integer ops (wraparound is defined), so the stream is identical on any
// conforming compiler/platform — no transcendentals, no FP, no platform libm.

#pragma once

#include <cstdint>

namespace mw::util {

class Prng {
public:
    Prng() noexcept { seed(0x853c49e6748fea9bULL); }
    explicit Prng(std::uint64_t s) noexcept { seed(s); }

    // Deterministic seeding (PCG init: set state, step, add seed, step).
    void seed(std::uint64_t s) noexcept {
        state_ = 0;
        nextU32();
        state_ += s;
        nextU32();
    }

    // Next 32-bit draw (PCG-XSH-RR). Pure integer arithmetic => cross-platform exact.
    std::uint32_t nextU32() noexcept {
        const std::uint64_t old = state_;
        state_ = old * kMultiplier + kIncrement;
        const std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((old >> 18) ^ old) >> 27);
        const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    // Next 64-bit draw (two 32-bit draws).
    std::uint64_t nextU64() noexcept {
        const std::uint64_t hi = nextU32();
        const std::uint64_t lo = nextU32();
        return (hi << 32) | lo;
    }

    // Uniform float in [0, 1) from the top 24 bits (no FP in the generator itself).
    float nextFloat() noexcept {
        return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); // 2^-24
    }

private:
    static constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;
    static constexpr std::uint64_t kIncrement  = 1442695040888963407ULL; // odd
    std::uint64_t state_ = 0;
};

} // namespace mw::util
