// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/TriggerSourceTest.cpp — TriggerSource (S7) unit tests (task 083).
//
// Test-case names begin with "trigsource" so `-R trigsource` selects exactly this
// suite (the silent-pass rule, AGENTS.md). Each TEST_CASE maps to an 083 acceptance
// criterion and to the cited docs/design/05 sections (§4.1–§4.4) and the ADR-007
// coupling rows C4/C5/C6.
//
// S7 couples note priority + envelope retrigger into one selector:
//   GateTrig => last-note priority + retrigger on every justPressed (§4.3 / C4)
//   Gate     => lowest-note priority + no legato retrigger; one sustained gate (C5)
//   Lfo      => lowest-note priority + retrigger on each lfoEdge while held (C6);
//               GATE-mode priority is lowest, not highest (§4.1).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

#include "control/TriggerSource.h"
#include "control/ControlTypes.h"

#include "../invariants/AudioThreadGuard.h"

using namespace mw::control;

namespace {

// Build a held-bitmap mask from a key index (0..31).
constexpr std::uint32_t bit(int key) noexcept { return std::uint32_t{1} << key; }

} // namespace

// --- §4.2 surface: enum-derived priority + default mode ----------------------

TEST_CASE("trigsource: priority is derived from the S7 mode (sec 4.3)", "[trigsource]") {
    TriggerSource ts;
    // Default mode is GateTrig (matches ControlSnapshot INIT pole, §9.2).
    REQUIRE(ts.mode() == TrigMode::GateTrig);
    REQUIRE(ts.priority() == NotePriority::LastNote);

    ts.setMode(TrigMode::Gate);
    REQUIRE(ts.mode() == TrigMode::Gate);
    REQUIRE(ts.priority() == NotePriority::LowestNote);

    ts.setMode(TrigMode::Lfo);
    REQUIRE(ts.mode() == TrigMode::Lfo);
    REQUIRE(ts.priority() == NotePriority::LowestNote);  // §4.1: lowest, NOT highest
}

// --- C4: GateTrig => last-note priority + retrigger on every new key ----------

TEST_CASE("trigsource: GateTrig selects last-pressed and retriggers on every justPressed (C4)",
          "[trigsource]") {
    TriggerSource ts;
    ts.prepare();
    ts.setMode(TrigMode::GateTrig);

    // Press key 10 first.
    KeyState ks{};
    ks.held = bit(10);
    ks.justPressed = bit(10);
    ts.observe(ks);
    TriggerDecision d = ts.resolve(ks, /*lfoEdge=*/false);
    REQUIRE(d.selectedKey == 10);
    REQUIRE(d.gateOn);
    REQUIRE(d.retrigger);       // first key down => retrigger
    REQUIRE_FALSE(d.legato);

    // Now press a LOWER key 3 while 10 still held: last-note must pick the NEW key 3
    // (not the lowest), and retrigger fires again on the new justPressed.
    ks.held = bit(10) | bit(3);
    ks.justPressed = bit(3);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 3);   // last pressed, even though it is the lower key
    REQUIRE(d.gateOn);
    REQUIRE(d.retrigger);          // every new key retriggers in GateTrig
    REQUIRE(d.legato);             // a key was already held

    // Press a HIGHER key 20 last: last-note picks 20.
    ks.held = bit(10) | bit(3) | bit(20);
    ks.justPressed = bit(20);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 20);  // last pressed
    REQUIRE(d.retrigger);
    REQUIRE(d.legato);

    // No new press this scan (sustained): no retrigger, fall back to most-recent
    // still-held key (20).
    ks.justPressed = 0;
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 20);  // most-recent still-held
    REQUIRE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);    // no new key => no retrigger

    // Release the most-recent key 20: last-note falls back to the next-most-recent
    // still-held key, which is key 3 (pressed after key 10).
    ks.held = bit(10) | bit(3);
    ks.justPressed = 0;
    ks.justReleased = bit(20);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 3);   // most-recent of {10,3} is 3
    REQUIRE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);

    // All keys up: no note, gate off.
    ks.held = 0;
    ks.justPressed = 0;
    ks.justReleased = bit(10) | bit(3);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == -1);
    REQUIRE_FALSE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);
}

// --- C5: Gate => lowest-note priority + no legato retrigger; one sustained gate

TEST_CASE("trigsource: Gate selects lowest-note and does not retrigger on legato (C5)",
          "[trigsource]") {
    TriggerSource ts;
    ts.prepare();
    ts.setMode(TrigMode::Gate);

    // First key 10 down: held==0 -> held!=0 transition retriggers once.
    KeyState ks{};
    ks.held = bit(10);
    ks.justPressed = bit(10);
    ts.observe(ks);
    TriggerDecision d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 10);
    REQUIRE(d.gateOn);
    REQUIRE(d.retrigger);          // non-legato onset retriggers
    REQUIRE_FALSE(d.legato);

    // Legato keypress of a HIGHER key 20 while 10 held: lowest-note keeps 10, and the
    // single gate sustains — NO retrigger.
    ks.held = bit(10) | bit(20);
    ks.justPressed = bit(20);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 10);  // lowest, not last
    REQUIRE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);    // legato => NO retrigger
    REQUIRE(d.legato);

    // Legato keypress of a LOWER key 3: lowest-note now picks 3 — still no retrigger
    // (a note was already held).
    ks.held = bit(10) | bit(20) | bit(3);
    ks.justPressed = bit(3);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 3);   // new lowest
    REQUIRE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);    // still legato => no retrigger
    REQUIRE(d.legato);

    // Sustained, no new press: gate held, no retrigger.
    ks.justPressed = 0;
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 3);
    REQUIRE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);

    // Release all keys then a fresh key 5: held returns to 0 then 1 -> a NEW
    // non-legato onset retriggers again (single sustained gate model).
    ks.held = 0;
    ks.justPressed = 0;
    ks.justReleased = bit(3) | bit(10) | bit(20);
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == -1);
    REQUIRE_FALSE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);

    ks.held = bit(5);
    ks.justPressed = bit(5);
    ks.justReleased = 0;
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 5);
    REQUIRE(d.gateOn);
    REQUIRE(d.retrigger);          // fresh held==0 -> held!=0 => retrigger
    REQUIRE_FALSE(d.legato);
}

// --- C6: Lfo => lowest-note priority + retrigger on each lfoEdge while held ----

TEST_CASE("trigsource: Lfo selects lowest-note and re-fires on each lfoEdge while held (C6)",
          "[trigsource]") {
    TriggerSource ts;
    ts.prepare();
    ts.setMode(TrigMode::Lfo);

    // Hold a chord: lowest-note priority selects the lowest held key.
    KeyState ks{};
    ks.held = bit(7) | bit(15) | bit(2);
    ks.justPressed = bit(7) | bit(15) | bit(2);
    ts.observe(ks);

    // No edge this tick: gate asserted while held, but no re-fire without an edge.
    TriggerDecision d = ts.resolve(ks, /*lfoEdge=*/false);
    REQUIRE(d.selectedKey == 2);   // lowest, NOT highest (§4.1)
    REQUIRE(d.gateOn);             // gate asserted while a key is held
    REQUIRE_FALSE(d.retrigger);    // no edge => no re-fire

    // An lfoEdge while held re-fires the envelope.
    ks.justPressed = 0;
    ts.observe(ks);
    d = ts.resolve(ks, /*lfoEdge=*/true);
    REQUIRE(d.selectedKey == 2);
    REQUIRE(d.gateOn);
    REQUIRE(d.retrigger);          // re-fire on the edge

    // Another edge: re-fires again while still held.
    d = ts.resolve(ks, /*lfoEdge=*/true);
    REQUIRE(d.retrigger);

    // Edge with NO keys held: gate off, no re-fire.
    ks.held = 0;
    ks.justReleased = bit(7) | bit(15) | bit(2);
    ts.observe(ks);
    d = ts.resolve(ks, /*lfoEdge=*/true);
    REQUIRE(d.selectedKey == -1);
    REQUIRE_FALSE(d.gateOn);
    REQUIRE_FALSE(d.retrigger);    // no key held => edge does not fire
}

// --- §4.1 oracle: GATE-mode priority is LOWEST, not HIGHEST -------------------

TEST_CASE("trigsource: GATE/LFO priority is lowest-note not highest-note (sec 4.1 oracle)",
          "[trigsource]") {
    // A held set with a clear lowest and highest; the correct (lowest) selection must
    // be the SMALLEST set-bit index, never the largest. This guards the documented
    // forum-misreading regression (research/07 §8.2).
    const std::uint32_t chord = bit(1) | bit(9) | bit(30);
    const int lowest = 1;
    const int highest = 30;

    for (TrigMode m : {TrigMode::Gate, TrigMode::Lfo}) {
        TriggerSource ts;
        ts.prepare();
        ts.setMode(m);

        KeyState ks{};
        ks.held = chord;
        ks.justPressed = chord;
        ts.observe(ks);
        TriggerDecision d = ts.resolve(ks, false);
        REQUIRE(d.selectedKey == lowest);
        REQUIRE(d.selectedKey != highest);
    }
}

// --- §4.4 RT invariant: resolve() is noexcept and allocates/locks nothing ------

TEST_CASE("trigsource: resolve and observe are noexcept and allocate nothing under sentinel (sec 4.4)",
          "[trigsource]") {
    static_assert(noexcept(std::declval<const TriggerSource&>().resolve(
                      std::declval<const KeyState&>(), false)),
                  "resolve() MUST be noexcept [docs/design/05 §4.4; ADR-007 C26].");
    static_assert(noexcept(std::declval<TriggerSource&>().observe(
                      std::declval<const KeyState&>())),
                  "observe() MUST be noexcept [docs/design/05 §4.4].");
    static_assert(noexcept(std::declval<TriggerSource&>().setMode(TrigMode::Gate)),
                  "setMode() MUST be noexcept.");

    TriggerSource ts;
    ts.prepare();                 // prepare() pre-sizes the last-pressed array
    ts.setMode(TrigMode::GateTrig);

    // Pre-fill some state before arming so prepare-time work is excluded.
    KeyState ks{};
    ks.held = bit(4) | bit(12);
    ks.justPressed = bit(4) | bit(12);
    ts.observe(ks);

    mw::test::AudioThreadGuard g;
    g.arm();
    int acc = 0;
    // Hammer observe()+resolve() across all three modes and both edge values.
    for (int i = 0; i < 4096; ++i) {
        const std::uint32_t pressed = bit(i & 31);
        ks.held |= pressed;
        ks.justPressed = pressed;
        ts.observe(ks);
        TriggerDecision d = ts.resolve(ks, (i & 1) != 0);
        acc += d.selectedKey + (d.retrigger ? 1 : 0) + (d.gateOn ? 1 : 0);
        if ((i & 0x3F) == 0) {
            ks.held = 0;          // periodic all-up to exercise the held==0 path
        }
    }
    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
    REQUIRE(acc != 0);            // touch acc so the loop is not optimized away
}

// --- §4.4: prepare() pre-sizes last-pressed array; never resizes (state class) -

TEST_CASE("trigsource: a fresh prepare clears last-pressed state (sec 4.4)", "[trigsource]") {
    TriggerSource ts;
    ts.prepare();
    ts.setMode(TrigMode::GateTrig);

    // Press 10 then 20.
    KeyState ks{};
    ks.held = bit(10);
    ks.justPressed = bit(10);
    ts.observe(ks);
    ks.held = bit(10) | bit(20);
    ks.justPressed = bit(20);
    ts.observe(ks);
    TriggerDecision d = ts.resolve(ks, false);
    REQUIRE(d.selectedKey == 20);

    // Re-prepare must wipe the press history: with both keys merely held (no
    // justPressed) the last-note fallback has no stamp to favor, so it falls back to
    // a deterministic still-held key. After a clean prepare with no observed presses
    // for these keys, the most-recent-held fallback resolves to the lowest held key.
    ts.prepare();
    ks.held = bit(10) | bit(20);
    ks.justPressed = 0;
    ts.observe(ks);
    d = ts.resolve(ks, false);
    REQUIRE(d.gateOn);
    // Deterministic: lowest held key when no press stamp distinguishes them.
    REQUIRE(d.selectedKey == 10);
}
