// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/CompareExact.h — the CLASS-EXACT golden comparer (task 078, golden-6):
// ExactResult compareExact(const RenderResult& got, const RenderResult& blessed).
//
// Realizes docs/design/11 §6.2 (CLASS-EXACT compare): SHA-256 over the canonical byte
// serialization of the integer/control output; equality is whole-digest. Normative
// contract: ADR-013 C5 — a CLASS-EXACT golden (sequencer bytes, divider-OR edges, PRNG
// stream, arp order, param-smooth boundaries, CC map, ... all rendered into the
// RenderResult's integer/control payload) must SHA-256 match bit-for-bit on macOS arm64
// AND Linux x64; any diff FAILS.
//
// WHAT THIS OWNS (task 078 Scope): the ExactResult aggregate {match, got, expected}, the
// canonical byte serialization of the RenderResult integer/control payload, and the
// whole-digest equality (any single-byte difference => match == false). The got AND
// expected digests are always reported (§6.2) so a downstream blesser/sidecar can print
// both on a mismatch [docs/design/11 §6.2; §7.3 artifactSha256].
//
// OUT OF SCOPE (other golden tasks): the CLASS-FP / two-stage compare (golden-7, lives in
// CompareFp.h); the engine-context refusal (sameEngineContext, golden-2 / GoldenKey.h,
// called by the FP comparer) [task 078 Out-of-scope]. CLASS-EXACT is a pure bit-for-bit
// hash equality: it is NOT engine-tag-banded and does not refuse — a cross-tag exact
// payload simply hashes differently and so does not match, which is the correct
// deterministic behavior for the integer/control class.
//
// Header-only: the design tree (docs/design/11 §2.1) lists tests/golden/CompareExact.{h,
// cpp} (the §2.1 entry is "Comparer.{h,cpp}", split per-class across golden-6/golden-7),
// but a header-only realization keeps the primitive self-contained and avoids editing the
// shared tests/CMakeLists.txt glob set — which compiles tests/unit/*.cpp; a
// tests/golden/*.cpp would NOT be picked up, and editing tests/CMakeLists.txt is forbidden
// by the parallel-fleet conflict-avoidance rule. This is the identical pattern used by the
// sibling tests/golden/Sha256.h (040), GoldenKey.h (041), Stimulus.h (042), CompareFp.h
// (043), and RenderHarness.h (076). This is OFFLINE harness code — the no-alloc/no-lock RT
// invariants are NOT in play here; the comparer/serializer allocate freely [docs/design/11
// §2.2].
//
// CROSS-PLATFORM DETERMINISM (the one property this file must encode). The serialization
// is a fixed field order of fixed-width little-endian bytes: a u64 little-endian sample
// count, then each f32 sample's raw IEEE-754 bytes in order. It hashes via the project's
// FIPS-180-4 SHA-256 (tests/golden/Sha256.h) — pure fixed-width unsigned integer
// arithmetic, no floating point, no transcendentals — so the digest is byte-identical
// run-to-run AND across the supported little-endian targets (macOS arm64 / Linux x64),
// exactly the CLASS-EXACT contract [docs/design/11 §5.1, §6.2; ADR-013 C5]. The
// length-prefix makes the serialization injective in the sample count so a truncated or
// extended payload cannot alias a shorter/longer one. This mirrors the explicit-field,
// no-struct-padding-dependence serialization of the sibling GoldenKey::hash().

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// CompareFp.h owns the canonical mw::golden::RenderResult aggregate (samples, sampleRate,
// engine) the comparers share [docs/design/11 §5.4]. Reusing it — rather than re-declaring
// a second RenderResult — keeps a single definition so a test TU may include BOTH comparers
// without an ODR clash. CompareExact only reads the integer/control `samples` payload.
#include "CompareFp.h"   // mw::golden::RenderResult, mw::golden::EngineTag (via GoldenKey.h)
#include "Sha256.h"      // mw::golden::Sha256, sha256(), toHex()

namespace mw::golden {

// The CLASS-EXACT compare result [docs/design/11 §6.2]. `match` is whole-digest equality;
// `got` and `expected` are the two SHA-256 digests, ALWAYS reported (so a mismatch surfaces
// both for an audit/blesser diff, §6.2 / §7.3). On a match, got == expected.
struct ExactResult {
    bool    match = false;
    Sha256  got{};
    Sha256  expected{};
};

// Canonical byte serialization of a RenderResult's integer/control payload [docs/design/11
// §6.2; ADR-013 C5]. Fixed field order, fixed-width little-endian, no struct-padding
// dependence:
//   [0..8)   u64  little-endian f32 sample count
//   [8..)    for each sample: 4 bytes of raw IEEE-754 little-endian f32
// Length-prefixed so the encoding is injective in the sample count (a truncated buffer
// cannot alias a shorter one). Byte-identical on arm64 / x64 (both little-endian; the
// integer/control payload is exact-representable, so no FP rounding enters the bytes).
[[nodiscard]] inline std::vector<std::byte> serializeExact(const RenderResult& r) {
    std::vector<std::byte> bytes;
    bytes.reserve(sizeof(std::uint64_t) + r.samples.size() * sizeof(float));

    // Length prefix: the f32 sample count as a fixed-width u64, little-endian.
    const std::uint64_t count = static_cast<std::uint64_t>(r.samples.size());
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::byte>((count >> (8 * i)) & 0xFFu));

    // Each sample's raw IEEE-754 bytes, little-endian. We extract the bits via memcpy into
    // a fixed-width u32 and emit them LE explicitly so the order does not depend on the
    // host's float endianness or struct layout (the supported targets are little-endian;
    // the explicit emit makes the contract self-evident and audit-stable).
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "CLASS-EXACT serialization assumes 32-bit IEEE-754 float");
    for (const float sample : r.samples) {
        std::uint32_t u = 0;
        std::memcpy(&u, &sample, sizeof(u));
        for (int i = 0; i < 4; ++i)
            bytes.push_back(static_cast<std::byte>((u >> (8 * i)) & 0xFFu));
    }
    return bytes;
}

// The CLASS-EXACT compare [docs/design/11 §6.2; ADR-013 C5]. SHA-256 over the canonical
// serialization of each side; `match` is WHOLE-DIGEST equality — any single-byte difference
// in the serialized integer/control payload flips match to false. Both digests are reported
// (§6.2) regardless of outcome. Deterministic and cross-platform-stable (pure integer hash
// over a fixed little-endian layout).
[[nodiscard]] inline ExactResult compareExact(const RenderResult& got,
                                              const RenderResult& blessed) {
    const std::vector<std::byte> gotBytes      = serializeExact(got);
    const std::vector<std::byte> blessedBytes  = serializeExact(blessed);

    ExactResult r{};
    r.got      = sha256(std::span<const std::byte>(gotBytes));
    r.expected = sha256(std::span<const std::byte>(blessedBytes));
    r.match    = (r.got == r.expected);
    return r;
}

} // namespace mw::golden
