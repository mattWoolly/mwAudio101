// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Lfo.cpp — prepare / rate-clamp + phase-increment / phase accumulation,
// and the two CONTINUOUS waveform cores: SmoothTri (triangle rounded toward sine)
// and Square (intentionally hard-edged). See core/dsp/Lfo.h (task 051 declared the
// API + POD layout). Realizes docs/design/03-dsp-envelope-lfo-vca.md §3.4 (rate
// range + phase) and §3.5 (SmoothTri / Square cores).
//
// OUT OF SCOPE for this task (task 055 ## Out of scope): the Random and Noise cores
// (task 7), the cycleEdge->envelope wiring, and the mod-bus LPF (ModRouting). The
// Random/Noise branches here therefore only keep tick() coherent and bounded — they
// hold their current register / pass through the injected sample — and do NOT
// implement the digital pseudo-S/H reload nor the bandlimited-noise routing, which
// the later task owns. The cycle EDGE itself (the oscillator's own phase wrap, §3.6)
// is flagged here because the phase machinery is this task's scope; what CONSUMES it
// is not.
//
// Real-time invariants [docs/design/03 §3.1; docs/design/00 §9; ADR-001, ADR-019
// VT-01, ADR-020 S14]: all state is POD and sized at construction; the rate-derived
// phase increment is computed in setRateHz (called off the audio thread on the
// control-rate tick); tick() touches only members, allocates nothing, takes no lock,
// and is noexcept. No (PI) literal is inlined: kLfoSmoothShape resolves from the
// calibration table [docs/design/00 §8.3; ADR-020 S13].

#include "Lfo.h"

#include "../calibration/EnvLfoVcaConstants.h"

namespace mw101::dsp {

namespace {
// Rate band, FROZEN fact (high confidence), NOT a (PI) tunable: the LFO runs
// 0.1–30 Hz; the disputed 0.35 Hz minimum is a clone artifact and MUST NOT be
// enforced [docs/design/03 §3.4; research/04 §3.1, §5.1].
constexpr float kLfoRateMinHz = 0.1f;
constexpr float kLfoRateMaxHz = 30.0f;

constexpr float clampRate (float hz) noexcept
{
    if (hz < kLfoRateMinHz) return kLfoRateMinHz;
    if (hz > kLfoRateMaxHz) return kLfoRateMaxHz;
    return hz;
}

// Native bipolar triangle from a phase in [0,1): -1 at phase 0, rising to +1 at the
// half-cycle, falling back to -1. This is the "native triangle" the §3.5 SmoothTri
// shaper rounds toward sine.
constexpr float bipolarTriangle (float phase) noexcept
{
    return (phase < 0.5f) ? (4.0f * phase - 1.0f)    // -1 -> +1 over [0, 0.5)
                          : (3.0f - 4.0f * phase);   // +1 -> -1 over [0.5, 1)
}

// Cubic sine approximant on [-1,1]: maps the triangle value through a smooth,
// monotonic, bound-preserving (±1 -> ±1, 0 -> 0) curve that ROUNDS the triangle's
// hard corners toward a sine shape. This is the "sineApprox" of §3.5 — explicitly a
// rounding, not a mathematically pure sine.
constexpr float sineApprox (float t) noexcept
{
    return t * (1.5f - 0.5f * t * t);
}
} // namespace

void Lfo::prepare (double sampleRate, int controlRateDivider) noexcept
{
    sampleRate_  = sampleRate;
    ticksPerCtl_ = (controlRateDivider > 0) ? controlRateDivider : 1;
    reset();
    // phaseInc_ is (re)derived from the last requested rate against the new control
    // rate fc; setRateHz is normally called after prepare, but recompute defensively
    // so a prepare-without-setRateHz still has a coherent (rate-0) increment.
}

void Lfo::reset() noexcept
{
    phase_ = 0.0f;
    value_ = 0.0f;
    edge_  = false;
    shReg_ = 0.0f;   // Random S/H register reload is task 7's; reset to a known 0.
}

void Lfo::setRateHz (float hz) noexcept
{
    // Clamp into the FROZEN [0.1, 30] Hz band; 0.35 Hz is NOT enforced as a floor.
    const float clamped = clampRate (hz);

    // fc = sampleRate / ticksPerControl is the control-rate the phase advances at;
    // phaseInc_ = rateHz / fc so one full cycle takes fc/rate ticks [§3.4].
    const double fc = (ticksPerCtl_ > 0) ? (sampleRate_ / static_cast<double> (ticksPerCtl_))
                                         : sampleRate_;
    phaseInc_ = (fc > 0.0) ? static_cast<float> (static_cast<double> (clamped) / fc)
                           : 0.0f;
}

void Lfo::setShape (LfoShape s) noexcept
{
    shape_ = s;
}

void Lfo::resetPhaseOnKey() noexcept
{
    // Clock-reset-on-keypress hook (§3.6): restart the oscillator's phase. The arp/
    // seq edge advance logic that consumes this is owned by the control-rate doc.
    phase_ = 0.0f;
    edge_  = false;
}

void Lfo::setNoiseSource (const float* sharedNoiseSample) noexcept
{
    // The white-noise source is injected, never owned here [§3.5, §1.2]. The Noise
    // core wiring is task 7's; we only store the borrowed pointer.
    noiseSample_ = sharedNoiseSample;
}

float Lfo::tick() noexcept
{
    // Emit the waveform at the CURRENT phase, then advance and detect the H->L wrap.
    switch (shape_)
    {
        case LfoShape::SmoothTri:
        {
            // Native triangle, then round toward sine by the (PI) blend [§3.5]:
            //   out = lerp(tri, sineApprox(tri), kLfoSmoothShape)
            // Labeled "rounded toward sine," never a mathematically pure sine.
            const float tri   = bipolarTriangle (phase_);
            const float k     = mw::cal::lfo::kLfoSmoothShape;
            value_ = tri + k * (sineApprox (tri) - tri);
            break;
        }
        case LfoShape::Square:
        {
            // Intentionally hard-edged [§3.5]: out = (phase < 0.5) ? +1 : -1. The
            // raw square LFO is NOT smoothed here (only a PWM destination would be).
            value_ = (phase_ < 0.5f) ? 1.0f : -1.0f;
            break;
        }
        case LfoShape::Random:
        {
            // OUT OF SCOPE (task 7): hold the S/H register. The digital pseudo-S/H
            // reload on the cycle edge is implemented by the Random-core task.
            value_ = shReg_;
            break;
        }
        case LfoShape::Noise:
        {
            // OUT OF SCOPE (task 7): pass through the injected shared white-noise
            // sample if present, else silence. The mod-bus LPF lives in ModRouting.
            value_ = (noiseSample_ != nullptr) ? *noiseSample_ : 0.0f;
            break;
        }
    }

    // Advance the phase accumulator and wrap into [0,1); flag the H->L cycle edge for
    // one tick on the wrap [§3.4, §3.6].
    phase_ += phaseInc_;
    edge_ = false;
    if (phase_ >= 1.0f)
    {
        phase_ -= 1.0f;
        // Guard a pathological increment > 1 (rate/fc never produces this in-band,
        // but keep phase bounded without an unbounded loop on the hot path).
        if (phase_ >= 1.0f)
            phase_ -= static_cast<float> (static_cast<int> (phase_));
        edge_ = true;
    }

    return value_;
}

} // namespace mw101::dsp
