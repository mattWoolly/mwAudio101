// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/KeyAssignerReferenceTest.cpp — self-check tests for the independent
// disassembly-semantics golden reference (task 152, voice-control-4). Names begin with
// "keyassignerref" so `-R keyassignerref` selects them under the silent-pass rule;
// the display strings avoid '[' so Catch2 does not parse them as tags.
//
// These tests assert the reference's OWN invariants against the §5.3/§5.4 firmware
// contract of docs/design/04-voice-and-control.md (the NORMATIVE K1-K7 table) and
// research/07 §3.2-§3.3 / §5.2. They are the acceptance checkboxes for task 152:
//   * lowest-note (GATE/LFO) and last-note + XOR-changed-down (GATE+TRIG) priority;
//   * the legato / overlap / release-order battery (K1-K6) across all three modes;
//   * batched multiple-downs-within-one-tick resolution (§5.4 K4);
//   * the reference is built WITHOUT including the production KeyAssigner (it is the
//     oracle, not a wrapper) [task 152 Acceptance; ADR-006 C19].
//
// IMPORTANT INDEPENDENCE GUARD: this TU includes ONLY the reference header and the
// shared VoiceTypes PODs. It deliberately does NOT include "voice/KeyAssigner.h" — the
// reference must stand alone as the conformance oracle.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "../golden/KeyAssignerReference.h"
#include "../../core/voice/VoiceTypes.h"

using mw::GateTrigMode;
using mw::NoteDecision;
using Ref   = mw::golden::KeyAssignerReference;
using Event = mw::golden::KeyAssignerReference::Event;
using Tick  = mw::golden::KeyAssignerReference::TickEvents;

namespace {

Event on(int n)  { return Event{Event::Kind::On, n}; }
Event off(int n) { return Event{Event::Kind::Off, n}; }

}  // namespace

// --- RT-shape / noexcept surface (matches the production seam) ----------------------

TEST_CASE("keyassignerref: hot-path methods are noexcept", "[keyassignerref]") {
    Ref ref;
    STATIC_REQUIRE(noexcept(ref.prepare()));
    STATIC_REQUIRE(noexcept(ref.reset()));
    STATIC_REQUIRE(noexcept(ref.setMode(GateTrigMode::Gate)));
    STATIC_REQUIRE(noexcept(ref.noteOn(60)));
    STATIC_REQUIRE(noexcept(ref.noteOff(60)));
    STATIC_REQUIRE(noexcept(ref.resolve()));
    STATIC_REQUIRE(noexcept(ref.anyHeld()));
}

// --- baseline: no keys held -> gate off, no note -----------------------------------

TEST_CASE("keyassignerref: no keys held resolves to gate off and no note", "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    REQUIRE_FALSE(ref.anyHeld());

    const NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == -1);
    REQUIRE_FALSE(d.gate);
    REQUIRE_FALSE(d.retrigger);
    REQUIRE_FALSE(d.clockReset);
}

// --- K1: GATE, new (higher then lower) key while held -> lowest wins, no retrigger ---

TEST_CASE("keyassignerref: K1 Gate new key while held keeps gate and selects lowest with no retrigger",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::Gate);

    ref.noteOn(60);
    NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);  // first key from silence DOES fire the ADSR (gate edge)

    // Higher key while 60 held: lowest (60) still wins, gate held, NO retrigger.
    ref.noteOn(67);
    d = ref.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);

    // Lower key while held: it becomes the new lowest, still no retrigger in GATE.
    ref.noteOn(55);
    d = ref.resolve();
    REQUIRE(d.activeNote == 55);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);
}

// --- K2: GATE, release of current lowest while others held -> next-lowest -----------

TEST_CASE("keyassignerref: K2 Gate release of lowest selects next-lowest with gate held",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::Gate);

    ref.noteOn(60);
    ref.noteOn(64);
    ref.noteOn(67);
    NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 60);

    ref.noteOff(60);
    d = ref.resolve();
    REQUIRE(d.activeNote == 64);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);

    ref.noteOff(64);
    d = ref.resolve();
    REQUIRE(d.activeNote == 67);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);
}

// --- K3: GATE+TRIG, new key -> last-note priority + retrigger -----------------------

TEST_CASE("keyassignerref: K3 GateTrig new key takes last-note priority and retriggers",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::GateTrig);

    ref.noteOn(60);
    NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.retrigger);

    // Higher key while held: last-note priority -> new key (67) wins, retrigger.
    ref.noteOn(67);
    d = ref.resolve();
    REQUIRE(d.activeNote == 67);
    REQUIRE(d.retrigger);

    // Lower key while held: the just-pressed key still wins (last-note), retrigger.
    ref.noteOn(55);
    d = ref.resolve();
    REQUIRE(d.activeNote == 55);
    REQUIRE(d.retrigger);
}

// --- K4: GATE+TRIG, multiple downs in ONE control tick -> lowest of just-pressed -----

TEST_CASE("keyassignerref: K4 GateTrig multiple downs in one tick pick lowest just-pressed with one retrigger",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::GateTrig);

    // Three keys arrive within ONE tick (no resolve between them).
    ref.noteOn(67);
    ref.noteOn(60);
    ref.noteOn(64);
    NoteDecision d = ref.resolve();
    // XOR changed-down = {60,64,67}; lowest of the just-pressed = 60.
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE(d.retrigger);  // exactly one retrigger for the whole batch

    // Next tick, no new down: no retrigger; the most-recent still-held stays selected.
    d = ref.resolve();
    REQUIRE_FALSE(d.retrigger);
    REQUIRE(d.gate);
}

TEST_CASE("keyassignerref: K4 a key still held next tick is NOT a new down (no false retrigger)",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::GateTrig);

    ref.noteOn(60);
    REQUIRE(ref.resolve().retrigger);

    ref.noteOn(62);
    NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 62);
    REQUIRE(d.retrigger);

    // No new event: both 60 and 62 are in the prior scan -> no changed-down -> no retrigger.
    d = ref.resolve();
    REQUIRE_FALSE(d.retrigger);
    REQUIRE(d.activeNote == 62);  // most-recent still-held stays
}

// --- K5: LFO uses lowest-note pitch but never key-retriggers -----------------------

TEST_CASE("keyassignerref: K5 Lfo uses lowest-note pitch and never retriggers from the key",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::Lfo);

    ref.noteOn(60);
    NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);  // ADSR is clock-driven, NOT key-driven in LFO

    ref.noteOn(72);
    d = ref.resolve();
    REQUIRE(d.activeNote == 60);  // lowest-note pitch
    REQUIRE_FALSE(d.retrigger);

    ref.noteOn(48);
    d = ref.resolve();
    REQUIRE(d.activeNote == 48);  // new lowest
    REQUIRE_FALSE(d.retrigger);
}

// --- K6: LFO, new keypress -> clockReset; not in Gate/GateTrig ----------------------

TEST_CASE("keyassignerref: K6 Lfo new keypress asserts clockReset and clears when no new key",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::Lfo);

    ref.noteOn(60);
    REQUIRE(ref.resolve().clockReset);

    // No new key this tick -> clockReset clears.
    REQUIRE_FALSE(ref.resolve().clockReset);

    // New keypress while a note is held -> clockReset asserts again.
    ref.noteOn(67);
    REQUIRE(ref.resolve().clockReset);
}

TEST_CASE("keyassignerref: clockReset is never asserted in Gate or GateTrig", "[keyassignerref]") {
    Ref ref;

    ref.prepare();
    ref.setMode(GateTrigMode::Gate);
    ref.noteOn(60);
    REQUIRE_FALSE(ref.resolve().clockReset);

    ref.prepare();
    ref.setMode(GateTrigMode::GateTrig);
    ref.noteOn(64);
    REQUIRE_FALSE(ref.resolve().clockReset);
}

// --- K7: all keys released -> gate de-asserts (all modes) ---------------------------

TEST_CASE("keyassignerref: K7 all keys released de-asserts the gate in every mode",
          "[keyassignerref]") {
    for (auto m : {GateTrigMode::Gate, GateTrigMode::GateTrig, GateTrigMode::Lfo}) {
        Ref ref;
        ref.prepare();
        ref.setMode(m);

        ref.noteOn(60);
        ref.noteOn(64);
        REQUIRE(ref.resolve().gate);

        ref.noteOff(60);
        REQUIRE(ref.resolve().gate);  // 64 still held

        ref.noteOff(64);
        const NoteDecision d = ref.resolve();
        REQUIRE_FALSE(d.gate);
        REQUIRE(d.activeNote == -1);
        REQUIRE_FALSE(d.retrigger);
        REQUIRE_FALSE(ref.anyHeld());
    }
}

// --- Coupling: last-note priority is never available without retrigger --------------

TEST_CASE("keyassignerref: coupling - last-note priority only ever comes with retrigger",
          "[keyassignerref]") {
    for (auto m : {GateTrigMode::Gate, GateTrigMode::GateTrig, GateTrigMode::Lfo}) {
        Ref ref;
        ref.prepare();
        ref.setMode(m);

        ref.noteOn(60);
        (void)ref.resolve();

        ref.noteOn(72);  // a non-lowest just-pressed key
        const NoteDecision d = ref.resolve();

        const bool lastNoteSelected = (d.activeNote == 72);
        if (lastNoteSelected) {
            REQUIRE(d.retrigger);  // the just-pressed non-lowest key always retriggers
        }
        if (m == GateTrigMode::Gate || m == GateTrigMode::Lfo) {
            REQUIRE(d.activeNote == 60);  // lowest-note modes never pick the just-pressed 72
            REQUIRE_FALSE(lastNoteSelected);
        }
    }
}

// --- GateTrig fallback: releasing the active key reverts to a still-held key --------

TEST_CASE("keyassignerref: GateTrig releasing the active key falls back to a still-held key without retrigger",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::GateTrig);

    ref.noteOn(60);
    (void)ref.resolve();
    ref.noteOn(72);
    REQUIRE(ref.resolve().activeNote == 72);

    // Release the active key (72), 60 still held, no new down -> fall back to 60.
    ref.noteOff(72);
    const NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 60);
    REQUIRE(d.gate);
    REQUIRE_FALSE(d.retrigger);
}

// --- reset()/prepare() clear all held + prior-scan state ----------------------------

TEST_CASE("keyassignerref: reset clears held and prior-scan so a fresh key behaves like a first key",
          "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::GateTrig);

    ref.noteOn(60);
    ref.noteOn(64);
    (void)ref.resolve();
    REQUIRE(ref.anyHeld());

    ref.reset();
    REQUIRE_FALSE(ref.anyHeld());

    const NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == -1);
    REQUIRE_FALSE(d.gate);

    ref.noteOn(48);
    const NoteDecision d2 = ref.resolve();
    REQUIRE(d2.activeNote == 48);
    REQUIRE(d2.gate);
    REQUIRE(d2.retrigger);  // no stale prevScan ghost from before the reset
}

// --- out-of-range MIDI notes are ignored safely (no UB) -----------------------------

TEST_CASE("keyassignerref: out-of-range MIDI notes are ignored safely", "[keyassignerref]") {
    Ref ref;
    ref.prepare();
    ref.setMode(GateTrigMode::Gate);

    ref.noteOn(-1);
    ref.noteOn(128);
    ref.noteOn(100000);
    REQUIRE_FALSE(ref.anyHeld());

    ref.noteOn(0);
    ref.noteOn(127);
    REQUIRE(ref.anyHeld());
    NoteDecision d = ref.resolve();
    REQUIRE(d.activeNote == 0);  // lowest valid held

    ref.noteOff(9999);  // no-op, must not corrupt state
    d = ref.resolve();
    REQUIRE(d.activeNote == 0);

    ref.noteOff(0);
    d = ref.resolve();
    REQUIRE(d.activeNote == 127);
}

// --- batched driver (§5.4 K4 multi-down-per-tick + release-order battery) ------------
//
// runScript() is the shape the conformance driver (voice-control-5) consumes: a vector
// of per-tick event batches -> a per-tick decision trace, pure in (mode, script). These
// exercise the legato / overlap / release-order battery as whole scripts.

TEST_CASE("keyassignerref: batched driver resolves a Gate legato/overlap/release battery",
          "[keyassignerref]") {
    Ref ref;
    const std::vector<Tick> script = {
        {on(60)},            // tick 0: 60 down            -> 60, gate, retrigger
        {on(67)},            // tick 1: 67 overlaps        -> 60 (lowest), no retrigger
        {on(55), on(62)},    // tick 2: two more overlap   -> 55 (lowest), no retrigger
        {off(55)},           // tick 3: release lowest     -> 60, no retrigger
        {off(60), off(62)},  // tick 4: release two        -> 67, no retrigger
        {off(67)},           // tick 5: release last       -> -1, gate off
    };
    const auto trace = ref.runScript(GateTrigMode::Gate, script);
    REQUIRE(trace.size() == script.size());

    REQUIRE(trace[0].activeNote == 60);  REQUIRE(trace[0].gate);  REQUIRE(trace[0].retrigger);
    REQUIRE(trace[1].activeNote == 60);  REQUIRE(trace[1].gate);  REQUIRE_FALSE(trace[1].retrigger);
    REQUIRE(trace[2].activeNote == 55);  REQUIRE(trace[2].gate);  REQUIRE_FALSE(trace[2].retrigger);
    REQUIRE(trace[3].activeNote == 60);  REQUIRE(trace[3].gate);  REQUIRE_FALSE(trace[3].retrigger);
    REQUIRE(trace[4].activeNote == 67);  REQUIRE(trace[4].gate);  REQUIRE_FALSE(trace[4].retrigger);
    REQUIRE(trace[5].activeNote == -1);  REQUIRE_FALSE(trace[5].gate);
}

TEST_CASE("keyassignerref: batched driver resolves a GateTrig last-note battery with XOR multi-down",
          "[keyassignerref]") {
    Ref ref;
    const std::vector<Tick> script = {
        {on(60)},                    // tick 0: 60          -> 60, retrigger
        {on(64), on(62), on(67)},    // tick 1: 3-down XOR  -> 62 (lowest just-pressed), one retrigger
        {},                          // tick 2: no event    -> stay 67 most-recent? see note below
        {off(67)},                   // tick 3: release one not active -> stays, no retrigger
        {on(59)},                    // tick 4: new low key  -> 59 (last-note), retrigger
    };
    const auto trace = ref.runScript(GateTrigMode::GateTrig, script);
    REQUIRE(trace.size() == script.size());

    REQUIRE(trace[0].activeNote == 60);  REQUIRE(trace[0].retrigger);
    // tick 1: changed-down = {62,64,67}; lowest just-pressed = 62; exactly one retrigger.
    REQUIRE(trace[1].activeNote == 62);  REQUIRE(trace[1].retrigger);
    // tick 2: no new down -> keep the most-recently-pressed still-held key. The last
    // resolve set lastActive_ = 62 and 62 is still held, so it stays 62, no retrigger.
    REQUIRE(trace[2].activeNote == 62);  REQUIRE_FALSE(trace[2].retrigger);
    // tick 3: releasing 67 (not the active note) leaves 62 active, no retrigger.
    REQUIRE(trace[3].activeNote == 62);  REQUIRE_FALSE(trace[3].retrigger);
    // tick 4: a brand-new key takes last-note priority and retriggers.
    REQUIRE(trace[4].activeNote == 59);  REQUIRE(trace[4].retrigger);
}

TEST_CASE("keyassignerref: batched driver Lfo trace - lowest pitch, no key retrigger, clockReset on each new key",
          "[keyassignerref]") {
    Ref ref;
    const std::vector<Tick> script = {
        {on(60)},          // 60 down: lowest 60, no retrigger, clockReset
        {on(72)},          // overlap higher: lowest still 60, clockReset (new key)
        {},                // idle tick: no clockReset
        {on(48)},          // new lower: lowest 48, clockReset
        {off(48), off(60), off(72)},  // release all: gate off
    };
    const auto trace = ref.runScript(GateTrigMode::Lfo, script);
    REQUIRE(trace.size() == script.size());

    REQUIRE(trace[0].activeNote == 60);  REQUIRE_FALSE(trace[0].retrigger);  REQUIRE(trace[0].clockReset);
    REQUIRE(trace[1].activeNote == 60);  REQUIRE_FALSE(trace[1].retrigger);  REQUIRE(trace[1].clockReset);
    REQUIRE_FALSE(trace[2].clockReset);  REQUIRE_FALSE(trace[2].retrigger);
    REQUIRE(trace[3].activeNote == 48);  REQUIRE_FALSE(trace[3].retrigger);  REQUIRE(trace[3].clockReset);
    REQUIRE_FALSE(trace[4].gate);
}

// --- determinism: the script trace is a pure function of (mode, script) -------------

TEST_CASE("keyassignerref: runScript is deterministic - identical inputs yield identical traces",
          "[keyassignerref]") {
    const std::vector<Tick> script = {
        {on(64), on(60)}, {on(67)}, {off(60)}, {}, {off(64), off(67)},
    };
    Ref a;
    Ref b;
    const auto ta = a.runScript(GateTrigMode::GateTrig, script);
    const auto tb = b.runScript(GateTrigMode::GateTrig, script);
    REQUIRE(ta.size() == tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) {
        REQUIRE(ta[i].activeNote == tb[i].activeNote);
        REQUIRE(ta[i].gate       == tb[i].gate);
        REQUIRE(ta[i].retrigger  == tb[i].retrigger);
        REQUIRE(ta[i].clockReset == tb[i].clockReset);
    }
}
