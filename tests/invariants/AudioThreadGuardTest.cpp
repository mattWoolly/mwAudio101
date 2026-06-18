// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Self-tests for the AudioThreadGuard sentinel (task 010). Names begin with "rt".
// Paired positive/negative + the one-time warm-up carve-out, per docs/design/11 sec 13.1.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <new>
#include <vector>

#include "AudioThreadGuard.h"

using mw::test::AudioThreadGuard;

namespace {
// A heap allocation the compiler cannot elide: the size is read from a volatile, so
// [expr.new] allocation-elision does not apply and operator new is genuinely called.
[[nodiscard]] void* nonElidableAlloc() noexcept {
    volatile std::size_t n = 64;
    return ::operator new(static_cast<std::size_t>(n));
}
} // namespace

TEST_CASE("rt: an allocation inside an armed scope trips a violation (positive)", "[rt]") {
    AudioThreadGuard g;
    g.arm();
    void* leak = nonElidableAlloc();    // a heap allocation while armed
    g.disarm();
    REQUIRE(g.violated());
    REQUIRE_FALSE(g.violations().empty());
    REQUIRE(g.violations().front().kind == AudioThreadGuard::Violation::Kind::Alloc);
    ::operator delete(leak);             // freed outside the armed scope
}

TEST_CASE("rt: a clean armed scope reports no violation (negative control)", "[rt]") {
    // Pre-touch a buffer so its storage is already allocated before arming.
    std::vector<float> buf(256, 0.0f);
    AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    for (float& x : buf) { x = acc; acc += 1.0f; }   // no heap alloc, just arithmetic
    g.disarm();
    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}

TEST_CASE("rt: the one-time warm-up carve-out excuses exactly one allocation", "[rt]") {
    AudioThreadGuard g;
    g.arm();
    g.allowWarmUpOnce();
    void* a = nonElidableAlloc();    // excused by the carve-out
    void* b = nonElidableAlloc();    // NOT excused — the carve-out is one-time
    g.disarm();
    REQUIRE(g.violated());           // the second alloc tripped it
    REQUIRE(g.violations().size() == 1);   // carve-out is non-blanket
    ::operator delete(a);
    ::operator delete(b);
}
