// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/NoiseSource.h — flat WHITE noise source (task 028).
//
// Realizes docs/design/01 §6.1-§6.4 and §10 (noise whiteness/range, reseed). Flat
// white noise (uniform power per Hz) from a fast integer PRNG (xorshift32) scaled to
// the half-open [-1, 1) range [research/02 §4.3, §7.3; research/10 §6]. There is NO
// pink shaping. An OPTIONAL single-pole HF rolloff (§6.4) models the gentle analog
// op-amp corner; it defaults OFF so the output is white.
//
// Real-time invariants [docs/design/01 §2.4; docs/design/00 §9.1]:
//   - renderSample() is noexcept and performs no heap allocation and takes no locks.
//   - The generator is isolated behind this class so xorshift32 can be swapped for a
//     64-bit LCG/PCG without touching callers (the choice is (PI), see §6.3 caveat).

#pragma once

#include <cstdint>

namespace mw101::dsp {

class NoiseSource
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (std::uint64_t seed) noexcept;   // per-voice seed; nonzero required

    // One white-noise sample in [-1, 1). noexcept, no allocation, no locks.
    [[nodiscard]] float renderSample() noexcept;

    // Optional single-pole HF rolloff (§6.4); disabled by default (output white).
    // Enable AFTER prepare() so the coefficient (computed from kNoiseHfRolloffHz at
    // the prepared sample rate) is valid. Non-RT control; never called per-sample.
    void setHfRolloffEnabled (bool enabled) noexcept;
    [[nodiscard]] bool hfRolloffEnabled() const noexcept { return hfRolloffEnabled_; }

private:
    double   sampleRate_ = 0.0;
    std::uint32_t state_ = 0u;          // xorshift32 state; reseeded nonzero in reset()
    bool  hfRolloffEnabled_ = false;
    float lpfZ_     = 0.0f;             // single-pole filter memory
    float lpfCoeff_ = 0.0f;            // computed in prepare() from kNoiseHfRolloffHz
};

} // namespace mw101::dsp
