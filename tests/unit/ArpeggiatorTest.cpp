// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the Arpeggiator (task 084). Realizes docs/design/05 §5.1–§5.4:
// three mutually-exclusive directions (UP / U&D / DOWN) over a fixed 32-bit held-key
// bitmap with HOLD latch, no automatic octave expansion, engaging on chord/legato,
// advancing one key per clock H->L edge.
//
// Test-case NAMES begin with "arp" so `ctest -R arp` selects them (catch_discover_tests
// registers test-case NAMES, not tags). The display names AVOID '[' so Catch2 does
// not mis-parse a tag out of the name and break `ctest -R` selection. The Catch2 [arp]
// tag is the task's subsystem label; the labels_snapshot test may FAIL until the
// orchestrator regenerates the snapshot for this wave (expected, do not edit it).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "control/Arpeggiator.h"
#include "calibration/ArpConstants.h"
#include "../invariants/AudioThreadGuard.h"

using mw::control::Arpeggiator;
using mw::control::ArpMode;

namespace {

// Collect `steps` outputs of advanceOnEdge() into a vector for sequence assertions.
std::vector<int> walk(Arpeggiator& a, int steps) {
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) out.push_back(a.advanceOnEdge());
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// §5.4 signature / construction defaults.
// ---------------------------------------------------------------------------
TEST_CASE("arp: prepare clears the bitmap and resets to UP defaults", "[arp]") {
    Arpeggiator a;
    a.noteOn(5);
    a.prepare();
    REQUIRE(a.heldBitmap() == 0u);
    REQUIRE(a.heldCount() == 0);
    REQUIRE_FALSE(a.isEngaged());
    // No held keys => nothing to sound.
    REQUIRE(a.advanceOnEdge() == -1);
}

// ---------------------------------------------------------------------------
// §5.1 / C8 — 32 distinct held keys are ALL cycled; held-note bitmap, not 4-note
// poly; NO automatic octave expansion.
// ---------------------------------------------------------------------------
TEST_CASE("arp: all 32 distinct held keys are cycled with no octave expansion", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::Up);
    for (int k = 0; k < 32; ++k) a.noteOn(k);

    REQUIRE(a.heldCount() == 32);
    REQUIRE(a.heldBitmap() == 0xFFFFFFFFu);

    // One full cycle visits every held key 0..31 exactly once, ascending.
    auto seq = walk(a, 32);
    for (int k = 0; k < 32; ++k) {
        REQUIRE(seq[static_cast<std::size_t>(k)] == k);
    }

    // No automatic octave expansion: the next step wraps back to the lowest key,
    // it does NOT emit key 32 (an octave above the top held key).
    REQUIRE(a.advanceOnEdge() == 0);
}

// ---------------------------------------------------------------------------
// §5.4 — UP walks set bits ascending; DOWN descending; sparse (non-contiguous) sets.
// ---------------------------------------------------------------------------
TEST_CASE("arp: UP walks held bits ascending and wraps", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::Up);
    a.noteOn(3);
    a.noteOn(7);
    a.noteOn(12);

    REQUIRE(a.heldCount() == 3);
    REQUIRE(walk(a, 6) == std::vector<int>{3, 7, 12, 3, 7, 12});
}

TEST_CASE("arp: DOWN walks held bits descending and wraps", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::Down);
    a.noteOn(3);
    a.noteOn(7);
    a.noteOn(12);

    REQUIRE(walk(a, 6) == std::vector<int>{12, 7, 3, 12, 7, 3});
}

// ---------------------------------------------------------------------------
// §5.2 / C11 — U&D turnaround honors UandDRepeatEndpoints for BOTH values, producing
// the documented sequences for a 4-note set {1,2,3,4}.
//   false => 1 2 3 4 3 2 (1 2 3 4 ...)   endpoints NOT repeated
//   true  => 1 2 3 4 4 3 2 1 (1 2 ...)   endpoints repeated
// (Keys here are 1..4 to mirror the design-doc 1-based illustration.)
// ---------------------------------------------------------------------------
TEST_CASE("arp: U and D with repeatEndpoints false does not repeat the turnaround notes", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::UandD);
    a.setUandDRepeatEndpoints(false);
    a.noteOn(1);
    a.noteOn(2);
    a.noteOn(3);
    a.noteOn(4);

    // One full period = 1 2 3 4 3 2, then it repeats.
    REQUIRE(walk(a, 12) == std::vector<int>{1, 2, 3, 4, 3, 2, 1, 2, 3, 4, 3, 2});
}

TEST_CASE("arp: U and D with repeatEndpoints true repeats the turnaround notes", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::UandD);
    a.setUandDRepeatEndpoints(true);
    a.noteOn(1);
    a.noteOn(2);
    a.noteOn(3);
    a.noteOn(4);

    // One full period = 1 2 3 4 4 3 2 1, then it repeats.
    REQUIRE(walk(a, 16) == std::vector<int>{1, 2, 3, 4, 4, 3, 2, 1, 1, 2, 3, 4, 4, 3, 2, 1});
}

// A two-note U&D set is the minimal turnaround case for both endpoint policies.
TEST_CASE("arp: U and D two-note set follows endpoint policy", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::UandD);

    a.setUandDRepeatEndpoints(false);
    a.noteOn(2);
    a.noteOn(9);
    // {2,9}: up 2,9 then down (no endpoint repeat) is just 2,9,2,9...
    REQUIRE(walk(a, 6) == std::vector<int>{2, 9, 2, 9, 2, 9});

    a.prepare();
    a.setMode(ArpMode::UandD);
    a.setUandDRepeatEndpoints(true);
    a.noteOn(2);
    a.noteOn(9);
    // {2,9} with endpoint repeat: 2 9 9 2 2 9 9 2 ...
    REQUIRE(walk(a, 8) == std::vector<int>{2, 9, 9, 2, 2, 9, 9, 2});
}

// ---------------------------------------------------------------------------
// §5.2 — the default U&D repeat-endpoints choice is sourced from Calibration.
// ---------------------------------------------------------------------------
TEST_CASE("arp: default U and D repeat-endpoints matches Calibration kArpUandDRepeatEndpoints", "[arp]") {
    // The calibration (PI) default is false.
    REQUIRE(mw::cal::arp::kArpUandDRepeatEndpoints == false);

    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::UandD);
    // No setUandDRepeatEndpoints() call: the object must default to the calibration
    // value, so a 4-note set must NOT repeat the turnaround.
    a.noteOn(1);
    a.noteOn(2);
    a.noteOn(3);
    a.noteOn(4);
    REQUIRE(walk(a, 6) == std::vector<int>{1, 2, 3, 4, 3, 2});
}

// ---------------------------------------------------------------------------
// §5.1 / C10 — engagement: chord/legato engages; a single non-legato note does NOT.
// ---------------------------------------------------------------------------
TEST_CASE("arp: a single non-legato note does not engage", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.noteOn(10);
    REQUIRE(a.heldCount() == 1);
    REQUIRE_FALSE(a.isEngaged());   // one key, no HOLD => plays normally, arp inactive
}

TEST_CASE("arp: a chord of two or more keys engages", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.noteOn(10);
    a.noteOn(14);
    REQUIRE(a.heldCount() == 2);
    REQUIRE(a.isEngaged());         // two keys => engaged
}

TEST_CASE("arp: a legato keypress while another is held engages", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.noteOn(10);                   // first key down
    REQUIRE_FALSE(a.isEngaged());
    a.noteOn(20);                   // second key pressed while first held => legato
    REQUIRE(a.isEngaged());
    a.noteOff(10);                  // back to a single held key (no HOLD)
    REQUIRE(a.heldCount() == 1);
    REQUIRE_FALSE(a.isEngaged());
}

// ---------------------------------------------------------------------------
// §5.1 / C10 — HOLD latch keeps the held set after key release; a NEW chord while
// latched REPLACES the latched set.
// ---------------------------------------------------------------------------
TEST_CASE("arp: HOLD latch survives key release", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::Up);
    a.noteOn(4);
    a.noteOn(8);
    a.setHold(true);

    REQUIRE(a.isEngaged());
    a.noteOff(4);
    a.noteOff(8);                   // all physical keys released, but HOLD latched
    REQUIRE(a.heldCount() == 2);    // latched set survives
    REQUIRE(a.heldBitmap() == ((1u << 4) | (1u << 8)));
    REQUIRE(a.isEngaged());         // still engaged because latched
    REQUIRE(walk(a, 4) == std::vector<int>{4, 8, 4, 8});
}

TEST_CASE("arp: a new chord while latched replaces the latched set", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::Up);
    a.noteOn(4);
    a.noteOn(8);
    a.setHold(true);
    a.noteOff(4);
    a.noteOff(8);                   // {4,8} latched
    REQUIRE(a.heldBitmap() == ((1u << 4) | (1u << 8)));

    // A fresh keypress while latched starts a NEW set, replacing the latched one.
    a.noteOn(15);
    a.noteOn(19);
    REQUIRE(a.heldBitmap() == ((1u << 15) | (1u << 19)));   // old set gone
    REQUIRE(a.heldCount() == 2);
    REQUIRE(walk(a, 4) == std::vector<int>{15, 19, 15, 19});
}

TEST_CASE("arp: releasing HOLD with no keys held clears the latched set", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.noteOn(4);
    a.noteOn(8);
    a.setHold(true);
    a.noteOff(4);
    a.noteOff(8);
    REQUIRE(a.heldCount() == 2);    // latched

    a.setHold(false);               // HOLD off, no physical keys held => set clears
    REQUIRE(a.heldCount() == 0);
    REQUIRE_FALSE(a.isEngaged());
}

// ---------------------------------------------------------------------------
// §5.4 — advanceOnEdge does NO heap allocation under the alloc sentinel.
// ---------------------------------------------------------------------------
TEST_CASE("arp: advanceOnEdge does no heap allocation under the sentinel", "[arp]") {
    Arpeggiator a;
    a.prepare();
    a.setMode(ArpMode::UandD);
    for (int k = 0; k < 32; ++k) a.noteOn(k);   // worst-case 32-key set, set up first

    mw::test::AudioThreadGuard g;
    g.arm();
    int acc = 0;
    for (int i = 0; i < 256; ++i) acc += a.advanceOnEdge();   // hot path only
    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
    REQUIRE(acc >= 0);   // use acc so it cannot be optimized away
}

// advanceOnEdge and the hot accessors are noexcept (RT contract, §5.4 / §10).
TEST_CASE("arp: advanceOnEdge and accessors are noexcept", "[arp]") {
    Arpeggiator a;
    STATIC_REQUIRE(noexcept(a.advanceOnEdge()));
    STATIC_REQUIRE(noexcept(a.heldBitmap()));
    STATIC_REQUIRE(noexcept(a.heldCount()));
    STATIC_REQUIRE(noexcept(a.isEngaged()));
    STATIC_REQUIRE(noexcept(a.noteOn(0)));
    STATIC_REQUIRE(noexcept(a.noteOff(0)));
}
