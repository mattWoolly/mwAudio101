// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/LadderResonanceConstants.h — (PI) calibration constants for the
// LadderFilter RESONANCE loop (task 039): the resonance->k taper, the output-side
// make-up Q depth, and the feedback-path diode-clamp threshold.
//
// These belong to the `mw::cal::vcf` namespace documented in
// core/calibration/Calibration.h (§9 of docs/design/02-dsp-filter.md). They live in a
// SEPARATE header so the parallel development fleet does not serialize on
// Calibration.h; the orchestrator wires the include into Calibration.h later
// [docs/design/02-dsp-filter.md §9; ADR-008 §1; docs/design/00 §8.3; ADR-003 F-15].
//
// This header EXTENDS the `mw::cal::vcf` namespace already opened by
// FastTanhConstants.h (invTwoVt, tanhCoeffs, kTanhClamp), FilterTablesConstants.h
// (kFcRefHz, kCompFit, …) and LadderFilterConstants.h (kAntiDenorm); it does NOT
// redefine any of those. It adds only the resonance-loop (PI) constants that
// LadderFilterConstants.h explicitly deferred to "core-filter-5 scope": kMax,
// kResoCurveExp, makeUpDepth and vClamp [LadderFilterConstants.h header note;
// docs/design/02-dsp-filter.md §9 vcf::kMax/kResoCurveExp/makeUpDepth/vClamp].
//
// Every value here is (PI) — a *pragmatic invention*, NOT a measured SH-101 spec.
// LadderFilter READS this; it never inlines a (PI) numeric literal at the DSP call
// site [ADR-003 F-15; docs/design/02 §10 F-15]. The values are frozen/versioned and
// part of the bit-exact bless contract [ADR-003 F-14].

#pragma once

#include <cstddef>

namespace mw::cal::vcf {

// Normalized self-oscillation loop gain reached at reso01 = 1.0
// [docs/design/02-dsp-filter.md §5.1, §9 vcf::kMax; docs/research/10 §3.4].
//
// In the ideal continuous Stilson-Smith / Zavalishin normalized model the
// self-oscillation onset is at k = 4 [docs/research/10 §3.4, §8]. The shipping engine
// is a DISCRETE Huovilainen cascade with the forward-Euler + two-sample-average
// feedback and a tanh transconductor on the input/feedback node (§5.5), whose discrete
// onset sits a little ABOVE the continuous k = 4 (measured ~4.4 here) because of the
// half-sample residual phase error the comp leaves below Fs/4 (the <10%-at-2x error of
// docs/research/10 §3.8). So reso01 = 1.0 must map to a k past the continuous 4.0 to
// actually REACH onset — and far enough past it to establish a STABLE oscillation whose
// amplitude is the diode-clamp fixed point, not a knife-edge balance.
//
// kMax = 8.0 places reso01 = 1.0 firmly in the self-oscillating regime (well above the
// ~4.4 discrete onset): the loop self-oscillates within ~20 ms to a clean, low-THD sine
// (THD < 1%), the frequency tracks cutoff within the <10% bound across the audio range,
// and the diode clamp (§5.4) governs the amplitude to a fixed point that is insensitive
// to k perturbations near reso01 = 1 (a ~23% k change from reso01 0.9->1.0 moves the
// amplitude only ~11%). This is (PI) and deliberate: k = 4 is a normalized MODEL value,
// NOT an SH-101 pot value [docs/design/02 §5.1 "(PI)" note; ADR-003 Decision], and the
// amplitude is set by the clamp fixed point, NOT by a knife-edge k, so the headroom past
// 4 is bounded by construction [ADR-003 F-05]. (PI).
inline constexpr float kMax = 8.0f;  // (PI) — loop gain at reso01 = 1 (firmly self-osc)

// Resonance taper exponent: the control->loop-gain law is k = kMax * reso01^exp, a
// unit-output x^p curve (resonanceCurve(1) == 1) [docs/design/02 §5.1, §9
// vcf::kResoCurveExp]. The SH-101 resonance-pot-to-feedback law is unmeasured, so the
// taper is a pragmatic invention [docs/research/03 §9.1]. exp = 2 gives a gentle
// quadratic ramp that keeps the low-resonance regime nearly linear (where the TPT
// cross-check applies, F-13) and concentrates the steep loop-gain rise near the top of
// the control where self-oscillation onset lives. (PI).
inline constexpr float kResoCurveExp = 2.0f;  // (PI) — resonance taper exponent (x^p)

// Output-side make-up Q depth: makeUpGain = 1 + makeUpDepth * resonanceCurve(reso01)
// [docs/design/02 §5.3, §9 vcf::makeUpDepth; ADR-003 F-06]. OTA 4-pole filters lose
// passband level as resonance rises; the SH-101 compensates on the OUTPUT side (routed
// to the VCA drive), NOT by boosting the filter input. The output-boost gain-vs-
// resonance law is unmeasured, so the depth is a pragmatic invention
// [docs/research/03 §4.3, §9.1]. 0.5 gives up to +3.5 dB of make-up at full resonance.
// This module only COMPUTES the scalar (exposed via makeUpGain()); the VCA stage
// applies it [docs/design/02 §1.2, §5.3]. (PI).
inline constexpr float makeUpDepth = 0.5f;  // (PI) — output-side Q make-up depth

// Diode-clamp threshold (model units) for the feedback-path amplitude governor
// [docs/design/02 §5.4, §9 vcf::vClamp; ADR-003 F-04]. The clamp is a memoryless soft
// saturator on the (inverting) feedback signal that "reduces the level as soon as it
// starts to conduct", keeping self-oscillation a fairly clean sine and bounding the
// loop to a fixed point [docs/research/03 §4.2]. Its small-signal slope is unity (so it
// does NOT change the resonant onset) but it limits large feedback excursions. The
// exact clipping-diode part / feedback-node threshold is reverse-engineered and
// unmeasured, so vClamp is a pragmatic invention [docs/research/03 §4.2, §9.3, §9.6].
// 0.1 sets the self-oscillation amplitude to a clean, low-THD sine in the modeled
// signal range. (PI).
inline constexpr float vClamp = 0.1f;  // (PI) — feedback diode-clamp threshold

} // namespace mw::cal::vcf
