// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/KeyAssigner.h — the bit-faithful firmware key-scan / note-priority /
// retrigger state machine (task 069). The literal model of the SH-101
// `keyboard_read` / `play` super-loop [research/07 §2.3, §3.1-§3.3].
//
// KeyAssigner is the SOLE note-priority authority for MONO and UNISON; POLY
// bypasses it entirely [docs/design/04 §5.1; ADR-006 §Decision item 3, C12]. It
// couples priority AND envelope-trigger to the single S7 selector exactly as the
// hardware does — there is deliberately NO separate priority parameter and NO
// separate retrigger toggle [research/07 §11; ADR-006 §Decision item 2, C].
//
// Layout and method surface mirror docs/design/04-voice-and-control.md §5.2
// verbatim. All hot-path methods are noexcept, allocation-free, lock-free, and the
// resolve() scan is a bounded O(128) (never per-sample) [docs/design/04 §3.4, §5.3;
// ADR-001; ADR-006 §4, C17]. Parameter-ID strings / the APVTS schema are owned by
// doc 06 [ADR-008], not here.

#pragma once

#include <bitset>

#include "VoiceTypes.h"

namespace mw {

// The bit-faithful firmware note-priority / retrigger state machine (§5.2).
//
// Held keys are tracked in a 128-bit set in arrival order (the 4x8 matrix analog,
// widened to the full MIDI 0..127 range because mwAudio101 is a software
// instrument, not the literal 32-key bed [docs/design/04 §5.2]). The prior-tick
// snapshot drives the last-note XOR of changed-down keys [research/07 §3.2].
class KeyAssigner {
public:
    // Clears all held/scan state. Sized once; allocation-free at use [§5.2].
    void prepare() noexcept;          // clears all held state
    void reset() noexcept;            // panic / all-notes-off

    void setMode(GateTrigMode m) noexcept { mode_ = m; }  // bound to S7

    // Apply all note events that land within the current control tick, then
    // resolve. Events are applied in arrival order; multiple downs in one tick
    // are batched for the last-note XOR (§5.4, C4).
    void noteOn(int midiNote) noexcept;
    void noteOff(int midiNote) noexcept;

    // Resolve priority + trigger for the current control tick. MUST be called
    // once per control tick (§7). Returns the {activeNote, gate, retrigger,
    // clockReset} the mono/unison Voice(s) consume.
    NoteDecision resolve() noexcept;

    bool anyHeld() const noexcept;

private:
    std::bitset<128> held_;        // currently-held keys
    std::bitset<128> prevScan_;    // prior-tick snapshot for last-note XOR
    GateTrigMode     mode_   = GateTrigMode::GateTrig;  // default per voice doc; param owned by doc 06
    int  lastActive_       = -1;   // active note emitted last tick
    bool gateWasAsserted_  = false;
    bool newKeyThisTick_   = false;  // for CLOCK RESET in Lfo/Arp (§5.4 C6)
};

} // namespace mw
