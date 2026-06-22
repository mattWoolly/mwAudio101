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

#include <array>
#include <cstdint>
#include <vector>

#include "BlockContext.h"
#include "params/ParamSnapshot.h"  // the registry-indexed normalized param POD the dispatch reads (ADR-028)

#include "voice/VoiceManager.h"   // owns the SOLE KeyAssigner (doc 04 §5.1/§9); GateTrigMode via VoiceTypes
#include "voice/Voice.h"          // VoiceControls — the ADR-028 dispatch payload (task 160)
#include "control/ControlCore.h"
#include "control/ControlTypes.h"     // KeyEvent / ControlEvent PODs (doc 05 §2.3)
#include "control/SequencerEngine.h"  // arp/seq/clock fixed-order state machine (task 087)
#include "state/Extras.h"             // the <extras> seq-pattern POD the load seam adopts (task 181)
#include "dsp/fx/FxChain.h"
#include "dsp/drift/DriftModel.h"     // the analog-character drift/vintage/variance engine (task 164)

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

    // Load an edited / preset sequencer pattern (the <extras> SeqStep buffer) into the
    // hosted StepSequencer so SequencerGrid edits + preset patterns reach the engine and
    // count_>0 (task 181; ADR-030 part 1 — closes break Q2's "pattern never reaches the
    // engine"). Converts the mw::state::Extras POD (note/gate/tie/rest) into the control
    // core's 6-bit SeqBuffer and forwards to SequencerEngine::loadPattern. RT-safe: a
    // bounded fixed-array transcode + a POD copy, no heap, no lock; noexcept. The load only
    // re-phases playback (StepSequencer::loadBuffer) when the incoming pattern actually
    // DIFFERS from the one already loaded, so the processor can call this every block (it
    // adopts the lock-free SPSC handoff each block) without rewinding the playhead.
    void loadSeqPattern(const mw::state::Extras& pattern) noexcept;

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
    // prepare()/reset() or the sequencer is not playing. This is the REAL playhead slot
    // read STRAIGHT from the StepSequencer (StepSequencer::currentSlot()), NOT a monotonic
    // display counter and NOT a reconstructed mirror — so it tracks the playhead exactly
    // even when a clock-reset-on-keypress rewinds the sequencer to slot 0 (the 118c
    // reconstructed-mirror divergence this closes). Closes the 111c/118c QA MEDIUM where
    // the published Snapshot.seqStep was a display counter rather than the live step; the
    // processor publishes THIS into Snapshot.seqStep (task 118d). A plain read of a member
    // written only on the single-threaded audio path; safe for tests.
    [[nodiscard]] int currentSeqStep() const noexcept { return currentSeqStep_; }

    // The dispatched/modulated VCF cutoff as a 0..1 display value (the mw101.vcf.cutoff pot
    // position the §1.2 filter tracks), for the §8.4 telemetry scope cutoff indicator. 0 until
    // the first param dispatch / after reset(). A plain read of a member written only on the
    // single-threaded audio path (task 118d); safe for tests [docs/design/10-ui.md §8.4].
    [[nodiscard]] float currentCutoffDisplay() const noexcept { return cutoffDisplay_; }

    // The LFO display phase: a deterministic fixed-point [0,2^32) accumulator advanced once
    // per control tick by the dispatched LFO rate, for the §8.4 telemetry mod-source
    // indicator. It ADVANCES whenever the LFO rate is non-zero (wrapping on uint32 overflow)
    // and is display-only — the per-voice audio LFOs own their own phase and are unchanged.
    // 0 until the first param dispatch / after reset(). A plain read of a member written only
    // on the single-threaded audio path (task 118d) [docs/design/10-ui.md §8.4].
    [[nodiscard]] std::uint32_t currentLfoPhase() const noexcept { return lfoPhase_; }

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
    // The analog-character orchestration engine (task 164; ADR-028 / docs/design/08). It owns
    // the per-voice DriftState + thermal OU integrators + de-zipper smoothers, advanced once
    // per control tick; its per-voice smoothed outputs (pitch/cutoff drift cents, var.pw, the
    // env/glide time scales) feed the analog-character fields of VoiceControls. Seeded off the
    // SAME kInstanceSeed as the voice pool so the personality is deterministic + reproducible.
    // GATED by mw101.vintage.enable: when off, the dispatch feeds identity (zero offset / unit
    // scale) and does NOT advance it, so the default render is bit-identical to pre-164.
    mw::dsp::drift::DriftModel drift_{};

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
    // NOT read here — they are off-thread setters (ADR-028 item 4). Task 161 EXTENDED this
    // (the VCF cutoff CV + resonance, the env A/D/S/R, the VCA level/mode) behind the same
    // per-tick call; task 162 (LFO/mod) extends it further the same way.
    void applyParamSnapshot(const ParamSnapshot& snap, int chunkSamples) noexcept;

    // THE SEQ/ARP CONTROL-TICK DISPATCH (task 181; ADR-030 part 1; the ADR-028 idiom
    // extended to the seq/arp params). Decode the seq.*/arp.* slots from the snapshot and
    // drive the hosted SequencerEngine + Arp + StepSequencer, so the subsystem stops
    // running on the INIT-default ControlSnapshot (arpHold=false, arpMode=Up, not playing).
    // Decodes:
    //   * arp.mode (direction) + arp.latch -> SequencerEngine::ControlParams.arpMode/arpHold;
    //   * arp/seq.tempo_sync + sync_div     -> the clock source (HostSync/Internal) + HostRate;
    //   * seq.mode { Off, Play, Record }     -> the StepSequencer PLAY / RECORD toggles.
    // A SINGLE shared clock drives arp + seq (the hardware has one clock); when seq.mode is
    // an active driver (Play/Record) the seq's tempo_sync/sync_div own the clock, otherwise
    // the arp's do. The internal-clock RATE authority + the Standalone free-run gate are
    // task 182 (ADR-030) — this leaves the live snapshot's internalRateHz untouched. Called
    // on the same per-control-tick path as applyParamSnapshot. RT-safe: a POD decode +
    // lock-free setters, no heap, no lock; noexcept [ADR-028; ADR-030 part 1].
    void dispatchSeqArp(const ParamSnapshot& snap) noexcept;

    // Decode the snapshot into the shared VCO/mixer/glide VoiceControls fields (the parts
    // that are the SAME for every active voice). Per-voice pitch (which depends on each
    // voice's resolved note) is filled in applyParamSnapshot. Pure arithmetic; noexcept.
    [[nodiscard]] VoiceControls decodeShared(const ParamSnapshot& snap) const noexcept;

    // Decode the analog-character group of the snapshot into the DriftModel's DriftParams POD
    // (task 164; ADR-028 / docs/design/08): drift.{depth,rate}, tune.slop, vintage.{cal_spread,
    // detune_amt}, var.{cutoff,env_time,pw,glide}, warmup.time. mw101.vintage.age is the host
    // Age MACRO — folded in here via VintageMacro::computeTargets so a high Age opens the drift
    // depth/rate + variance group (the macro the host bridge applies off-thread is replicated at
    // the seam so age alone perturbs the model). vintage.enable gates the whole group: when off
    // every field is the identity (zero / schema-min) so the DriftModel contributes nothing.
    // Pure arithmetic; noexcept. The std::pow inside the Age curve is control-rate only.
    [[nodiscard]] mw::dsp::drift::DriftParams decodeDriftParams(const ParamSnapshot& snap)
        const noexcept;

    // -----------------------------------------------------------------------------------
    // THE FX-PARAM DISPATCH (task 163; ADR-028 item 5). Decode the FX range of the
    // snapshot (fx.bypass + drive/chorus/delay enables+params + out.mono) into a single
    // decoded fx::FxParams and publish it via fx_.setParams() ONCE per block, at the §4.1
    // FX site in renderChunk — a SEPARATE site from the per-voice applyParamSnapshot (the
    // FX run once on the mono voice sum, not per voice). hostBpm comes from the block's
    // transport so the Delay tempo-sync conversion tracks the host. Pure POD read +
    // arithmetic; the std::pow log-maps (delay ms / chorus Hz) run at this per-block
    // control rate only, never on the per-sample audio path. noexcept, alloc-free,
    // lock-free [ADR-028 item 5; docs/design/07 §3.1/§7].
    [[nodiscard]] fx::FxParams decodeFxParams(const ParamSnapshot& snap,
                                              double hostBpm) const noexcept;

    // Stage the decoded (note-independent) ADSR onto EVERY pool voice BEFORE the control tick
    // fires any note-on this chunk (task 161). The envelope latches its active-stage one-pole
    // coefficient at the trigger edge, so the A/D/S/R must be in place before the tick triggers
    // a voice — otherwise the Attack runs the prepared default (the attack-time knob would have
    // no effect). One shared ADSR per voice (note-independent), so the same EnvParams stages to
    // all; the per-voice pitch/cutoff still apply after the tick. Bounded O(kMaxVoices), pure
    // POD copy; noexcept, alloc-free, lock-free [ADR-028; docs/design/03 §2.5].
    void stageEnvParams(const ParamSnapshot& snap) noexcept;

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
    // The pattern most recently loaded into the StepSequencer through loadSeqPattern (task
    // 181). The processor adopts the lock-free SPSC seq-pattern handoff every block and
    // forwards it here; we only re-load (and thus re-phase playback) when the incoming POD
    // DIFFERS from this shadow, so a steady pattern does not rewind the playhead every
    // block. A by-value POD copy written only on the single-threaded audio path; no alloc,
    // no lock. seqPatternLoaded_ guards the first load (a default Extras == empty pattern is
    // a legitimate value, so a bool flag, not an all-zero compare, decides the first load).
    mw::state::Extras lastLoadedSeqPattern_{};
    bool              seqPatternLoaded_ = false;
    // The live current sequencer step (the slot the last edge played), -1 if none. Latched
    // from the StepSequencer's OWN currentSlot() (the real playhead) each seq edge, NOT a
    // reconstructed mirror — so it tracks a clock-reset-on-keypress rewind exactly (task 118d
    // closes the 111c/118c QA MEDIUM) [docs/design/05 §6.3].
    int currentSeqStep_ = -1;
    // --- audio->GUI telemetry display latches (task 118d) ---------------------------------
    // The dispatched/modulated cutoff as a 0..1 display value + a deterministic fixed-point
    // LFO display phase, latched once per control tick in applyParamSnapshot from the SAME
    // decoded values the per-voice dispatch uses, surfaced to the processor for the §8.4
    // telemetry publish. Display-only; the audio path is unchanged. Plain members written
    // only on the single-threaded audio path; no alloc, no lock [docs/design/10-ui.md §8.4].
    float         cutoffDisplay_ = 0.0f;
    std::uint32_t lfoPhase_      = 0;

    // Per-voice note-serial shadow for the analog-character note-on draw (task 164). The
    // VoiceManager stamps a fresh monotonic noteSerial on every allocation; when a voice's
    // live serial differs from the value last seen here, that voice was freshly triggered, so
    // the dispatch fires DriftModel::noteOn(voiceIndex) ONCE to freeze its Tier-3 slop + the
    // four variance spreads for the note. Reset to a sentinel on prepare/reset. A plain array
    // of integers written only on the single-threaded audio path; no alloc, no lock.
    std::array<std::uint64_t, mw::kMaxVoices> lastDriftSerial_{};

    // --- cached registry slot indices for the dispatch-consumed params (ADR-028; task
    // 160). Resolved once in prepare() (cacheParamSlots) so applyParamSnapshot reads the
    // snapshot by index with no per-tick string scan. ---
    struct ParamSlots {
        int vcoTune    = -1;
        int vcoFine    = -1;
        int vcoPw      = -1;
        int vcoPwmDepth = -1;  // 0..1 -> MANUAL static PWM depth (task 162e; distinct from lfo.depth_pwm)
        int vcoRange   = -1;   // choice 0..5 -> footage(+ext octaves)
        int subMode    = -1;   // choice 0..2 -> SubShape
        int sawLevel   = -1;
        int pulseLevel = -1;
        int subLevel   = -1;
        int noiseLevel = -1;
        int glideTime  = -1;
        int glideMode  = -1;   // choice 0..2 -> GlideMode {Off,Auto,On}

        // --- VCF / Env / VCA (task 161) ---
        int vcfCutoff    = -1;   // 0..1 -> cutoff CV volts (skew kCutoff)
        int vcfResonance = -1;   // 0..1 -> setResonance (self-osc at 1)
        int vcfEnvMod    = -1;   // 0..1 -> ENV->cutoff depth (octaves)
        int vcfLfoMod    = -1;   // 0..1 -> VCF-panel LFO->cutoff amount (task 162e; distinct from lfo.depth_cutoff)
        int vcfKbdTrack  = -1;   // 0..1 -> keyboard-track depth (1 V/oct at 1)
        int envAttack    = -1;   // 0..1 -> seconds (skew kEnvTime)
        int envDecay     = -1;   // 0..1 -> seconds (skew kEnvTime)
        int envSustain   = -1;   // 0..1 -> sustain level (linear)
        int envRelease   = -1;   // 0..1 -> seconds (skew kEnvTime)
        int vcaLevel     = -1;   // 0..1 -> output level (linear)
        int vcaMode      = -1;   // choice 0..1 -> VcaMode {Env,Gate}

        // --- LFO + modulation routing (task 162) ---
        int lfoRate        = -1;   // 0.1..30 Hz (skew kLfoRate)
        int lfoShape       = -1;   // choice 0..4 -> LfoShape (Sine -> SmoothTri)
        int lfoDest        = -1;   // choice 0..2 -> {Pitch,Filter,PWM}
        int lfoDelay       = -1;   // 0..1 -> fade-in seconds (skew kLfoDelay)
        int lfoDepthPitch  = -1;   // 0..1 -> vibrato depth
        int lfoDepthPwm    = -1;   // 0..1 -> PWM-sweep depth
        int lfoDepthCutoff = -1;   // 0..1 -> filter-wobble depth
        int modLfoModWheel = -1;   // 0..1 -> mod-wheel->LFO depth routing (no live wheel)
        int modBendDest    = -1;   // choice 0..2 -> {VCO,VCF,Both}
        int modBendRangeVco = -1;  // 0..1200 cents
        int modBendRangeVcf = -1;  // 0..1200 cents
        int velEnable      = -1;   // bool -> velocity sensing on/off
        int velDepth       = -1;   // 0..1 -> velocity depth

        // --- FX param dispatch (task 163; ADR-028 item 5) — decoded into FxParams and
        // applied via fx_.setParams() at the §4.1 FX site, a SEPARATE site from the
        // per-voice applyParamSnapshot. ---
        int fxBypass        = -1;  // bool -> FxParams.masterBypass
        int fxDriveEnable   = -1;  // bool -> drive.on
        int fxDriveAmount   = -1;  // 0..1 -> drive.amount
        int fxDriveTone     = -1;  // 0..1 -> drive.tone
        int fxDriveOutput   = -1;  // 0..1 -> drive.output
        int fxChorusEnable  = -1;  // bool -> (with mode) chorus on/off
        int fxChorusMode    = -1;  // choice 0..3 -> chorus.mode (Off/I/II/I+II)
        int fxChorusRate    = -1;  // 0..1 -> chorus.rate Hz override (log)
        int fxChorusDepth   = -1;  // 0..1 -> chorus.depth
        int fxChorusWidth   = -1;  // 0..1 -> chorus.width
        int fxChorusMix     = -1;  // 0..1 -> chorus.mix
        int fxDelayEnable   = -1;  // bool -> delay.on
        int fxDelaySync     = -1;  // bool -> delay.sync
        int fxDelayDivision = -1;  // choice 0..5 -> delay.division
        int fxDelayTime     = -1;  // 0..1 (skew) -> delay.timeMs (free ms, log)
        int fxDelayFeedback = -1;  // 0..0.95 -> delay.feedback
        int fxDelayDamp     = -1;  // 0..1 -> delay.damp
        int fxDelayWidth    = -1;  // 0..1 -> delay.width
        int fxDelayMix      = -1;  // 0..1 -> delay.mix
        int fxDelayPingpong = -1;  // bool -> delay.pingpong
        int outMono         = -1;  // bool -> FxParams.monoOutput

        // --- analog character / tuning / expression / MPE (task 164) ---
        int vintageEnable    = -1;  // bool  -> the master gate for the whole character group
        int vintageAge       = -1;  // 0..1  -> Age macro (folded into drift depth/rate + var)
        int vintageCalSpread = -1;  // 0..1  -> Tier-1 cal spread width
        int vintageDetuneAmt = -1;  // 0..1  -> per-voice (unison/poly) detune spread scale
        int driftDepth       = -1;  // 0..50 cents -> thermal->pitch drift depth
        int driftRate        = -1;  // 0.01..1 Hz (skew) -> OU drift rate
        int tuneA4           = -1;  // 400..460 Hz -> A4 pitch reference (442 == hardware home)
        int tuneSlop         = -1;  // 0..20 cents -> Tier-3 per-note tuning slop
        int warmupTime       = -1;  // 0..30 min -> warm-up transient (gated by useWarmup)
        int varCutoff        = -1;  // 0..1  -> per-voice cutoff variance
        int varEnvTime       = -1;  // 0..1  -> per-voice env-time variance (multiplier)
        int varPw            = -1;  // 0..1  -> per-voice PW variance (additive duty)
        int varGlide         = -1;  // 0..1  -> per-voice glide-time variance (multiplier)
        int ampExpression    = -1;  // 0..1  -> CC11 VCA output scaler (1 == unity)
        int mpeEnable        = -1;  // bool  -> MPE routing enable (decode; inert without ingress)
        int mpeBendRange     = -1;  // 0..96 semitones -> MPE per-note bend range
        int mpePressureDest  = -1;  // choice 0..2 -> {VCF,VCA,PW} pressure destination

        // --- seq / arp control-tick dispatch (task 181; ADR-030 part 1) — decoded into a
        // SequencerEngine::ControlParams + the StepSequencer PLAY/RECORD toggles, applied
        // via dispatchSeqArp(). Before this task these reached zero production code. ---
        int arpMode      = -1;  // choice 0..3 { Off, Up, Down, Up-Down } -> ArpMode + engaged gate
        int arpTempoSync = -1;  // bool  -> HostSync (on) vs Internal (off) clock source
        int arpSyncDiv   = -1;  // choice 0..5 (kSyncDiv) -> HostRate
        int arpLatch     = -1;  // bool  -> ControlSnapshot.arpHold
        int seqMode      = -1;  // choice 0..2 { Off, Play, Record } -> setSeqPlay / setSeqRecord
        int seqTempoSync = -1;  // bool  -> HostSync (on) vs Internal (off) clock source
        int seqSyncDiv   = -1;  // choice 0..5 (kSyncDiv) -> HostRate
    } slots_{};

    // --- live continuous-controller state (task 162c; ADR-028 control-dispatch repair;
    // bend authority reconciled in task 162d). The 162 dispatch WIRED pitch-bend->{VCO,VCF}
    // and mod-wheel->LFO-depth, but the live controller POSITION never reached the engine.
    // This running state carries the latest bend + mod-wheel position applyParamSnapshot reads
    // each control tick so the 162 bend/wheel legs ACTIVATE. Their ingress differs:
    //   * pitchBend_ — the centered [-1,+1] WHEEL unit, seeded by process() from
    //     BlockContext::controllers.pitchBend. It is NOT re-read from the per-chunk PitchBend
    //     MidiEvent: that event carries the §4.4 bend-range-scaled SEMITONE offset (the Pre-Q
    //     tuning path), not a unit, so re-reading it mis-scaled the wheel (task 162d fix).
    //   * modWheel_ — the [0,1] CC1 position, seeded by process() from
    //     BlockContext::controllers.modWheel and updated per-chunk from the CC1 MidiEvent.
    // Neutral defaults (centered bend / wheel down) are the no-controller identity. Written
    // only on the single-threaded audio path; no alloc, no lock — a plain pair of floats. ---
    float pitchBend_ = 0.0f;   // [-1,+1]; 0 == centered (neutral)
    float modWheel_  = 0.0f;   // [0,1];   0 == wheel down (neutral)

    double sampleRate_      = 0.0;
    int    maxBlockSize_    = 0;
    int    maxVoices_       = 0;
    int    oversampleFactor_ = 1;
    bool   prepared_        = false;
};

} // namespace mw
