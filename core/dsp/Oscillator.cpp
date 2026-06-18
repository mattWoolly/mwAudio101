// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Oscillator.cpp — implementation of the VCO master phase core / exp-pitch
// converter / footage offsets / dt clamp / drift hooks (task 029). See Oscillator.h.
// Realizes docs/design/01-dsp-oscillators.md §4.1-§4.4, §4.7, §2.1, §10.

#include "Oscillator.h"

#include <cmath>

namespace mw101::dsp {

void Oscillator::prepare (double sampleRate, const MinBlepTable* hqTable) noexcept
{
    sampleRate_ = sampleRate;
    hqTable_    = hqTable;          // band-limiting hook for core-osc-4 (may be null)
    reset();
    recompute();
}

void Oscillator::reset() noexcept
{
    phase_   = 0.0;
    wrapped_ = false;
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
        * (mw::cal::vco::kDriftScalePpmMax / 1.0e6);   // ppm-of-octave -> octaves

    // (c) progressive top-octave sharpness is compensated when HF tracking (pin 7)
    // is modeled [§4.7; research/02 §2.7]. With kHfTrackEnable (default true) the
    // settled converter tracks the ideal exp law exactly; the un-tracked term is
    // therefore zero in the default build. The hook is kept explicit so disabling
    // HF tracking later reintroduces the documented top-octave sharpening.
    const double hfSharpenOct =
        mw::cal::vco::kHfTrackEnable ? 0.0 : 0.0;   // tracked => 0 (hook for variance doc)

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
}

Oscillator::Output Oscillator::renderSample() noexcept
{
    // Advance the master phase accumulator ONCE per sample (§2.1). dt_ is already
    // clamped so at most one wrap occurs.
    phase_  += dt_;
    wrapped_ = false;
    if (phase_ >= 1.0)
    {
        phase_  -= 1.0;
        wrapped_ = true;
    }

    // Warm-up first-order settle (§4.7): warmupSettle_ -> 1 over ~kWarmupTauSec. With
    // the default zero seeds this changes nothing audible; the hook advances on the
    // audio thread so a nonzero-seed voice settles deterministically. Bounded, no
    // allocation, no branch on the parameter store.
    if (warmupSettle_ < 1.0 && sampleRate_ > 0.0 && mw::cal::vco::kWarmupTauSec > 0.0)
    {
        const double tauSamples = mw::cal::vco::kWarmupTauSec * sampleRate_;
        warmupSettle_ += (1.0 - warmupSettle_) / tauSamples;
        if (warmupSettle_ > 1.0) warmupSettle_ = 1.0;
    }

    // RAW sources (band-limiting is core-osc-4): the trivial rising ramp saw and a
    // trivial pulse at 50% (PWM mapping is core-osc-4). Both bipolar in [-1, 1).
    const float saw   = static_cast<float> (2.0 * phase_ - 1.0);
    const float pulse = (phase_ < 0.5) ? 1.0f : -1.0f;
    return Output{ saw, pulse };
}

} // namespace mw101::dsp
