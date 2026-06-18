// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/FastTanhConstants.h — (PI) calibration constants for the shared
// fast tanh approximation + OTA-knee transconductor (task 033).
//
// These are the FROZEN, versioned coefficients that make mw::dsp::fastTanh part of
// the bit-exact bless contract [docs/design/02 §4.2, §9, §10 F-14/F-15; ADR-003
// F-10, F-14, F-15]. They live in mw::cal::vcf — the namespace Calibration.h
// (task 005b) declared for tasks 033/035 to APPEND into — but in a SEPARATE header
// so the parallel fleet does not serialize on Calibration.h. The orchestrator wires
// the include into Calibration.h later; FastTanh.h includes THIS header directly so
// it never inlines a (PI) literal at the use site [docs/design/02 §9; AGENTS.md
// "(PI) discipline"].
//
// Every value here is (PI) — a *pragmatic invention*, NOT a measured SH-101 spec.
// No research source prescribes a particular fast tanh; the rational form and its
// coefficients are an engineering choice [docs/design/02 §4.2 "(PI)"]. The OTA knee
// scaler 1/(2*Vt) is device physics (Vt ~ 25.85 mV), NOT an SH-101 datum
// [docs/design/02 §9; research/10 §4].

#pragma once

#include <array>

namespace mw::cal::vcf {

// FastTanh rational coefficients [docs/design/02 §9 vcf::tanhCoeffs].
//
// The shared approximation is the Pade-style odd rational
//
//     fastTanh(x) = x * (num0 + num1*x^2) / (den0 + den1*x^2)
//
// with the reference coefficients x*(27 + x^2)/(27 + 9*x^2) [docs/design/02 §4.2].
// Indices: { num0, num1, den0, den1 }. Frozen for the bless contract (F-14). (PI).
inline constexpr std::array<float, 4> tanhCoeffs = { 27.0f, 1.0f, 27.0f, 9.0f };

// Beyond this |x| the rational drifts from the true tanh; the approximation is
// clamped to the saturating value +/-1 outside +/-kTanhClamp so it stays monotone
// and bounded [docs/design/02 §4.2 "clamped for |x| beyond the validity range to
// +/-1"]. (PI) — chosen where the rational is still within the §10 F-10 error bound.
inline constexpr float kTanhClamp = 3.0f; // (PI)

// OTA knee scaler invTwoVt = 1/(2*Vt) [docs/design/02 §9 vcf::invTwoVt; §6 row
// "OTA knee scaler"]. Vt ~ 25.85 mV (device physics, theory-by-analogy), so
// 1/(2*Vt) ~ 1/0.0517 ~ 19.34 1/V. The transconductor folds it in as
// fastTanh(x * invTwoVt) [docs/design/02 §4.2 fastTanhKnee]. (PI).
inline constexpr float invTwoVt = 19.342359f; // (PI) — 1 / (2 * 0.02585 V)

} // namespace mw::cal::vcf
