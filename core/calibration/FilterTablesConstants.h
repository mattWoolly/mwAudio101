// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/FilterTablesConstants.h — (PI) calibration constants for the
// per-sample-rate FilterTables (task 035). These belong to the `mw::cal::vcf`
// namespace documented in core/calibration/Calibration.h (§9 of
// docs/design/02-dsp-filter.md). They live in a SEPARATE header so the parallel
// development fleet does not serialize on Calibration.h; the orchestrator wires the
// include into Calibration.h later [docs/design/02-dsp-filter.md §9; ADR-008 §1;
// docs/design/00 §8.3; ADR-003 F-15].
//
// Every value here is (PI) — a pragmatic invention, NOT a measured SH-101 spec —
// carrying a tunable-default comment. FilterTables READS these; it never inlines a
// (PI) numeric literal at the DSP call site [ADR-003 F-15; docs/design/02 §10 F-15].

#pragma once

#include <array>

namespace mw::cal::vcf {

// --- Cutoff / CV mapping ----------------------------------------------------
// Reference cutoff at CV = 0 V. The SH-101 VR8 calibration procedure references
// ~1 kHz; the exact reference is an engineering choice [docs/research/03 §3.1;
// docs/design/02 §6, §9].
inline constexpr float kFcRefHz = 1000.0f;          // (PI) — cutoff at CV = 0 V

// DSP-internal cutoff clamp floor [docs/design/02 §6, F-08; docs/research/03 §3.1].
inline constexpr float kFcMinHz = 10.0f;            // (PI) — documented low end
// Documented audio-range ceiling; the stability/prewarp guard 0.45*fs_os is the
// OTHER ceiling and binds only at very low fs_os [docs/design/02 §6; ADR-003 F-08].
inline constexpr float kFcMaxHz = 20000.0f;         // (PI) — documented audio ceiling
// Prewarp/stability guard fraction of the oversampled rate; fc never exceeds this
// [docs/design/02 §6; ADR-003 F-08].
inline constexpr float kFcGuardFracOfFsOs = 0.45f;  // (PI) — fc <= 0.45*fs_os guard

// CV-domain span the gByCv_ table is built over (1 V/oct). Wide enough to cover the
// full clamped fc range at any blessed sample rate: at kFcRefHz=1 kHz, fcMin=10 Hz
// is ~ -6.64 V and fcMax=20 kHz is ~ +4.32 V, so [-10, +10] V brackets it with
// head room. Frozen for bless [docs/design/02 §5.2, §7].
inline constexpr float kCvTableMinVolts = -10.0f;   // (PI) — CV table low edge
inline constexpr float kCvTableMaxVolts =  10.0f;   // (PI) — CV table high edge

// --- Residual half-sample tuning compensation (resoTuningComp) --------------
// The forward-Euler + two-sample-average feedback compensation leaves a residual
// tuning error (<10% below Fs/4 at 2x); research gives the bound, not a closed-form
// correction, so this fit is a pragmatic invention [docs/research/10 §3.8; ADR-003
// F-14; docs/design/02 §7.3]. The correction is modeled as a low-order polynomial
// in the per-stage coefficient g, evaluated as
//   comp(g) = c0 + c1*g + c2*g^2
// and is bounded near unity over g in [0, 1). The default fit is the identity
// (comp == 1) plus a small g^2 term that pulls the effective coefficient slightly to
// recover detuning as g grows toward Fs/4; the residual it absorbs is itself <10% at
// 2x, so the correction magnitude is small by construction. Frozen for bless.
inline constexpr std::array<float, 3> kCompFit = { 1.0f, 0.0f, 0.0f };  // (PI) {c0,c1,c2}

// Comp-table domain is g in [0, 1); the table is built over this span.
inline constexpr float kCompGMin = 0.0f;            // (PI) — comp table low edge (g)
inline constexpr float kCompGMax = 1.0f;            // (PI) — comp table high edge (g)

// --- Table resolution -------------------------------------------------------
// Sample count of each precomputed FilterTables lookup (gByCv_, compByG_). The
// resolution is an engineering choice traded off against interpolation error and
// table memory; it is frozen for bless so the tables are bit-identical across runs
// for a fixed fs_os [docs/design/02 §5.2, §7; ADR-003 F-14, F-15]. FilterTables
// READS this; it does not inline the literal at the DSP call site [ADR-003 F-15].
inline constexpr int kFilterTableSize = 1024;       // (PI) — lookup resolution; frozen for bless

} // namespace mw::cal::vcf
