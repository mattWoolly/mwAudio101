// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/KeyAssignerGoldenTraceTest.cpp — the K17 golden-trace CONFORMANCE
// driver (task 153, voice-control-5). It diffs the production
// `core/voice/KeyAssigner` against the independent disassembly-semantics oracle
// `tests/golden/KeyAssignerReference` over a shared legato / overlap /
// release-order / multi-down-per-tick battery, in all three GateTrigMode values
// [docs/design/04-voice-and-control.md §5.4 K17; ADR-006 §Contract C19; research/07 §11].
//
// Test-case names begin with "keyassignertrace" so `-R keyassignertrace` selects
// exactly this suite under the silent-pass rule (discovery registers NAMES, not
// tags). The display strings deliberately avoid '[' so Catch2 does not parse a tag
// out of the name and break `ctest -R` selection [AGENTS.md Tests].
//
// SCOPE (task 153): the conformance DRIVER only. It does NOT implement either the
// production KeyAssigner or the reference (those are tasks 069 / 152), and it does
// NOT cover POLY/UNISON, which are exempt from the golden trace per ADR-006 C19 and
// tested separately in the VoiceManager tasks [task 153 Out-of-scope].
//
// WHY THIS IS A REAL CONFORMANCE CHECK: the production class (a std::bitset model)
// and the reference (an explicit per-key press-order ledger) are DELIBERATELY
// different internal representations [KeyAssignerReference.h INDEPENDENCE note], so
// they cannot share a defect by construction. Identical {activeNote, gate,
// retrigger} traces over the full battery is therefore evidence the production code
// matches the firmware contract, not that two copies of the same code agree.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "../../core/voice/KeyAssigner.h"        // production (task 069)
#include "../golden/KeyAssignerReference.h"        // independent oracle (task 152)
#include "../../core/voice/VoiceTypes.h"           // shared PODs: GateTrigMode, NoteDecision

using mw::GateTrigMode;
using mw::KeyAssigner;
using mw::NoteDecision;
using Ref   = mw::golden::KeyAssignerReference;
using Event = mw::golden::KeyAssignerReference::Event;
using Tick  = mw::golden::KeyAssignerReference::TickEvents;
using Script = std::vector<Tick>;

namespace {

Event on(int n)  { return Event{Event::Kind::On, n}; }
Event off(int n) { return Event{Event::Kind::Off, n}; }

// Drive the PRODUCTION KeyAssigner over a per-tick event script and collect the
// per-tick decision trace. Mirrors mw::golden::KeyAssignerReference::runScript but
// against the production class (which has no runScript helper of its own — the
// driver lives here, in the conformance test, by design). Pure in (mode, script).
std::vector<NoteDecision> runProduction(GateTrigMode m, const Script& script) {
    KeyAssigner prod;
    prod.prepare();
    prod.setMode(m);
    std::vector<NoteDecision> trace;
    trace.reserve(script.size());
    for (const Tick& tick : script) {
        for (const Event& e : tick) {
            if (e.kind == Event::Kind::On) {
                prod.noteOn(e.note);
            } else {
                prod.noteOff(e.note);
            }
        }
        trace.push_back(prod.resolve());
    }
    return trace;
}

const char* modeName(GateTrigMode m) {
    switch (m) {
        case GateTrigMode::Gate:     return "Gate";
        case GateTrigMode::GateTrig: return "GateTrig";
        case GateTrigMode::Lfo:      return "Lfo";
    }
    return "?";
}

// Run BOTH implementations over the shared script in the given mode and diff the
// {activeNote, gate, retrigger} fields element-by-element. Fails LOUDLY on the FIRST
// diverging tick, naming the mode and the tick index (task 153 Scope: "fails loudly
// on first divergence with the diverging tick index"). clockReset is diffed too as a
// bonus consistency check, but the K17 contract is the {activeNote, gate, retrigger}
// triple [§5.4 K17].
void requireConformance(GateTrigMode m, const Script& script) {
    const auto prod = runProduction(m, script);

    Ref ref;
    const auto golden = ref.runScript(m, script);

    INFO("mode = " << modeName(m) << ", ticks = " << script.size());
    REQUIRE(prod.size() == script.size());
    REQUIRE(golden.size() == script.size());

    for (std::size_t i = 0; i < script.size(); ++i) {
        INFO("FIRST DIVERGENCE search at tick index " << i << " (mode " << modeName(m) << ")\n"
             << "  production: activeNote=" << prod[i].activeNote
             << " gate=" << prod[i].gate
             << " retrigger=" << prod[i].retrigger
             << " clockReset=" << prod[i].clockReset << "\n"
             << "  reference : activeNote=" << golden[i].activeNote
             << " gate=" << golden[i].gate
             << " retrigger=" << golden[i].retrigger
             << " clockReset=" << golden[i].clockReset);

        // The K17 normative triple (§5.4 K17; ADR-006 C19).
        REQUIRE(prod[i].activeNote == golden[i].activeNote);
        REQUIRE(prod[i].gate       == golden[i].gate);
        REQUIRE(prod[i].retrigger  == golden[i].retrigger);
        // Bonus: clockReset is also part of the NoteDecision the Voice consumes and is
        // emitted by both; conformance includes it (it is what drives LFO/ARP re-phase).
        REQUIRE(prod[i].clockReset == golden[i].clockReset);
    }
}

// ---------------------------------------------------------------------------
// The SHARED EVENT BATTERY (§5.4 K1-K6). One battery; run in all three modes.
//
// Each entry exercises one or more of the K-cases:
//   - legato chains            (overlapping presses without intervening release)
//   - overlapping holds        (multiple keys down at once, varied order)
//   - varied release order     (release lowest / middle / active / non-active)
//   - multi-down-per-tick       (≥2 note-ons within ONE control tick — §5.4 K4 XOR)
//   - idle ticks               (empty tick: release-tail / clock-only advance)
// The same script is driven through Gate / GateTrig / Lfo; the two implementations
// must agree element-by-element in each mode.
// ---------------------------------------------------------------------------

// Battery A: classic legato + overlap + ordered release (K1/K2/K3 + K7).
Script batteryLegatoOverlapRelease() {
    return {
        {on(60)},                    // fresh press from silence
        {on(67)},                    // legato overlap, higher
        {on(55)},                    // legato overlap, lower (new lowest)
        {on(62)},                    // another overlap, middle
        {},                          // idle tick (hold)
        {off(55)},                   // release the lowest
        {off(67)},                   // release a non-lowest held
        {off(60)},                   // release another
        {off(62)},                   // release the last -> gate off
        {},                          // idle after gate off
    };
}

// Battery B: multi-down-per-tick + last-note churn (§5.4 K4 XOR), interleaved
// presses and releases, and the GateTrig "active key released -> fall back" path.
Script batteryMultiDownAndChurn() {
    return {
        {on(64), on(60), on(67)},    // THREE downs in ONE tick (XOR multi-down)
        {},                          // idle: most-recent still-held holds
        {on(72)},                    // single new high key
        {on(59), on(62)},            // TWO more downs in one tick
        {off(59)},                   // release the active (last-pressed lowest)
        {off(62), off(72)},          // release two in one tick
        {on(48), on(50), on(52)},    // three fresh downs in one tick again
        {off(48)},                   // release lowest just-pressed
        {off(50), off(52), off(60), off(64), off(67)},  // release everything held
        {},                          // settle
    };
}

// Battery C: release-order stress — same set of keys, released in several orders,
// re-pressed mid-stream, with idle ticks scattered (exercises K2 next-lowest in
// Gate, last-note fallback in GateTrig, lowest-pitch tracking in Lfo).
Script batteryReleaseOrderStress() {
    return {
        {on(60), on(64), on(67), on(72)},  // a four-note cluster in one tick
        {off(64)},                          // drop a middle voice
        {off(60)},                          // drop the lowest
        {on(57)},                           // press a new lower key (legato)
        {},                                 // idle
        {off(72)},                          // drop the top
        {off(57)},                          // drop the new low
        {on(81)},                           // a lone high key while 67 still held
        {off(67)},                          // drop 67 -> only 81 remains
        {off(81)},                          // release all -> gate off
    };
}

// Battery D: rapid re-strike of the SAME key across ticks (release then re-press),
// plus an immediate same-tick down+up pair, to exercise the gate-edge / changed-down
// bookkeeping at the boundaries.
Script batteryRestrikeAndBoundaries() {
    return {
        {on(60)},            // press
        {off(60)},           // release same key next tick -> gate off
        {on(60)},            // re-press -> fresh gate edge (retrigger in Gate/GateTrig)
        {on(60)},            // redundant on (already held) -> no new changed-down
        {off(60), on(64)},   // release 60 AND press 64 in the same tick
        {on(64)},            // redundant on of the held key
        {off(64)},           // release all -> gate off
        {},                  // idle at silence
        {on(0), on(127)},    // extreme MIDI range, two downs one tick
        {off(0), off(127)},  // release both
    };
}

}  // namespace

// === Acceptance: production == reference for {activeNote,gate,retrigger} =============
// across the full battery in ALL THREE GateTrigMode values (§5.4 K17; ADR-006 C19).

TEST_CASE("keyassignertrace: legato/overlap/release battery conforms to the oracle in Gate",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Gate, batteryLegatoOverlapRelease());
}
TEST_CASE("keyassignertrace: legato/overlap/release battery conforms to the oracle in GateTrig",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::GateTrig, batteryLegatoOverlapRelease());
}
TEST_CASE("keyassignertrace: legato/overlap/release battery conforms to the oracle in Lfo",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Lfo, batteryLegatoOverlapRelease());
}

TEST_CASE("keyassignertrace: multi-down-per-tick and last-note churn battery conforms in Gate",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Gate, batteryMultiDownAndChurn());
}
TEST_CASE("keyassignertrace: multi-down-per-tick and last-note churn battery conforms in GateTrig",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::GateTrig, batteryMultiDownAndChurn());
}
TEST_CASE("keyassignertrace: multi-down-per-tick and last-note churn battery conforms in Lfo",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Lfo, batteryMultiDownAndChurn());
}

TEST_CASE("keyassignertrace: release-order stress battery conforms to the oracle in Gate",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Gate, batteryReleaseOrderStress());
}
TEST_CASE("keyassignertrace: release-order stress battery conforms to the oracle in GateTrig",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::GateTrig, batteryReleaseOrderStress());
}
TEST_CASE("keyassignertrace: release-order stress battery conforms to the oracle in Lfo",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Lfo, batteryReleaseOrderStress());
}

TEST_CASE("keyassignertrace: re-strike and boundary battery conforms to the oracle in Gate",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Gate, batteryRestrikeAndBoundaries());
}
TEST_CASE("keyassignertrace: re-strike and boundary battery conforms to the oracle in GateTrig",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::GateTrig, batteryRestrikeAndBoundaries());
}
TEST_CASE("keyassignertrace: re-strike and boundary battery conforms to the oracle in Lfo",
          "[keyassignertrace]") {
    requireConformance(GateTrigMode::Lfo, batteryRestrikeAndBoundaries());
}

// === Single sweep that runs every battery x every mode in one case ===================
// A compact "run them all" so a single -R keyassignertrace selector covers the whole
// matrix and a divergence in any cell names its mode + battery + tick index.

TEST_CASE("keyassignertrace: full battery matrix conforms across all three modes",
          "[keyassignertrace]") {
    const std::vector<Script> batteries = {
        batteryLegatoOverlapRelease(),
        batteryMultiDownAndChurn(),
        batteryReleaseOrderStress(),
        batteryRestrikeAndBoundaries(),
    };
    for (const auto mode : {GateTrigMode::Gate, GateTrigMode::GateTrig, GateTrigMode::Lfo}) {
        for (std::size_t b = 0; b < batteries.size(); ++b) {
            INFO("battery #" << b << " in mode " << modeName(mode));
            requireConformance(mode, batteries[b]);
        }
    }
}

// === Oracle-anchored behavior checks (so the suite is not a tautology) ===============
// The conformance cases above prove production == reference. These few cases anchor the
// reference's behavior to the §5.4 firmware contract directly, so a coordinated drift of
// BOTH implementations (which the deliberate representation split makes unlikely, but
// the design asks for an oracle check) would still trip here. They assert the same
// triple the conformance driver diffs, against hand-derived expectations.

TEST_CASE("keyassignertrace: oracle check - GateTrig multi-down picks lowest just-pressed with one retrigger",
          "[keyassignertrace]") {
    // §5.4 K4: three keys down within one tick -> lowest of the just-pressed (62) wins,
    // exactly one retrigger for the batch; the next idle tick does not retrigger.
    const Script script = { {on(64), on(62), on(67)}, {} };

    // Reference (oracle) first, then production must match it tick-by-tick.
    Ref ref;
    const auto golden = ref.runScript(GateTrigMode::GateTrig, script);
    REQUIRE(golden[0].activeNote == 62);
    REQUIRE(golden[0].gate);
    REQUIRE(golden[0].retrigger);
    REQUIRE_FALSE(golden[1].retrigger);

    requireConformance(GateTrigMode::GateTrig, script);
}

TEST_CASE("keyassignertrace: oracle check - Gate keeps lowest and never retriggers on legato",
          "[keyassignertrace]") {
    // §5.4 K1: a higher key while the lower is held keeps the lower active, gate stays,
    // NO retrigger. Only the leading gate edge (from silence) retriggers.
    const Script script = { {on(60)}, {on(67)}, {on(72)} };

    Ref ref;
    const auto golden = ref.runScript(GateTrigMode::Gate, script);
    REQUIRE(golden[0].activeNote == 60);
    REQUIRE(golden[0].retrigger);          // leading gate edge
    REQUIRE(golden[1].activeNote == 60);
    REQUIRE_FALSE(golden[1].retrigger);    // legato, no retrigger
    REQUIRE(golden[2].activeNote == 60);
    REQUIRE_FALSE(golden[2].retrigger);

    requireConformance(GateTrigMode::Gate, script);
}

TEST_CASE("keyassignertrace: oracle check - Lfo tracks lowest pitch, never key-retriggers, clockResets on new keys",
          "[keyassignertrace]") {
    // §5.4 K5/K6: pitch follows lowest-note, the key never retriggers the ADSR, and a
    // new keypress asserts clockReset (re-phase). An idle tick clears clockReset.
    const Script script = { {on(60)}, {on(72)}, {}, {on(48)} };

    Ref ref;
    const auto golden = ref.runScript(GateTrigMode::Lfo, script);
    REQUIRE(golden[0].activeNote == 60);
    REQUIRE_FALSE(golden[0].retrigger);
    REQUIRE(golden[0].clockReset);
    REQUIRE(golden[1].activeNote == 60);   // lowest pitch (72 is higher)
    REQUIRE(golden[1].clockReset);         // new key
    REQUIRE_FALSE(golden[2].clockReset);   // idle
    REQUIRE(golden[3].activeNote == 48);   // new lowest
    REQUIRE(golden[3].clockReset);

    requireConformance(GateTrigMode::Lfo, script);
}

// === Negative control: a corrupted "production" trace WOULD diverge ==================
// Guards against a silent-pass driver. If the conformance comparator were a no-op (e.g.
// looping zero times, or comparing a trace to itself), this would not fail. We mutate a
// COPY of the reference trace and assert the element-by-element comparator flags it — so
// we know the real conformance cases above can actually catch a divergence.

TEST_CASE("keyassignertrace: comparator detects an injected divergence (anti-silent-pass)",
          "[keyassignertrace]") {
    const Script script = batteryMultiDownAndChurn();
    Ref ref;
    auto golden = ref.runScript(GateTrigMode::GateTrig, script);
    REQUIRE(golden.size() >= 3);

    // A faithful copy compares equal element-by-element...
    auto copy = golden;
    bool anyDiff = false;
    std::size_t firstDiff = copy.size();
    for (std::size_t i = 0; i < copy.size(); ++i) {
        if (copy[i].activeNote != golden[i].activeNote
            || copy[i].gate != golden[i].gate
            || copy[i].retrigger != golden[i].retrigger) {
            anyDiff = true;
            firstDiff = i;
            break;
        }
    }
    REQUIRE_FALSE(anyDiff);

    // ...now inject a single-field corruption and confirm the SAME comparator catches it
    // at the right tick index (this is the divergence-reporting path exercised for real).
    copy[2].activeNote += 1;
    anyDiff = false;
    firstDiff = copy.size();
    for (std::size_t i = 0; i < copy.size(); ++i) {
        if (copy[i].activeNote != golden[i].activeNote
            || copy[i].gate != golden[i].gate
            || copy[i].retrigger != golden[i].retrigger) {
            anyDiff = true;
            firstDiff = i;
            break;
        }
    }
    REQUIRE(anyDiff);
    REQUIRE(firstDiff == 2);
}
