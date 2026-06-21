// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/StepSequencer.h — the 100-slot record/play sequencer (task 085).
//
// Realizes docs/design/05 §6.1 (step model), §6.3 (transport: LOAD/PLAY,
// auto-exit at 100, wrap-around), §6.4 (articulation: REST drops gate, TIE
// sustains + suppresses re-gate), and §6.5 (the class signature). Per ADR-007
// Decision 4 / C12–C16 and ADR-025 (NO per-step accent, NO per-step gate-time):
// the per-step payload is note / rest / tie / gate only — there is no accent
// field anywhere in the type.
//
// Fixed 100-slot model: one event per slot. A note = one slot, a REST = one slot,
// and each tie/long-note extension = one slot (§6.1; ADR-007 C12). Storage is the
// pre-sized `SeqBuffer` from control/ControlTypes.h (task 081); this class never
// resizes it and never heap-allocates on any path (ADR-007 C26).
//
// No `juce::*` type appears here; mwcore is JUCE-free [ADR-001/D2]. Parameter IDs
// (`mw101.seq.mode`, etc.) are owned by docs/design/06 §2 [ADR-008], not minted
// here — this header models only behavior. Clock-edge production is the caller's
// job (Clock §7); this class advances one slot per edge it is handed.

#pragma once

#include "control/ControlTypes.h"

namespace mw::control {

// 100-slot one-event-per-slot sequencer with keyboard-only LOAD recording and
// wrap-around PLAY playback. All hot paths (`advanceOnEdge`, the `record*`
// appenders) are `noexcept` and allocate no heap; the buffer is fixed-size and
// pre-sized [docs/design/05 §6.5; ADR-007 C26].
class StepSequencer {
public:
    // Zero the buffer and reset all counters/flags. No allocation after this
    // [§6.5]. Safe to re-call (idempotent reset to the known start state).
    void prepare() noexcept;

    // --- LOAD path (message/key thread): keyboard-only writes [§6.3] ---------

    // LOAD toggle. Turning record on while already at kMaxSteps is a no-op
    // (record auto-exits when the buffer is full; C14).
    void setRecord(bool on) noexcept;
    bool isRecording() const noexcept { return recording_; }

    // Append a note slot at `count_` (low 6 bits of `pitch6`, DAC range) — only
    // while recording. Auto-clears `recording_` when `count_` reaches kMaxSteps
    // (§6.3 C14; §6.5).
    void recordNote(int pitch6) noexcept;

    // Append a REST slot (gate dropped on playback) — only while recording (§6.3).
    void recordRest() noexcept;

    // Append a TIE/legato slot (sustains envelope + portamento, no re-gate, on
    // playback) — only while recording (§6.3 / §6.4 C16).
    void recordTie(int pitch6) noexcept;

    // Reset the filled count to 0 (does not exit record) [§6.5].
    void clear() noexcept;

    // --- PLAY path (control tick) --------------------------------------------

    void setPlay(bool on) noexcept;
    bool isPlaying() const noexcept { return playing_; }

    // Clock-reset re-phase: rewind playback to slot 0 (Clock §7.5) [§6.5].
    void resetToStart() noexcept;

    // Advance one slot on a clock H->L edge; decodes the current slot's flags
    // into a SeqPlayResult, then advances `playPos_ = (playPos_ + 1) % count_`
    // (a no-op when `count_ == 0`). RT-safe, `noexcept`, no allocation
    // [§6.5; ADR-007 C26].
    SeqPlayResult advanceOnEdge() noexcept;

    // --- Accessors / preset restore ------------------------------------------

    int count() const noexcept { return count_; }          // filled slots (<= 100)
    const SeqBuffer& buffer() const noexcept { return steps_; }

    // The RAW play cursor: the slot `advanceOnEdge()` will play on the NEXT clock edge
    // (0..count_-1, or 0 when empty). This is the StepSequencer's own deterministic
    // (playPos+1)%count state — exposed so the engine/telemetry need not reconstruct it
    // [docs/design/05 §6.3/§6.5; task 118d].
    [[nodiscard]] int playPos() const noexcept { return playPos_; }

    // The REAL current sequencer step: the slot the MOST-RECENT clock edge actually
    // played (== the SeqPlayResult.slotIndex of the last advanceOnEdge), or -1 if no
    // edge has played since the last prepare()/clear()/loadBuffer()/resetToStart(). This
    // is the authoritative live playhead the UI seq-grid highlights — it is read straight
    // from the StepSequencer, so it can NEVER diverge from the real playhead when a
    // clock-reset-on-keypress rewinds playback (the 118c reconstructed-mirror divergence
    // this closes) [docs/design/05 §6.3; task 118d closes the 111c/118c QA MEDIUM].
    [[nodiscard]] int currentSlot() const noexcept { return lastPlayedSlot_; }

    // Preset restore: copy a full buffer + filled count (clamped to kMaxSteps),
    // and re-phase playback to the start [§6.5].
    void loadBuffer(const SeqBuffer& b, int count) noexcept;

private:
    // Append one already-encoded slot byte if recording and room remains; bumps
    // `count_` and auto-exits record at kMaxSteps (§6.3 C14).
    void append(std::uint8_t bits) noexcept;

    SeqBuffer steps_{};
    int count_ = 0;
    int playPos_ = 0;
    // The slot the most-recent advanceOnEdge() played (the REAL live playhead the UI
    // highlights), -1 == none played since the last reset/clear/load/resetToStart. The
    // engine/telemetry read THIS rather than reconstruct it, so it tracks the playhead
    // exactly across a clock-reset-on-keypress rewind (task 118d) [docs/design/05 §6.3].
    int lastPlayedSlot_ = -1;
    bool recording_ = false;
    bool playing_ = false;
};

} // namespace mw::control
