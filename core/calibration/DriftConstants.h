// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/DriftConstants.h — the vintage-variance / analog-drift (PI)
// tunable-constant block: Tier-1 frozen-trimmer band widths and the four
// frozen-at-note-on variance-spread bands (task 065).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h. It #includes that root and APPENDS its values into
// the SAME namespace (mw::cal::drift) that Calibration.h reserves for the vintage
// stream, so the two headers compose additively and no DSP call site inlines a (PI)
// literal [docs/design/08 §1.2, §4.1; docs/design/06 §3.10; ADR-008 §1]. The
// calibration orchestrator wires this include from Calibration.h later.
//
// PROVENANCE (docs/design/08 §1.3, §2; ADR-009 §8 — no-oracle re-affirmation):
//   - the trimmer SET-POINTS (VR-2/VR-7/VR-9/VR-8) are documented (VR-anchored);
//   - the BAND WIDTHS here, and every variance-spread band, are (PI) — pragmatic
//     inventions / TUNABLE DEFAULTS, NOT measured SH-101 specs. A re-tune is one
//     localized edit here [docs/design/08 §4.1, §7.1; ADR-009 §Decision 1, §8].
//
// The numeric defaults reproduce the docs/design/08 §4.1 and §7.1 tables verbatim.

#pragma once

#include "Calibration.h"

namespace mw::cal::drift {

// ---------------------------------------------------------------------------
// Tier-1 — frozen per-instance calibration band widths [docs/design/08 §4.1;
// ADR-009 §Decision 1]. The cal.spread (mw101.vintage.cal_spread, 0..1) scales
// every band; spread=0 => zero perturbation [docs/design/08 §4.2, VV-6].
//
// VR-8 (VCF Width) is modeled as a SCALE multiplier on the cutoff CV slope, NEVER
// an offset; the uncalibrated cutoff OFFSET path (IR3109 has no per-unit cutoff
// cal, §2.3) legitimately gets the most generous Tier-1 band.
// ---------------------------------------------------------------------------

// VR-7 / VR-9 VCO Tune (nominal 442 Hz): additive cents offset to global pitch.
inline constexpr float kCalBandTuneCents    = 6.0f;    // (PI) +/-6 cents

// VR-2 D/A Tune (0 V +/-1 mV): additive cents offset, coupled with the Tune trim.
inline constexpr float kCalBandDacCents     = 2.0f;    // (PI) +/-2 cents

// VR-8 VCF Width (F5 cycle = 2x F4): MULTIPLICATIVE scale on cutoff CV slope, as a
// fraction (vcfWidthScale = 1 + draw * this), NOT an additive offset.
inline constexpr float kCalBandVcfScale     = 0.015f;  // (PI) +/-1.5 %

// Cutoff offset — uncalibrated on hardware (§2.3): additive cents-equiv offset, the
// WIDEST band of the Tier-1 set.
inline constexpr float kCalBandCutoffOffset = 180.0f;  // (PI) +/-180 cents-equiv

// ---------------------------------------------------------------------------
// Variance spreads — frozen at note-on [docs/design/08 §7.1; ADR-009 §Decision 4,
// VV-7..VV-10]. cutoff/PW are ADDITIVE in the parameter's native domain; env-time
// and glide are MULTIPLICATIVE (scale = 1 + draw * band) on the time constant. Each
// var.* param (0..1) scales its band; param=0 => zero perturbation.
//
// Cutoff gets the WIDEST band of the variance set (uncalibrated, §2.3 / §7.1).
// ---------------------------------------------------------------------------

// Cutoff variance (mw101.var.cutoff): additive cents-equiv offset, widest band.
inline constexpr float kVarCutoffCents = 300.0f;  // (PI) +/-300 cents-equiv

// Env-time variance (mw101.var.env_time): multiplicative on A/D/R time constants.
// The +/-5..20 % magnitude is an unverified RC-tolerance heuristic, embellishment
// [docs/design/08 §7.1; research/09 §3.4, §8.2]; default to the upper bound.
inline constexpr float kVarEnvBand     = 0.20f;   // (PI) +/-20 %

// PW variance (mw101.var.pw): additive duty-cycle fraction.
inline constexpr float kVarPwFrac      = 0.04f;   // (PI) +/-4 % duty

// Glide variance (mw101.var.glide): multiplicative on the glide time constant.
inline constexpr float kVarGlideBand   = 0.15f;   // (PI) +/-15 %

// ---------------------------------------------------------------------------
// Tier-3 slop shape selector [docs/design/08 §6; ADR-009 §Decision 3]. A labelled
// taste choice between Box-Muller Gaussian (default) and the cubic (2u-1)^3 shaper;
// both are zero-mean. Used by drawSlopCents.
// ---------------------------------------------------------------------------
enum class SlopShape : int {
    Gaussian = 0,   // (PI) default — Box-Muller N(0,1) * slopCents
    Cubic    = 1,   // (PI) labelled alternative — (2u-1)^3 * slopCents
};

inline constexpr SlopShape kSlopShape = SlopShape::Gaussian;  // (PI) default

// Output de-zipper time constant for the mandatory per-voice smoother [docs/design/08
// §9; ADR-009 VV-15]. Within the mandated 5..20 ms window. Owned here as the single
// (PI) home; DriftModel (vintage-4) calls setTimeConstant with it in prepare().
inline constexpr float kDriftSmoothMs = 8.0f;     // (PI) 8 ms, in [5,20] ms

} // namespace mw::cal::drift
