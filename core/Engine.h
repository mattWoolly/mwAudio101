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
// WHAT THIS OWNS (task 118 scope; task 118b reconcile; task 118c sequencer wiring): the
// Engine seam class itself — it consumes the already-built VoiceManager (task 074),
// ControlCore (task 071), SequencerEngine (task 087), and FxChain (task 094) and
// assembles them behind the seam. prepare() is the ONLY allocation site; process()
// chunks the host block, walks active voices in fixed index order summing into a mono
// mix, then runs the shared FX once on that mono sum; reset() clears to a known start.
//
// SEQUENCER / ARP ROUTING (task 118c; doc 05 §2.1/§2.3, doc 04 §9, ADR-006/ADR-007):
// the SequencerEngine hosts the arp/seq/clock/RANDOM fixed-order state machine. The
// Engine routes note ingress through it ONLY while the transport is RUNNING
// (ctx.transport.isPlaying): MIDI notes become KeyEvents fed to the SequencerEngine, and
// its emitted ControlEvents (the selected note + gate + trig from the step sequencer or
// arpeggiator) are translated back into NoteEvents and driven into the SAME single
// MONO/UNISON voice path as a keyboard key — through VoiceManager::handleNoteEvent into
// the SOLE KeyAssigner (doc 04 §9: "the arp/seq, when driving notes, emit NoteEvents like
// a keyboard"; ADR-006 C12). One H->L clock edge advances arp + seq + RANDOM together
// (§2.1 C17), inside the SequencerEngine. When the transport is STOPPED, note ingress
// takes the unchanged direct keyboard path (no regression to the task-118 voice path).
// POLY arp/seq note routing is OUT OF SCOPE (the SH-101 is mono; ADR-006 C12 bypasses the
// KeyAssigner for POLY).
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

#include <cstdint>
#include <vector>

#include "BlockContext.h"
#include "params/ParamSnapshot.h"  // the registry-indexed normalized param POD the dispatch reads (ADR-028)

#include "voice/VoiceManager.h"   // owns the SOLE KeyAssigner (doc 04 §5.1/§9); GateTrigMode via VoiceTypes
#include "voice/Voice.h"          // VoiceControls — the ADR-028 dispatch payload (task 160)
#include "control/ControlCore.h"
#include "control/ControlTypes.h"     // KeyEvent / ControlEvent PODs (doc 05 §2.3)
#include "control/SequencerEngine.h"  // arp/seq/clock fixed-order state machine (task 087)
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
    [[nodiscard]] const seq::SequencerEngine& sequencer() const noexcept { return sequencer_; }

    // The LIVE current sequencer step the playhead last advanced to (the slot the most
    // recent clock H->L edge played), for telemetry. -1 == no step has played since
    // prepare()/reset() or the sequencer is not playing. This is the REAL playhead slot,
    // NOT a monotonic display counter — it closes the 111c QA MEDIUM where the published
    // Snapshot.seqStep was a display counter rather than the live step (the processor's
    // 1-line publish update to read this is the out-of-scope follow-up 118d). A plain
    // read of a member written only on the single-threaded audio path; safe for tests.
    [[nodiscard]] int currentSeqStep() const noexcept { return currentSeqStep_; }

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
    seq::SequencerEngine sequencer_{};   // arp/seq/clock fixed-order state machine (task 087)
    fx::FxChain  fx_{};

    // Translate the SequencerEngine's emitted ControlEvents for this chunk into the
    // VoiceManager's single note ingress (handleNoteEvent into the sole KeyAssigner),
    // updating the engine-held seq note so each gate-on step releases the previous note
    // and presses the new one (a fresh trigger), a REST/gate-off releases it, and a TIE
    // sustains it (doc 04 §9; ADR-006 C12; doc 05 §6.4). Also latches currentSeqStep_ for
    // telemetry. noexcept, alloc-free, lock-free.
    void routeControlEvents(int eventCount) noexcept;

    // -----------------------------------------------------------------------------------
    // THE CONTROL-DISPATCH SEAM (ADR-028; task 160, the keystone). Read once per control
    // tick: decode BlockContext::params (the immutable, registry-indexed mw::ParamSnapshot)
    // into a per-voice VoiceControls and drive the active voice(s)' DSP setters. This is
    // the ONLY site that reads ctx.params — before this task the snapshot was never read,
    // so the synth ignored every knob. RT-safe: a POD read + arithmetic + setters; NO heap,
    // NO lock; noexcept. The VCO pitch is the ADR-005 count-domain CV (ControlCore authority)
    // for each voice's KeyAssigner-resolved active note, anchored into the VCO converter's CV
    // frame; tune/fine sum into pitch; range->footage(+software-ext octaves); pw->PWM CV;
    // sub.mode->sub shape; the source mixer levels feed Voice's saw+pulse+sub+noise sum.
    // STRUCTURAL params (quality/voice.mode/voice.count/unison.count/control.vintage) are
    // NOT read here — they are off-thread setters (ADR-028 item 4). Tasks 161 (VCF/Env/VCA)
    // and 162 (LFO/mod) EXTEND VoiceControls + this decode behind the same per-tick call.
    void applyParamSnapshot(const ParamSnapshot& snap, int chunkSamples) noexcept;

    // Decode the snapshot into the shared VCO/mixer/glide VoiceControls fields (the parts
    // that are the SAME for every active voice). Per-voice pitch (which depends on each
    // voice's resolved note) is filled in applyParamSnapshot. Pure arithmetic; noexcept.
    [[nodiscard]] VoiceControls decodeShared(const ParamSnapshot& snap) const noexcept;

    // Resolve the registry slot index for every dispatch-consumed parameter ID ONCE, off
    // the audio thread (prepare), so applyParamSnapshot never scans the 91-row registry on
    // the hot path. -1 means "not found" (defensive; a layout drift fails loudly elsewhere).
    void cacheParamSlots() noexcept;

    // Preallocated scratch (sized to maxBlockSize_ in prepare): the per-voice stereo
    // accumulation buffers and the mono voice-sum the FX chain consumes [§9 RT6].
    std::vector<float> mixL_{};
    std::vector<float> mixR_{};
    std::vector<float> mono_{};

    // Preallocated control-event scratch for the sequencer handoff (sized in prepare;
    // never grown on the hot path) [§9 RT6; doc 05 §10]. keyScratch_ collects the
    // chunk's MIDI notes as KeyEvents fed to the SequencerEngine; ctrlScratch_ receives
    // its emitted step/gate ControlEvents. Both are bounded by the worst-case event count.
    std::vector<control::KeyEvent>     keyScratch_{};
    std::vector<control::ControlEvent> ctrlScratch_{};

    // The MIDI note the sequencer/arp is currently sounding through the KeyAssigner
    // (-1 == none), so a step transition issues the right NoteOff(prev)/NoteOn(new). Set
    // on the single-threaded audio path only.
    int seqHeldNote_   = -1;
    // The live current sequencer step (the slot the last edge played), -1 if none.
    int currentSeqStep_ = -1;
    // The next slot the StepSequencer will play, mirrored from its deterministic
    // (playPos+1)%count advance so currentSeqStep_ is the REAL playhead slot, not a free
    // display counter (closes 111c). Re-synced to 0 on prepare/reset and on a
    // not-playing -> playing transition (the StepSequencer rewinds playPos to 0 there).
    int seqPlayMirror_ = 0;
    bool seqWasPlaying_ = false;

    // --- cached registry slot indices for the dispatch-consumed params (ADR-028; task
    // 160). Resolved once in prepare() (cacheParamSlots) so applyParamSnapshot reads the
    // snapshot by index with no per-tick string scan. ---
    struct ParamSlots {
        int vcoTune    = -1;
        int vcoFine    = -1;
        int vcoPw      = -1;
        int vcoRange   = -1;   // choice 0..5 -> footage(+ext octaves)
        int subMode    = -1;   // choice 0..2 -> SubShape
        int sawLevel   = -1;
        int pulseLevel = -1;
        int subLevel   = -1;
        int noiseLevel = -1;
        int glideTime  = -1;
        int glideMode  = -1;   // choice 0..2 -> GlideMode {Off,Auto,On}
    } slots_{};

    double sampleRate_      = 0.0;
    int    maxBlockSize_    = 0;
    int    maxVoices_       = 0;
    int    oversampleFactor_ = 1;
    bool   prepared_        = false;
};

} // namespace mw
