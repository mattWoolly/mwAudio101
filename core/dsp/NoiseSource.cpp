// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/NoiseSource.cpp — implementation of the flat white noise source
// (task 028). See NoiseSource.h. Realizes docs/design/01 §6.1-§6.4, §10.

#include "NoiseSource.h"

#include "../calibration/NoiseConstants.h"

#include <cmath>

namespace mw101::dsp {

void NoiseSource::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate;

    // Single-pole one-pole LPF coefficient from the (PI) rolloff corner, if/when
    // enabled (§6.4). y[n] = y[n-1] + a*(x[n] - y[n-1]); a = 1 - exp(-2*pi*fc/fs).
    // Computed here (non-RT), never on the audio thread. Guard a non-positive fs.
    if (sampleRate_ > 0.0)
    {
        constexpr double kTwoPi = 6.283185307179586476925286766559;
        const double fc = mw::cal::noise::kNoiseHfRolloffHz;
        const double a  = 1.0 - std::exp (-kTwoPi * fc / sampleRate_);
        lpfCoeff_ = static_cast<float> (a);
    }
    else
    {
        lpfCoeff_ = 0.0f;
    }

    lpfZ_ = 0.0f;
}

void NoiseSource::reset (std::uint64_t seed) noexcept
{
    // Fold the 64-bit per-voice seed into the 32-bit xorshift state. xorshift32
    // cannot escape the all-zero state, so a zero result is rejected and replaced
    // by the (PI) nonzero fallback (§6.3).
    std::uint32_t s = static_cast<std::uint32_t> (seed ^ (seed >> 32));
    if (s == 0u)
        s = mw::cal::noise::kNoiseZeroSeedFallback;

    state_ = s;
    lpfZ_  = 0.0f;
}

float NoiseSource::renderSample() noexcept
{
    // xorshift32 step (§6.3).
    std::uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;

    // Scale uint32 -> half-open [-1, 1) via int*(2/2^32) - 1 (§6.3; research/10 §6).
    float out = static_cast<float> (x) * mw::cal::noise::kNoiseScale
                - mw::cal::noise::kNoiseOffset;

    // Half-open guarantee (§6.3, §10): the literal float formula rounds the top of
    // the uint32 range UP to exactly +1.0f (single-precision can't represent the
    // true 0.999999999534, and (float)0xFFFFFFFF rounds to 2^32), which would break
    // the [-1, 1) contract. Clamp the (vanishingly rare) +1.0f case down to the
    // largest float strictly below 1. Lower bound -1.0f stays inclusive. This is a
    // deliberate, minimal deviation from the doc's bare formula to honor the binding
    // half-open acceptance criterion; flagged in the PR for an ADR.
    if (out >= 1.0f)
        out = mw::cal::noise::kNoiseMaxBelowOne;

    // Optional single-pole HF rolloff (§6.4); OFF by default => output is white.
    if (hfRolloffEnabled_)
    {
        lpfZ_ += lpfCoeff_ * (out - lpfZ_);
        out = lpfZ_;
    }

    return out;
}

void NoiseSource::setHfRolloffEnabled (bool enabled) noexcept
{
    hfRolloffEnabled_ = enabled;
}

} // namespace mw101::dsp
