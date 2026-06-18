// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/FastTanh.h — the single header-only fast tanh approximation shared by
// every filter stage and the feedback path (task 033).
//
// Realizes docs/design/02 §4 (the tanh nonlinearity) under [ADR-003] F-10 (a single
// shared fast rational/polynomial tanh, no std::tanh on the audio thread) and F-14
// (frozen, versioned coefficients => bit-exact bless). The rational form and its
// coefficients are (PI) and live in core/calibration/FastTanhConstants.h (the
// mw::cal::vcf namespace Calibration.h reserves); this header reads them and inlines
// NO (PI) numeric literal at the use site [docs/design/02 §4.2, §9, §10 F-15].
//
// Contract (docs/design/02 §10 F-10): odd-symmetric, monotone on the working range,
// saturating to +/-1, max abs error vs std::tanh below a fixed bound, and reachable
// from the audio hot path without any std::tanh / std::tan / std::exp call. Header
// is branch-light and noexcept (RT invariants, docs/design/02 §1.3).

#pragma once

#include "../calibration/FastTanhConstants.h"

namespace mw::dsp {

// Fast, frozen, branch-light tanh approximation [docs/design/02 §4.2]. Odd-symmetric
// and monotone on the working range, saturating to +/-1. The reference realization
// is the Pade-style rational x*(27 + x^2)/(27 + 9*x^2), clamped to +/-1 for |x|
// beyond the validity range; the coefficients are the frozen (PI) bless constants
// (cal::vcf::tanhCoeffs) [docs/design/02 §4.2, §9; ADR-003 F-10, F-14].
[[nodiscard]] inline float fastTanh(float x) noexcept {
    // Branch-light clamp to the validity range: outside +/-kTanhClamp the rational
    // is pinned to the saturating value +/-1 [docs/design/02 §4.2]. Implemented with
    // a clamped argument (no data-dependent control flow on the hot path).
    const float c  = mw::cal::vcf::kTanhClamp;
    const float xc = (x < -c) ? -c : (x > c ? c : x); // min/max, no transcendental
    const float x2 = xc * xc;

    const float num0 = mw::cal::vcf::tanhCoeffs[0];
    const float num1 = mw::cal::vcf::tanhCoeffs[1];
    const float den0 = mw::cal::vcf::tanhCoeffs[2];
    const float den1 = mw::cal::vcf::tanhCoeffs[3];

    // x*(num0 + num1*x^2) / (den0 + den1*x^2). Denominator is strictly positive for
    // den0,den1 > 0, so no division-by-zero branch is needed.
    const float r = xc * (num0 + num1 * x2) / (den0 + den1 * x2);

    // Pin to +/-1: at the validity edge the rational can round just past the rail in
    // float, which would break monotonicity and the bounded-saturator property. The
    // clamp to +/-1 is part of the contract [docs/design/02 §4.2 "clamped ... to
    // +/-1"]. min/max only — branch-light, no transcendental.
    return (r < -1.0f) ? -1.0f : (r > 1.0f ? 1.0f : r);
}

// Saturating transconductor with the OTA knee folded in: fastTanh(x * invTwoVt),
// where invTwoVt = 1/(2*Vt) is the (PI) saturation-knee scaler from calibration
// [docs/design/02 §4.2, §9]. The knee scaler is passed in (the ladder reads it from
// the per-SR tables) so this stays a pure, frozen function. noexcept, branch-light.
[[nodiscard]] inline float fastTanhKnee(float x, float invTwoVt) noexcept {
    return fastTanh(x * invTwoVt);
}

} // namespace mw::dsp
