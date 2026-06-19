// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/Engine.h — the three-call DSP-core seam that wires the voice loop, the voice
// manager, the control core, and the post-voice FX chain behind prepare/process/reset
// (task 118).
//
// Realizes docs/design/00 §5.1 (the three-call surface), §5.5 (lifecycle invariants),
// §4.1 (the end-to-end graph: per-voice circuit -> MONO VOICE SUM -> post-voice FX),
// §4.4 (internal sub-blocking at MIDI/event offsets, capped at kRenderBlock), §6.1
// (single-threaded, fixed voice-index-order accumulation, no synchronization
// primitive), and the §9 global RT invariants [ADR-001 C2-C6, C11; ADR-019 VT-01/VT-02/
// VT-03; ADR-017 §Decision (post-voice FX once on the mono sum)].
//
// WHAT THIS OWNS (task 118 scope; task 118b reconcile): the Engine seam class itself —
// it consumes the already-built VoiceManager (task 074), ControlCore (task 071), and
// FxChain (task 094) and assembles them behind the seam. prepare() is the ONLY
// allocation site; process() chunks the host block, walks active voices in fixed index
// order summing into a mono mix, then runs the shared FX once on that mono sum; reset()
// clears to a known start.
//
// NOTE-PRIORITY OWNERSHIP (task 118b): the engine routes note events through
// VoiceManager::handleNoteEvent into the VoiceManager's SOLE KeyAssigner — the
// documented single MONO/UNISON note-priority authority (doc 04 §5.1, §9; ADR-006 C12)
// — and the ControlCore clocks that same KeyAssigner via the no-arg
// VoiceManager::controlTick(). The engine holds NO second/duplicate KeyAssigner, and
// the S7 selector (GATE / GATE+TRIG / LFO, incl. LFO clock-reset, ADR-006 C17) is
// reachable through Engine::setGateTrigMode. This closes the wave-11 QA MEDIUM on
// PR #71, where an Engine-owned adapter KeyAssigner left the VoiceManager's own dead.
//
// OUT OF SCOPE (other streams own these): the DSP internals of any consumed module;
// the JUCE/plugin marshalling of host events -> BlockContext and the setLatencySamples
// host call (plugin-processor / integration); the golden bless capture (golden-harness).
//
// No `juce::*` type appears here; mwcore is JUCE-free [ADR-001 C1/C14]. process() and
// reset() are noexcept hot paths; all sizing happens in prepare() [ADR-001 C2-C5; §9].

#pragma once

#include <vector>

#include "BlockContext.h"

#include "voice/VoiceManager.h"   // owns the SOLE KeyAssigner (doc 04 §5.1/§9); GateTrigMode via VoiceTypes
#include "control/ControlCore.h"
#include "dsp/fx/FxChain.h"

namespace mw {

// The single, value-typed DSP-core seam [docs/design/00 §5.1; ADR-001 Decision]. The
// shell and every test drive the core through exactly these three calls.
class Engine {
public:
    Engine() noexcept = default;

    // Off-the-audio-thread setup; the ONLY place allocation happens [§5.5; ADR-001 C2].
    // Sizes the mono mix + stereo scratch and prepares the voice pool, control core,
    // and FX chain. The per-voice 2x-oversampled-zone factor is selected here and
    // clamped to 1x above OS_CEILING_HZ [§8.5; ADR-023 V15]. Re-callable; idempotent on
    // sample-rate / block-size / voice-cap change.
    void prepare(double sampleRate, int maxBlockSize, int maxVoices) noexcept;

    // Pure render. Touches ONLY pre-sized member storage. Hot path [§5.5; ADR-001
    // C3-C6, C11]. Sets FTZ/DAZ at entry, splits the host block at MIDI/event offsets,
    // renders each segment in fixed kRenderBlock-capped chunks (§4.4), accumulates
    // active voices in fixed voice-index order into the mono mix (§6.1), then runs the
    // post-voice FX chain once on that mono sum (§4.1; ADR-017).
    void process(const BlockContext& ctx) noexcept;

    // Clears state to a known start. No allocation [§5.5; ADR-001 Decision].
    void reset() noexcept;

    // Set the coupled S7 selector (note priority + envelope trigger) through the seam:
    // GATE (lowest-note, no legato retrigger), GATE+TRIG (last-note, retrigger every
    // key), or LFO (lowest-note, clock-reset on keypress) [docs/design/04 §5.1, §5;
    // ADR-006 C12/C17]. Forwards to the VoiceManager's SOLE KeyAssigner — the single
    // MONO/UNISON note-priority authority that the engine's render path resolves
    // (task 118b reconcile). A lock-free flag write; safe from the audio thread.
    void setGateTrigMode(GateTrigMode m) noexcept { voices_.setGateTrigMode(m); }

    // --- accessors (for tests; no audio-thread state mutation) -----------------------
    [[nodiscard]] bool   isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int    maxBlockSize() const noexcept { return maxBlockSize_; }
    [[nodiscard]] int    oversampleFactor() const noexcept { return oversampleFactor_; }
    [[nodiscard]] const VoiceManager& voiceManager() const noexcept { return voices_; }
    [[nodiscard]] const ControlCore&  controlCore()  const noexcept { return control_; }

    // Expose the FX latency the FX chain reports (the constant FX group delay). The
    // plugin sums this with the per-voice zone delay for setLatencySamples (out of
    // scope here) [§7; ADR-017 L4]. Surfaced for tests / the bridge.
    [[nodiscard]] int fxLatencySamples() const noexcept { return fx_.getLatencySamples(); }

private:
    // Render one already-bounded segment [n0, n0+len) (len <= kRenderBlock) of the host
    // block into the output channels. Applies the events whose offset lands at the head
    // of this segment through the VoiceManager's SINGLE note ingress (handleNoteEvent ->
    // its sole KeyAssigner), advances the control core (which fires the control tick,
    // clocking that same KeyAssigner via VoiceManager::controlTick()), renders active
    // voices in fixed index order into the mono mix, and runs the FX once on that mono
    // sum. noexcept, alloc-free, lock-free.
    void renderChunk(const BlockContext& ctx, int n0, int len) noexcept;

    // The consumed modules — assembled, not re-implemented, by this task. The
    // VoiceManager owns the SOLE note-priority KeyAssigner for MONO/UNISON (doc 04
    // §5.1/§9, ADR-006 C12); the engine routes notes into it via handleNoteEvent and the
    // ControlCore clocks its resolution via the no-arg controlTick() — there is no
    // second/duplicate KeyAssigner here (task 118b reconcile of the wave-11 PR #71
    // finding).
    VoiceManager voices_{};
    ControlCore  control_{};
    fx::FxChain  fx_{};

    // Preallocated scratch (sized to maxBlockSize_ in prepare): the per-voice stereo
    // accumulation buffers and the mono voice-sum the FX chain consumes [§9 RT6].
    std::vector<float> mixL_{};
    std::vector<float> mixR_{};
    std::vector<float> mono_{};

    double sampleRate_      = 0.0;
    int    maxBlockSize_    = 0;
    int    maxVoices_       = 0;
    int    oversampleFactor_ = 1;
    bool   prepared_        = false;
};

} // namespace mw
