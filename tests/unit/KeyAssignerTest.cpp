// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the bit-faithful KeyAssigner note-priority/retrigger
// state machine (task 069). Names begin with "keyassigner" so
// `-R keyassigner` selects them under the silent-pass rule.
//
// Asserts the NORMATIVE §5.4 K1-K7 contract of
// docs/design/04-voice-and-control.md (and ADR-006 §Contract C1-C7):
//   - K1/K2 Gate:    lowest-note priority, gate stays asserted (no retrigger),
//                    active note tracks the lowest still-held on release.
//   - K3/K4 GateTrig: last-note priority; new key retriggers; multiple downs in
//                    one tick resolve to the lowest of the just-pressed
//                    (XOR changed-down vs prior scan) with exactly one retrigger.
//   - K5/K6 Lfo:     lowest-note pitch but NO key-driven retrigger; a new keypress
//                    asserts clockReset.
//   - K7:            all keys released de-asserts the gate.
//   - coupling:      no path yields last-note priority without retrigger.
//   - resolve()/noteOn/noteOff are noexcept; the scan is bounded O(128).

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "voice/KeyAssigner.h"
#include "voice/VoiceTypes.h"

using mw::GateTrigMode;
using mw::KeyAssigner;
using mw::NoteDecision;

// --- noexcept / RT-safety surface (acceptance: hot paths noexcept) ----------

TEST_CASE("keyassigner: hot-path methods are noexcept", "[keyassigner]") {
    KeyAssigner ka;
    STATIC_REQUIRE(noexcept(ka.noteOn(60)));
    STATIC_REQUIRE(noexcept(ka.noteOff(60)));
    STATIC_REQUIRE(noexcept(ka.resolve()));
    STATIC_REQUIRE(noexcept(ka.anyHeld()));
    STATIC_REQUIRE(noexcept(ka.prepare()));
    STATIC_REQUIRE(noexcept(ka.reset()));
    STATIC_REQUIRE(noexcept(ka.setMode(GateTrigMode::Gate)));
}

// --- empty / no-note baseline -----------------------------------------------

TEST_CASE("keyassigner: no keys held resolves to gate off, no note", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    REQUIRE_FALSE(ka.anyHeld());

    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == -1);
    REQUIRE_FALSE(d.gate);
    REQUIRE_FALSE(d.retrigger);
    REQUIRE_FALSE(d.clockReset);
}

// --- K1: Gate, new key while held -> lowest wins, gate stays, no retrigger ---

TEST_CASE("keyassigner: K1 Gate new key while held keeps gate, no retrigger, lowest wins",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Gate);

    ka.noteOn(60);
    NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);  // first key from silence DOES trigger the ADSR

    // New, HIGHER key while 60 still held: lowest (60) still wins, gate held, no retrigger.
    ka.noteOn(67);
    d = ka.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);

    // New, LOWER key while held: it becomes the lowest, still NO retrigger in Gate.
    ka.noteOn(55);
    d = ka.resolve();
    REQUIRE(d.activeNote == 55);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);
}

// --- K2: Gate, release the current lowest while others held -----------------

TEST_CASE("keyassigner: K2 Gate release of lowest selects next-lowest, gate stays",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Gate);

    ka.noteOn(60);
    ka.noteOn(64);
    ka.noteOn(67);
    NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 60);  // lowest
    REQUIRE(d.gate);

    // Release the lowest (60): active becomes next-lowest still-held (64); gate stays; no retrigger.
    ka.noteOff(60);
    d = ka.resolve();
    REQUIRE(d.activeNote == 64);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);

    // Release 64: next-lowest is 67.
    ka.noteOff(64);
    d = ka.resolve();
    REQUIRE(d.activeNote == 67);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);
}

// --- K3: GateTrig, new key while held -> last-note priority + retrigger ------

TEST_CASE("keyassigner: K3 GateTrig new key takes last-note priority and retriggers",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);

    // New HIGHER key while held: last-note priority -> new key (67) wins, retrigger.
    ka.noteOn(67);
    d = ka.resolve();
    REQUIRE(d.activeNote == 67);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);

    // New LOWER key while held: last-note still means the just-pressed key wins, retrigger.
    ka.noteOn(55);
    d = ka.resolve();
    REQUIRE(d.activeNote == 55);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);
}

// --- K4: GateTrig, multiple downs in one tick -> lowest-of-just-pressed ------

TEST_CASE("keyassigner: K4 GateTrig multiple downs in one tick pick lowest just-pressed, one retrigger",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    // Three keys arrive within ONE control tick (no resolve between them).
    ka.noteOn(67);
    ka.noteOn(60);
    ka.noteOn(64);
    NoteDecision d = ka.resolve();
    // XOR changed-down = {60,64,67}; lowest of the just-pressed = 60.
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);  // exactly one retrigger for the batch

    // Next tick with NO new down: no retrigger, active note stays.
    d = ka.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);
}

TEST_CASE("keyassigner: K4 a held key re-found next tick is NOT a new down (no false retrigger)",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    REQUIRE(ka.resolve().retrigger);

    // 60 is still held; pressing a NEW key 62 in this tick is the only changed-down.
    ka.noteOn(62);
    NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 62);
    REQUIRE(d.retrigger);

    // No new event: prevScan now contains both -> no changed-down -> no retrigger.
    d = ka.resolve();
    REQUIRE_FALSE(d.retrigger);
    REQUIRE(d.activeNote == 62);  // most-recent still-held stays selected
}

// --- K5: Lfo uses lowest-note pitch but does NOT key-retrigger ---------------

TEST_CASE("keyassigner: K5 Lfo uses lowest-note pitch and does NOT retrigger from the key",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Lfo);

    ka.noteOn(60);
    NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 60);  // lowest-note priority pitch
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);  // ADSR is clock-driven, NOT key-driven in Lfo

    // Higher key while held: lowest (60) still drives pitch, still no key retrigger.
    ka.noteOn(72);
    d = ka.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE_FALSE(d.retrigger);

    // Lower key: becomes the lowest pitch, still no key retrigger.
    ka.noteOn(48);
    d = ka.resolve();
    REQUIRE(d.activeNote == 48);
    REQUIRE_FALSE(d.retrigger);
}

// --- K6: Lfo, new keypress -> clockReset asserted ----------------------------

TEST_CASE("keyassigner: K6 Lfo new keypress asserts clockReset", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Lfo);

    ka.noteOn(60);
    NoteDecision d = ka.resolve();
    REQUIRE(d.clockReset);  // new keypress in Lfo re-phases the clock

    // No new key this tick: clockReset clears.
    d = ka.resolve();
    REQUIRE_FALSE(d.clockReset);

    // Another new keypress: clockReset asserts again.
    ka.noteOn(67);
    d = ka.resolve();
    REQUIRE(d.clockReset);
}

TEST_CASE("keyassigner: clockReset is NOT asserted in Gate or GateTrig modes", "[keyassigner]") {
    KeyAssigner ka;

    ka.prepare();
    ka.setMode(GateTrigMode::Gate);
    ka.noteOn(60);
    REQUIRE_FALSE(ka.resolve().clockReset);

    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);
    ka.noteOn(64);
    REQUIRE_FALSE(ka.resolve().clockReset);
}

// --- K7: all keys released -> gate de-asserts --------------------------------

TEST_CASE("keyassigner: K7 all keys released de-asserts the gate", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Gate);

    ka.noteOn(60);
    ka.noteOn(64);
    REQUIRE(ka.resolve().gate);

    ka.noteOff(60);
    REQUIRE(ka.resolve().gate);  // 64 still held

    ka.noteOff(64);
    NoteDecision d = ka.resolve();
    REQUIRE_FALSE(d.gate);
    REQUIRE(d.activeNote == -1);
    REQUIRE_FALSE(d.retrigger);
    REQUIRE_FALSE(ka.anyHeld());
}

TEST_CASE("keyassigner: K7 all keys released de-asserts the gate in GateTrig too", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    REQUIRE(ka.resolve().gate);
    ka.noteOff(60);
    NoteDecision d = ka.resolve();
    REQUIRE_FALSE(d.gate);
    REQUIRE(d.activeNote == -1);
}

// --- Coupling: no path yields last-note priority WITHOUT retrigger -----------

TEST_CASE("keyassigner: coupling - last-note priority only ever comes with retrigger",
          "[keyassigner]") {
    // The only mode that gives last-note priority is GateTrig, and it ALWAYS
    // retriggers a just-pressed key. There is no mode/flag combination that
    // selects a just-pressed (non-lowest) key without a retrigger.
    KeyAssigner ka;

    for (auto mode : {GateTrigMode::Gate, GateTrigMode::GateTrig, GateTrigMode::Lfo}) {
        ka.prepare();
        ka.setMode(mode);
        ka.noteOn(60);
        (void)ka.resolve();

        // Press a HIGHER key (a non-lowest just-pressed key).
        ka.noteOn(72);
        const NoteDecision d = ka.resolve();

        const bool lastNoteSelected = (d.activeNote == 72);  // the just-pressed, non-lowest key
        if (lastNoteSelected) {
            // last-note priority was applied -> a retrigger MUST accompany it.
            REQUIRE(d.retrigger);
        }
        // Gate and Lfo are lowest-note: they select 60, never the just-pressed 72.
        if (mode == GateTrigMode::Gate || mode == GateTrigMode::Lfo) {
            REQUIRE(d.activeNote == 60);
            REQUIRE_FALSE(lastNoteSelected);
        }
    }
}

// --- reset()/prepare() clear all state ---------------------------------------

TEST_CASE("keyassigner: reset clears all held state and prior scan", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    ka.noteOn(64);
    (void)ka.resolve();
    REQUIRE(ka.anyHeld());

    ka.reset();
    REQUIRE_FALSE(ka.anyHeld());

    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == -1);
    REQUIRE_FALSE(d.gate);
    REQUIRE_FALSE(d.retrigger);
    REQUIRE_FALSE(d.clockReset);

    // A fresh key after reset behaves like a first key (retrigger, no stale prevScan ghost).
    ka.noteOn(48);
    const NoteDecision d2 = ka.resolve();
    REQUIRE(d2.activeNote == 48);
    REQUIRE(d2.gate);
    REQUIRE(d2.retrigger);
}

// --- snapshot semantics: prevScan_ = held_ after each resolve ----------------

TEST_CASE("keyassigner: a down resolved last tick does not retrigger again this tick", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    REQUIRE(ka.resolve().retrigger);   // first scan: 60 is a changed-down
    REQUIRE_FALSE(ka.resolve().retrigger);  // second scan: 60 already in prevScan, no new down
    REQUIRE_FALSE(ka.resolve().retrigger);
}

// --- bounds: out-of-range notes are ignored, never UB ------------------------

TEST_CASE("keyassigner: out-of-range MIDI notes are ignored safely", "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::Gate);

    ka.noteOn(-1);
    ka.noteOn(128);
    ka.noteOn(1000);
    REQUIRE_FALSE(ka.anyHeld());

    ka.noteOn(0);
    ka.noteOn(127);
    REQUIRE(ka.anyHeld());
    NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 0);  // lowest valid held

    ka.noteOff(200);  // no-op, must not corrupt state
    d = ka.resolve();
    REQUIRE(d.activeNote == 0);

    ka.noteOff(0);
    d = ka.resolve();
    REQUIRE(d.activeNote == 127);
}

// --- mode is the ONLY priority/trigger control (no separate knobs) -----------

TEST_CASE("keyassigner: GateTrig retrigger and last-note are inseparable across a sequence",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    // Build a held chord, one note per tick, each must retrigger AND take priority.
    const int seq[] = {60, 64, 62, 67, 59};
    int expectedActive = -1;
    for (int n : seq) {
        ka.noteOn(n);
        const NoteDecision d = ka.resolve();
        REQUIRE(d.activeNote == n);  // last-note priority: the just-pressed key
        REQUIRE(d.retrigger);        // coupled: every new key retriggers
        expectedActive = n;
    }

    // Releasing a NON-active held key does not change the active note and does not retrigger.
    ka.noteOff(62);
    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == expectedActive);
    REQUIRE_FALSE(d.retrigger);
}

// --- GateTrig last-note fallback when the active key is released -------------

TEST_CASE("keyassigner: GateTrig releasing the active key falls back to a still-held key, no retrigger",
          "[keyassigner]") {
    KeyAssigner ka;
    ka.prepare();
    ka.setMode(GateTrigMode::GateTrig);

    ka.noteOn(60);
    (void)ka.resolve();
    ka.noteOn(72);  // last-note -> active 72
    REQUIRE(ka.resolve().activeNote == 72);

    // Release the active key (72) while 60 still held; no new down -> fall back to still-held 60.
    ka.noteOff(72);
    const NoteDecision d = ka.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);  // a release never retriggers
}
