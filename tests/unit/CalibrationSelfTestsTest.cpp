// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/CalibrationSelfTestsTest.cpp — Layer-4 calibration-tool SELF-TESTS
// (task 079): planted-answer, disjoint cal/val split, negative control.
//
// Realizes docs/design/11 §12 and ADR-013 Layer 4 (C15/C16/C17). Test-case names
// begin with "cal" so `ctest -R cal --no-tests=error` selects them under the
// silent-pass rule (AGENTS.md / docs/design/11 §8.3); tagged [cal] (an existing
// snapshot label, tests/golden/corpus/ctest-labels.snapshot line "[cal]"). The
// display names avoid '[' so Catch2 does not mis-parse a tag from the name and
// break `-R` selection.
//
// Since mwAudio101 holds NO physical SH-101 oracle [docs/design/11 §1.3; research/13
// §1.1], the PLANTED ANSWER is the only oracle: a signal is synthesized from KNOWN
// params, the calibrator recovers them from the SAMPLES ALONE, and recovery is
// asserted within the (PI) tolerance from CalibrationSelfTestConstants.h (never a
// magic number in this TU). This is OFFLINE harness code; RT invariants are not in
// play [docs/design/11 §2.2].

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "calibration/CalibrationModel.h"
#include "calibration/CalibrationSelfTestConstants.h"

namespace {

using mw::cal::Calibrator;
using mw::cal::CalibrationParams;
using mw::cal::CalibrationResult;
using mw::cal::SignalSpec;
using mw::cal::synthesize;
using mw::cal::test::CalValSplit;
using mw::cal::test::makeCalValSplit;
using mw::cal::test::makePlantedFixture;
using mw::cal::test::makeWrongFixture;
using mw::cal::test::PlantedFixture;

namespace tol = mw::cal::selftest;

// Fractional recovery error for one parameter against its planted (known) value.
double relErr(double recovered, double known) noexcept {
    const double denom = std::abs(known) > 1.0e-12 ? std::abs(known) : 1.0;
    return std::abs(recovered - known) / denom;
}

// The worst (largest) fractional recovery error across all three params of a fixture.
double worstRecoveryRelErr(const CalibrationResult& r, const CalibrationParams& known) noexcept {
    const double ea = relErr(r.recovered.amplitude, known.amplitude);
    const double ef = relErr(r.recovered.frequencyHz, known.frequencyHz);
    const double ed = relErr(r.recovered.decayPerSec, known.decayPerSec);
    double w = ea;
    if (ef > w) w = ef;
    if (ed > w) w = ed;
    return w;
}

} // namespace

// === C15 — planted-answer recovery (+ paired echo/stub negative control) ===========

TEST_CASE("cal: planted-answer recovers known params within tolerance", "[cal]") {
    const SignalSpec spec{};                       // default 48 kHz / 4096 samples
    const PlantedFixture f = makePlantedFixture(/*seed=*/0xA53Cull, spec);

    // Sanity: the fixture is a real, non-trivial signal (positive control on the
    // synthesizer — a stub returning {} would fail this).
    REQUIRE(f.signal.size() == static_cast<std::size_t>(spec.numSamples));
    REQUIRE(f.knownParams.frequencyHz > 0.0);

    Calibrator cal;
    const CalibrationResult r = cal.calibrate(f.signal, f.spec);

    // The calibrator NEVER sees f.knownParams — only f.signal — so it cannot pass by
    // echoing the planted answer. Recovery must land within the (PI) tolerance.
    REQUIRE(worstRecoveryRelErr(r, f.knownParams) <= tol::kRecoveryRelTolerance);
    REQUIRE(r.accepted);                            // a consistent planted fixture is accepted

    // PAIRED NEGATIVE CONTROL [ADR-013 C15]: a calibrator that "succeeds" without
    // measuring the signal — here a stub that returns fixed default params instead of
    // estimating from the samples — MUST blow the same tolerance. This proves the
    // recovery assertion above is not vacuously satisfiable.
    const CalibrationParams stubAnswer{};           // defaults: 1.0 / 440 Hz / 8.0
    REQUIRE(worstRecoveryRelErr(CalibrationResult{ stubAnswer, 0.0, true },
                                f.knownParams) > tol::kRecoveryRelTolerance);
}

TEST_CASE("cal: planted-answer recovery holds across multiple seeds", "[cal]") {
    const SignalSpec spec{};
    Calibrator cal;
    // Several independent planted fixtures; every one must recover within tolerance.
    for (std::uint64_t s = 1; s <= 8; ++s) {
        const PlantedFixture f = makePlantedFixture(s * 0x9E3779B97F4A7C15ull, spec);
        const CalibrationResult r = cal.calibrate(f.signal, f.spec);
        INFO("seed index " << s
             << " known f=" << f.knownParams.frequencyHz
             << " recovered f=" << r.recovered.frequencyHz);
        REQUIRE(worstRecoveryRelErr(r, f.knownParams) <= tol::kRecoveryRelTolerance);
        REQUIRE(r.accepted);
    }
}

// === C16 — disjoint cal/val split, held-out error within tolerance ==================

TEST_CASE("cal: cal-val split is disjoint by construction", "[cal]") {
    const CalValSplit split = makeCalValSplit();
    REQUIRE(split.fitSet.size() == static_cast<std::size_t>(tol::kFitSetSize));
    REQUIRE(split.valSet.size() == static_cast<std::size_t>(tol::kValSetSize));

    // Seeds are drawn from two non-overlapping ranges => no seed appears in both sets.
    for (const auto& a : split.fitSet)
        for (const auto& b : split.valSet)
            REQUIRE(a.seed != b.seed);

    // The PARAMETER draws are disjoint too (distinct seeds draw distinct points): no
    // fit-set parameter point coincides with a validation-set parameter point.
    for (const auto& a : split.fitSet)
        for (const auto& b : split.valSet)
            REQUIRE_FALSE(a.knownParams == b.knownParams);
}

TEST_CASE("cal: held-out error on the disjoint validation set is within tolerance", "[cal]") {
    const CalValSplit split = makeCalValSplit();
    Calibrator cal;

    // "Fit" set A (here: confirm the calibrator recovers each fit fixture — the model
    // is self-describing per fixture, so the fit pass is the per-fixture recovery).
    for (const auto& f : split.fitSet) {
        const CalibrationResult r = cal.calibrate(f.signal, f.spec);
        REQUIRE(worstRecoveryRelErr(r, f.knownParams) <= tol::kRecoveryRelTolerance);
    }

    // HELD-OUT validation on the DISJOINT set B. If the calibrator had merely
    // memorized set A it would miss here; the held-out error must stay within the
    // (PI) held-out tolerance [ADR-013 C16].
    double worstHeldOut = 0.0;
    for (const auto& f : split.valSet) {
        const CalibrationResult r = cal.calibrate(f.signal, f.spec);
        const double e = worstRecoveryRelErr(r, f.knownParams);
        if (e > worstHeldOut) worstHeldOut = e;
        REQUIRE(r.accepted);                         // each held-out fixture is consistent
    }
    INFO("worst held-out relative error = " << worstHeldOut);
    REQUIRE(worstHeldOut <= tol::kHeldOutRelTolerance);
}

// === C17 — negative control: the calibrator must REJECT a wrong fixture =============

TEST_CASE("cal: negative control - calibrator rejects a deliberately-wrong fixture", "[cal]") {
    const SignalSpec spec{};
    Calibrator cal;

    // Positive control: the matching planted fixture (same seed) IS accepted, so the
    // rejection below is attributable to the corruption, not a calibrator that rejects
    // everything.
    const PlantedFixture good = makePlantedFixture(/*seed=*/0xBADC0DEull, spec);
    const CalibrationResult goodR = cal.calibrate(good.signal, good.spec);
    REQUIRE(goodR.accepted);

    // The deliberately-WRONG fixture: a signal no single damped sinusoid can fit. The
    // calibrator MUST reject it; acceptance here is a self-test FAILURE [ADR-013 C17].
    const PlantedFixture wrong = makeWrongFixture(/*seed=*/0xBADC0DEull, spec);
    const CalibrationResult wrongR = cal.calibrate(wrong.signal, wrong.spec);
    REQUIRE_FALSE(wrongR.accepted);

    // And the rejection is decisive: the wrong fixture's residual is well above the
    // acceptance floor, while the good one is well below it (paired property control).
    REQUIRE(wrongR.residual > tol::kAcceptResidualFloor);
    REQUIRE(goodR.residual  < tol::kAcceptResidualFloor);
    REQUIRE(wrongR.residual > goodR.residual);
}

// === Tolerance constants live in the calibration table, not inlined here ============

TEST_CASE("cal: recovery tolerances come from the calibration constants header", "[cal]") {
    // (PI) discipline: the test reads its tolerances from CalibrationSelfTestConstants.h
    // (the mw::cal home), never a magic literal in this TU [docs/design/11 §12].
    REQUIRE(tol::kRecoveryRelTolerance > 0.0);
    REQUIRE(tol::kHeldOutRelTolerance >= tol::kRecoveryRelTolerance); // val band >= fit band
    REQUIRE(tol::kAcceptResidualFloor > 0.0);
    REQUIRE(tol::kFitSeedBase != tol::kValSeedBase);                   // disjoint seed ranges
}
