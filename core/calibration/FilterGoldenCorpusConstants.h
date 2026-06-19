// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/FilterGoldenCorpusConstants.h — (PI) corpus-authoring constants
// for the FILTER CLASS-FP golden corpus, the EARLY freeze gate (task 048,
// golden-filter-corpus).
//
// Per the conflict-avoidance rule for the parallel development fleet, this module's
// constants land in a dedicated header that #includes (and extends the mw::cal
// namespace of) the shared core/calibration/Calibration.h, rather than being
// appended directly to it [AGENTS.md "ADRs & decisions"; docs/design/00 §8.3]. It
// also re-uses (does NOT redefine) the blessed sample-rate set already centralized in
// GoldenKeyConstants.h (mw::cal::golden::kBlessedSampleRatesHz) [docs/design/11 §5.2].
//
// These are the parameters of the THREE filter stimuli the corpus is blessed from —
// self-oscillation, a cutoff sweep, and a resonance sweep — rendered through the
// shipping ladder nonlinear path (the Huovilainen IR3109 cascade) and CLASS-FP
// compared per docs/design/11 §5.1/§5.2 + ADR-013 C6 + ADR-023 V12. Every numeric here
// is (PI) — a pragmatic stimulus/authoring choice, NOT a measured SH-101 spec
// [docs/design/11 §1.3; ADR-013 owner ratification]. The corpus proves the DSP still
// renders what it rendered when blessed; it can NEVER prove fidelity to a real SH-101.
//
// The ONE non-(PI) fact reproduced here is the normalized self-oscillation onset
// k = 4 of the ideal continuous Stilson-Smith / Zavalishin model: the corpus self-osc
// stimulus encodes the k >= 4 self-oscillation vs k = 3.9 silence distinction
// [docs/design/11 §4.2; docs/research/10 §3.4] using the offline linear TPT oracle
// (LadderReferenceTPT::setResonanceK), whose k axis is exactly that normalized model.

#pragma once

#include "Calibration.h"
#include "GoldenKeyConstants.h"   // mw::cal::golden::kBlessedSampleRatesHz (re-used, not redefined)

namespace mw::cal::golden::filter {

// --- Stimulus length ---------------------------------------------------------------
// (PI) per-rate render length, in BASE-rate frames, of each filter stimulus blob. Long
// enough that the self-oscillation tail settles to its diode-clamp fixed point and a
// cutoff/resonance sweep spans its full range, short enough to keep the four-rate
// corpus footprint bounded [docs/design/11 §5.4 Consequences — the rate axis multiplies
// the footprint by four]. (PI).
inline constexpr int kStimulusFrames = 8192;

// --- Cutoff-sweep stimulus ---------------------------------------------------------
// (PI) geometric cutoff sweep endpoints (Hz) for the cutoff-sweep stimulus, driven on a
// fixed sub-self-oscillation resonance so the sweep is a stable filtered tone, not a
// runaway. The endpoints sit comfortably inside the table's clamped range
// [fcMinHz, 0.45*fs_os] at every blessed rate [docs/design/02 §5.2; ADR-003 F-08]. (PI).
inline constexpr float kSweepFcLoHz = 120.0f;
inline constexpr float kSweepFcHiHz = 6000.0f;

// (PI) the fixed excitation tone (Hz) fed through the cutoff/resonance sweeps, and its
// amplitude — a steady saw-like sine kept small so the nonlinear cascade stays in its
// modeled signal range. (PI).
inline constexpr float kSweepExciteHz  = 220.0f;
inline constexpr float kSweepExciteAmp = 0.25f;

// (PI) the fixed cutoff (Hz) the resonance-sweep stimulus holds while resonance ramps
// 0 -> 1 across the blob (so the sweep traverses from a near-linear lowpass up into the
// self-oscillating regime). (PI).
inline constexpr float kResoSweepFcHz = 1000.0f;

// --- Self-oscillation stimulus -----------------------------------------------------
// (PI) the self-oscillation stimulus cutoff (Hz) and the one-shot kick amplitude that
// seeds the ring (after the kick the input is silent, so steady output is genuine
// self-oscillation, not a forced response) [docs/design/02 §5.6]. (PI).
inline constexpr float kSelfOscFcHz   = 1000.0f;
inline constexpr float kSelfOscKick   = 0.5f;

// (PI) RMS floor (over the settled tail) separating "self-oscillating" from "silent".
// Mirrors the docs/design/11 §4.2 kSelfOscRmsFloor fixture constant: the paired
// positive/negative control is rmsAfterSettle(k>=4) > floor and rmsAfterSettle(k=3.9)
// < floor. A stubbed-to-constant filter fails one side or the other [ADR-013 C4]. (PI).
inline constexpr double kSelfOscRmsFloor = 1.0e-3;

// (PI) the number of tail frames over which the settled RMS is measured for the self-
// osc distinction (the leading transient is excluded so the kick decay does not inflate
// the "silent" reading). (PI).
inline constexpr int kSelfOscSettleFrames = 2048;

} // namespace mw::cal::golden::filter
