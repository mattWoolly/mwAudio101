// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/LadderReferenceTPT.h — the OFFLINE Zavalishin TPT/ZDF 4-pole ladder
// reference oracle (task 034). The single source of truth for its contract is
// docs/design/02-dsp-filter.md §8 (interface §8.2); it realizes ADR-003 F-13
// (offline reference oracle, NOT the shipping engine) and reproduces the analytic
// behaviors of ADR-003 F-07 (H(0) = 1/(1+k) bass droop) and F-05 (k -> 4 self-osc),
// per docs/research/10 §3.2/§3.3/§3.4.
//
// This is a LINEAR model: four trapezoidal one-poles with a single delay-free
// (zero-delay-feedback) global feedback `k`, solved instantaneously each sample.
// There is NO embedded tanh (the shipping Huovilainen LadderFilter has that; this
// oracle does not). Double precision, run at base rate (no oversampling needed for
// a linear model), used by tests/calibration as the known-correct linear baseline.
//
// REAL-TIME: this type is offline-only. It carries NO audio-thread / RT-safety
// guarantee and MUST NOT be compiled into the audio hot path (docs/design/02 §8.1;
// task 034 Out-of-scope). It uses std::tan in setCutoffHz deliberately — that call
// is off the hot path by construction.

#pragma once

namespace mw::dsp {

// Zavalishin TPT 4-pole ladder oracle. Four trapezoidal one-poles, prewarped
// g = tan(pi*fc/fs), single delay-free global feedback k, solved instantaneously.
// LINEAR (no embedded tanh). Reference/test only. (docs/design/02 §8.2)
class LadderReferenceTPT {
public:
    LadderReferenceTPT() noexcept = default;

    // Off-thread setup. Records the (base) sample rate and clears state. fsHz is the
    // host/base rate; no oversampling is applied to a linear model. (§8.2)
    void prepare(double fsHz) noexcept;

    // Drop all integrator state to zero; keep coefficients. (§8.2)
    void reset() noexcept;

    // Set cutoff in Hz. Computes the TPT prewarped one-pole coefficient
    // g = tan(pi*fc/fs) and the instantaneous one-pole gain G = g/(1+g).
    // fc is clamped to a stable open interval below Nyquist. (docs/research/10 §3.3)
    void setCutoffHz(double fcHz) noexcept;

    // Set the dimensionless global loop gain k in [0, 4). k = 4 is the analytic
    // self-oscillation threshold of the normalized model. (docs/research/10 §3.4)
    void setResonanceK(double k) noexcept;

    // Process one sample through the four trapezoidal one-poles with the delay-free
    // global feedback solved instantaneously. Returns the stage-4 (lowpass) output.
    // Double precision for use as the comparison baseline. (§8.2)
    [[nodiscard]] double processSample(double x) noexcept;

    // --- Introspection for tests / cross-check ---
    [[nodiscard]] double cutoffCoeffG() const noexcept { return g_; }
    [[nodiscard]] double loopGainK() const noexcept { return k_; }
    [[nodiscard]] double sampleRate() const noexcept { return fs_; }

private:
    double fs_ = 48000.0; // base sample rate
    double g_  = 0.0;     // prewarped one-pole coeff = tan(pi*fc/fs)
    double G_  = 0.0;     // instantaneous one-pole gain = g/(1+g)
    double k_  = 0.0;     // global loop gain in [0, 4)

    // Trapezoidal integrator states, one per one-pole stage.
    double s_[4] = { 0.0, 0.0, 0.0, 0.0 };
};

} // namespace mw::dsp
