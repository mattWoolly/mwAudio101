// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/VoiceManager.h — owns the fixed Voice[kMaxVoices] pool, dispatches the
// MONO and UNISON drive paths (both fed by the single KeyAssigner), propagates the
// control tick to the active voice(s), and renders/sums active voices in FIXED
// voice-index order before FX (task 074).
//
// Single source of truth: docs/design/04-voice-and-control.md §6.1, §6.2, §6.3, §8
// (RT1-RT7), under ADR-006 §Decision item 3 (MONO/UNISON), ADR-019 VT-01/VT-02, and
// ADR-001 (the no-alloc/no-lock/noexcept hot-path seam).
//
// WHAT THIS OWNS (task 074 scope):
//   - the preallocated std::array<Voice, kMaxVoices> pool_, sized in prepare (§6.1);
//   - the single KeyAssigner that is the SOLE note-priority authority for MONO and
//     UNISON [§6.1; ADR-006 C12];
//   - the dense active-voice index list so render skips Idle slots (§6.1);
//   - driveMono: exactly ONE voice driven verbatim by the NoteDecision — a
//     bit-faithful pass-through (§6.2; ADR-006 §Decision item 3 MONO);
//   - driveUnison: broadcast ONE NoteDecision to U voices, then apply symmetric
//     centered cents detune and (PI) stereo-spread pan from Calibration.h, each voice
//     keeping its distinct deterministic drift seed (§6.3; ADR-006 C9/C10);
//   - handleNoteEvent: forward MONO/UNISON note-ons/offs to keyAssigner_ (§6.1, §9);
//   - controlTick: propagate one resolved NoteDecision to the active voice(s) (§6.1);
//   - render: walk active_ and sum voices in FIXED voice-index order (§8 RT2;
//     ADR-019 VT-02); mode/unison-count changes apply only at prepare or a block
//     boundary via a lock-free pending flag, NEVER mid-block (§8 RT7; ADR-006 C17).
//
// OUT OF SCOPE (other tasks own these — this file does NOT implement them):
//   - POLY allocation, re-strike, stealing, unison-group steal (task 075);
//   - KeyAssigner resolution internals (task 069) and ControlCore's control tick
//     (task 071) — VoiceManager consumes their outputs;
//   - Voice DSP internals (task 073);
//   - MPE per-note routing (ADR-012/022).
//
// RT invariants [docs/design/04 §8 RT1-RT7; ADR-001; ADR-019]: the pool / active list
// / KeyAssigner state are sized in prepare; handleNoteEvent / controlTick / render are
// noexcept, allocation-free, lock-free; the active scan is bounded O(kMaxVoices);
// mode/unison changes are deferred to a block boundary via a plain non-atomic pending
// flag read at the top of render (single-threaded voice path => nothing to lock,
// ADR-019 VT-03).

#pragma once

#include <array>
#include <cstdint>

#include "VoiceTypes.h"
#include "Voice.h"
#include "KeyAssigner.h"

namespace mw {

// Owns the fixed Voice pool and switches between MONO / UNISON / POLY (§6.1). The
// three modes are skins over the ONE pool — one render path for all three
// [ADR-019 VT-01]. POLY's allocator/stealing is task 075; this file owns the
// MONO/UNISON drive paths, the control-tick propagation, the active-voice list, and
// the fixed-index-order render/sum.
class VoiceManager {
public:
    VoiceManager() noexcept = default;

    // Off-the-audio-thread setup; the ONLY place allocation happens (each Voice
    // pre-sizes its own scratch) [§8 RT6; ADR-006 §4, C17; ADR-001 C2]. Every voice
    // is prepared with its fixed voice index and the per-instance seed so each carries
    // a distinct deterministic drift seed (§6.5; ADR-006 C18).
    void prepare(double sampleRate, int oversampleFactor, std::uint32_t instanceSeed) noexcept;

    // Mode / unison-count / S7 selector. Mode and unison-count changes are LATCHED and
    // applied only at the next block boundary (top of render), never mid-block
    // [§8 RT7; ADR-006 C17]. The GateTrigMode forwards straight to the KeyAssigner
    // (the S7 selector); it does not change pool topology, so it applies immediately.
    void setMode(VoiceMode m) noexcept;
    void setUnisonCount(int u) noexcept;           // clamped to 1..kMaxUnison [ADR-006 C9]
    void setGateTrigMode(GateTrigMode m) noexcept; // forwards to keyAssigner_ (S7)

    // Clear note-priority + voice state to a known start: reset the sole KeyAssigner
    // (panic / all-notes-off, doc 04 §5.2) and de-assert the gate on any sounding
    // voice so its tail releases in place, then rebuild the active list. RT-safe
    // (noexcept, alloc-free, lock-free); used by the engine seam's reset() (§5.5).
    void reset() noexcept;

    // Note events demultiplexed from MIDI/MPE/arp-seq (§9). MONO/UNISON forward to the
    // KeyAssigner; POLY (task 075) is not handled here. Sample-accurate; noexcept.
    void handleNoteEvent(const NoteEvent& e) noexcept;

    // Propagate one resolved NoteDecision to the active voice(s) for MONO/UNISON
    // (§6.1). Used by direct unit tests that supply the decision explicitly; the
    // KeyAssigner::resolve() that produces it lives here (keyAssigner_) per doc 04
    // §5.1/§9 — the sole MONO/UNISON note-priority authority. noexcept, alloc-free.
    void controlTick(const NoteDecision& d) noexcept;

    // The control-tick surface the ControlCore drives (task 071): advance() is
    // duck-typed on a NO-ARG controlTick(). This overload resolves the manager's OWN
    // keyAssigner_ (the single authority — doc 04 §5.1/§9, ADR-006 C12/C17) and applies
    // the decision, so the engine has exactly ONE KeyAssigner in the path: notes enter
    // via handleNoteEvent into keyAssigner_, and the ControlCore clocks resolution here
    // (task 118b reconcile of the wave-11 PR #71 finding — no dead duplicate). The Lfo
    // (clock-reset) selector is therefore reachable through the engine. noexcept,
    // alloc-free, lock-free.
    void controlTick() noexcept;

    // Render all active voices for `numSamples`, ACCUMULATING the sum in FIXED
    // voice-index order into outL/outR [§8 RT1/RT2; ADR-019 VT-01/VT-02]. Applies any
    // pending mode/unison-count change at the top (block boundary, §8 RT7). noexcept,
    // alloc-free, lock-free.
    void render(float* outL, float* outR, int numSamples) noexcept;

    // --- accessors (for tests / the engine) ----------------------------------------
    [[nodiscard]] VoiceMode    mode() const noexcept { return mode_; }
    [[nodiscard]] int          unisonCount() const noexcept { return unison_; }
    [[nodiscard]] int          activeCount() const noexcept { return activeCount_; }
    [[nodiscard]] const Voice& voice(int i) const noexcept { return pool_[static_cast<std::size_t>(i)]; }

    // Read-only access to the active-voice index list (dense prefix of size
    // activeCount()). Exposed so tests can assert fixed voice-index order (§8 RT2).
    [[nodiscard]] std::uint8_t activeIndex(int k) const noexcept {
        return active_[static_cast<std::size_t>(k)];
    }

private:
    std::array<Voice, kMaxVoices> pool_{};       // preallocated; no heap on the audio thread
    KeyAssigner  keyAssigner_{};                 // sole authority for MONO/UNISON (§5)

    VoiceMode    mode_       = VoiceMode::Mono;   // default [ADR-016 R-3]
    int          unison_     = 1;
    std::uint64_t nextSerial_ = 0;               // monotonic; for poly steal ordering [ADR-006 C14]

    // Latched, block-boundary-applied reconfiguration (§8 RT7; ADR-006 C17). Plain
    // (non-atomic) flags: the single-threaded voice path means there is nothing to
    // synchronize [ADR-019 VT-03]. The shell sets these off the hot loop; render reads
    // and applies them at the next block boundary.
    bool      pendingReconfig_ = false;
    VoiceMode pendingMode_     = VoiceMode::Mono;
    int       pendingUnison_   = 1;

    // Active-voice indices, dense prefix; size = activeCount_. No allocation.
    std::array<std::uint8_t, kMaxVoices> active_{};
    int          activeCount_ = 0;

    // --- mode-specific dispatch (§6.2/§6.3) ----------------------------------------
    void driveMono(const NoteDecision& d) noexcept;    // exactly ONE voice (§6.2)
    void driveUnison(const NoteDecision& d) noexcept;  // U voices on one note (§6.3)

    // Apply a latched mode/unison-count change at a block boundary (§8 RT7). Clears
    // any sounding voices outside the new active set so a mode switch never leaves a
    // stuck note (ADR-006 §Consequences). noexcept, alloc-free.
    void applyPendingReconfig() noexcept;

    // Rebuild the dense active-voice index list from the pool's per-voice state
    // (§6.1). Bounded O(kMaxVoices); run once per control tick / boundary, never per
    // sample [§8 RT6; ADR-006 §4, C14].
    void rebuildActiveList() noexcept;

    // Drive one voice slot verbatim from a NoteDecision (the MONO pass-through used by
    // both driveMono and, per-voice, driveUnison) [§6.2].
    void applyDecisionToVoice(int voiceIndex, const NoteDecision& d) noexcept;
};

} // namespace mw
