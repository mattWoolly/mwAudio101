// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/Chorus.cpp — implementation of the Juno-style BBD Chorus stage
// (task 092). See Chorus.h for the contract. Realizes docs/design/07-fx-section.md
// §5.1 / §5.1.3 / §5.1.4 and ADR-010 FX-6, ADR-017 L3.

#include "Chorus.h"

#include <algorithm>
#include <cmath>

namespace mw::fx {

float Chorus::lfoWave(float phase) noexcept {
    // Wrap into [0,1).
    phase -= std::floor(phase);
    // Unit bipolar triangle: 0 at phase 0, +1 at 0.25, 0 at 0.5, -1 at 0.75.
    // tri = 1 - 4*|frac(phase + 0.25) - 0.5| maps to a +/-1 triangle, but we build it
    // piecewise for clarity and exactness at the breakpoints.
    if (phase < 0.25f)      return 4.0f * phase;                 // 0 -> +1
    if (phase < 0.75f)      return 2.0f - 4.0f * phase;          // +1 -> -1
    return 4.0f * phase - 4.0f;                                  // -1 -> 0
}

void Chorus::modePreset(Mode m, float& rateHz, float& depthScalar) noexcept {
    using namespace mw::cal::chorus;
    switch (m) {
        case Mode::I:
            rateHz      = kChorusModeIRateHz;
            depthScalar = kChorusModeIDepth;
            break;
        case Mode::II:
            rateHz      = kChorusModeIIRateHz;
            depthScalar = kChorusModeIIDepth;
            break;
        case Mode::IandII:
            // I+II combines both rate/depth pairs [§5.1.4]: a blended (averaged) rate
            // and the deeper of the two depths (clamped to <= 1).
            rateHz      = 0.5f * (kChorusModeIRateHz + kChorusModeIIRateHz);
            depthScalar = std::min(1.0f, kChorusModeIDepth + kChorusModeIIDepth);
            break;
        case Mode::Off:
        default:
            // Off is the chain early-out (fx-7); keep a sane resting preset so a
            // mis-dispatched call does not divide by zero or read uninitialized rate.
            rateHz      = kChorusModeIRateHz;
            depthScalar = 0.0f;
            break;
    }
}

void Chorus::prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

    const float msToSamples = static_cast<float>(sampleRate_) * 0.001f;
    baseDelaySmp_ = mw::cal::chorus::kChorusBaseDelayMs * msToSamples;
    depthSmp_     = mw::cal::chorus::kChorusDepthMs * msToSamples;

    // Size each ring to the deepest read tap we can request: base + full excursion,
    // plus one sample of head room for the interpolation bracket. The
    // FractionalDelayLine rounds this up to a power of two internally [§5.3].
    const int maxDelaySamples =
        static_cast<int>(std::ceil(baseDelaySmp_ + depthSmp_)) + 2;
    lineL_.prepare(maxDelaySamples);
    lineR_.prepare(maxDelaySamples);

    // Per-sample one-pole de-zipper coefficient. We reuse the existing level-smoothing
    // time constant (cal::smoothing::kLevelSeconds, ~15 ms) so rate/depth/width/mix
    // never zipper [§5.1.2 "smoothed ..."; ADR-020]. a = exp(-1/(tau*fs)).
    const double tau = mw::cal::smoothing::kLevelSeconds;
    smoothCoeff_ = (tau > 0.0)
        ? static_cast<float>(std::exp(-1.0 / (tau * sampleRate_)))
        : 0.0f;

    reset();
}

void Chorus::reset() noexcept {
    lineL_.reset();
    lineR_.reset();
    lfoPhaseL_ = 0.0f;
    lfoPhaseR_ = 0.5f;                 // re-establish the 0.5-cycle anti-phase offset
    // Snap the smoothed values to their current targets so reset() has no transient.
    rateHz_ = rateTarget_;
    depth_  = depthTarget_;
    width_  = widthTarget_;
    mix_    = mixTarget_;
}

void Chorus::setParams(const FxParams::ChorusP& p) noexcept {
    mode = static_cast<Mode>(p.mode);

    // Mode supplies the rate/depth preset; the decoded param values scale/override
    // them [§5.1.3, §5.1.5]. rate is a multiplicative override of the mode rate;
    // depth multiplies the mode depth scalar.
    float modeRate = mw::cal::chorus::kChorusModeIRateHz;
    float modeDepth = 0.0f;
    modePreset(mode, modeRate, modeDepth);

    // p.rate carries the doc-06 rate value (Hz). When the host leaves it at the mode
    // default (or 0), fall back to the mode rate; otherwise honor the explicit knob.
    rateTarget_  = (p.rate > 0.0f) ? p.rate : modeRate;
    depthTarget_ = modeDepth * p.depth;
    widthTarget_ = std::clamp(p.width, 0.0f, 1.0f);
    mixTarget_   = std::clamp(p.mix, 0.0f, 1.0f);
}

void Chorus::process(const float* mono, float* L, float* R, int numSamples) noexcept {
    const float coeff = smoothCoeff_;
    const float lfoIncBase = 1.0f / static_cast<float>(sampleRate_);

    for (int n = 0; n < numSamples; ++n) {
        // --- control-rate (here per-sample) de-zipper of rate/depth/width/mix -----
        rateHz_ += (1.0f - coeff) * (rateTarget_ - rateHz_);
        depth_  += (1.0f - coeff) * (depthTarget_ - depth_);
        width_  += (1.0f - coeff) * (widthTarget_ - width_);
        mix_    += (1.0f - coeff) * (mixTarget_ - mix_);

        const float x = mono[n];
        lineL_.write(x);
        lineR_.write(x);

        // --- two anti-phase LFO-modulated read offsets [§5.1.3] -------------------
        // offset = base + depth*depthMs*lfo(phase). The triangle returns [-1,1]; the
        // depth excursion is half the peak-to-peak so reads stay within the buffer.
        const float modL = depth_ * depthSmp_ * lfoWave(lfoPhaseL_);
        const float modR = depth_ * depthSmp_ * lfoWave(lfoPhaseR_);
        const float delL = std::max(0.0f, baseDelaySmp_ + modL);
        const float delR = std::max(0.0f, baseDelaySmp_ + modR);

        const float tapL = lineL_.read(delL);
        const float tapR = lineR_.read(delR);

        // --- width: 0 = centered mono collapse, 1 = hard-panned anti-phase [§5.1.3]
        // center is the equal sum; width scales each tap's deviation from center, so
        // width=0 => wetL==wetR==center (true mono), width=1 => wetL=tapL, wetR=tapR.
        const float center = 0.5f * (tapL + tapR);
        const float wetL = center + width_ * (tapL - center);
        const float wetR = center + width_ * (tapR - center);

        // --- mix wet into the dry already present in L/R [§5.1.3] ------------------
        L[n] += mix_ * wetL;
        R[n] += mix_ * wetR;

        // Advance the two LFO phases keeping the 0.5-cycle anti-phase relationship.
        const float inc = rateHz_ * lfoIncBase;
        lfoPhaseL_ += inc;
        lfoPhaseR_ += inc;
        lfoPhaseL_ -= std::floor(lfoPhaseL_);
        lfoPhaseR_ -= std::floor(lfoPhaseR_);
    }
}

} // namespace mw::fx
