// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/PolyAllocator.h — the POLY per-note allocator + deterministic
// voice-stealing scan over the fixed Voice pool (task 075).
//
// Single source of truth: docs/design/04-voice-and-control.md §6.4 (POLY) and §6.5
// (Determinism), under ADR-006 §Decision item 3 POLY / C11-C18 and ADR-019
// VT-01/VT-02/VT-04. This is the ONLY mode that bypasses the KeyAssigner
// [ADR-006 C12].
//
// WHAT THIS OWNS (task 075 scope):
//   - allocatePoly(midiNote): the per-note allocator (§6.4 steps 1-3):
//       1. prefer any Idle voice group;
//       2. re-strike — if the note matches a still-held group's note within the (PI)
//          re-strike window, reuse that group (no doubling) [ADR-006 C13];
//       3. otherwise run the deterministic O(kMaxVoices) integer-stamped steal scan:
//          oldest-in-release -> quietest (lowest currentLevel) -> oldest-held,
//          tie-broken by ascending integer note-serial [ADR-006 C14].
//   - releasePoly(midiNote): release every group sounding that note (gate de-assert;
//     the release tail finishes in place) [ADR-006 C12, C15].
//   - nextSerial stamping: each allocation stamps the group's voices with the next
//     monotonic note-serial for deterministic steal ordering [ADR-006 C14, C18].
//   - a steal calls Voice::beginSteal (fast forced fade, NOT a hard cut) on the whole
//     victim group; OTHER voices' release tails finish in place [ADR-006 C15].
//   - unison-on-poly: floor(maxPoly / U) groups of U contiguous voice slots; the
//     active-voice count is hard-capped at kMaxVoices; steals remove WHOLE unison
//     groups, never an individual stacked voice [ADR-006 C11, C16].
//
// OUT OF SCOPE (other tasks/docs own these):
//   - MONO/UNISON drive + the shared pool/render walk (VoiceManager, task 074);
//   - the KeyAssigner (POLY bypasses it) (task 069);
//   - the fade-ramp DSP inside Voice — beginSteal/stealGain_ live on Voice (task 073);
//   - MPE per-note routing (ADR-012/022).
//
// DESIGN NOTE — why a stand-alone allocator over a pool view rather than methods on
// VoiceManager: the allocator is a pure deterministic policy over the Voice[] pool. It
// is written as its own module so it is independently unit-testable against the §6.4
// steal-order oracle without standing up the whole VoiceManager/KeyAssigner/render
// stack, and so this task adds NEW files only (no edit to the task-074 VoiceManager
// surface). VoiceManager composes it for its POLY path. It operates ONLY on the
// pre-sized pool (the pool/scratch are sized in VoiceManager::prepare, §8 RT6); it
// never allocates, never locks, and is noexcept on every method.
//
// RT invariants [docs/design/04 §8 RT3/RT6/RT7; ADR-001; ADR-019]: every method is
// noexcept, heap-allocation-free, and lock-free; the steal scan is a single bounded
// O(kMaxVoices) integer-comparison pass run once per note event, never per sample —
// no sort, no heap, no priority queue, no allocating container [ADR-006 C14, C17].

#pragma once

#include <array>
#include <cstdint>

#include "VoiceTypes.h"
#include "Voice.h"

namespace mw {

// Deterministic POLY per-note allocator + steal scan over a fixed Voice pool (§6.4).
// Holds a reference to the externally-owned, prepare-sized pool and the shared
// monotonic note-serial counter; owns no DSP state and no allocation of its own.
class PolyAllocator {
public:
    using Pool = std::array<Voice, kMaxVoices>;

    PolyAllocator() noexcept = default;

    // Bind the allocator to the externally-owned pool and the shared note-serial
    // counter, and set the active configuration. maxPoly is the poly-group budget
    // (<= kMaxPoly); unisonCount is U (1..kMaxUnison). Group count G =
    // floor(maxPoly / U), clamped so G*U <= kMaxVoices [ADR-006 C16]. Group g occupies
    // the U contiguous voice slots [g*U, g*U + U), giving fixed voice-index order
    // (ADR-019 VT-02) and whole-group steal by construction (ADR-006 C11). Called off
    // the audio thread (prepare / reconfiguration boundary), never mid-block (§8 RT7).
    void configure(Pool& pool, std::uint64_t& nextSerial,
                   int maxPoly, int unisonCount) noexcept;

    // Allocate a voice GROUP for `midiNote` (§6.4). Returns the chosen group index
    // (0..groupCount()-1); always succeeds (steals if necessary). Stamps the group's
    // voices with the next monotonic note-serial and fires a fresh GATE+TRIG trigger on
    // every voice in the group (every poly note is its own fresh trigger, no cross-voice
    // lowest-note concept) [ADR-006 C12, C14, C18]. noexcept, alloc-free, lock-free.
    int allocatePoly(int midiNote) noexcept;

    // Release EVERY group currently sounding `midiNote` (gate de-assert -> release tail
    // finishes in place) [ADR-006 C12, C15]. Voices already Releasing/Stealing/Idle are
    // untouched. noexcept, alloc-free, lock-free.
    void releasePoly(int midiNote) noexcept;

    // --- configuration accessors ----------------------------------------------------
    [[nodiscard]] int groupCount()   const noexcept { return groupCount_; }
    [[nodiscard]] int unisonCount()  const noexcept { return unison_; }
    [[nodiscard]] int voicesPerGroup() const noexcept { return unison_; }

    // First voice-slot index of group g (g in 0..groupCount()-1). Group g owns slots
    // [groupBase(g), groupBase(g) + unisonCount()).
    [[nodiscard]] int groupBase(int g) const noexcept { return g * unison_; }

private:
    // Per-group lifecycle, derived from the group's lead voice (slot groupBase(g)).
    // A group is Idle iff its lead voice is Idle; Releasing iff Releasing/Stealing;
    // Active iff Active [ADR-006 §4]. The lead voice is authoritative because every
    // voice in a group is driven by the SAME note event (whole-group semantics).

    // Find an Idle group, or -1 (§6.4 step 1).
    [[nodiscard]] int findIdleGroup() const noexcept;

    // Find a held (Active) group whose note matches `midiNote` within the re-strike
    // window, or -1 (§6.4 step 2; ADR-006 C13).
    [[nodiscard]] int findRestrikeGroup(int midiNote) const noexcept;

    // The deterministic steal scan (§6.4 step 3; ADR-006 C14): oldest-in-release ->
    // quietest -> oldest-held, tie-broken by ascending note-serial. Returns a valid
    // group index (always succeeds when groupCount_ > 0). Single bounded
    // O(kMaxVoices) integer-comparison pass; no sort, no heap.
    [[nodiscard]] int scanStealVictim() const noexcept;

    // Fire a fresh GATE+TRIG trigger on every voice of group g for `midiNote`, stamping
    // the shared monotonic note-serial (§6.4; ADR-006 C12, C14, C18).
    void triggerGroup(int g, int midiNote) noexcept;

    // Begin the fast forced fade on every voice of group g (whole-group steal,
    // ADR-006 C11/C15).
    void beginStealGroup(int g) noexcept;

    // --- group-state predicates over the lead voice ---------------------------------
    [[nodiscard]] const Voice& lead(int g) const noexcept {
        return (*pool_)[static_cast<std::size_t>(groupBase(g))];
    }
    [[nodiscard]] bool groupIsIdle(int g)      const noexcept { return lead(g).state() == VoiceState::Idle; }
    [[nodiscard]] bool groupIsActive(int g)    const noexcept { return lead(g).state() == VoiceState::Active; }
    [[nodiscard]] bool groupIsReleasing(int g) const noexcept {
        const VoiceState s = lead(g).state();
        return s == VoiceState::Releasing || s == VoiceState::Stealing;
    }

    Pool*          pool_       = nullptr;   // externally owned, prepare-sized
    std::uint64_t* nextSerial_ = nullptr;   // shared monotonic stamp source [C14, C18]

    int unison_     = 1;   // U (voices per group)
    int groupCount_ = 0;   // floor(maxPoly / U), clamped so groupCount_*U <= kMaxVoices
};

} // namespace mw
