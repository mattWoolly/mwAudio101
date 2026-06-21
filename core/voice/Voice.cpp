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
#include "calibration/ControlDispatchLfoConstants.h"   // LFO depth-fade-in cap (task 162)

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
    vcaLevel_   = 0.8f;        // VCA output level back to the registry default (task 161)
    vcaVelScale_ = 1.0f;       // velocity->VCA scale back to neutral (task 162)
    vcaExprScale_ = 1.0f;      // amp.expression scale back to unity (task 164)
    pendingNoteOnLegato_ = false;
    freshNoteOn_         = false;
    velocity_                = 1.0f;   // velocity routing back to neutral (task 162)
    lfoDelayElapsedSamples_  = 0;      // LFO depth fade-in counter cleared (task 162)
    pendingEnv_    = mw101::dsp::EnvParams{};   // INIT ADSR until the seam dispatches (task 161)
    hasPendingEnv_ = false;
    setStereoPan(0.0f);
    env_.reset();
    lfo_.reset();
    osc_.reset(static_cast<std::uint64_t>(drift_.seed));
    vcf_.reset();
    vca_.reset();
    vcaGate_.reset();
}

void Voice::noteOn(int midiNote, float velocity, bool retrigger) noexcept {
    // Velocity is now ROUTED (task 162): record the per-note velocity [0,1] so the dispatch
    // builds the VCA/VCF velocity terms (mw101.vel.{enable,depth}). Before 162 it was
    // discarded; the Engine reads it back via currentVelocity().
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    currentNote_ = midiNote;

    // LFO delay (task 162): a key restarts the LFO depth fade-in only when the voice was
    // SILENT (a fresh note from idle, like the glide snap rule) — a legato re-press keeps
    // the LFO swell that was already in progress. The fresh-from-idle reset is finalized in
    // the first applyControls after this keypress (where pendingNoteOnLegato_ is consumed).
    if (state_ == VoiceState::Idle)
        lfoDelayElapsedSamples_ = 0;

    // legato = a note was already sounding (drives the glide slew-vs-snap rule). The VCO
    // pitch CV is owned by the ADR-028 dispatch (volts domain), so noteOn no longer seeds
    // the glide in Hz (that seeded the old DISCARDED per-sample glide); instead it records
    // whether this keypress is legato, and the first applyControls after the keypress sets
    // the glide target in the VOLTS domain with that legato flag (task 160 reconciliation).
    const bool legato = (state_ != VoiceState::Idle);
    pendingNoteOnLegato_ = legato;
    freshNoteOn_         = true;

    // Apply the latest dispatched ADSR BEFORE firing the attack (task 161): startAttack()
    // latches the live attack coefficient at this edge, so the times must be in place first.
    // Until the seam has dispatched once we keep the env's INIT defaults (hasPendingEnv_).
    if (hasPendingEnv_)
        env_.setParams(pendingEnv_);

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
    // var.glide variance (task 164) multiplies the glide time constant per-voice (identity
    // 1.0 when vintage is off), so stacked/retriggered notes slew at subtly different rates.
    glide_.setTimeSeconds(c.glideSeconds * c.glideTimeScale);

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

    // --- LFO (task 162): advance the single per-voice LFO at the control-tick cadence and
    // route its bipolar [-1,1] output to the dispatched destination. The LFO is configured
    // (rate/shape) and advanced HERE (advanceSamples steps), then sampled ONCE — the same
    // reconciliation the glide uses. The old per-sample lfo_.tick() in render() (which ticked
    // the LFO but routed it NOWHERE — the LFO analogue of the discarded-glide bug) is removed,
    // so the LFO advances exactly once per tick and its output modulates a real destination.
    // The LFO is a control-rate modulator by design (docs/design/03 §3.1 "advances on the
    // control-rate tick"); sampling it once per chunk + summing into the per-tick CVs gives a
    // smooth vibrato/wobble/PWM-sweep at the chunk cadence (<= kRenderBlock). -----------------
    lfo_.setRateHz(c.lfoRateHz);
    lfo_.setShape(c.lfoShape);
    float lfoOut = lfo_.value();
    for (int n = 0; n < steps; ++n)
        lfoOut = lfo_.tick();

    // LFO depth fade-in (mw101.lfo.delay): ramp a 0->1 depth scale over lfoDelaySec from the
    // keypress so a held note's modulation swells in. delaySamples == 0 => instant full depth.
    // Bounded integer counter; the elapsed count saturates at the delay length.
    const double delaySamples = static_cast<double>(c.lfoDelaySec) * sampleRate_;
    float lfoDelayScale = 1.0f;
    if (delaySamples > 0.0) {
        const double elapsed = static_cast<double>(lfoDelayElapsedSamples_);
        lfoDelayScale = (elapsed >= delaySamples)
                            ? 1.0f
                            : static_cast<float>(elapsed / delaySamples);
    }
    // Advance the elapsed counter by this chunk's samples (saturating well below overflow).
    lfoDelayElapsedSamples_ += static_cast<std::uint32_t>(steps);

    const float lfoEff = lfoOut * lfoDelayScale;   // delayed bipolar LFO output

    // Route to EXACTLY ONE destination per lfo.dest (the single MOD switch). Each term is the
    // bipolar LFO times the per-dest depth (already scaled to volts/octaves/norm by the Engine).
    float lfoPitchVolts = 0.0f;     // -> summed into the pitch CV (vibrato)
    float lfoCutoffOct  = 0.0f;     // -> summed into the cutoff CV (wobble)
    float lfoPwmNorm    = 0.0f;     // -> summed into the PWM CV (PWM sweep)
    switch (c.lfoDest) {
        case 1:  lfoCutoffOct = lfoEff * c.lfoCutoffDepthOct;   break;  // Filter
        case 2:  lfoPwmNorm   = lfoEff * c.lfoPwmDepthNorm;     break;  // PWM
        default: lfoPitchVolts = lfoEff * c.lfoPitchDepthVolts; break;  // Pitch
    }

    // VCF-panel LFO->cutoff (mw101.vcf.lfo_mod, task 162e): the VCF module's OWN LFO mod amount,
    // DISTINCT from the LFO panel's lfo.depth_cutoff (lfoCutoffOct above, gated by the single MOD
    // dest switch). The VCF panel routes the per-voice LFO to the cutoff REGARDLESS of the dest
    // switch, so this term is computed UNCONDITIONALLY (not inside the dest switch) and SUMMED
    // ALONGSIDE lfoCutoffOct into the cutoff CV below. Same bipolar lfoEff, scaled to octaves by
    // the Engine. Zero when vcf.lfo_mod == 0 [docs/design/02 §1.2; docs/design/05 §3.1; ADR-028].
    const float vcfLfoModCutoffOct = lfoEff * c.vcfLfoModDepthOct;

    // --- assemble the oscillator-section control block (§7.2) ---------------------------
    // The pitch CV summed into the oscillator is the glided base note CV + the LFO vibrato
    // term + the resolved pitch-bend term (bendVcoVolts; LIVE wheel via task 162c) + the
    // analog-character terms (task 164): the tune.a4 reference offset (pitchRefVolts, a global
    // bias), the drift/cal/slop perturbation (pitchDriftVolts, cents->volts from the DriftModel),
    // and the resolved MPE per-note bend (mpeBendVolts; zero with no live position ingress).
    // All character terms are ZERO when vintage is off, so the default pitch is unchanged.
    // Vibrato + drift ride on TOP of the slewed base pitch (not glided). The PWM CV is the base
    // width (mw101.vco.pw) + the MANUAL static PWM depth (manualPwmDepthNorm, mw101.vco.pwm_depth,
    // task 162e — an LFO-INDEPENDENT duty bias, DISTINCT from the LFO->PWM sweep lfoPwmNorm below)
    // + the LFO PWM-sweep term (lfoPwmNorm, mw101.lfo.depth_pwm, fires only at dest==PWM) + the
    // var.pw drift (pwDriftNorm), clamped to [0,1] duty [docs/design/01 §4.6; docs/design/05 §3.1].
    mw101::dsp::OscillatorSection::Controls oc{};
    oc.vco.pitchCvVolts = pitchCv + lfoPitchVolts + c.bendVcoVolts
                        + c.pitchRefVolts + c.pitchDriftVolts + c.mpeBendVolts;
    oc.vco.footage      = c.footage;
    oc.vco.pwmCvNorm    = std::clamp(c.pwmCvNorm + c.manualPwmDepthNorm + lfoPwmNorm
                                         + c.pwDriftNorm,
                                     0.0f, 1.0f);
    oc.vco.aaMode       = qualityAaMode_;     // tier; section forces it onto all sources
    oc.subShape         = c.subShape;
    oc.aaMode           = qualityAaMode_;
    osc_.setControls(oc);

    // --- cache the source-mixer levels render() sums (§4.1) -----------------------------
    sawLevel_   = c.sawLevel;
    pulseLevel_ = c.pulseLevel;
    subLevel_   = c.subLevel;
    noiseLevel_ = c.noiseLevel;

    // --- VCF (task 161): the SUMMED cutoff CV + resonance (§1.2) ------------------------
    // The cutoff CV handed to the filter is the SUM of the base cutoff (param->volts) plus
    // the routed modulators (ADR-028 item 3): the ENV opens the filter (env_mod x the LIVE
    // ADSR contour, scaled to octaves) and keyboard tracking raises it with note pitch
    // (kbd_track x note-delta CV, precomputed by the Engine). env_.level() is THIS voice's
    // current envelope contour at this control tick, so the cutoff follows the envelope
    // over time as the dispatch fires each chunk (a control-rate envelope-follow — the
    // filter cutoff is a control-rate setter, not a per-sample one). Pure arithmetic +
    // one table-backed setter; noexcept, alloc-free [docs/design/02 §5.2; ADR-003 F-08].
    // The summed cutoff CV adds the task-162 modulators on top of the 161 terms: the LFO
    // wobble (lfoCutoffOct, octaves of CV, when dest=Filter), the velocity->cutoff term
    // (velCutoffVolts, a hard key opens the filter when vel.enable), and the resolved
    // pitch-bend->VCF term (bendVcfVolts; LIVE wheel via task 162c). All are volts in the same
    // 1 V/oct CV frame the filter consumes [docs/design/02 §1.2; ADR-028 item 3].
    const float envLevel = env_.level();
    const float cutoffCv = c.cutoffBaseCvVolts
                         + c.envModOctaves * envLevel
                         + c.kbdTrackCvVolts
                         + lfoCutoffOct
                         + vcfLfoModCutoffOct   // VCF-panel LFO->cutoff (task 162e; distinct from lfo.depth_cutoff)
                         + c.velCutoffVolts
                         + c.bendVcfVolts
                         + c.cutoffDriftVolts;   // analog character (task 164): cal+drift+var.cutoff
    vcf_.setCutoffCv(cutoffCv);
    vcf_.setResonance(c.resonance01);

    // --- Envelope (task 161): push the calibrated A/D/S/R each control tick ------------
    // setParams recomputes the per-stage one-pole coefficients from the calibrated times
    // (the only transcendental site; off the per-sample path). The trig mode stays the
    // S7-driven default the lifecycle set; this leg supplies only the A/D/S/R values.
    // ALSO cache the params so the NEXT note-on can apply them BEFORE firing the attack:
    // the env latches its active-stage coefficient at the trigger edge (startAttack copies
    // aCoeff_ into the live coeff_), so a note that triggers before its A/D/S/R are pushed
    // would run the Attack at the stale prepared default — caching + applying in noteOn
    // fixes the attack-time-has-no-effect ordering without re-architecting the env.
    // var.env_time variance (task 164) multiplies the A/D/R time constants per-voice (identity
    // 1.0 when vintage is off); sustain is a level, NOT a time, so it is not scaled.
    mw101::dsp::EnvParams ep{};
    ep.attackSec  = c.envAttackSec  * c.envTimeScale;
    ep.decaySec   = c.envDecaySec   * c.envTimeScale;
    ep.sustain    = c.envSustain;
    ep.releaseSec = c.envReleaseSec * c.envTimeScale;
    env_.setParams(ep);
    pendingEnv_    = ep;
    hasPendingEnv_ = true;

    // --- VCA (task 161): ENV vs GATE amplitude source + the output level scale ---------
    // vca.mode picks whether the click-safe gate fade follows the ADSR contour (ENV) or a
    // flat full level (GATE, organ-style); the level is a clean linear post-VCA amplitude
    // scale applied in render() (the channel fader), NOT folded into the OTA taper input
    // (which would distort the contour shape) [docs/design/03 §4.4].
    vcaGate_.setMode(c.vcaMode);
    vcaLevel_ = c.vcaLevel;

    // --- velocity->VCA (task 162): fold this voice's velocity into a clean post-VCA gain --
    // scale = (1 - depth) + depth*velocity, so depth 0 => 1.0 (velocity has no effect) and
    // depth 1 => the velocity itself (a soft key plays softer, a hard key full). velVcaDepth
    // is ZERO when vel.enable is off (the Engine zeroes it), so sensing-off keeps scale 1.0.
    // Applied in render() as a linear amplitude scale beside vcaLevel_ (the §5 "velocity folded
    // into the amplitude" routing) — NOT into the OTA control taper, so the gate contour stays
    // intact. velocity_ was recorded at noteOn (task 162).
    vcaVelScale_ = (1.0f - c.velVcaDepth) + c.velVcaDepth * velocity_;

    // --- amp.expression (task 164): cache the CC11 output scaler render() applies as a clean
    // linear post-VCA amplitude scale (beside vcaLevel_ + vcaVelScale_). The param reaches the
    // seam (schema default 1.0 == unity), so this is directly audible. ---
    vcaExprScale_ = c.ampExpression;
}

void Voice::stageEnvParams(const mw101::dsp::EnvParams& ep) noexcept {
    // Cache the dispatched ADSR for the next note-on AND apply it live to env_ (task 161).
    // Applying live keeps an already-sounding voice's segment coefficients current (a changed
    // release/decay takes effect before the next stage transition); caching ensures the NEXT
    // trigger latches these times at its edge (the env snapshots its stage coeff at the
    // trigger). Does NOT fire the envelope — only setParams. noexcept, alloc-free.
    pendingEnv_    = ep;
    hasPendingEnv_ = true;
    env_.setParams(ep);
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
        // NOTE: BOTH the VCO pitch CV glide AND the LFO are advanced ONCE per control tick in
        // applyControls (the ADR-028 dispatch seam), NOT per sample here. The old per-sample
        // glide_.nextValue() (task 160) and lfo_.tick() (task 162) calls modulated NOTHING
        // (the LFO output was discarded) — both DISCARD bugs (§4.2), now removed. The LFO is a
        // control-rate modulator (docs/design/03 §3.1) summed into the per-tick pitch/cutoff/
        // PWM CVs in applyControls.

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

        // Click-safe amplitude control (anti-thump fade) for the selected ENV/GATE source,
        // then the OTA taper, then the VCA output level (§4.1; task 161) — the clean linear
        // channel-fader scale (mw101.vca.level), applied after the taper so it does not
        // reshape the OTA contour.
        const float ctrl = vcaGate_.tickControl(envLevel);
        // vcaLevel_ is the channel fader (mw101.vca.level); vcaVelScale_ folds in this voice's
        // velocity (task 162); vcaExprScale_ folds in the amp.expression CC11 scaler (task 164)
        // — all clean linear amplitude scales after the OTA taper, so none reshapes the contour.
        s = vca_.process(s, ctrl) * vcaLevel_ * vcaVelScale_ * vcaExprScale_;

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
