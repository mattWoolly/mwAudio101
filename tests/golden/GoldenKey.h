// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/GoldenKey.h — GoldenKey / EngineTag / DeterminismClass / LadderEngine
// types, a stable hash(GoldenKey), sameEngineContext(), and a blessed-sample-rate
// construction helper (task 041).
//
// Realizes docs/design/11 §5.3 (engine-tag + renderVersion keying), §5.2 (the
// blessed sample-rate set), ADR-013 C22 (a golden compared across a different engine
// tag / oversample factor is REFUSED), and ADR-023 V11 (the engine tag carries
// renderVersion; a golden compared across a different renderVersion is refused).
//
// Header-only: the design tree lists tests/golden/GoldenKey.{h,cpp}, but a header-
// only realization keeps the primitive self-contained and avoids touching the shared
// tests/CMakeLists glob set (which compiles tests/unit/*.cpp; a tests/golden/*.cpp
// would not be picked up). It is the same pattern as the sibling tests/golden/
// Sha256.h (task 040). This is OFFLINE harness code — RT invariants are not in play
// [docs/design/11 §2.2].
//
// hash() is SHA-256-derived and stable: it serializes the key's fields into a
// canonical little-endian byte buffer (fixed field order, no padding/struct layout
// dependence), takes the SHA-256 (pure fixed-width integer arithmetic — byte-
// identical run-to-run and across macOS arm64 / Linux x64, task 040), and folds the
// first 8 digest bytes into a uint64. It is therefore stable across runs and across
// platforms, and changes when ANY field changes [docs/design/11 §5.3].

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

#include "../../core/calibration/GoldenKeyConstants.h"
#include "Sha256.h"

namespace mw::golden {

// The open ADR-003 ladder-engine A/B. Goldens are engine-tagged so the ZDF-vs-
// Huovilainen comparison runs on identical stimuli without cross-contaminating
// blesses [docs/design/11 §5.3].
enum class LadderEngine { ZDF, Huovilainen };

// Determinism-class partition: integer/integer-derived (Exact, SHA-256 hash-
// compared, bit-exact on both platforms) vs the nonlinear FP audio path (Fp, two-
// stage tolerance-banded) [docs/design/11 §5.1].
enum class DeterminismClass { Exact, Fp };

// The engine tag. Two goldens are comparable ONLY if their engine tags match on all
// three axes; otherwise the compare is refused (sameEngineContext) [docs/design/11
// §5.3; ADR-013 C22; ADR-023 V11].
struct EngineTag {
    LadderEngine ladder;
    int          oversampleFactor;   // 1 or 2 (ADR-003 §F-09; clamp ADR-023 V15)
    int          renderVersion;      // ADR-023 V11 — the bless-affecting contract
};

// The full corpus key [docs/design/11 §5.3]. sampleRate must be in the blessed set
// (§5.2) at construction via makeGoldenKey().
struct GoldenKey {
    std::uint64_t    renderGraphHash; // SHA-256-derived hash of the render graph
    EngineTag        engine;
    double           sampleRate;      // must be in the blessed set (§5.2)
    int              blockSize;
    std::uint64_t    seed;
    DeterminismClass cls;
};

// True iff `sampleRate` is one of {44100, 48000, 88200, 96000} Hz [docs/design/11
// §5.2; ADR-023 V12]. Delegates to the centralized blessed-set predicate.
inline constexpr bool isBlessedSampleRate(double sampleRate) noexcept {
    return mw::cal::golden::isBlessedSampleRate(sampleRate);
}

// True iff the two engine tags are the SAME context on every axis — same ladder
// engine, same oversample factor, same renderVersion. A compare across any
// difference must be refused (return false) [docs/design/11 §5.3; ADR-013 C22;
// ADR-023 V11].
inline bool sameEngineContext(const EngineTag& a, const EngineTag& b) noexcept {
    return a.ladder == b.ladder
        && a.oversampleFactor == b.oversampleFactor
        && a.renderVersion == b.renderVersion;
}

namespace detail {

// Append a value's raw little-endian bytes to a canonical serialization buffer. We
// serialize each field explicitly (rather than hashing the struct's memory) so the
// hash is independent of struct padding / member layout and stable across compilers.
template <typename T>
inline void appendLe(std::array<std::byte, 64>& buf, std::size_t& n, T value) noexcept {
    // Reinterpret the value's bytes (the source values are integers and an IEEE-754
    // double; on the supported little-endian targets — macOS arm64 / Linux x64 —
    // this byte order is identical, matching the CLASS-EXACT cross-platform contract
    // [docs/design/11 §5.1]).
    std::array<std::byte, sizeof(T)> tmp{};
    std::memcpy(tmp.data(), &value, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i) buf[n++] = tmp[i];
}

} // namespace detail

// Stable, SHA-256-derived 64-bit hash of a GoldenKey [docs/design/11 §5.3]. Stable
// across runs and platforms; differs when any field differs.
inline std::uint64_t hash(const GoldenKey& k) noexcept {
    // Canonical field serialization. Fixed order; fixed widths. The enums are folded
    // to a fixed-width integer so their underlying type does not perturb the bytes.
    std::array<std::byte, 64> buf{};
    std::size_t n = 0;
    detail::appendLe<std::uint64_t>(buf, n, k.renderGraphHash);
    detail::appendLe<std::uint32_t>(buf, n, static_cast<std::uint32_t>(k.engine.ladder));
    detail::appendLe<std::int32_t>(buf, n, k.engine.oversampleFactor);
    detail::appendLe<std::int32_t>(buf, n, k.engine.renderVersion);
    detail::appendLe<double>(buf, n, k.sampleRate);
    detail::appendLe<std::int32_t>(buf, n, k.blockSize);
    detail::appendLe<std::uint64_t>(buf, n, k.seed);
    detail::appendLe<std::uint32_t>(buf, n, static_cast<std::uint32_t>(k.cls));

    const Sha256 d = sha256(std::span<const std::byte>(buf.data(), n));

    // Fold the first 8 digest bytes (big-endian) into a uint64. SHA-256's avalanche
    // means any input field change flips ~half these bits.
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < 8; ++i)
        out = (out << 8) | static_cast<std::uint64_t>(d.bytes[i]);
    return out;
}

// Construct a GoldenKey, asserting the sample rate is in the blessed set (§5.2). A
// non-blessed rate at construction is REJECTED — this is offline harness code, so
// the rejection is a thrown std::invalid_argument [docs/design/11 §5.2; ADR-023
// V12/V14/V17]. Use isBlessedSampleRate() for a non-throwing predicate.
inline GoldenKey makeGoldenKey(std::uint64_t renderGraphHash,
                               const EngineTag& engine,
                               double sampleRate,
                               int blockSize,
                               std::uint64_t seed,
                               DeterminismClass cls) {
    if (!isBlessedSampleRate(sampleRate))
        throw std::invalid_argument(
            "GoldenKey: sampleRate is not in the blessed set {44100,48000,88200,96000} Hz");
    return GoldenKey{renderGraphHash, engine, sampleRate, blockSize, seed, cls};
}

} // namespace mw::golden
