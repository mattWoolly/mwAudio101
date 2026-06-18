// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-2 golden support: self-contained SHA-256 byte hasher (task 040). Test-case
// names begin with "golden" so `ctest -R golden` selects them (silent-pass rule,
// AGENTS.md / docs/design/11 sec 8.3). This is the CLASS-EXACT compare primitive
// from docs/design/11 sec 6.2 / ADR-013 C5.
//
// Acceptance coverage (plan/backlog/040):
//  - NIST short-message vectors match published digests [docs/design/11 sec 6.2]
//  - single-bit input flip changes the digest (negative control) [ADR-013 C5]
//  - byte-identical digest run twice on the same input (determinism) [docs/design/11 sec 6.2]
//  - hex formatter round-trips for MANIFEST/sidecar use [plan/backlog/040 Scope]
//
// Tagged [core] only (an existing snapshot tag) so the checked-in label-snapshot
// (tests/golden/corpus/ctest-labels.snapshot, docs/design/11 sec 8.4) is unaffected;
// selection is by the `golden` NAME prefix, not by tag.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../golden/Sha256.h"

namespace {

// Reinterpret a string's chars as a byte span (the hasher's input type).
std::span<const std::byte> bytesOf(std::string_view s) noexcept {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(s.data()), s.size());
}

mw::golden::Sha256 hashString(std::string_view s) noexcept {
    return mw::golden::sha256(bytesOf(s));
}

} // namespace

TEST_CASE("golden: sha256 matches NIST short-message published digests [docs/design/11 6.2]",
          "[core]") {
    // FIPS 180-4 / NIST CAVS published digests. These are reproducibility anchors
    // for CLASS-EXACT compare, not measured facts.
    struct Vector { std::string_view msg; std::string_view hex; };
    const std::array<Vector, 4> vectors{{
        // empty message
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        // "abc"
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        // 448-bit two-block message
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        // 896-bit message
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    }};

    for (const auto& v : vectors) {
        const auto digest = hashString(v.msg);
        REQUIRE(mw::golden::toHex(digest) == std::string(v.hex));
        // Positive: equality vs the parsed expected digest.
        const auto expected = mw::golden::fromHex(v.hex);
        REQUIRE(digest == expected);
    }
}

TEST_CASE("golden: sha256 long repeated-byte vector matches NIST [docs/design/11 6.2]",
          "[core]") {
    // FIPS 180-4 example: one million 'a' characters -> known digest. Exercises
    // many full blocks and the length-encoding path.
    std::string msg(1'000'000, 'a');
    const auto digest = mw::golden::sha256(bytesOf(msg));
    REQUIRE(mw::golden::toHex(digest)
            == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("golden: sha256 single-bit input flip changes the digest (negative control) [ADR-013 C5]",
          "[core]") {
    std::vector<std::byte> a{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    std::vector<std::byte> b = a;
    b[2] = std::byte{0x02 ^ 0x01};   // flip exactly one bit in one byte

    const auto da = mw::golden::sha256(std::span<const std::byte>(a));
    const auto db = mw::golden::sha256(std::span<const std::byte>(b));

    REQUIRE_FALSE(da == db);                       // avalanche: a 1-bit flip diverges
    REQUIRE(mw::golden::toHex(da) != mw::golden::toHex(db));

    // Sanity: re-hashing the unmodified input still equals the original digest, so
    // the inequality above is the flip, not nondeterminism.
    REQUIRE(da == mw::golden::sha256(std::span<const std::byte>(a)));
}

TEST_CASE("golden: sha256 is deterministic — byte-identical on repeat [docs/design/11 6.2]",
          "[core]") {
    std::string msg = "the quick brown fox jumps over the lazy dog";
    const auto d1 = hashString(msg);
    const auto d2 = hashString(msg);
    REQUIRE(d1 == d2);
    REQUIRE(d1.bytes == d2.bytes);                 // every one of the 32 bytes matches
    REQUIRE(mw::golden::toHex(d1) == mw::golden::toHex(d2));
}

TEST_CASE("golden: sha256 digest is 32 bytes and equality is value equality [docs/design/11 6.2]",
          "[core]") {
    const auto d = hashString("abc");
    REQUIRE(d.bytes.size() == 32u);

    // operator== is value equality: an identical-bytes copy compares equal; a
    // one-byte perturbation does not (paired control so a stub `return true` fails).
    mw::golden::Sha256 copy = d;
    REQUIRE(d == copy);
    copy.bytes[0] = static_cast<std::uint8_t>(copy.bytes[0] ^ 0xFF);
    REQUIRE_FALSE(d == copy);
}

TEST_CASE("golden: sha256 hex formatter is lowercase, 64 chars, and round-trips [plan/backlog/040]",
          "[core]") {
    const auto d = hashString("abc");
    const std::string hex = mw::golden::toHex(d);
    REQUIRE(hex.size() == 64u);
    for (char c : hex) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        REQUIRE(ok);                               // lowercase hex, MANIFEST/sidecar form
    }
    // fromHex(toHex(x)) == x for MANIFEST/sidecar persistence round-tripping.
    REQUIRE(mw::golden::fromHex(hex) == d);
}
