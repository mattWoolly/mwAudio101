// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/Voice.cpp — implementation of the per-voice signal-path assembly, the
// note lifecycle, and the state-machine render contract (task 073).
//
// Realizes docs/design/04-voice-and-control.md §4.1-§4.4 under ADR-006 §Decision
// item 1 / C14 / C15 / C18 and ADR-019 VT-01. No (PI) literal is inlined here: the
// drift-seed mixer and the steal-fade length live in
// core/calibration/VoiceDriftConstants.h [docs/design/00 §1.2; ADR-020 S13].
//
// This file ASSEMBLES and DRIVES the per-voice DSP blocks (their internal algorithms
// are owned by their own modules) and implements the lifecycle/state machine. It
// pre-sizes everything in prepare(); the audio-thread methods only activate/idle the
// already-allocated voice and never touch the heap or a lock [ADR-006 §4, C17].

#include "voice/Voice.h"

#include <algorithm>
#include <cmath>

#include "calibration/EnvLfoVcaConstants.h"
#include "calibration/VoiceDriftConstants.h"

namespace mw {

namespace {

// Equal-power pan: pan in [-1,+1] -> (gainL, gainR). pan = -1 => hard left.
inline void equalPowerPan(float pan, float& gainL, float& gainR) noexcept {
    const float clamped = std::clamp(pan, -1.0f, 1.0f);
    // Map [-1,+1] -> [0, pi/2]; cos/sin give the constant-power taper.
    const float theta = (clamped * 0.5f + 0.5f) * 1.57079632679489661923f; // *pi/2
    gainL = std::cos(theta);
    gainR = std::sin(theta);
}

} // namespace

void Voice::prepare(double sampleRate, int oversampleFactor,
                    int voiceIndex, std::uint32_t instanceSeed) noexcept {
    sampleRate_ = sampleRate;
    oversample_ = std::max(1, oversampleFactor);

    // --- Per-voice deterministic drift seed: the CANONICAL cross-module derivation
    // seedFromInstance = splitmix64(instanceSeed ^ goldenMix(voiceIndex)) [ADR-009 VV-17;
    // docs/design/08 §8.2]. This reconciles §4.4's illustrative hashCombine/uint32 to the
    // seeding the drift DSP (DriftState, task 063/064/065) actually uses, so this voice's
    // drift PRNG stream is bit-identical to the drift module's for the same (instanceSeed,
    // voiceIndex). Derived ONLY from voiceIndex + instanceSeed (never wall-clock). ---
    drift_.seed = mw::dsp::drift::seedFromInstance(static_cast<std::uint64_t>(instanceSeed), voiceIndex);
    drift_.rng.seed(drift_.seed);

    // The drift walk smoothers exist as wired one-poles; their walk COEFFICIENTS /
    // depth scaling are ADR-009's (out of scope). Reset them to a clean start so the
    // POD state is deterministic for a fixed seed.
    drift_.tuneWalk.reset(0.0);
    drift_.pwWalk.reset(0.0);
    drift_.cutoffWalk.reset(0.0);

    // The control-rate divider used by the per-voice modulation/VCA-gate blocks: one
    // control tick per sample here (the engine drives finer control cadence over the
    // seam; Voice assembles at base rate).
    constexpr int kControlRateDivider = 1;

    // --- Modulation section (ONE ADSR + ONE LFO per voice) ---
    env_.prepare(sampleRate_, kControlRateDivider);
    env_.setParams(mw101::dsp::EnvParams{});      // INIT defaults until ControlCore drives params
    lfo_.prepare(sampleRate_, kControlRateDivider);

    // --- Signal path. Build the shared read-only minBLEP table once (off the audio
    // thread; the ONLY allocation), then pre-size the oscillator section's ring. ---
    if (!hqTable_.isBuilt())
        hqTable_.build();
    osc_.prepare(sampleRate_, hqTable_);

    // The filter runs in the oversampled zone (fs_os = factor * host rate) [ADR-003].
    const double fsOs = sampleRate_ * static_cast<double>(oversample_);
    vcf_.prepare(fsOs, /*maxBlockOs=*/0);

    vca_.prepare(sampleRate_);
    vcaGate_.prepare(sampleRate_, kControlRateDivider);

    glide_.prepare(sampleRate_);

    // Precompute the per-sample steal-fade decrement from the (PI) fade length so a
    // steal ramps stealGain_ 1->0 over kStealFadeMs (§6.4). No per-sample transcendental.
    const double fadeSamples =
        std::max(1.0, static_cast<double>(cal::voice::kStealFadeMs) * 0.001 * sampleRate_);
    stealStep_ = static_cast<float>(1.0 / fadeSamples);

    // Clean start.
    state_       = VoiceState::Idle;
    currentNote_ = -1;
    noteSerial_  = 0;
    stealGain_   = 1.0f;
    detuneCents_ = 0.0f;
    // Source-mixer levels back to the saw-only INIT mix; glide-domain note-on flags clear.
    // A known fixed point so reset()/prepare() are byte-stable (§5.5) [ADR-001 Decision].
    sawLevel_   = 0.8f;
    pulseLevel_ = 0.0f;
    subLevel_   = 0.0f;
    noiseLevel_ = 0.0f;
    pendingNoteOnLegato_ = false;
    freshNoteOn_         = false;
    setStereoPan(0.0f);
    env_.reset();
    lfo_.reset();
    osc_.reset(static_cast<std::uint64_t>(drift_.seed));
    vcf_.reset();
    vca_.reset();
    vcaGate_.reset();
}

void Voice::noteOn(int midiNote, float velocity, bool retrigger) noexcept {
    (void) velocity;   // velocity routing (VCA/VCF) owned by ModRouting; passed through.
    currentNote_ = midiNote;

    // legato = a note was already sounding (drives the glide slew-vs-snap rule). The VCO
    // pitch CV is owned by the ADR-028 dispatch (volts domain), so noteOn no longer seeds
    // the glide in Hz (that seeded the old DISCARDED per-sample glide); instead it records
    // whether this keypress is legato, and the first applyControls after the keypress sets
    // the glide target in the VOLTS domain with that legato flag (task 160 reconciliation).
    const bool legato = (state_ != VoiceState::Idle);
    pendingNoteOnLegato_ = legato;
    freshNoteOn_         = true;

    // Fire the ADSR from its trigger state when the decision says so (retrigger).
    env_.noteOn(/*legato=*/legato && !retrigger);
    lfo_.resetPhaseOnKey();
    vcaGate_.gateOn();

    state_     = VoiceState::Active;
    stealGain_ = 1.0f;
}

void Voice::noteOff() noexcept {
    if (state_ == VoiceState::Idle)
        return;
    env_.noteOff();
    vcaGate_.gateOff();
    // Release the tail in place; render() self-transitions to Idle at silence (§4.3).
    if (state_ != VoiceState::Stealing)
        state_ = VoiceState::Releasing;
}

void Voice::setGlideTarget(float targetPitchHz) noexcept {
    glide_.setTarget(targetPitchHz, /*legato=*/true, /*arpActive=*/false);
}

void Voice::applyControls(const VoiceControls& c, int advanceSamples) noexcept {
    // ADR-028 dispatch seam (task 160). The Engine decoded the ParamSnapshot into `c` for
    // THIS control tick; here we drive the per-voice DSP setters. RT-safe: a bounded glide
    // loop + one osc_.setControls re-derive (pure arithmetic), no heap, no lock.

    // --- VCO PITCH (ADR-005 count-domain authority, applied EXACTLY ONCE) ---------------
    // The single per-voice Glide owns portamento. We slew the pitch CV in the VOLTS domain
    // (the hardware glides the CV, not Hz, giving a correct exponential pitch sweep), so the
    // Glide target is the count-domain target CV. This RECONCILES the duplicate glide: the
    // old per-sample glide_.nextValue() in render() was DISCARDED (the §4.2 bug); glide is
    // advanced here at the per-sample rate (advanceSamples steps) and its output drives the
    // oscillator. The Glide coefficient is a PER-SAMPLE one-pole, so it must be stepped once
    // per elapsed sample (not once per chunk) to reach the target in the configured TIME.
    glide_.setMode(c.glideMode);
    glide_.setTimeSeconds(c.glideSeconds);

    // A note pressed from SILENCE (the voice was Idle at keypress, !pendingNoteOnLegato_)
    // has no prior pitch to glide FROM, so it SNAPS to its target regardless of glide mode
    // (matches the hardware: portamento glides note-to-note, the first note lands directly).
    // Any later keypress while a note is sounding (legato) glides per the mode: On always
    // glides between distinct holds, Auto glides on legato, Off snaps (Glide owns the rules).
    // This keeps glide applied EXACTLY ONCE in the volts domain.
    const bool freshFromIdle = freshNoteOn_ && !pendingNoteOnLegato_;
    freshNoteOn_ = false;
    if (freshFromIdle) {
        glide_.snapTo(c.targetPitchCvVolts);   // land directly on the new pitch
    } else {
        glide_.setTarget(c.targetPitchCvVolts, /*legato=*/true, /*arpActive=*/false);
    }

    // Advance the per-sample glide by the elapsed sample count so it slews in real time. A
    // bounded loop (advanceSamples <= the chunk cap kRenderBlock); each step is the same
    // one-pole the de-zipper uses. The LAST value is the CV applied to the oscillator this
    // chunk. >=1 step always (a zero/negative count still advances one so the CV is current).
    const int steps = advanceSamples > 0 ? advanceSamples : 1;
    float pitchCv = glide_.current();
    for (int n = 0; n < steps; ++n)
        pitchCv = glide_.nextValue();

    // --- assemble the oscillator-section control block (§7.2) ---------------------------
    mw101::dsp::OscillatorSection::Controls oc{};
    oc.vco.pitchCvVolts = pitchCv;
    oc.vco.footage      = c.footage;
    oc.vco.pwmCvNorm    = c.pwmCvNorm;
    oc.vco.aaMode       = qualityAaMode_;     // tier; section forces it onto all sources
    oc.subShape         = c.subShape;
    oc.aaMode           = qualityAaMode_;
    osc_.setControls(oc);

    // --- cache the source-mixer levels render() sums (§4.1) -----------------------------
    sawLevel_   = c.sawLevel;
    pulseLevel_ = c.pulseLevel;
    subLevel_   = c.subLevel;
    noiseLevel_ = c.noiseLevel;
}

void Voice::setDetuneCents(float cents) noexcept {
    detuneCents_ = cents;   // applied to the VCO pitch CV by the control path (§5.3).
}

void Voice::setStereoPan(float pan) noexcept {
    equalPowerPan(pan, panGainL_, panGainR_);
}

void Voice::beginSteal() noexcept {
    // Fast forced fade-then-reuse (NOT a hard cut); render() ramps stealGain_ to 0
    // then goes Idle (§6.4; ADR-006 C15).
    state_     = VoiceState::Stealing;
    stealGain_ = 1.0f;
}

float Voice::currentLevel() const noexcept {
    // The VCA/env level used for the quietest-steal scan (§4.2; ADR-006 C14). During a
    // steal, fold in the fade so a stolen voice reads quieter as it ramps down.
    return env_.level() * stealGain_;
}

void Voice::render(float* outL, float* outR, int numSamples) noexcept {
    // §4.3: an Idle voice is skipped entirely — costs nothing, touches no buffer.
    if (state_ == VoiceState::Idle)
        return;

    for (int i = 0; i < numSamples; ++i) {
        // --- per-sample modulation (the blocks own their internal DSP) ---
        const float envLevel = env_.tick();
        lfo_.tick();
        // NOTE: the VCO pitch CV glide is advanced ONCE per control tick in applyControls
        // (the ADR-028 dispatch seam), NOT per sample here — the old per-sample
        // glide_.nextValue() call was a DISCARD bug (§4.2) and is removed (task 160).

        // --- signal path: VCO+sub+noise -> source mixer -> VCF -> VCA ---
        // §4.1 SOURCE MIXER (task 160): sum ALL four raw sources by their level params
        // (mw101.{saw,pulse,sub,noise}.level), cached from the dispatch. Before this task
        // render() summed ONLY src.saw — pulse/sub/noise were silently dropped (the audit
        // finding). The four sources are bipolar, pre-level [docs/design/01 §8].
        const mw101::dsp::OscillatorSection::Sources src = osc_.renderSample();
        float s = src.saw   * sawLevel_
                + src.pulse * pulseLevel_
                + src.sub   * subLevel_
                + src.noise * noiseLevel_;

        // Oversampled-zone filter: run the per-voice ratio so the zone advances at
        // fs_os. The decimation/up-sampling pair is the engine's; here we drive the
        // filter once per sample as the base-rate proxy for the assembled chain.
        s = vcf_.processSample(s);

        // Click-safe amplitude control (anti-thump fade), then the OTA taper.
        const float ctrl = vcaGate_.tickControl(envLevel);
        s = vca_.process(s, ctrl);

        // --- steal fade (§6.4): ramp stealGain_ 1 -> 0, then self-transition Idle ---
        if (state_ == VoiceState::Stealing) {
            s *= stealGain_;
            stealGain_ -= stealStep_;
            if (stealGain_ <= 0.0f) {
                stealGain_ = 0.0f;
                // Finish writing zeros for the remainder is implicit (s already 0);
                // mark Idle so the slot is reusable.
                state_ = VoiceState::Idle;
            }
        }

        // Accumulate into the block mix with the equal-power pan gains (§5.3).
        outL[i] += s * panGainL_;
        outR[i] += s * panGainR_;

        if (state_ == VoiceState::Idle)
            return;   // stolen mid-block: stop accumulating for the rest of the block.
    }

    // §4.3: a Releasing voice keeps rendering until the ADSR release reaches the
    // silence threshold (env back to Idle), then self-transitions to Idle in place.
    if (state_ == VoiceState::Releasing && !env_.active())
        state_ = VoiceState::Idle;
}

} // namespace mw
