// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/Drive.cpp — implementation of the post-voice FX Drive stage (task 091).
// See Drive.h for the contract. Realizes docs/design/07-fx-section.md §4.1 (flow),
// §4.3 (asymmetric shaper), §4.4 (pre/de-emphasis tilt), §4.5 (DC blocker), §4.6
// (param interpretation), §4.7 (RT invariants), and ADR-010 FX-5, ADR-017 L2/L8.

#include "Drive.h"

#include <algorithm>
#include <cmath>

namespace mw::fx {

namespace {

// dB -> linear gain (amplitude). Pure helper, no allocation.
inline float dbToGain(float dB) noexcept {
    return std::pow(10.0f, dB * (1.0f / 20.0f));
}

} // namespace

float Drive::shape(float x, float preGain) noexcept {
    // §4.3 asymmetric memoryless nonlinearity. The bias introduces asymmetry (even
    // harmonics); the subtracted second term re-centers so shape(0) == 0 and there is
    // no large standing DC before the blocker.
    using namespace mw::cal::drive;
    const float k = preGain * kDrivePreGain;
    return std::tanh(k * (x + kDriveBias)) - std::tanh(k * kDriveBias);
}

void Drive::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

    // The ONLY allocation site: size the dedicated 2x oversampler scratch and measure
    // its fixed round-trip group delay once [§4.7; ADR-017 L2, L10].
    os_.prepare(maxBlockSize);

    // --- Tilt-shelf pivot coefficient [§4.4] -------------------------------------
    // The pre/de-emphasis tilt is a one-pole high-shelf about kDriveTiltHz. The shelf
    // shares the same one-pole low-pass coefficient on both sides (pre-emphasis uses
    // +g, de-emphasis uses the inverse) so the round trip is complementary at unity.
    // The pre/de stages run at the OVERSAMPLED rate (inside the 2x zone, §4.1), so the
    // pivot uses 2x the base sample rate.
    const double osRate = sampleRate_ * static_cast<double>(FxOversampler2x::kFactor);
    const double wc = 2.0 * M_PI * static_cast<double>(mw::cal::drive::kDriveTiltHz) / osRate;
    // One-pole low-pass coefficient a = 1 - exp(-wc); clamp to [0,1].
    const float a = static_cast<float>(std::clamp(1.0 - std::exp(-wc), 0.0, 1.0));
    preTilt_.a = a;
    deTilt_.a  = a;

    // --- DC blocker pole radius, scaled for this sample rate [§4.5] --------------
    // kDcBlockR is defined at kDcBlockRefSampleRate; scale the pole so the corner
    // frequency stays constant across sample rates: R' = R^(refSR/SR). The DC blocker
    // runs at the BASE rate (after downsampling, §4.1/§4.5).
    const double rRef = static_cast<double>(mw::cal::drive::kDcBlockR);
    const double scaled = std::pow(rRef, mw::cal::drive::kDcBlockRefSampleRate / sampleRate_);
    dcR_ = static_cast<float>(std::clamp(scaled, 0.0, 0.99999));

    // --- De-zipper coefficient for the smoothed gains [§4.2] ----------------------
    // Reuse the level-smoothing time constant (~15 ms) so amount/tone/output never
    // zipper [ADR-020]. a = exp(-1/(tau*fs)) at the BASE rate.
    const double tau = mw::cal::smoothing::kLevelSeconds;
    smoothCoeff_ = (tau > 0.0)
        ? static_cast<float>(std::exp(-1.0 / (tau * sampleRate_)))
        : 0.0f;

    reset();
}

void Drive::reset() noexcept {
    os_.reset();
    preTilt_.reset();
    deTilt_.reset();
    dcX1_ = 0.0f;
    dcY1_ = 0.0f;
    // Snap smoothed values to their targets so reset() introduces no transient.
    preGain_  = preGainTarget_;
    tiltGain_ = tiltTarget_;
    outGain_  = outTarget_;
}

void Drive::setParams(const FxParams::DriveP& p) noexcept {
    on = p.on;

    using namespace mw::cal::drive;

    // drive_amount in [0,1] -> linear-in-dB pre-gain across [0, kDriveMaxGainDb]
    // (more = more grit) [§4.6]. amount=0 -> 0 dB (unity, clean pass-through).
    const float amount = std::clamp(p.amount, 0.0f, 1.0f);
    preGainTarget_ = dbToGain(amount * kDriveMaxGainDb);

    // drive_tone in [0,1]; 0.5 = flat. Maps linearly to a signed shelf gain in
    // [-kDriveTiltMaxDb, +kDriveTiltMaxDb] dB; tone=0.5 -> 0 dB (unity) [§4.4]. The
    // shelf "gain" g is expressed as a linear multiplier of the high band: a +tiltDb
    // boost is g = (10^(tiltDb/20) - 1) added to the high-pass component.
    const float tone = std::clamp(p.tone, 0.0f, 1.0f);
    const float tiltDb = (tone - 0.5f) * 2.0f * kDriveTiltMaxDb; // 0 at tone=0.5
    tiltTarget_ = dbToGain(tiltDb) - 1.0f;                       // 0 at tone=0.5

    // drive_output in [0,1] -> linear-in-dB makeup across [kDriveOutMinDb,
    // kDriveOutMaxDb]; 0.5 ~= 0 dB unity [§4.6].
    const float output = std::clamp(p.output, 0.0f, 1.0f);
    const float outDb = kDriveOutMinDb + output * (kDriveOutMaxDb - kDriveOutMinDb);
    outTarget_ = dbToGain(outDb);
}

float* Drive::process(float* mono, int numSamples) noexcept {
    const float coeff = smoothCoeff_;

    for (int n = 0; n < numSamples; ++n) {
        // --- control-rate (here per-sample) de-zipper of the gains [§4.2] ---------
        preGain_  += (1.0f - coeff) * (preGainTarget_ - preGain_);
        tiltGain_ += (1.0f - coeff) * (tiltTarget_ - tiltGain_);
        outGain_  += (1.0f - coeff) * (outTarget_ - outGain_);

        const float x = mono[n];

        // --- upsample 2x: process BOTH high-rate samples through the nonlinear zone,
        // then downsample back to one base-rate sample [§4.1] ---------------------
        const float* hi = os_.upsampleSample(x);   // hi[0], hi[1] (high-rate pair)
        float shaped[FxOversampler2x::kFactor];
        for (int k = 0; k < FxOversampler2x::kFactor; ++k) {
            // pre-emphasis tilt INTO the shaper (+tiltGain_ boosts highs) [§4.4]
            const float pre = preTilt_.process(hi[k], tiltGain_);
            // input gain (Drive) + asymmetric waveshaper [§4.3]
            const float sh = shape(pre, preGain_);
            // de-emphasis tilt AFTER the shaper: the inverse of pre (−tiltGain_) so
            // tone=0.5 (tiltGain_=0) is a flat unity round trip [§4.4]
            shaped[k] = deTilt_.process(sh, -tiltGain_);
        }
        const float down = os_.downsampleSample(shaped); // base-rate sample

        // --- DC blocker AFTER downsample [§4.5]: y = x - x1 + R*y1 ----------------
        const float y = down - dcX1_ + dcR_ * dcY1_;
        dcX1_ = down;
        dcY1_ = y;

        // --- output makeup [§4.1] -------------------------------------------------
        mono[n] = y * outGain_;
    }

    return mono;
}

} // namespace mw::fx
