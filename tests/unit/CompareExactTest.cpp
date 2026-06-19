// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-2 golden support: the CLASS-EXACT comparer (task 078, golden-6). Test-case
// names begin with "golden" so `ctest -R golden` (and `-R class-exact`) selects them
// (silent-pass rule, AGENTS.md / docs/design/11 §8.3). This realizes the CLASS-EXACT
// compare contract: docs/design/11 §6.2 (SHA-256 over the canonical byte serialization
// of the integer/control output; equality is whole-digest) and ADR-013 C5 (a CLASS-EXACT
// golden must SHA-256 match bit-for-bit on macOS arm64 AND Linux x64; any diff FAILS).
//
// Acceptance coverage (plan/backlog/078):
//  - identical blessed/got => match true; a one-sample diff => match false (paired)
//    [ADR-013 C5]
//  - compareExact reports the got AND expected digests on a mismatch [docs/design/11 §6.2]
//  - the serialization is STABLE so the same output hashes identically on arm64 and Linux
//    [ADR-013 C5]: pinned to a fixed published SHA-256 vector, independent of struct
//    layout/padding, and whole-digest (any single byte diff flips match to false).
//
// Tagged [golden] (already in the snapshot) AND [class-exact] (a NEW tag the orchestrator
// will pick up at wave integration — an expected, transient red on labels_snapshot ONLY,
// never on the scoped -R class-exact / -R golden selection).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "../golden/CompareExact.h"
#include "../golden/GoldenKey.h"
#include "../golden/Sha256.h"

namespace {

using mw::golden::compareExact;
using mw::golden::EngineTag;
using mw::golden::ExactResult;
using mw::golden::LadderEngine;
using mw::golden::RenderResult;
using mw::golden::serializeExact;
using mw::golden::Sha256;
using mw::golden::sha256;
using mw::golden::toHex;

// The canonical blessed engine tag (matches the GoldenKeyTest / CompareFpTest tag).
EngineTag canonicalEngine() noexcept {
    return EngineTag{LadderEngine::Huovilainen, /*oversampleFactor=*/2, /*renderVersion=*/1};
}

// A small deterministic CLASS-EXACT payload (integer/control output rendered to f32):
// a short ramp of exact integer-valued floats. Integer-valued floats have an exact
// IEEE-754 representation, so the byte serialization is identical on arm64 and x64.
RenderResult makePayload(double sampleRate = 48000.0) {
    RenderResult r{};
    r.sampleRate = sampleRate;
    r.engine     = canonicalEngine();
    r.samples    = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    return r;
}

} // namespace

// --- C5: identical => match true; one-sample diff => match false (paired) ---------

TEST_CASE("golden: class-exact identical blessed and got hash-match (match true) "
          "[ADR-013 C5]",
          "[golden][class-exact]") {
    const auto blessed = makePayload();
    const auto got     = blessed;   // bit-identical payload

    const ExactResult res = compareExact(got, blessed);
    REQUIRE(res.match);
    REQUIRE(res.got == res.expected);   // the digests agree on a match
}

TEST_CASE("golden: class-exact a one-sample diff makes match false (paired with the "
          "identical positive control) [ADR-013 C5]",
          "[golden][class-exact]") {
    const auto blessed = makePayload();

    // Paired POSITIVE control: identical payload matches (so a stub that always
    // returns match=false would fail this half).
    REQUIRE(compareExact(blessed, blessed).match);

    // NEGATIVE control: flip exactly ONE sample by the smallest distinct integer step.
    // A stub that always returns match=true fails here.
    auto got = blessed;
    got.samples[3] = 99.0f;   // one sample differs
    const ExactResult res = compareExact(got, blessed);
    REQUIRE_FALSE(res.match);
}

// --- §6.2: compareExact reports the got AND expected digests on a mismatch --------

TEST_CASE("golden: class-exact reports the got and expected digests on a mismatch "
          "[docs/design/11 6.2]",
          "[golden][class-exact]") {
    const auto blessed = makePayload();
    auto got = blessed;
    got.samples[0] = 1.0f;   // differs from blessed's 0.0f

    const ExactResult res = compareExact(got, blessed);
    REQUIRE_FALSE(res.match);

    // The two digests are populated and DIFFER on a mismatch.
    REQUIRE_FALSE(res.got == res.expected);

    // They equal the standalone SHA-256 of each side's canonical serialization, i.e.
    // compareExact reports the real digests, not sentinels.
    const Sha256 gotDigest      = sha256(std::span<const std::byte>(serializeExact(got)));
    const Sha256 expectedDigest = sha256(std::span<const std::byte>(serializeExact(blessed)));
    REQUIRE(res.got == gotDigest);
    REQUIRE(res.expected == expectedDigest);

    // The reported hex digests are the 64-char SHA-256 strings (the §7.3 artifactSha256
    // surface a downstream blesser/sidecar prints).
    REQUIRE(toHex(res.got).size() == 64);
    REQUIRE(toHex(res.expected).size() == 64);
    REQUIRE(toHex(res.got) != toHex(res.expected));
}

// --- C5: stable, cross-platform-deterministic serialization -----------------------

TEST_CASE("golden: class-exact serialization is byte-stable run-to-run for the same "
          "payload [ADR-013 C5]",
          "[golden][class-exact]") {
    const auto a = makePayload();
    const auto b = makePayload();   // independently constructed, identical content
    REQUIRE(serializeExact(a) == serializeExact(b));
    REQUIRE(compareExact(a, b).got == compareExact(a, b).expected);
}

TEST_CASE("golden: class-exact pins a known SHA-256 vector so a serialization-layout "
          "change is caught (cross-platform stable) [ADR-013 C5]",
          "[golden][class-exact]") {
    // A fixed payload whose canonical serialization is a known byte string. The eight
    // samples {0..7} are integer-valued floats with exact IEEE-754 little-endian bytes
    // identical on arm64 and x64; the serialization prepends the f32 sample count as a
    // fixed-width little-endian u64 so a truncated/extended buffer cannot collide.
    const auto payload = makePayload();
    const std::vector<std::byte> bytes = serializeExact(payload);

    // The serialization is exactly: u64 LE sample-count (8) followed by 8 * 4 = 32 bytes
    // of little-endian IEEE-754 f32 samples = 40 bytes total. (Length-prefix prevents a
    // "{0,1} then a different rate" payload from aliasing a longer one.)
    REQUIRE(bytes.size() == 8u + 8u * sizeof(float));

    // Build the SAME bytes independently here (not via serializeExact) to pin the exact
    // canonical layout the digest is taken over — a layout drift breaks this oracle.
    std::vector<std::byte> expectBytes;
    const std::uint64_t count = 8u;
    for (int i = 0; i < 8; ++i)
        expectBytes.push_back(static_cast<std::byte>((count >> (8 * i)) & 0xFFu));
    for (int s = 0; s < 8; ++s) {
        const float f = static_cast<float>(s);
        std::uint32_t u = 0;
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        std::memcpy(&u, &f, sizeof(u));
        for (int i = 0; i < 4; ++i)
            expectBytes.push_back(static_cast<std::byte>((u >> (8 * i)) & 0xFFu));
    }
    REQUIRE(bytes == expectBytes);

    // Pin the digest to the published SHA-256 of those exact 40 bytes. Computed with the
    // project's FIPS-180-4 hasher (tests/golden/Sha256.h), itself pinned to NIST vectors
    // in tests/unit/Sha256GoldenTest.cpp. Any change to the canonical layout flips this.
    const Sha256 d = sha256(std::span<const std::byte>(bytes));
    REQUIRE(toHex(d) ==
            "94dd40e6e20b03bf6f2d12a5dcb53275758a2dc15058f8bf67b46450e72fc056");
}
