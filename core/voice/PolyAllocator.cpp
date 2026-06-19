// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/PolyAllocator.cpp — implementation of the deterministic POLY per-note
// allocator + voice-stealing scan over the fixed Voice pool (task 075).
//
// Realizes docs/design/04-voice-and-control.md §6.4 (POLY) and §6.5 (Determinism)
// under ADR-006 §Decision item 3 POLY / C11-C18 and ADR-019 VT-01/VT-02/VT-04. No
// (PI) literal is inlined: the re-strike window lives in
// core/calibration/PolyAllocatorConstants.h and the steal-fade length in
// core/calibration/VoiceDriftConstants.h [docs/design/00 §1.2; ADR-020 S13].
//
// Every method is noexcept, heap-allocation-free, and lock-free; the steal scan is a
// single bounded O(kMaxVoices) integer-comparison pass run once per note event, never
// per sample — no sort, no heap, no priority queue [ADR-006 C14, C17].

#include "voice/PolyAllocator.h"

#include <algorithm>

#include "calibration/PolyAllocatorConstants.h"

namespace mw {

void PolyAllocator::configure(Pool& pool, std::uint64_t& nextSerial,
                              int maxPoly, int unisonCount) noexcept {
    pool_       = &pool;
    nextSerial_ = &nextSerial;

    unison_ = std::clamp(unisonCount, 1, kMaxUnison);

    // Effective polyphony = floor(maxPoly / U) groups [ADR-006 C16], clamped so the
    // groups never exceed the pool: groupCount_ * U <= kMaxVoices (hard cap, C16). The
    // poly budget itself is clamped into 1..kMaxPoly first.
    const int budget = std::clamp(maxPoly, 1, kMaxPoly);
    int groups       = budget / unison_;              // floor division [ADR-006 C16]
    groups           = std::min(groups, kMaxVoices / unison_);  // hard cap [ADR-006 C16]
    groupCount_      = std::max(groups, 0);
}

int PolyAllocator::findIdleGroup() const noexcept {
    // §6.4 step 1: prefer any Idle group. Lowest group index first => fixed-order
    // determinism (ADR-019 VT-02).
    for (int g = 0; g < groupCount_; ++g) {
        if (groupIsIdle(g)) {
            return g;
        }
    }
    return -1;
}

int PolyAllocator::findRestrikeGroup(int midiNote) const noexcept {
    // §6.4 step 2 / ADR-006 C13: re-strike reuse. An incoming note that matches a
    // still-held (Active) group's note within the (PI) re-strike window reuses THAT
    // group — no doubling. Lowest group index first for determinism. Releasing groups
    // are NOT re-struck: their tails finish in place (C15); a re-strike targets a live
    // held note only.
    for (int g = 0; g < groupCount_; ++g) {
        if (!groupIsActive(g)) {
            continue;
        }
        const int diff = lead(g).currentNote() - midiNote;
        const int absDiff = diff < 0 ? -diff : diff;
        if (absDiff <= cal::voice::kRestrikeWindowSemitones) {
            return g;
        }
    }
    return -1;
}

int PolyAllocator::scanStealVictim() const noexcept {
    // §6.4 step 3 / ADR-006 C14: the deterministic steal scan. A single bounded
    // O(kMaxVoices) integer-comparison pass picks the most-stealable group by a strict
    // lexicographic order:
    //   1. PHASE — a Releasing group (release tail / already-stealing) is preferred
    //      over an Active (held) group ("oldest-in-release" first).
    //   2a. among Releasing groups: the OLDEST (lowest note-serial) wins.
    //   2b. among Active groups: the QUIETEST (lowest currentLevel) wins.
    //   3. TIE-BREAK — equal-level Active groups break by ascending note-serial
    //      (= oldest-held). This same ascending-serial rule orders Releasing groups.
    // No timestamps, no sort, no heap, no allocating container, no priority queue.
    // Deterministic: same pool state => same victim, every run, every platform
    // [ADR-006 C14, C18; ADR-019 VT-04].
    int victim = -1;

    // Best-so-far ranking fields. phaseRank: 0 = Releasing (more stealable), 1 = Active.
    int           bestPhase  = 2;            // worse than any real group
    float         bestLevel  = 0.0f;
    std::uint64_t bestSerial = 0;

    for (int g = 0; g < groupCount_; ++g) {
        // Group lifecycle from the lead voice (whole-group semantics, C11).
        int   phase;
        float level;
        if (groupIsReleasing(g)) {
            phase = 0;
            level = lead(g).currentLevel();   // unused for ordering within phase 0
        } else if (groupIsActive(g)) {
            phase = 1;
            level = lead(g).currentLevel();
        } else {
            // Idle — never a steal target (an idle slot would have been taken in step 1).
            continue;
        }
        const std::uint64_t serial = lead(g).noteSerial();

        bool better;
        if (victim < 0) {
            better = true;
        } else if (phase != bestPhase) {
            better = phase < bestPhase;           // 1. release-phase beats held-phase
        } else if (phase == 0) {
            better = serial < bestSerial;         // 2a. oldest release (lowest serial)
        } else if (level != bestLevel) {
            better = level < bestLevel;           // 2b. quietest held
        } else {
            better = serial < bestSerial;         // 3. tie-break: oldest held
        }

        if (better) {
            victim     = g;
            bestPhase  = phase;
            bestLevel  = level;
            bestSerial = serial;
        }
    }

    return victim;
}

void PolyAllocator::triggerGroup(int g, int midiNote) noexcept {
    // Every poly note is its own FRESH GATE+TRIG trigger; there is no cross-voice
    // lowest-note concept [ADR-006 C12]. Stamp the WHOLE group with one shared
    // monotonic note-serial so the group steals/orders as a unit (C14, C16, C18).
    const std::uint64_t serial = (*nextSerial_)++;
    const int base = groupBase(g);
    for (int v = 0; v < unison_; ++v) {
        Voice& voice = (*pool_)[static_cast<std::size_t>(base + v)];
        voice.setNoteSerial(serial);
        voice.noteOn(midiNote, /*velocity=*/1.0f, /*retrigger=*/true);
    }
}

void PolyAllocator::beginStealGroup(int g) noexcept {
    // Whole-group steal: fast forced fade on every voice of the group (NOT a hard cut)
    // [ADR-006 C11, C15]. Other voices' release tails finish in place (untouched).
    const int base = groupBase(g);
    for (int v = 0; v < unison_; ++v) {
        (*pool_)[static_cast<std::size_t>(base + v)].beginSteal();
    }
}

int PolyAllocator::allocatePoly(int midiNote) noexcept {
    if (groupCount_ <= 0) {
        return -1;
    }

    // §6.4 step 2 first for re-strike of a held key (no doubling, C13): if the note is
    // already sounding on a held group, reuse THAT group rather than taking an idle
    // slot or stealing — re-striking a held key must not spawn a second voice.
    int g = findRestrikeGroup(midiNote);
    if (g < 0) {
        // §6.4 step 1: prefer any idle group.
        g = findIdleGroup();
    }
    if (g < 0) {
        // §6.4 step 3: no idle/re-strike target -> steal deterministically, then begin
        // the whole-group fast fade before reusing the slot (C15).
        g = scanStealVictim();
        if (g >= 0) {
            beginStealGroup(g);
        }
    }
    if (g < 0) {
        return -1;   // unreachable when groupCount_ > 0
    }

    // Fresh GATE+TRIG trigger on the (idle / re-struck / just-stolen) group.
    triggerGroup(g, midiNote);
    return g;
}

void PolyAllocator::releasePoly(int midiNote) noexcept {
    // Release EVERY held group currently sounding `midiNote` (gate de-assert -> release
    // tail finishes in place, C12/C15). Groups in Releasing/Stealing/Idle are left
    // alone. Whole-group: drive every voice of a matching group to release.
    for (int g = 0; g < groupCount_; ++g) {
        if (!groupIsActive(g)) {
            continue;
        }
        if (lead(g).currentNote() != midiNote) {
            continue;
        }
        const int base = groupBase(g);
        for (int v = 0; v < unison_; ++v) {
            (*pool_)[static_cast<std::size_t>(base + v)].noteOff();
        }
    }
}

} // namespace mw
