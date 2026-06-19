// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Oscillator.cpp — implementation of the VCO: master phase core / exp-pitch
// converter / footage offsets / dt clamp / drift hooks (task 029) PLUS the band-
// limited saw + variable-width pulse construction, the PWM width map, and the duty/dt
// overlap clamp (task 030). See Oscillator.h.
// Realizes docs/design/01-dsp-oscillators.md §4.1-§4.6, §4.7, §2.1-§2.3, §10 and
// ADR-002 C1-C3, C7, C9.

#include "Oscillator.h"

#include "PolyBlep.h"        // closed-form default-tier residual (task 026)
#include "MinBlepTable.h"    // HQ / escalated applicator (task 027)

#include <algorithm>
#include <cmath>

namespace mw101::dsp {

void Oscillator::prepare (double sampleRate, const MinBlepTable* hqTable) noexcept
{
    sampleRate_ = sampleRate;
    hqTable_    = hqTable;          // shared read-only minBLEP table (HQ / escalated)

    // Pre-size the per-voice minBLEP applicators OFF the audio thread, the ONLY place
    // allocation may happen [§2.4; ADR-002 C8, C11]. With no table the HQ path is
    // unavailable and renderSample falls back to PolyBLEP regardless of mode.
    if (hqTable_ != nullptr)
    {
        sawBlep_.prepare (*hqTable_, sampleRate);
        pulseBlep_.prepare (*hqTable_, sampleRate);
    }

    reset();
    recompute();
}

void Oscillator::reset() noexcept
{
    phase_   = 0.0;
    wrapped_ = false;

    // Reset the band-limited shape state to the phase-0 plateau. At phase 0 the pulse
    // is HIGH (t < duty), so the naive pulse level is +1; prime the HQ applicator's held
    // DC to +1 so the first samples hold the correct plateau (the same reset-priming the
    // sub-oscillator uses). The saw residual-DC tracker restarts at 0.
    sawBlep_.reset();
    pulseBlep_.reset();
    sawBlepLevel_ = 0.0;
    pulseNaive_   = 1.0f;
    pulseBlep_.scheduleStep (1.0f, 1.0f);   // hold +1 from phase 0 (HQ tier)

    // The warm-up transient restarts from cold on a hard reset (§4.7). With the
    // default zero seeds this is inaudible; the hook exists regardless.
    warmupSettle_ = 0.0;
    recompute();
}

void Oscillator::setControls (const OscControls& c) noexcept
{
    controls_ = c;
    recompute();
}

void Oscillator::setDriftSeeds (float scaleErrSeed, float offsetErrSeed) noexcept
{
    scaleErr_  = scaleErrSeed;
    offsetErr_ = offsetErrSeed;
    recompute();
}

void Oscillator::recompute() noexcept
{
    // Exponential 1V/oct converter (§4.3):
    //   freqHz = kPitchRefHz * 2^(pitchCvVolts - kPitchRefVolts + footageOffsetV + driftScale)
    // footageOffsetV is the octave offset for the selected range (§4.4); the octave
    // switch is a CV offset, NOT an analog divider, so footage ratios are EXACT.
    const double footageOffsetV = mw::cal::vco::footageOffsetV (controls_.footage);

    // Drift / stability model (§4.7) — SMALL, slow, default near-zero. The full
    // positive scale seed detunes by at most kDriftScalePpmMax ppm of an octave; the
    // first-order warm-up settle scales the residual error in from cold start. The
    // detailed per-voice variance distribution is owned by the variance doc; here we
    // only realize the bounded HOOK the VCO exposes via scaleErr_/offsetErr_.
    const double driftScaleOct =
        warmupSettle_
        * static_cast<double> (scaleErr_)
        * (mw::cal::drift::kDriftScalePpmMax / 1.0e6);   // ppm-of-octave -> octaves

    // (c) progressive top-octave sharpness is compensated when HF tracking (pin 7)
    // is modeled [§4.7; research/02 §2.7]. With kHfTrackEnable (default true) the
    // settled converter tracks the ideal exp law exactly; the un-tracked term is
    // therefore zero in the default build. The hook is kept explicit so disabling
    // HF tracking later reintroduces the documented top-octave sharpening.
    const double hfSharpenOct =
        mw::cal::drift::kHfTrackEnable ? 0.0 : 0.0;   // tracked => 0 (hook for variance doc)

    const double exponent =
        static_cast<double> (controls_.pitchCvVolts)
        - mw::cal::vco::kPitchRefVolts
        + footageOffsetV
        + driftScaleOct
        + hfSharpenOct;

    freqHz_ = mw::cal::vco::kPitchRefHz * std::pow (2.0, exponent);

    // dt clamp (§4.4): dt_ = min(freqHz/fs, kDtMax). kDtMax = 0.5 (Nyquist) bounds the
    // accumulator to at most one wrap per sample so the single-wrap edge assumption
    // (§2.1) holds across the audio band. Guard a non-positive sample rate.
    if (sampleRate_ > 0.0)
    {
        const double raw = freqHz_ / sampleRate_;
        dt_ = (raw < mw::cal::vco::kDtMax) ? raw : mw::cal::vco::kDtMax;
    }
    else
    {
        dt_ = 0.0;
    }

    // The band-limited SHAPE state (PWM duty + effective AA mode) depends on dt_ and
    // freqHz_, so it is recomputed PER BLOCK here, never per sample [§2.2-§2.3, §4.6].
    recomputeShape();
}

void Oscillator::recomputeShape() noexcept
{
    // --- PWM width map (§4.6): duty = kPwmDutyMax - pwmCvNorm*(kPwmDutyMax - kPwmDutyMin).
    // pwmCvNorm = 0 => 50% (square), pwmCvNorm = 1 => ~5%. Centralized in calibration.
    const double rawDuty =
        static_cast<double> (mw::cal::vco::pwmDutyFromCvNorm (controls_.pwmCvNorm));

    // --- Duty/dt overlap clamp (ADR-002 C3, §4.6): each BLEP correction window is dt
    // wide (one at phase 0, one at the duty phase). Require duty >= dt AND
    // (1 - duty) >= dt so neither correction window straddles the other. The effective
    // minimum duty is therefore max(kPwmDutyMin, dt); also bound below by kPwmDutyMin
    // and above by kPwmDutyMax. Symmetric so the falling window also clears the wrap.
    double d = std::clamp (rawDuty,
                           static_cast<double> (mw::cal::vco::kPwmDutyMin),
                           static_cast<double> (mw::cal::vco::kPwmDutyMax));
    if (d < dt_)            d = dt_;            // rising window clears the falling edge
    if ((1.0 - d) < dt_)    d = 1.0 - dt_;      // falling window clears the next wrap
    duty_ = d;

    // --- Effective AA mode (§2.2-§2.3; ADR-002 C9): the requested tier, escalated to
    // minBLEP whenever the fundamental exceeds the (sample-rate-scaled) Valimaki
    // threshold AND a built table is available. Decided PER BLOCK here, never flipped
    // per sample on the audio thread [ADR-018 Q5].
    const bool escalate = (hqTable_ != nullptr)
                        && (freqHz_ > mw::cal::vco::hqEscalationHzAt (sampleRate_));
    if (controls_.aaMode == OscAaMode::MinBlepHq || escalate)
        effectiveAaMode_ = (hqTable_ != nullptr) ? OscAaMode::MinBlepHq
                                                 : OscAaMode::PolyBlep;
    else
        effectiveAaMode_ = OscAaMode::PolyBlep;
}

Oscillator::Output Oscillator::renderSample() noexcept
{
    // Advance the master phase accumulator ONCE per sample (§2.1). dt_ is already
    // clamped (kDtMax = 0.5) so at most one wrap occurs and the rising/falling BLEP
    // windows never collide [§4.4; ADR-002 C3]. Keep the pre-advance phase so the
    // sub-sample edge fractions (wrap, duty crossing) can be derived this sample.
    const double pPrev = phase_;
    phase_  += dt_;
    wrapped_ = false;
    if (phase_ >= 1.0)
    {
        phase_  -= 1.0;
        wrapped_ = true;
    }
    const double t  = phase_;
    const float  tf = static_cast<float> (t);
    const float  dt = static_cast<float> (dt_);

    // Warm-up first-order settle (§4.7): warmupSettle_ -> 1 over ~kWarmupTauSec. With
    // the default zero seeds this changes nothing audible; the hook advances on the
    // audio thread so a nonzero-seed voice settles deterministically. Bounded, no
    // allocation, no branch on the parameter store.
    if (warmupSettle_ < 1.0 && sampleRate_ > 0.0 && mw::cal::warmup::kWarmupTauSec > 0.0)
    {
        const double tauSamples = mw::cal::warmup::kWarmupTauSec * sampleRate_;
        warmupSettle_ += (1.0 - warmupSettle_) / tauSamples;
        if (warmupSettle_ > 1.0) warmupSettle_ = 1.0;
    }

    const bool hq = (effectiveAaMode_ == OscAaMode::MinBlepHq) && (hqTable_ != nullptr);

    // -------------------------------------------------------------------------
    // SAWTOOTH (§4.5; ADR-002 C1): naive rising ramp 2*t-1, one residual per wrap.
    //   PolyBLEP tier:   value = (2*t-1) - polyBlep(t, dt)        (the -2 reset => -1*blep)
    //   HQ / escalated:  schedule a minBLEP step of -2 at the wrap fraction; add the
    //                    RESIDUAL only (the trivial ramp already carries the -2 reset DC).
    // -------------------------------------------------------------------------
    float saw;
    if (! hq)
    {
        // 0.5 * stepAmplitude * polyBlep; the saw reset is a -2 step => coefficient -1.
        saw = static_cast<float> (2.0 * t - 1.0) - polyBlep (tf, dt);
    }
    else
    {
        if (wrapped_ && dt > 0.0f)
        {
            // The edge crossed phase 1.0 (dt - t)/dt of the way into THIS sample; the
            // residual-fraction remaining after it is frac = t/dt in [0,1).
            float frac = tf / dt;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            sawBlep_.scheduleStep (-2.0f, frac);
            sawBlepLevel_ += -2.0;
        }
        // Residual = next() - tracked DC; add to the trivial ramp (which carries the DC).
        const double residual = static_cast<double> (sawBlep_.next()) - sawBlepLevel_;
        saw = static_cast<float> ((2.0 * t - 1.0) + residual);
    }

    // -------------------------------------------------------------------------
    // VARIABLE-WIDTH PULSE / PWM (§4.5, §4.6; ADR-002 C2-C3): naive +1 for t<duty
    // else -1, with TWO independent BLEPs/period — rising at phase 0, falling at the
    // duty phase. The two edges are sample-accurate and (by the dt overlap clamp)
    // never overlap.
    //   PolyBLEP tier:  +polyBlep(t)            at the rising edge (phase 0)
    //                   -polyBlep(t - duty)     at the falling edge (duty phase)
    //   HQ / escalated: schedule +2 (rising) / -2 (falling) into pulseBlep_ at their
    //                   sub-sample fractions; pulseBlep_.next() IS the band-limited pulse.
    // -------------------------------------------------------------------------
    const double duty = duty_;

    // Did the phase cross the falling-edge threshold (duty) this sample (no wrap path)?
    // pPrev < duty <= t. (On a wrap, t < dt < duty so this can't also fire this sample.)
    const bool fellThisSample = (! wrapped_) && (pPrev < duty) && (t >= duty);

    float pulse;
    if (! hq)
    {
        const float naive = (t < duty) ? 1.0f : -1.0f;
        // Rising edge at phase 0: a +2 step => coefficient +1 on polyBlep(t).
        float corr = polyBlep (tf, dt);
        // Falling edge at the duty phase: shift the phase so `duty` maps to a wrap, then
        // a -2 step => coefficient -1 on polyBlep(shifted).
        double td = t - duty;
        if (td < 0.0) td += 1.0;
        corr -= polyBlep (static_cast<float> (td), dt);
        pulse = naive + corr;
    }
    else
    {
        if (wrapped_ && dt > 0.0f && pulseNaive_ < 0.0f)
        {
            // Rising edge at the wrap (low -> high, +2). frac = t/dt as for the saw.
            float frac = tf / dt;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            pulseBlep_.scheduleStep (2.0f, frac);
            pulseNaive_ = 1.0f;
        }
        if (fellThisSample && dt > 0.0f && pulseNaive_ > 0.0f)
        {
            // Falling edge at the duty phase (high -> low, -2). The edge crossed `duty`
            // (t - duty)/dt of the way into this sample; residual-fraction = same form.
            float frac = static_cast<float> ((t - duty) / dt_);
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            pulseBlep_.scheduleStep (-2.0f, frac);
            pulseNaive_ = -1.0f;
        }
        // The applicator owns the held DC (driven by scheduleStep), so pop it directly.
        pulse = pulseBlep_.next();
    }

    return Output{ saw, pulse };
}

} // namespace mw101::dsp
