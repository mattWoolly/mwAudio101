// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/DriftModelConstants.h — the (PI) constant(s) the DriftModel
// orchestration engine owns (task 072): the VCO<->VCF drift coupling ratio.
//
// Per the parallel-fleet conflict-avoidance rule, this module's NEW (PI) constant
// lands in a dedicated header that #includes (and extends the mw::cal::drift
// namespace of) the shared core/calibration/Calibration.h, rather than editing that
// orchestrator file or the sibling DriftConstants.h / ThermalConstants.h directly
// [docs/design/06 §3.10; docs/design/00 §8.3; ADR-008 §1; AGENTS.md "(PI)
// centralization"]. The calibration orchestrator wires this include from
// Calibration.h later.
//
// Source contract: docs/design/08 §5.2 — `cutoffDriftCents = T * drift.depth *
// kVcfDriftRatio`. kVcfDriftRatio is the inferred-filter-tempco RETUNING KNOB; the
// "same -3300 ppm/degC" VCO/VCF equivalence is THEORY, not a measured IR3109 spec
// [docs/design/08 §5.2, §13; research/09 §3.3, §8.3; ADR-009 VV-13]. It is (PI) — a
// tunable default, NOT a measured SH-101 value.

#pragma once

#include "Calibration.h"

namespace mw::cal::drift {

// Tier-2 thermal coupling: the shared scalar T(t) drives BOTH VCO pitch and VCF
// cutoff through the SAME coefficient, so they wander together [docs/design/08 §5.2;
// ADR-009 §Decision 2, VV-13]. kVcfDriftRatio scales the cutoff side relative to the
// pitch side:
//   driftCents       = T * drift.depth                 (VCO pitch, cents)
//   cutoffDriftCents = T * drift.depth * kVcfDriftRatio (VCF cutoff, cents-equiv)
// Default 1.0 == the "same coefficient" assumption. Setting it to 0 removes cutoff
// drift while pitch drift persists (the §5.2 / VV-13 retuning property the tests
// assert). (PI) — inferred-tempco retuning knob, not a measured fact.
inline constexpr float kVcfDriftRatio = 1.0f;   // (PI) default 1.0 [§5.2, VV-13]

} // namespace mw::cal::drift
