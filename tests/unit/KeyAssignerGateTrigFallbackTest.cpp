// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Regression tests for the KeyAssigner GATE+TRIG last-note fallback bug (task
// 069b). Names begin with "keyassignerfix" so `-R keyassignerfix` selects them.
//
// The bug: in GateTrig (last-note) mode, when the active key is released with
// NO new key down this tick, resolve()'s `else` arm fell back to lowestHeld()
// instead of the NORMATIVE §5.3 / §5.4 K3 rule "most-recently-pressed
// still-held key". The §5.3 wording and the task-152 oracle
// (tests/golden/KeyAssignerReference) both require most-recently-pressed.
//
// Covered here:
//   - the minimal repro: press 60,64,67 (active 67), release 67 -> 64 (NOT 60);
//   - the unwind chain continues last-note: release 64 -> 60;
//   - Gate-mode (K1/K2) lowest-note release behavior is UNCHANGED;
//   - resolve()/noteOn remain noexcept.
//
// These mirror the oracle's mostRecentHeld() semantics in
// tests/golden/KeyAssignerReference.h [docs/design/04 §5.3, §5.4 K2/K3].

#include <catch2/catch_test_macros.hpp>

#include "voice/KeyAssigner.h"
#include "voice/VoiceTypes.h"

using mw::GateTrigMode;
using mw::KeyAssigner;
using mw::NoteDecision;

// --- noexcept surface (acceptance: resolve()/noteOn remain noexcept) ---------

TEST_CASE("keyassignerfix: resolve and noteOn remain noexcept", "[keyassignerfix]") {
    KeyAssigner ka;
    STATIC_REQUIRE(noexcept(ka.noteOn(60)));
    STATIC_REQUIRE(noexcept(ka.resolve()));
    STATIC_REQUIRE(noexcept(ka.reset()));
    STATIC_REQUIRE(noexcept(ka.prepare()));
}

// --- the minimal repro: GateTrig release falls back to most-recent-held ------
//
// press 60,64,67 (each its own tick) -> active 67. Release 67 with no new
// down -> §5.3 requires the most-recently-pressed still-held key = 64, NOT the
// lowest-held (60). [§5.4 K3; §5.3]

TEST_CASE("keyassignerfix: GateTrig release of active picks most-recent still-held not lowest",
          "[keyassignerfix]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    REQUIRE(ka.resolve().activeNote == 60);

    ka.noteOn(64);
    REQUIRE(ka.resolve().activeNote == 64);

    ka.noteOn(67);
    REQUIRE(ka.resolve().activeNote == 67);

    // Release the active key (67) with NO new key down this tick.
    ka.noteOff(67);
    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 64);   // most-recently-pressed still-held (NOT 60)
    REQUIRE(d.gate);
}

// --- the unwind chain continues last-note: release 64 -> 60 ------------------

TEST_CASE("keyassignerfix: GateTrig unwind chain continues most-recent still-held",
          "[keyassignerfix]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    ka.resolve();
    ka.noteOn(64);
    ka.resolve();
    ka.noteOn(67);
    REQUIRE(ka.resolve().activeNote == 67);

    ka.noteOff(67);
    REQUIRE(ka.resolve().activeNote == 64);

    ka.noteOff(64);
    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 60);   // only 60 still held -> 60
    REQUIRE(d.gate);

    ka.noteOff(60);
    const NoteDecision off = ka.resolve();
    REQUIRE(off.activeNote == -1);
    REQUIRE_FALSE(off.gate);
}

// --- non-monotonic press order: most-recent need not be the highest note -----
//
// press 67, then 60, then 62 (active 62). Release 62 -> most-recent still-held
// is 60 (pressed after 67), proving "most-recent" is by press order, NOT note
// number and NOT lowest-held. [§5.3]

TEST_CASE("keyassignerfix: GateTrig most-recent is by press order not pitch",
          "[keyassignerfix]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(67);
    REQUIRE(ka.resolve().activeNote == 67);
    ka.noteOn(60);
    REQUIRE(ka.resolve().activeNote == 60);
    ka.noteOn(62);
    REQUIRE(ka.resolve().activeNote == 62);

    ka.noteOff(62);
    // still held: 67, 60. Most-recently-pressed of those is 60 (serial after 67).
    REQUIRE(ka.resolve().activeNote == 60);

    ka.noteOff(60);
    // only 67 remains.
    REQUIRE(ka.resolve().activeNote == 67);
}

// --- re-press of an already-held key does NOT re-stamp (mirror oracle) -------
//
// Holding 60 then 64 (active 64): a redundant noteOn(60) while 60 is already
// held must NOT bump 60's press serial. Releasing 64 must therefore still pick
// 60 only because it is the lone remaining key — but the no-re-stamp guard is
// what keeps the serial monotonic. We check the held-set semantics: re-pressing
// 60 does not make it "more recent" than 64. [task 069b Scope; oracle parity]

TEST_CASE("keyassignerfix: GateTrig re-press of held key does not re-stamp recency",
          "[keyassignerfix]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    ka.resolve();
    ka.noteOn(64);
    REQUIRE(ka.resolve().activeNote == 64);

    // Redundant down on the already-held 60 (no fresh press): no new changed-down
    // either, since 60 was already in prevScan. Active note must remain 64.
    ka.noteOn(60);
    REQUIRE(ka.resolve().activeNote == 64);

    // Now release 64. 60's recency was NOT bumped by the redundant press, but 60
    // is the only remaining key, so it is selected.
    ka.noteOff(64);
    REQUIRE(ka.resolve().activeNote == 60);
}

// --- Gate-mode no-regression: lowest-note release UNCHANGED (K2) -------------
//
// In Gate (lowest-note) mode, releasing the active lowest with others held must
// still yield the next-LOWEST still-held key, never the most-recent. This arm
// (K1/K2/K5) must be untouched by the fix. [§5.4 K2]

TEST_CASE("keyassignerfix: Gate mode release still yields next-lowest unchanged",
          "[keyassignerfix]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Gate);

    // Press high-to-low arrival order to make lowest != most-recent.
    ka.noteOn(67);
    REQUIRE(ka.resolve().activeNote == 67);   // only key held
    ka.noteOn(64);
    REQUIRE(ka.resolve().activeNote == 64);   // lowest of {67,64}
    ka.noteOn(60);
    REQUIRE(ka.resolve().activeNote == 60);   // lowest of {67,64,60}

    // Release the active lowest (60). Lowest-note priority -> next-lowest = 64,
    // NOT the most-recently-pressed (which would be 60's predecessor logic).
    ka.noteOff(60);
    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 64);   // next-lowest still-held (K2 unchanged)
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);    // Gate: no retrigger on release legato
}

// --- reset clears recency state (no stale serial across reset) ---------------

TEST_CASE("keyassignerfix: reset clears press-serial recency state", "[keyassignerfix]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    ka.resolve();
    ka.noteOn(64);
    ka.resolve();
    ka.noteOn(67);
    ka.resolve();

    ka.reset();
    REQUIRE_FALSE(ka.anyHeld());

    // Fresh sequence after reset: press 62 then 65 (active 65). Release 65 ->
    // most-recent still-held = 62. A stale serial table would mis-rank.
    ka.noteOn(62);
    REQUIRE(ka.resolve().activeNote == 62);
    ka.noteOn(65);
    REQUIRE(ka.resolve().activeNote == 65);

    ka.noteOff(65);
    REQUIRE(ka.resolve().activeNote == 62);
}
