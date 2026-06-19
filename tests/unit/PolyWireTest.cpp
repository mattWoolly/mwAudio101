// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for wiring the PolyAllocator (task 075) into the VoiceManager
// POLY drive path (task 075b) so VoiceMode::Poly actually allocates and SOUNDS voices
// through the assembled engine instead of the old early-return that rendered silence.
//
// Test-case display names begin with "polywire" so `ctest -R polywire --no-tests=error`
// selects exactly these under the silent-pass rule (AGENTS.md "Tests"); '[' is kept OUT
// of the display text (Catch2 parses it as a tag and it would break -R selection).
//
// Covers every acceptance criterion of
// plan/backlog/075b-wire-polyallocator-into-voicemanager.md against
// docs/design/04-voice-and-control.md §6.4 (POLY) / §6.5 (Determinism), ADR-006
// §Decision item 3 POLY / C12-C16, and ADR-019 VT-01/VT-02/VT-04:
//   - In VoiceMode::Poly, K simultaneous note-ons produce K sounding voices through the
//     VoiceManager render path; the (K+1)th triggers a deterministic steal (the
//     PolyAllocator §6.4 order) [§6.4; ADR-006 C12/C14].
//   - A note-off releases the matching voice; MONO/UNISON behavior is unchanged.
//   - RT invariants preserved: handleNoteEvent / controlTick / render are noexcept,
//     alloc-free and lock-free under the AudioThreadGuard [§8 RT3/RT6; ADR-001 C3/C4].
//
// ORACLE: the POLY note path is the documented PolyAllocator policy (§6.4). A standalone
// PolyAllocator bound to an identically-prepared pool, fed the identical note sequence,
// must pick the SAME steal victim the wired VoiceManager picks — proving the VoiceManager
// routes POLY through the allocator rather than re-implementing a divergent policy.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#include "voice/VoiceManager.h"
#include "voice/PolyAllocator.h"
#include "voice/Voice.h"
#include "voice/VoiceTypes.h"

#include "../invariants/AudioThreadGuard.h"

using mw::NoteEvent;
using mw::PolyAllocator;
using mw::Voice;
using mw::VoiceManager;
using mw::VoiceMode;
using mw::VoiceState;
using mw::GateTrigMode;

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr int           kOversample = 2;
constexpr std::uint32_t kSeed       = 0xC0FFEEu;

struct Stereo {
    std::array<float, 512> l{};
    std::array<float, 512> r{};
};

// Apply the latched mode/unison reconfig at a block boundary (top of render).
void boundary(VoiceManager& vm) {
    Stereo s;
    vm.render(s.l.data(), s.r.data(), 1);
}

NoteEvent noteOn(int n) { return NoteEvent{NoteEvent::Type::NoteOn, static_cast<std::uint8_t>(n), 1.0f, 0}; }
NoteEvent noteOff(int n) { return NoteEvent{NoteEvent::Type::NoteOff, static_cast<std::uint8_t>(n), 0.0f, 0}; }

// Count Active voices in the whole pool.
int activeVoices(const VoiceManager& vm) {
    int n = 0;
    for (int i = 0; i < mw::kMaxVoices; ++i)
        if (vm.voice(i).state() == VoiceState::Active) ++n;
    return n;
}

// Count voices currently sounding `note` (Active).
int activeSounding(const VoiceManager& vm, int note) {
    int n = 0;
    for (int i = 0; i < mw::kMaxVoices; ++i) {
        const Voice& v = vm.voice(i);
        if (v.state() == VoiceState::Active && v.currentNote() == note) ++n;
    }
    return n;
}

} // namespace

// --- §6.4 / K12: POLY note-ons allocate independent sounding voices ----------------

TEST_CASE("polywire: POLY mode sounds one voice per simultaneous note-on", "[polywire]") {
    // §6.4 / ADR-006 C12: in VoiceMode::Poly each note-on allocates its OWN voice (every
    // note a fresh trigger; no cross-voice lowest-note concept). K distinct note-ons must
    // produce K sounding voices through the VoiceManager — NOT the old POLY early-return
    // that made POLY silent (the wave-16 QA finding 076b).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);
    REQUIRE(vm.mode() == VoiceMode::Poly);

    const std::array<int, 4> notes{60, 64, 67, 72};
    for (int n : notes) vm.handleNoteEvent(noteOn(n));

    // K=4 note-ons -> 4 sounding voices, each on its own note.
    REQUIRE(activeVoices(vm) == 4);
    for (int n : notes) REQUIRE(activeSounding(vm, n) == 1);
}

TEST_CASE("polywire: POLY voices actually render non-silent audio through the manager", "[polywire]") {
    // The wave-16 finding: POLY rendered SILENCE because the allocator never ran. Wired,
    // the active POLY voices must accumulate non-zero audio through VoiceManager::render
    // (the same fixed-index-order render path MONO/UNISON use, ADR-019 VT-01/VT-02).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    vm.handleNoteEvent(noteOn(60));
    vm.handleNoteEvent(noteOn(67));
    REQUIRE(activeVoices(vm) == 2);

    // Render several blocks so the envelopes open and the oscillators produce signal.
    bool sawSignal = false;
    for (int b = 0; b < 16 && !sawSignal; ++b) {
        Stereo s;
        vm.render(s.l.data(), s.r.data(), 256);
        for (int i = 0; i < 256; ++i) {
            if (s.l[static_cast<std::size_t>(i)] != 0.0f
                || s.r[static_cast<std::size_t>(i)] != 0.0f) {
                sawSignal = true;
                break;
            }
        }
    }
    REQUIRE(sawSignal);
}

// --- §6.4 / K14: the (K+1)th note steals deterministically (allocator order) -------

TEST_CASE("polywire: POLY at capacity steals deterministically in PolyAllocator order", "[polywire]") {
    // §6.4 step 3 / ADR-006 C14: with no idle voice, the (K+1)th note-on steals a voice
    // following the PolyAllocator order (oldest-in-release -> quietest -> oldest-held,
    // tie-broken by ascending note-serial). ORACLE: a standalone PolyAllocator bound to a
    // pool prepared IDENTICALLY (same SR/oversample/seed => same per-voice drift seeds =>
    // same env levels) and fed the IDENTICAL note sequence must pick the SAME victim group
    // the wired VoiceManager picks. This proves the VM routes POLY through the allocator,
    // not a divergent re-implementation.
    //
    // We fill the full poly capacity (kMaxPoly groups at U=1), release one note so it is
    // the oldest-in-release victim, then allocate one more note that forces a steal, and
    // assert the same voice slot was reused in both the VM and the oracle allocator.

    // --- the wired VoiceManager POLY path -------------------------------------------
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    for (int g = 0; g < mw::kMaxPoly; ++g) vm.handleNoteEvent(noteOn(48 + g));
    REQUIRE(activeVoices(vm) == mw::kMaxPoly);   // full capacity, all idle slots taken

    // Release one note -> that voice enters Releasing (oldest-in-release steal target).
    vm.handleNoteEvent(noteOff(48 + 2));
    REQUIRE(vm.voice(2).state() == VoiceState::Releasing);

    // The (K+1)th note: no idle slot -> a deterministic steal of the releasing voice.
    const int stealNote = 96;
    vm.handleNoteEvent(noteOn(stealNote));

    // --- the standalone PolyAllocator oracle on an identically-prepared pool ---------
    auto pool = std::make_unique<PolyAllocator::Pool>();
    for (int i = 0; i < mw::kMaxVoices; ++i)
        (*pool)[static_cast<std::size_t>(i)].prepare(kSampleRate, kOversample, i, kSeed);
    std::uint64_t serial = 0;
    PolyAllocator oracle;
    oracle.configure(*pool, serial, /*maxPoly=*/mw::kMaxPoly, /*unison=*/1);

    for (int g = 0; g < mw::kMaxPoly; ++g) oracle.allocatePoly(48 + g);
    oracle.releasePoly(48 + 2);
    const int oracleVictimGroup = oracle.allocatePoly(stealNote);
    const int oracleVictimSlot  = oracle.groupBase(oracleVictimGroup);

    // The stolen slot in the oracle is the releasing slot (group/slot 2 at U=1).
    REQUIRE(oracleVictimSlot == 2);
    // The wired VM stole the SAME slot and re-triggered it on the new note.
    REQUIRE(vm.voice(oracleVictimSlot).state() == VoiceState::Active);
    REQUIRE(vm.voice(oracleVictimSlot).currentNote() == stealNote);
    // Capacity is still hard-capped (no extra voice spawned past the pool budget, C16).
    REQUIRE(activeSounding(vm, stealNote) == 1);
    REQUIRE(activeVoices(vm) <= mw::kMaxVoices);
}

TEST_CASE("polywire: POLY steal is deterministic across identical runs", "[polywire]") {
    // §6.5 / ADR-006 C18; ADR-019 VT-04: the SAME note sequence yields the SAME stolen
    // slot every run (no wall-clock, no nondeterministic ordering). Two independently
    // prepared VoiceManagers fed the identical sequence steal the identical slot.
    auto runStealSlot = []() {
        VoiceManager vm;
        vm.prepare(kSampleRate, kOversample, kSeed);
        vm.setMode(VoiceMode::Poly);
        boundary(vm);
        for (int g = 0; g < mw::kMaxPoly; ++g) vm.handleNoteEvent(noteOn(48 + g));
        vm.handleNoteEvent(noteOff(50));   // make one a releasing victim
        vm.handleNoteEvent(noteOn(96));    // force the steal
        // Return the slot now sounding the steal note.
        for (int i = 0; i < mw::kMaxVoices; ++i)
            if (vm.voice(i).state() == VoiceState::Active && vm.voice(i).currentNote() == 96)
                return i;
        return -1;
    };
    const int a = runStealSlot();
    const int b = runStealSlot();
    REQUIRE(a >= 0);
    REQUIRE(a == b);
}

// --- §6.4 / K13: re-strike of a held key reuses its voice (no doubling) ------------

TEST_CASE("polywire: POLY re-striking a held note reuses its voice with no doubling", "[polywire]") {
    // §6.4 step 2 / ADR-006 C13: a poly note-on that matches a still-held voice reuses
    // THAT voice rather than spawning a second one. After re-striking note 60, exactly one
    // voice sounds note 60.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    vm.handleNoteEvent(noteOn(60));
    REQUIRE(activeSounding(vm, 60) == 1);
    vm.handleNoteEvent(noteOn(60));   // re-strike
    REQUIRE(activeSounding(vm, 60) == 1);   // no doubling
    REQUIRE(activeVoices(vm) == 1);
}

// --- note-off releases the matching voice ------------------------------------------

TEST_CASE("polywire: POLY note-off releases the matching voice and leaves the others", "[polywire]") {
    // §6.4 / ADR-006 C12/C15: a note-off de-asserts ONLY the matching voice's gate (its
    // release tail finishes in place); other held voices keep sounding.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    vm.handleNoteEvent(noteOn(60));
    vm.handleNoteEvent(noteOn(64));
    vm.handleNoteEvent(noteOn(67));
    REQUIRE(activeVoices(vm) == 3);

    vm.handleNoteEvent(noteOff(64));
    // The 64 voice is releasing (not Active); 60 and 67 still held.
    REQUIRE(activeSounding(vm, 64) == 0);
    REQUIRE(activeSounding(vm, 60) == 1);
    REQUIRE(activeSounding(vm, 67) == 1);
}

TEST_CASE("polywire: POLY AllNotesOff releases every sounding poly voice", "[polywire]") {
    // §9 / K7-analogue for POLY: an AllNotesOff de-asserts every held poly voice's gate.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    vm.handleNoteEvent(noteOn(60));
    vm.handleNoteEvent(noteOn(64));
    REQUIRE(activeVoices(vm) == 2);

    vm.handleNoteEvent({NoteEvent::Type::AllNotesOff, 0, 0.0f, 0});
    REQUIRE(activeVoices(vm) == 0);   // none Active (released/idle)
}

// --- unison-on-poly: whole groups (§6.4 / C16) -------------------------------------

TEST_CASE("polywire: POLY with unison stacks whole groups per note", "[polywire]") {
    // §6.4 / ADR-006 C16: under unison-on-poly, each poly note-on drives a whole U-voice
    // group; effective polyphony = floor(maxPoly / U). With U=2 a single note sounds 2
    // voices (its group), and the group footprint never exceeds kMaxVoices.
    constexpr int U = 2;
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    vm.setUnisonCount(U);
    boundary(vm);
    REQUIRE(vm.unisonCount() == U);

    vm.handleNoteEvent(noteOn(60));
    REQUIRE(activeSounding(vm, 60) == U);   // a whole group of U voices sounds the note
    REQUIRE(activeVoices(vm) <= mw::kMaxVoices);

    vm.handleNoteEvent(noteOn(64));
    REQUIRE(activeSounding(vm, 64) == U);
    REQUIRE(activeVoices(vm) == 2 * U);
}

// --- MONO / UNISON behavior unchanged ----------------------------------------------

TEST_CASE("polywire: MONO behavior is unchanged after the POLY wiring", "[polywire]") {
    // Regression guard (075b Scope: "Keep MONO/UNISON paths intact"): MONO still sounds
    // exactly ONE voice via the KeyAssigner path, untouched by the POLY wiring.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    REQUIRE(vm.mode() == VoiceMode::Mono);

    vm.handleNoteEvent(noteOn(60));
    vm.controlTick();   // resolve the sole KeyAssigner (the MONO authority)

    REQUIRE(vm.activeCount() == 1);
    REQUIRE(vm.voice(0).state() == VoiceState::Active);
    REQUIRE(vm.voice(0).currentNote() == 60);
    for (int i = 1; i < mw::kMaxVoices; ++i)
        REQUIRE(vm.voice(i).state() == VoiceState::Idle);
}

TEST_CASE("polywire: UNISON behavior is unchanged after the POLY wiring", "[polywire]") {
    // Regression guard: UNISON still broadcasts the single KeyAssigner decision to U
    // voices on one note (untouched by the POLY wiring).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(4);
    boundary(vm);

    vm.handleNoteEvent(noteOn(67));
    vm.controlTick();

    REQUIRE(vm.activeCount() == 4);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(vm.voice(i).state() == VoiceState::Active);
        REQUIRE(vm.voice(i).currentNote() == 67);
    }
}

TEST_CASE("polywire: switching POLY back to MONO does not leave stuck poly voices", "[polywire]") {
    // ADR-006 §Consequences / C17: a mode switch is handled at the block boundary so it
    // never leaves a stuck note. Sound several POLY voices, switch to MONO; the boundary
    // hard-stops the voices outside the MONO active slot.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    vm.handleNoteEvent(noteOn(60));
    vm.handleNoteEvent(noteOn(64));
    vm.handleNoteEvent(noteOn(67));
    REQUIRE(activeVoices(vm) == 3);

    vm.setMode(VoiceMode::Mono);
    boundary(vm);   // boundary applies MONO and hard-stops voices outside slot 0
    REQUIRE(vm.mode() == VoiceMode::Mono);
    for (int i = 1; i < mw::kMaxVoices; ++i)
        REQUIRE(vm.voice(i).state() != VoiceState::Active);
}

// --- §8 RT3/RT6 + ADR-001 C3/C4: POLY note path is alloc-free / lock-free ----------

TEST_CASE("polywire: POLY note handling and render are alloc-free and lock-free", "[polywire]") {
    // §8 RT3/RT6 / ADR-001 C3/C4; ADR-006 C17: routing POLY note events through the
    // allocator only flips existing voice state via a bounded O(kMaxVoices) integer scan;
    // it never touches the heap or a lock. Arm the AudioThreadGuard around a steal-forcing
    // POLY sequence plus a render.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Poly);
    boundary(vm);

    // Warm up the event/render path once OUTSIDE the armed window (lazy one-time init).
    vm.handleNoteEvent(noteOn(60));
    { Stereo warm; vm.render(warm.l.data(), warm.r.data(), 64); }

    Stereo s;
    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (int g = 0; g < mw::kMaxPoly; ++g) vm.handleNoteEvent(noteOn(48 + g));  // fills + steals
    vm.handleNoteEvent(noteOn(96));   // forces a deterministic steal
    vm.handleNoteEvent(noteOff(48));
    vm.render(s.l.data(), s.r.data(), 64);
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
}

TEST_CASE("polywire: POLY hot-path methods are noexcept", "[polywire]") {
    // ADR-001 C5: the POLY note ingress and render stay noexcept (no-throw audio thread).
    VoiceManager vm;
    NoteEvent e = noteOn(60);
    float l = 0.0f, r = 0.0f;
    STATIC_REQUIRE(noexcept(vm.handleNoteEvent(e)));
    STATIC_REQUIRE(noexcept(vm.render(&l, &r, 1)));
    STATIC_REQUIRE(noexcept(vm.setMode(VoiceMode::Poly)));
}
