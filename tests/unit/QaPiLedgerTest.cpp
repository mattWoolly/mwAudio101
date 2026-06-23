// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/QaPiLedgerTest.cpp — the test-enforced spine of the Phase-5 (PI)
// pragmatic-invention ledger sweep (task 155; docs/QA-REPORT.md "(PI)
// Pragmatic-Invention Ledger Sweep" section). It makes the sweep's CENTRAL
// invariant — "every (PI) constant has a SINGLE centralized home in
// core/calibration/*, never a silently-duplicated literal" (docs/design/11 §1,
// §4.2, §12; ADR-013 "ADRs & decisions" / AGENTS.md) — a MECHANICALLY-CHECKED
// guard rather than prose that can silently rot as constants drift.
//
// Test-case display names ALL begin with "qa" so `ctest -R qa --no-tests=error`
// selects this suite under the silent-pass rule (AGENTS.md "Tests"; docs/design/11
// §8.3). The tag is the EXISTING "[qa]" tag (no new tag => no ctest-labels.snapshot
// regen). No literal '[' appears in any display name so Catch2 never mis-parses a
// tag out of the name.
//
// WHAT THIS PINS (the ledger invariant, expressed as a test):
//  1. The named, correctly-centralized (PI) anchors the sweep tabulates each resolve
//     to their documented single home + value (Calibration.h / a core/calibration/*.h
//     header). A move or silent value-edit turns this red.
//  2. cal::smoothing::kSnapThreshold (Calibration.h:50, the de-zipper snap epsilon)
//     holds the value that core/params/Smoother.h textually duplicates inline
//     ("kept in sync with cal::smoothing::kSnapThreshold"). This makes the documented
//     duplication (QA-155 finding) a TRIPWIRE: if the canonical value is ever changed,
//     this assertion forces a deliberate review of the uncentralized duplicate rather
//     than letting the two drift apart silently (the §4.2 anti-pattern).
//  3. A paired negative control (ADR-013 C4 discipline): a deliberately-WRONG expected
//     value must NOT match, proving the check discriminates and is not vacuously green.
//
// It is read-only introspection (no engine state, no DSP render); it links mwcore +
// Catch2 only. It REPORTS the ledger invariant; it does NOT relocate any (PI) constant
// (out of scope per task 155 / AGENTS.md "Diagnose only").

#include <catch2/catch_test_macros.hpp>

#include "calibration/Calibration.h"
#include "calibration/CalibrationSelfTestConstants.h"
#include "calibration/CompareFpConstants.h"
#include "calibration/ControlDispatchLfoConstants.h"
#include "calibration/FilterGoldenCorpusConstants.h"
#include "calibration/MinBlepConstants.h"

// ===========================================================================
// (1) Named (PI) anchors resolve to their single centralized home + documented
//     value (docs/design/11 §1/§4.2/§12; the sweep table in docs/QA-REPORT.md).
//     Each constant is referenced through its calibration namespace ONLY — there is
//     no second textual literal here, so this suite cannot itself become a duplicate.
// ===========================================================================
TEST_CASE("qa pi ledger: named (PI) anchors resolve to their centralized home",
          "[qa]") {
    // §12 calibration recovery tolerance (CalibrationSelfTestConstants.h).
    REQUIRE(mw::cal::selftest::kRecoveryRelTolerance == 0.02);
    // §12 held-out (disjoint cal/val) band must sit at-or-above the fit band.
    REQUIRE(mw::cal::selftest::kHeldOutRelTolerance
            >= mw::cal::selftest::kRecoveryRelTolerance);

    // §4.2 paired-assert RMS floor fixture (FilterGoldenCorpusConstants.h).
    REQUIRE(mw::cal::golden::filter::kSelfOscRmsFloor == 1.0e-3);

    // §6.4 comparer (PI) seeds that DO live in a header (the FP tolerance BANDS
    // 1e-6/1e-7/-120 dB deliberately live ONLY in MANIFEST.toml, never as compile
    // constants — see the sweep notes; here we pin the header-resident comparer (PI)s).
    REQUIRE(mw::cal::golden::kStage1FlagMargin == 1.0);
    REQUIRE(mw::cal::golden::kStage2FftLength == 4096u);

    // LFO emphasis (PI) anchors (task 180 / ADR-029) — both default to unity and the
    // unselected gain MUST be > 0 so all three depth legs stay always-active.
    REQUIRE(mw::cal::dispatch::kLfoEmphasisSelected == 1.0f);
    REQUIRE(mw::cal::dispatch::kLfoEmphasisUnselected == 1.0f);
    REQUIRE(mw::cal::dispatch::kLfoEmphasisUnselected > 0.0f);

    // §3 minBLEP (PI) anchors — the DSP TU DERIVES table length from these (it does not
    // re-mint a literal), so pinning them here guards the derived 2*kZC*kOS length too.
    REQUIRE(mw::cal::minblep::kOversampling == 64);
    REQUIRE(mw::cal::minblep::kZeroCrossings == 16);
}

// ===========================================================================
// (2) Drift tripwire for the documented uncentralized duplicate: Smoother.h inlines
//     the literal 1.0e-5 ("kept in sync with cal::smoothing::kSnapThreshold"). Pin the
//     canonical value so any change to it forces a review of that duplicate (§4.2).
// ===========================================================================
TEST_CASE("qa pi ledger: de-zipper snap epsilon canonical value is pinned",
          "[qa]") {
    REQUIRE(mw::cal::smoothing::kSnapThreshold == 1.0e-5);
}

// ===========================================================================
// (3) Negative control (paired per ADR-013 C4): a deliberately-WRONG expected value
//     must NOT equal the centralized constant, proving these checks discriminate and
//     are not vacuously green (the silent-pass backstop for this suite).
// ===========================================================================
TEST_CASE("qa pi ledger: a wrong expected value does not match the centralized (PI)",
          "[qa]") {
    REQUIRE_FALSE(mw::cal::selftest::kRecoveryRelTolerance == 0.99);
    REQUIRE_FALSE(mw::cal::smoothing::kSnapThreshold == 1.0);
    REQUIRE_FALSE(mw::cal::golden::filter::kSelfOscRmsFloor == 0.0);
}
