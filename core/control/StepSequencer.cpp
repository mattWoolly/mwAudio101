// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/StepSequencer.cpp — implementation of the 100-slot record/play
// sequencer (task 085). See StepSequencer.h for the design citations
// (docs/design/05 §6.1/§6.3/§6.4/§6.5; ADR-007 C12–C16; ADR-025).
//
// Every method touches only the pre-sized fixed-capacity members; no path
// allocates heap or takes a lock [ADR-007 C26]. Integer step counters are
// bit-exact by construction (docs/design/00 §9.2).

#include "control/StepSequencer.h"

#include <cstdint>

namespace mw::control {

void StepSequencer::prepare() noexcept {
    steps_ = SeqBuffer{};   // zero all 100 slots (trivially-copyable; no alloc)
    count_ = 0;
    playPos_ = 0;
    lastPlayedSlot_ = -1;   // no step has played yet (task 118d live-playhead authority)
    recording_ = false;
    playing_ = false;
}

void StepSequencer::append(std::uint8_t bits) noexcept {
    // Keyboard-only writes are honored only while recording, and only while room
    // remains (§6.3 C14). count_ is in [0, kMaxSteps] at all times.
    if (!recording_ || count_ >= kMaxSteps)
        return;

    steps_[static_cast<std::size_t>(count_)].bits = bits;
    ++count_;

    // Record auto-exits when all 100 slots are filled (§6.3 C14; §6.5).
    if (count_ >= kMaxSteps)
        recording_ = false;
}

void StepSequencer::setRecord(bool on) noexcept {
    // Cannot enter record on a full buffer — it would have nothing to write and
    // the hardware auto-exits at 100 (§6.3 C14).
    recording_ = on && (count_ < kMaxSteps);
}

void StepSequencer::recordNote(int pitch6) noexcept {
    // A plain note slot: low 6 bits = pitch, REST/TIE clear (§6.1/§6.2).
    append(static_cast<std::uint8_t>(pitch6) & SeqStep::kPitchMask);
}

void StepSequencer::recordRest() noexcept {
    // A REST slot: gate dropped on playback (§6.4). Pitch bits are don't-care; a
    // REST carries no pitch.
    append(SeqStep::kRestFlag);
}

void StepSequencer::recordTie(int pitch6) noexcept {
    // A TIE/legato slot: sustains envelope + portamento, no re-gate (§6.4 C16).
    append(static_cast<std::uint8_t>(
        SeqStep::kTieFlag
        | (static_cast<std::uint8_t>(pitch6) & SeqStep::kPitchMask)));
}

void StepSequencer::clear() noexcept {
    count_ = 0;
    playPos_ = 0;
    lastPlayedSlot_ = -1;   // nothing left to have played (task 118d)
}

void StepSequencer::setPlay(bool on) noexcept {
    playing_ = on;
}

void StepSequencer::resetToStart() noexcept {
    // Clock-reset re-phase (Clock §7.5): rewind playback to slot 0 AND clear the
    // last-played slot so the live playhead reads "no step played" until the next edge
    // plays slot 0. This is exactly the rewind path the 118c reconstructed mirror could
    // not observe; reading lastPlayedSlot_ here makes currentSlot() track it (task 118d).
    playPos_ = 0;
    lastPlayedSlot_ = -1;
}

SeqPlayResult StepSequencer::advanceOnEdge() noexcept {
    SeqPlayResult r{};

    // No filled slots: no-op, no wrap (avoids a %0). playPos_ stays 0 (§6.5).
    if (count_ == 0)
        return r;

    const SeqStep step = steps_[static_cast<std::size_t>(playPos_)];
    r.slotIndex = playPos_;
    r.pitch6 = step.pitch();
    // Latch the slot this edge plays as the REAL live playhead (task 118d). The engine/
    // telemetry read currentSlot() instead of reconstructing the playhead, so it stays
    // exact across a resetToStart() rewind.
    lastPlayedSlot_ = playPos_;

    if (step.isRest()) {
        // REST drops the gate (§6.4 C16). No tie, no retrigger of a sounding note.
        r.gateOn = false;
        r.tie = false;
        r.retrigger = false;
    } else if (step.isTie()) {
        // TIE sustains the envelope (gate stays on) + engages portamento, with no
        // re-gate (§6.4 C16).
        r.gateOn = true;
        r.tie = true;
        r.retrigger = false;
    } else {
        // A plain note gates and retriggers (the per-step default; the global S7
        // GATE/TRIG mode further shapes retrigger downstream — §6.4).
        r.gateOn = true;
        r.tie = false;
        r.retrigger = true;
    }

    // Advance one slot, wrapping last -> first (§6.3 C15).
    playPos_ = (playPos_ + 1) % count_;
    return r;
}

void StepSequencer::loadBuffer(const SeqBuffer& b, int count) noexcept {
    steps_ = b;   // trivially-copyable fixed array; no alloc
    if (count < 0)
        count = 0;
    if (count > kMaxSteps)
        count = kMaxSteps;
    count_ = count;
    playPos_ = 0;
    lastPlayedSlot_ = -1;   // a freshly-loaded pattern has played no step yet (task 118d)
}

} // namespace mw::control
