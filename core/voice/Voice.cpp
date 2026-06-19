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

// MIDI note -> Hz (A4 = note 69 = 440 Hz). Used only to seed the glide on note-on
// before the ControlCore feeds a resolved glide target in Hz (§4.2 / out-of-scope).
inline float midiToHz(int midiNote) noexcept {
    return 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
}

} // namespace

void Voice::prepare(double sampleRate, int oversampleFactor,
                    int voiceIndex, std::uint32_t instanceSeed) noexcept {
    sampleRate_ = sampleRate;
    oversample_ = std::max(1, oversampleFactor);

    // --- Per-voice deterministic drift seed (§4.4; ADR-006 C18). Derived ONLY from
    // voiceIndex + instanceSeed, never wall-clock, so renders are byte-stable. ---
    drift_.seed = cal::voice::hashCombine(instanceSeed,
                                          static_cast<std::uint32_t>(voiceIndex));
    drift_.rng.seed(static_cast<std::uint64_t>(drift_.seed));

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

    // Glide target follows the note (Hz); ControlCore overrides via setGlideTarget for
    // the resolved/quantized pitch. legato = a note was already sounding.
    const bool legato = (state_ != VoiceState::Idle);
    glide_.setTarget(midiToHz(midiNote), legato, /*arpActive=*/false);

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
        glide_.nextValue();

        // --- signal path: VCO+sub+noise -> VCF -> VCA ---
        const mw101::dsp::OscillatorSection::Sources src = osc_.renderSample();
        float s = src.saw;   // pre-mix tap (the source mixer is the engine's, §4.1)

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
