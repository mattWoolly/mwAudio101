// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/LadderFilterConstants.h — (PI) calibration constants for the
// shipping Huovilainen ladder LadderFilter (task 038, the LINEAR core: 4-stage
// cascade with feedback gain k forced to zero; resonance/feedback/diode-clamp/
// make-up are core-filter-5 and out of this task's scope).
//
// These belong to the `mw::cal::vcf` namespace documented in
// core/calibration/Calibration.h (§9 of docs/design/02-dsp-filter.md). They live in
// a SEPARATE header so the parallel development fleet does not serialize on
// Calibration.h; the orchestrator wires the include into Calibration.h later
// [docs/design/02-dsp-filter.md §9; ADR-008 §1; docs/design/00 §8.3; ADR-003 F-15].
//
// This header EXTENDS the `mw::cal::vcf` namespace already opened by
// FastTanhConstants.h (invTwoVt, tanhCoeffs, kTanhClamp) and FilterTablesConstants.h
// (kFcRefHz, …); it does NOT redefine any of those. It adds only the anti-denormal
// bias the LadderFilter's integrator state carries (§3.2, §5.5 step 5; ADR-003
// F-12). The resonance->k taper, make-up depth, diode-clamp threshold and per-stage
// drive asymmetry (§9 vcf::kMax/kResoCurveExp/makeUpDepth/vClamp/driveAsym) are NOT
// declared here: they are core-filter-5 scope (the feedback wiring) and are added by
// that task to keep this task's surface minimal.
//
// Every value here is (PI) — a pragmatic invention, NOT a measured SH-101 spec.
// LadderFilter READS this; it never inlines a (PI) numeric literal at the DSP call
// site [ADR-003 F-15; docs/design/02 §10 F-15].

#pragma once

namespace mw::cal::vcf {

// Anti-denormal bias added into every integrator-state accumulation so a
// drive-to-silence (decay) tail never enters the subnormal range and stalls the
// CPU [docs/design/02 §3.2, §5.5 step 5, §6 row "Anti-denormal bias", §9
// vcf::antiDenorm; ADR-003 F-12]. FTZ/DAZ is set by the engine at process entry;
// this bias is the in-state belt-and-suspenders. 1e-20 is far below any audible /
// normal value yet keeps the running state out of the subnormal band. (PI).
inline constexpr float kAntiDenorm = 1.0e-20f;  // (PI) — anti-denormal integrator bias

} // namespace mw::cal::vcf
