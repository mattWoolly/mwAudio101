// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/CalibrationSelfTestConstants.h — (PI) tolerances + fixture
// sizing for the Layer-4 calibration-tool SELF-TESTS (task 079).
//
// Per the conflict-avoidance rule for the parallel development fleet, this module's
// constants land in a dedicated header that #includes (and extends the mw::cal
// namespace of) the shared core/calibration/Calibration.h, rather than being
// appended directly to it [AGENTS.md "(PI) discipline"; docs/design/00 §8.3].
//
// The recovery tolerance is a calibration-fixture constant (PI) that the design
// doc says "centralizes in core/calibration/Calibration.h, never duplicated in the
// test TU" [docs/design/11 §12]; it lives HERE in the mw::cal namespace so the
// test TU reads it from one home instead of inlining a magic number.
//
// Every value here is (PI) — a pragmatic invention. There is NO physical SH-101
// oracle, so the planted answer IS the only oracle the harness can manufacture; a
// tolerance is therefore a property of the synthetic fixture, never a measured fact
// [docs/design/11 §1.3, §12; ADR-013 Layer 4, C15-C17]. These feed the OFFLINE
// calibration self-tests only; no audio-thread / RT concern is in play
// [docs/design/11 §2.2].

#pragma once

#include <cstdint>

#include "Calibration.h"

namespace mw::cal::selftest {

// --- Recovery tolerances (planted-answer & disjoint cal/val) ----------------------
// The fractional tolerance the recovered parameter must fall within, relative to the
// planted (known) parameter: |recovered - known| / |known| <= kRecoveryRelTolerance.
// Chosen loose enough to absorb the estimator's discretization error on a finite,
// noiseless synthetic signal, yet far tighter than an echo-input stub could ever hit
// by accident [docs/design/11 §12 "recover within tolerance"; ADR-013 C15]. (PI).
inline constexpr double kRecoveryRelTolerance = 0.02; // (PI) — 2% of the planted value

// The held-out (validation) error tolerance for the disjoint cal/val split. The fit
// is performed per-fixture (each fixture is self-describing), so the held-out metric
// is the same recovery error measured on the DISJOINT validation set; it must stay
// within this band or overfitting/memorization is declared [docs/design/11 §12;
// ADR-013 C16]. (PI) — held marginally above the per-fixture recovery tolerance.
inline constexpr double kHeldOutRelTolerance = 0.03; // (PI) — 3% on the disjoint set

// --- Acceptance / rejection (negative control) -----------------------------------
// The calibrator ACCEPTS a fixture only if the normalized fit residual between the
// re-synthesized recovered signal and the fixture's signal is at or below this floor.
// A deliberately-wrong fixture (signal that does NOT match its labelled knownParams)
// produces a residual far above this floor and is REJECTED; acceptance of a wrong
// fixture is a self-test FAILURE [docs/design/11 §12 "MUST reject"; ADR-013 C17].
// (PI) — set well above a clean recovered fit's residual and well below a mismatched
// fixture's residual, so the accept/reject decision is unambiguous.
inline constexpr double kAcceptResidualFloor = 1.0e-2; // (PI) — normalized RMS residual

// --- Disjoint-split sizing (by construction) --------------------------------------
// The fit set (A) and validation set (B) are disjoint BY CONSTRUCTION: non-overlapping
// seed ranges and non-overlapping parameter draws [docs/design/11 §12; ADR-013 C16].
// These sizes pick how many planted fixtures populate each disjoint set. (PI).
inline constexpr int kFitSetSize        = 6;          // (PI) — fixtures in set A (FIT)
inline constexpr int kValSetSize        = 6;          // (PI) — fixtures in set B (VALIDATE)

// The base seeds for the two disjoint seed ranges. Set A draws seeds in
// [kFitSeedBase, kFitSeedBase + kFitSetSize); set B draws in
// [kValSeedBase, kValSeedBase + kValSetSize); the ranges never overlap. (PI).
inline constexpr std::uint64_t kFitSeedBase = 0x0000'A100'0000'0001ULL; // (PI)
inline constexpr std::uint64_t kValSeedBase = 0x0000'B200'0000'0001ULL; // (PI)

} // namespace mw::cal::selftest
