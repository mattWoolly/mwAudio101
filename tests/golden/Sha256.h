// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/Sha256.h — self-contained SHA-256 byte hasher (task 040).
//
// Realizes docs/design/11 §6.2 (CLASS-EXACT compare) and ADR-013 C5: a SHA-256
// over a byte span returning a 32-byte digest with value equality, used by the
// CLASS-EXACT comparer and artifact hashing. The whole computation is fixed-width
// unsigned integer arithmetic (wraparound is defined) with no transcendentals and
// no floating point, so the digest is byte-identical run-to-run AND across
// platforms (macOS arm64 / Linux x64) — the property CLASS-EXACT depends on
// [docs/design/11 §5.1, §6.2; ADR-013 C5].
//
// Header-only: the design tree lists tests/golden/Sha256.{h,cpp}, but a header-only
// realization keeps the primitive self-contained and avoids touching the shared
// tests/CMakeLists glob set (it is consumed by globbed tests/unit sources). RT
// invariants are not in play here — this is offline harness code [docs/design/11
// §2.2]. sha256() does not allocate; only the FIPS reference implementation's
// fixed-size 64-word message schedule lives on the stack.
//
// Algorithm: FIPS 180-4 SHA-256. Constants and the round function are the standard
// published values; correctness is pinned against NIST published digests in
// tests/unit/Sha256GoldenTest.cpp.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mw::golden {

// 32-byte SHA-256 digest with value equality (docs/design/11 §6.2 signature).
struct Sha256 {
    std::array<std::uint8_t, 32> bytes{};
    bool operator==(const Sha256& other) const noexcept { return bytes == other.bytes; }
};

namespace detail {

inline constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32u - n));
}

// FIPS 180-4 §4.2.2 round constants (first 32 bits of the fractional parts of the
// cube roots of the first 64 primes).
inline constexpr std::array<std::uint32_t, 64> kRoundConstants = {{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

// Compress one 64-byte block into the running state h[0..7].
inline void compress(std::array<std::uint32_t, 8>& h, const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[static_cast<std::size_t>(i)] =
            (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
            (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
            (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
            (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            rotr(w[static_cast<std::size_t>(i - 15)], 7) ^
            rotr(w[static_cast<std::size_t>(i - 15)], 18) ^
            (w[static_cast<std::size_t>(i - 15)] >> 3);
        const std::uint32_t s1 =
            rotr(w[static_cast<std::size_t>(i - 2)], 17) ^
            rotr(w[static_cast<std::size_t>(i - 2)], 19) ^
            (w[static_cast<std::size_t>(i - 2)] >> 10);
        w[static_cast<std::size_t>(i)] =
            w[static_cast<std::size_t>(i - 16)] + s0 +
            w[static_cast<std::size_t>(i - 7)] + s1;
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = hh + S1 + ch +
            kRoundConstants[static_cast<std::size_t>(i)] + w[static_cast<std::size_t>(i)];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = S0 + maj;

        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace detail

// SHA-256 over a byte span (docs/design/11 §6.2 signature). Deterministic; no heap
// allocation in the hot loop (only a fixed-size stack message schedule); identical
// digest on arm64 and Linux x64 (pure integer arithmetic) [ADR-013 C5].
inline Sha256 sha256(std::span<const std::byte> data) noexcept {
    // FIPS 180-4 §5.3.3 initial hash values (fractional parts of the square roots of
    // the first 8 primes).
    std::array<std::uint32_t, 8> h = {{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    }};

    const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
    const std::uint64_t len = static_cast<std::uint64_t>(data.size());

    // Process all complete 64-byte blocks directly from the input.
    std::uint64_t offset = 0;
    while (len - offset >= 64) {
        detail::compress(h, p + offset);
        offset += 64;
    }

    // Final block(s): copy the tail, append 0x80, pad with zeros, then the 64-bit
    // big-endian bit length. One fixed-size stack buffer (no allocation).
    std::array<std::uint8_t, 128> tail{};
    const std::size_t rem = static_cast<std::size_t>(len - offset);
    for (std::size_t i = 0; i < rem; ++i) tail[i] = p[offset + i];
    tail[rem] = 0x80u;

    // If the remainder + the 0x80 + the 8-byte length do not fit in one block, use
    // two blocks.
    const std::size_t tailBlocks = (rem >= 56) ? 2u : 1u;
    const std::size_t totalTail = tailBlocks * 64u;

    const std::uint64_t bitLen = len * 8u;
    for (int i = 0; i < 8; ++i) {
        tail[totalTail - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((bitLen >> (8 * i)) & 0xFFu);
    }

    for (std::size_t b = 0; b < tailBlocks; ++b) {
        detail::compress(h, tail.data() + b * 64u);
    }

    Sha256 out{};
    for (int i = 0; i < 8; ++i) {
        out.bytes[static_cast<std::size_t>(i * 4 + 0)] =
            static_cast<std::uint8_t>((h[static_cast<std::size_t>(i)] >> 24) & 0xFFu);
        out.bytes[static_cast<std::size_t>(i * 4 + 1)] =
            static_cast<std::uint8_t>((h[static_cast<std::size_t>(i)] >> 16) & 0xFFu);
        out.bytes[static_cast<std::size_t>(i * 4 + 2)] =
            static_cast<std::uint8_t>((h[static_cast<std::size_t>(i)] >> 8) & 0xFFu);
        out.bytes[static_cast<std::size_t>(i * 4 + 3)] =
            static_cast<std::uint8_t>((h[static_cast<std::size_t>(i)]) & 0xFFu);
    }
    return out;
}

// Lowercase 64-char hex of the digest, for MANIFEST.toml / sidecar JSON use
// (plan/backlog/040 Scope; docs/design/11 §7.3 artifactSha256 field).
inline std::string toHex(const Sha256& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.resize(64);
    for (std::size_t i = 0; i < d.bytes.size(); ++i) {
        s[i * 2 + 0] = kHex[(d.bytes[i] >> 4) & 0x0Fu];
        s[i * 2 + 1] = kHex[d.bytes[i] & 0x0Fu];
    }
    return s;
}

// Parse a 64-char lowercase/uppercase hex string back into a digest (inverse of
// toHex) for reading expected hashes from MANIFEST/sidecar/test vectors. A malformed
// input (wrong length or non-hex) yields an all-zero digest.
inline Sha256 fromHex(std::string_view hex) noexcept {
    Sha256 out{};
    if (hex.size() != 64) return out;
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < 32; ++i) {
        const int hi = nibble(hex[i * 2 + 0]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return Sha256{};   // malformed -> all-zero
        out.bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

} // namespace mw::golden
