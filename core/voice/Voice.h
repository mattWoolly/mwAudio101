// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/Voice.h — the per-voice circuit-accurate signal-path assembly plus the
// inline per-voice drift block, the note lifecycle, and the state-machine render
// contract (task 073).
//
// Single source of truth: docs/design/04-voice-and-control.md §4.1-§4.4, under
// ADR-006 §Decision item 1 / C14 / C15 / C18 and ADR-019 VT-01.
//
// WHAT THIS OWNS (§4.1): a Voice is the SH-101 signal path ONLY plus an inline
// per-voice drift block. It has NO knowledge of polyphony or note priority — it
// consumes a resolved note, a gate, a retrigger flag, and a glide target, and renders
// audio. Per ADR-006 §Decision item 1 each Voice owns EXACTLY ONE ADSR and ONE LFO;
// the modulation section is per-voice, never globalized [research/04 §2.1, §3.1].
//
// OUT OF SCOPE (other modules/docs own these; this file only assembles + drives):
//   - the internal DSP of the VCO/sub/noise (doc 01), VCF (doc 02), VCA/ADSR/LFO
//     (doc 03), and Glide (§5.5) — their own modules;
//   - the drift DSP law / walk coefficients / vintage.age scaling (ADR-009) — only
//     the seed derivation + where the drift state lives is owned here (§4.4);
//   - polyphony, note priority, unison fan-out (VoiceManager, §6);
//   - 6-bit pitch quantization (ControlCore, §7) — Voice consumes a glide target in Hz.
//
// REALIZED-TYPE NOTE: the §4.2 sketch names `XorShift32`, `dsp::OnePoleSmoother`, and
// `hashCombine`. The realized canonical types this project built earlier are
// mw::dsp::drift::Xorshift128p (the deterministic drift PRNG, task 063),
// mw::params::OnePoleSmoother (the single canonical de-zipper, task 008), and the
// hashCombine mixer in core/calibration/VoiceDriftConstants.h. This file consumes
// those existing declarations rather than re-introducing a parallel flavor (the same
// pattern core/dsp/drift/DriftState.h and core/dsp/VcaGate.h use).
//
// RT invariants [ADR-001; ADR-019 VT-01; ADR-006 §4, C17]: Voice is a flat value type
// (no virtual dispatch in the inner loop); render/noteOn/noteOff/beginSteal are
// noexcept, allocation-free, lock-free; an Idle voice costs nothing; ALL sizing
// happens in prepare() off the audio thread (the OscillatorSection minBLEP ring and
// the filter tables are pre-sized there).

#pragma once

#include <cstdint>

#include "VoiceTypes.h"
#include "Glide.h"

#include "dsp/Envelope.h"
#include "dsp/Lfo.h"
#include "dsp/OscillatorSection.h"
#include "dsp/LadderFilter.h"
#include "dsp/Vca.h"
#include "dsp/VcaGate.h"
#include "dsp/MinBlepTable.h"
#include "dsp/drift/Xorshift128p.h"

#include "params/Smoother.h"

#include "calibration/VoiceDriftConstants.h"

namespace mw {

// ---------------------------------------------------------------------------
// VoiceControls — the per-voice control-apply payload of the ADR-028 dispatch seam
// (task 160). The Engine decodes the immutable mw::ParamSnapshot once per control tick
// into this POD and hands it to Voice::applyControls, which drives the per-voice DSP
// setters. This is the SEAM the rest of the cluster extends WITHOUT re-architecting:
// task 160 fills the VCO + source-mixer fields below; task 161 (VCF/Env/VCA) and task
// 162 (LFO/modulation) ADD their own fields + apply them in applyControls behind the
// same once-per-tick call. RT-safe: a flat POD of floats/enums, no heap, no JUCE.
//
// VCO PITCH AUTHORITY (ADR-005 / ADR-028 item 3): targetPitchCvVolts is the count-domain
// CV (assemblePitchCounts -> blendedPitchVolts), anchored into the VCO converter's CV
// frame (ControlDispatchConstants.h), for the KeyAssigner-resolved active note. The Voice
// SLEWS toward it through its single Glide (the count-domain glide reconciliation, below),
// then feeds the slewed CV to the oscillator — so different notes render different
// pitches at 1 V/octave and portamento is applied EXACTLY ONCE.
struct VoiceControls {
    // --- VCO (task 160) ---
    float                          targetPitchCvVolts = mw::cal::vco::kPitchRefVolts;
    mw101::dsp::Footage            footage   = mw101::dsp::Footage::Eight;
    float                          pwmCvNorm = 0.0f;   // 0..1 pulse-width CV (0 => square)
    mw101::dsp::SubShape           subShape  = mw101::dsp::SubShape::OctDownSquare;

    // --- source mixer (§4.1; task 160) ---  saw+pulse+sub+noise summed by these levels.
    float sawLevel   = 0.0f;
    float pulseLevel = 0.0f;
    float subLevel   = 0.0f;
    float noiseLevel = 0.0f;

    // --- glide / portamento (§5.5; task 160 reconciliation) — the single per-voice Glide
    // owns the count-domain pitch slew; these come from mw101.glide.{mode,time}. ---
    GlideMode glideMode    = GlideMode::Off;
    float     glideSeconds = 0.0f;

    // --- VCF (task 161) — the filter responds to its params + the env/keyboard modulation
    // of cutoff. cutoffBaseCvVolts is the base cutoff CV (mw101.vcf.cutoff mapped to volts,
    // ControlDispatchVcfConstants.h); the SUMMED cutoff CV applied to the filter is
    //   cutoffBaseCvVolts + envModOctaves * envLevel + kbdTrackCvVolts
    // assembled inside applyControls from THIS voice's live env level + resolved note (§1.2
    // "CUTOFF knob + keyboard x KeyFollow + ENV x EnvDepth"). resonance01 -> setResonance
    // (self-oscillates at 1); envModOctaves is env_mod x kEnvModOctaves; kbdTrackCvVolts is
    // the precomputed kbd_track x note-delta CV (note-dependent, filled by the Engine). ---
    float cutoffBaseCvVolts = 0.0f;
    float resonance01       = 0.0f;
    float envModOctaves     = 0.0f;   // env_mod depth already scaled to octaves of CV
    float kbdTrackCvVolts   = 0.0f;   // kbd_track x (note - ref) CV, note-dependent

    // --- Envelope (task 161) — mw101.env.{attack,decay,sustain,release} -> env_.setParams
    // each control tick (calibrated times). A/D/R are seconds; sustain is a [0,1] level. ---
    float envAttackSec  = 0.003f;
    float envDecaySec   = 0.060f;
    float envSustain    = 0.7f;
    float envReleaseSec = 0.100f;

    // --- VCA (task 161) — mw101.vca.{level,mode}. vcaLevel scales the voice output
    // amplitude; vcaMode selects the ENV-shaped vs flat-GATE amplitude source (§4.4). ---
    float                vcaLevel = 0.8f;
    mw101::dsp::VcaMode  vcaMode  = mw101::dsp::VcaMode::Env;

    // --- LFO + modulation routing (task 162) — the MOD section finally modulates its
    // destination + velocity opens the VCA/VCF. The single per-voice LFO is configured
    // here (rate/shape) and ADVANCED at the control-tick cadence in applyControls (the
    // same reconciliation the glide uses: the old per-sample lfo_.tick() in render() is
    // REMOVED so the LFO is sampled exactly once per tick and routed). Its bipolar [-1,1]
    // output is scaled by the per-dest depth and SUMMED into the existing 160/161 CVs:
    //   dest=Pitch  -> + lfoOut * lfoPitchDepthVolts   into the pitch CV (vibrato)
    //   dest=Filter -> + lfoOut * lfoCutoffDepthOct     into the cutoff CV (wobble)
    //   dest=Pwm    -> + lfoOut * lfoPwmDepthNorm       into the PWM CV (PWM sweep)
    // lfoDelaySec fades the depth in over time from the keypress; the effective depths
    // already fold in the mod.lfo_mod_wheel routing param (the live wheel POSITION is not
    // in the core seam — see Engine.cpp). ---
    float                lfoRateHz       = 5.0f;
    mw101::dsp::LfoShape lfoShape        = mw101::dsp::LfoShape::SmoothTri;
    int                  lfoDest         = 0;     // 0 Pitch / 1 Filter / 2 PWM (decoded enum)
    float                lfoPitchDepthVolts = 0.0f;  // peak pitch-CV swing at this depth (volts)
    float                lfoCutoffDepthOct  = 0.0f;  // peak cutoff-CV swing at this depth (octaves)
    float                lfoPwmDepthNorm    = 0.0f;  // peak PWM-CV swing at this depth (norm)
    float                lfoDelaySec        = 0.0f;  // LFO depth fade-in time from keypress (s)

    // --- velocity routing (task 162) — mw101.vel.{enable,depth}. The per-note velocity is
    // held in the Voice (from noteOn). velVcaDepth folds velocity into the VCA control so a
    // soft key plays quieter; velCutoffVolts is the per-voice velocity->cutoff CV term summed
    // into the cutoff CV (a hard key opens the filter). Both are ZERO when vel.enable is off
    // (the Engine zeroes them), so velocity has no effect with sensing disabled. ---
    float                velVcaDepth    = 0.0f;   // 0 => velocity does not scale the VCA
    float                velCutoffVolts = 0.0f;   // per-voice velocity->cutoff CV (volts)

    // --- pitch-bend routing (task 162) — mw101.mod.{bend_dest,bend_range_vco,bend_range_vcf}.
    // The bend RANGE (cents) per destination, decoded from the params; the live bend WHEEL
    // value is not present in the core seam (BlockContext carries no continuous-controller
    // state — see Engine.cpp), so these carry the decoded range and the resolved bend offset
    // is bendWheel x range. With no wheel the offsets are zero (no audible bend) but the
    // routing math + range decode are in place for when the controller seam lands. ---
    float                bendVcoVolts   = 0.0f;   // resolved pitch-bend -> VCO CV (volts)
    float                bendVcfVolts   = 0.0f;   // resolved pitch-bend -> VCF cutoff CV (volts)
};

// Inline per-voice drift state [ADR-006 §Decision item 1; docs/design/04 §4.2/§4.4].
// This file owns ONLY the seed derivation and where the state lives; the drift DSP law
// (walk coefficients, depth scaling, vintage.age) is ADR-009's (DriftConstants.h).
struct VoiceDrift {
    std::uint64_t seed = 0;                      // = seedFromInstance(instanceSeed, voiceIndex) (ADR-009 VV-17; §8.2)
    mw::dsp::drift::Xorshift128p rng;            // deterministic PRNG; never wall-clock [C18]
    mw::params::OnePoleSmoother tuneWalk;        // slow random-walk -> tuning drift
    mw::params::OnePoleSmoother pwWalk;          // -> pulse-width drift
    mw::params::OnePoleSmoother cutoffWalk;      // -> cutoff drift
    // DSP law (random-walk coefficients, depth scaling) is owned by ADR-009.
};

class Voice {
public:
    Voice() noexcept = default;

    // Off-the-audio-thread setup; the ONLY place allocation may happen (the section's
    // minBLEP ring + the filter tables are pre-sized here) [ADR-006 §4, C17; ADR-001
    // C2]. oversampleFactor is the per-voice 2x-oversampled-zone ratio (doc 02 /
    // ADR-003). The drift seed is derived deterministically from voiceIndex +
    // instanceSeed (§4.4) — never from wall-clock, so renders are byte-stable.
    void prepare(double sampleRate, int oversampleFactor,
                 int voiceIndex, std::uint32_t instanceSeed) noexcept;

    // Note lifecycle. retrigger=true fires the ADSR from its trigger state. velocity is
    // RECORDED here (task 162): the dispatch routes it to VCA/VCF per mw101.vel.{enable,
    // depth} (before 162 it was discarded). Exposed via currentVelocity() so the Engine
    // builds the per-voice velocity term in the seam.
    void noteOn(int midiNote, float velocity, bool retrigger) noexcept;
    void noteOff() noexcept;                          // gate de-assert -> release
    void setGlideTarget(float targetPitchHz) noexcept; // glide handled per-voice (§5.5)

    // Apply one control-tick's decoded controls to this voice's DSP (the ADR-028 dispatch
    // seam, task 160). Sets the glide TARGET to the count-domain pitch CV (volts) and
    // ADVANCES the single per-voice Glide by `advanceSamples` (the samples elapsed since the
    // last dispatch, i.e. this chunk's length) so the portamento slews at the correct
    // PER-SAMPLE rate even though the oscillator pitch CV is pushed once per chunk — the
    // count-domain glide reconciliation: glide is applied EXACTLY ONCE here (the discarded
    // per-sample glide_.nextValue() in render() is removed). The slewed CV + footage + PWM +
    // sub-shape go to the oscillator section via osc_.setControls; the source-mixer levels
    // are cached for render(). Per CHUNK (not per sample): a bounded advanceSamples-step
    // glide loop + one osc_.setControls re-derive, pure arithmetic. noexcept, alloc-free,
    // lock-free [ADR-028 items 1-2; ADR-005; ADR-001 §9]. quality is structural/off-thread,
    // so the AA tier is carried separately via setQualityAaMode().
    void applyControls(const VoiceControls& c, int advanceSamples) noexcept;

    // Stage the dispatched ADSR for the NEXT note-on (task 161). Called by the Engine BEFORE
    // the control tick fires any trigger, so a voice triggered this chunk latches the correct
    // attack coefficient at its edge (the env copies its active-stage coefficient at the
    // trigger; pushing the times only after would leave the Attack at the prepared default).
    // For an ALREADY-sounding voice this also applies the times live (e.g. a changed release
    // is in effect before the next gate-off). A plain POD copy + one setParams; noexcept,
    // alloc-free, lock-free [ADR-028; docs/design/03 §2.5].
    void stageEnvParams(const mw101::dsp::EnvParams& ep) noexcept;

    // Structural AA tier (mw101.quality), set off the audio thread / at a block boundary —
    // NOT per control tick [ADR-018 Q5; ADR-028 item 4 structural params off-thread]. The
    // dispatch caches it; applyControls folds it into the section Controls.aaMode.
    void setQualityAaMode(mw101::dsp::OscAaMode m) noexcept { qualityAaMode_ = m; }

    // Unison voices are configured once per note via these (§5.3).
    void setDetuneCents(float cents) noexcept;
    void setStereoPan(float pan) noexcept;            // -1..+1

    // Render `numSamples` of this voice into outL/outR, ACCUMULATING. Idle costs
    // nothing; Releasing finishes its tail then goes Idle; Stealing fades via
    // stealGain_ then Idle (§4.3). noexcept, alloc-free, lock-free.
    void render(float* outL, float* outR, int numSamples) noexcept;

    // Forced fast fade for a poly steal (§6.4); flips state to Stealing.
    void beginSteal() noexcept;

    // The VoiceManager stamps the monotonic note-serial on allocation (§4.2; C14).
    void setNoteSerial(std::uint64_t serial) noexcept { noteSerial_ = serial; }

    [[nodiscard]] VoiceState state() const noexcept { return state_; }
    [[nodiscard]] bool       isActive() const noexcept { return state_ != VoiceState::Idle; }
    [[nodiscard]] int        currentNote() const noexcept { return currentNote_; }
    [[nodiscard]] float      currentVelocity() const noexcept { return velocity_; }  // [0,1] (task 162)
    [[nodiscard]] float      currentLevel() const noexcept;       // VCA/env level for quietest-steal [C14]
    [[nodiscard]] std::uint64_t noteSerial() const noexcept { return noteSerial_; } // steal ordering [C14]

    // Per-voice deterministic drift seed (§4.4). Exposed for the determinism tests
    // and for the drift DSP (ADR-009) to consume.
    [[nodiscard]] std::uint64_t driftSeed() const noexcept { return drift_.seed; }

private:
    // --- modulation section: ONE each per Voice [research/04 §2.1, §3.1] ---
    mw101::dsp::Envelope env_;       // single shared ADSR (-> VCF, VCA, PW)
    mw101::dsp::Lfo      lfo_;       // single LFO

    // --- signal path (defined in their own design docs) ---
    mw101::dsp::OscillatorSection osc_;   // CEM3340 VCO + sub + noise (doc 01)
    mw::dsp::LadderFilter         vcf_;   // IR3109 ladder, oversampled (doc 02, ADR-003)
    mw101::dsp::Vca               vca_;   // BA662A OTA
    mw101::dsp::VcaGate           vcaGate_; // anti-thump click-safe gate fade
    Glide                         glide_; // per-voice portamento (§5.5)

    // --- shared read-only minBLEP table, built once in prepare (owned here) ---
    mw101::dsp::MinBlepTable hqTable_;

    VoiceDrift drift_;  // inline drift state (§4.4)

    VoiceState    state_       = VoiceState::Idle;
    int           currentNote_ = -1;
    std::uint64_t noteSerial_  = 0;     // set by VoiceManager on allocation [C14]
    float         stealGain_   = 1.0f;  // fast-fade ramp during Stealing [§6.4]
    float         stealStep_   = 0.0f;  // per-sample steal-fade decrement (precomputed)
    double        sampleRate_  = 48000.0;
    int           oversample_  = 2;
    float         detuneCents_ = 0.0f;
    float         panGainL_    = 1.0f;  // equal-power pan gains (precomputed on setPan)
    float         panGainR_    = 1.0f;

    // --- source-mixer levels (§4.1; task 160) — cached from the dispatch, summed in
    // render(): out = saw*sawLevel + pulse*pulseLevel + sub*subLevel + noise*noiseLevel.
    // Default: the saw-only INIT mix (saw at its registry default 0.8, others 0), so an
    // engine that never dispatches still sounds the historical fixed saw. ---
    float sawLevel_   = 0.8f;
    float pulseLevel_ = 0.0f;
    float subLevel_   = 0.0f;
    float noiseLevel_ = 0.0f;

    // --- VCA output level (§4.1; task 161) — mw101.vca.level, cached from the dispatch and
    // applied as a clean linear post-VCA amplitude scale in render() (the channel fader; the
    // OTA taper inside Vca::process is driven by the ENV/GATE amplitude SOURCE, not the
    // level, so the contour shape stays intact). Default: the registry default 0.8. ---
    float vcaLevel_   = 0.8f;

    // --- velocity->VCA scale (task 162) — cached from the dispatch, applied as a clean
    // linear amplitude scale in render() alongside vcaLevel_. 1.0 == no velocity effect
    // (vel.enable off, or vel.depth 0); a softer key yields a smaller scale when enabled.
    // Folded as a post-VCA gain (the §4.1/§5 "velocity folded into the amplitude" routing),
    // NOT into the OTA control taper, so it does not reshape the gate/anti-thump contour. ---
    float vcaVelScale_ = 1.0f;

    // Structural AA tier (mw101.quality), set off the audio thread; folded into the
    // oscillator-section Controls each applyControls [ADR-018 Q5].
    mw101::dsp::OscAaMode qualityAaMode_ = mw101::dsp::OscAaMode::PolyBlep;

    // Glide-domain note-on bookkeeping (task 160): the pitch CV glide lives in the volts
    // domain in applyControls, so noteOn records the legato flag for the first post-keypress
    // tick (snap-vs-slew) instead of seeding the old Hz glide. freshNoteOn_ is consumed once.
    bool pendingNoteOnLegato_ = false;
    bool freshNoteOn_         = false;

    // --- velocity (task 162) — the per-note velocity [0,1] recorded at noteOn. The dispatch
    // reads it (currentVelocity) and builds the VCA/VCF velocity terms; the Voice itself only
    // STORES it (routing depth is the Engine's, so vel.enable/depth live in one place). ---
    float velocity_ = 1.0f;

    // --- LFO modulation (task 162) — the single per-voice LFO is now ROUTED. Its bipolar
    // output is sampled once per control tick (advanceSamples steps) in applyControls and
    // summed into the pitch/cutoff/PWM CVs per the dispatched dest+depth. The per-sample
    // lfo_.tick() in render() was the LFO analogue of the discarded-glide bug (the LFO ticked
    // but modulated nothing) — it is REMOVED so the LFO advances exactly once per tick here.
    //
    // lfoDelayElapsedSamples_ counts samples since the keypress so the LFO depth fades in over
    // mw101.lfo.delay (a per-voice ramp 0->1); reset on a fresh note-from-idle. The cached
    // depth fields hold this tick's effective (post-delay, post-dest) routing so render() does
    // not re-derive them — applyControls writes them, render() reads them. ---
    std::uint32_t lfoDelayElapsedSamples_ = 0;

    // Latest dispatched ADSR (task 161). applyControls caches the decoded EnvParams here each
    // control tick; noteOn applies them to env_ BEFORE firing the attack so startAttack()
    // latches the CURRENT attack coefficient (the env's live stage coeff is set at the trigger
    // edge — pushing setParams only AFTER the trigger would leave the Attack running the stale
    // prepared default). hasPendingEnv_ guards the pre-first-dispatch note (keeps the INIT
    // defaults until the seam supplies params). A plain POD copy; noexcept, alloc-free.
    mw101::dsp::EnvParams pendingEnv_{};
    bool                  hasPendingEnv_ = false;
};

} // namespace mw
