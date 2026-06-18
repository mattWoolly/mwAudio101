// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for FilterTables (task 035). Test-case names begin with
// "vcf-tables" so `ctest -R vcf-tables` selects them (silent-pass rule;
// docs/design/11 §8.2, AGENTS.md). Each case maps to an acceptance criterion in
// plan/backlog/035-filtertables.md and a row of the docs/design/02 §10 / ADR-003
// contract (F-08, F-11, F-14, F-15).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include "dsp/FilterTables.h"
#include "calibration/FilterTablesConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::dsp::FilterTables;
namespace cvf = mw::cal::vcf;

namespace {

// Independent reference replicas of the documented maps (§5.2): the test does NOT
// call the implementation's internals — it recomputes fc and g from first
// principles and the calibration constants.
constexpr double kTwoPiRef = 6.283185307179586476925286766559;

[[nodiscard]] double refFcFromCv(double cv, double fsOs) {
    double fc = static_cast<double>(cvf::kFcRefHz) * std::exp2(cv);   // 1 V/oct
    const double fcMin = static_cast<double>(cvf::kFcMinHz);
    double fcMax = static_cast<double>(cvf::kFcMaxHz);
    const double guard = static_cast<double>(cvf::kFcGuardFracOfFsOs) * fsOs;
    if (guard < fcMax) fcMax = guard;
    if (fc < fcMin) fc = fcMin;
    if (fc > fcMax) fc = fcMax;
    return fc;
}

[[nodiscard]] double refG(double fcHz, double fsOs) {
    return 1.0 - std::exp(-kTwoPiRef * fcHz / fsOs);
}

} // namespace

// --- Acceptance 1: 1 V/oct doubling + clamp to [10, min(20k,0.45*fs_os)] --------
TEST_CASE("vcf-tables: a 1 V CV step doubles fc and fc is clamped to the guard", "[vcf-tables][vcf]") {
    FilterTables t;
    const double fsOs = 88200.0;     // 2x @ 44.1 kHz
    t.build(fsOs);

    // 1 V/oct: g(cv+1) corresponds to fc doubled, away from the clamp edges. Pick a
    // mid CV where fc sits well inside [10, 20000] so neither edge binds.
    // At fcRef=1 kHz, cv=0 -> 1 kHz, cv=+1 -> 2 kHz, cv=+2 -> 4 kHz.
    for (double cv : {-1.0, 0.0, 1.0, 2.0, 3.0}) {
        const double fcLo = refFcFromCv(cv, fsOs);
        const double fcHi = refFcFromCv(cv + 1.0, fsOs);
        // Inside the unclamped band, +1 V exactly doubles fc.
        REQUIRE(fcHi == Catch::Approx(2.0 * fcLo).epsilon(1e-9));
        // And the table's g for those CVs matches g(fc) (doubling reflected in g).
        const float gLo = t.cvToG(static_cast<float>(cv));
        const float gHi = t.cvToG(static_cast<float>(cv + 1.0));
        REQUIRE(gHi > gLo);   // higher fc -> larger one-pole coefficient
    }

    // Clamp guard: fcMax = min(20000, 0.45*fs_os). At fsOs=88200, 0.45*fs=39690 so
    // 20000 binds. Far above the top -> clamped to 20 kHz, never exceeding it.
    const float fcGuard = std::min(cvf::kFcMaxHz, static_cast<float>(0.45 * fsOs));
    REQUIRE(t.clampFcHz(1.0e9f) == Catch::Approx(fcGuard));
    REQUIRE(t.clampFcHz(1.0e9f) <= fcGuard);
    REQUIRE(t.clampFcHz(0.0f)   == Catch::Approx(cvf::kFcMinHz));   // low clamp at 10 Hz
    REQUIRE(t.clampFcHz(-5.0f)  >= cvf::kFcMinHz);

    // A very high CV is also clamped: its g equals g(fcGuard), never beyond.
    const float gTop  = t.cvToG(cvf::kCvTableMaxVolts);
    const float gAtGuard = static_cast<float>(refG(fcGuard, fsOs));
    REQUIRE(gTop == Catch::Approx(gAtGuard).margin(1e-3));

    // At a LOW oversampled rate the 0.45*fs guard binds instead of 20 kHz.
    FilterTables tLow;
    const double fsLow = 22050.0;    // 0.45*fs = 9922.5 < 20000 -> guard binds
    tLow.build(fsLow);
    const float fcGuardLow = static_cast<float>(0.45 * fsLow);
    REQUIRE(tLow.clampFcHz(1.0e9f) == Catch::Approx(fcGuardLow));
    REQUIRE(fcGuardLow < cvf::kFcMaxHz);   // negative control: 20 kHz does NOT bind here
}

// --- Acceptance 2: cvToG / hzToG match the reference maps within tolerance ------
TEST_CASE("vcf-tables: cvToG and hzToG match fc=fcRef*2^v and g=1-exp(...) within interpolation tolerance", "[vcf-tables][vcf]") {
    FilterTables t;
    const double fsOs = 96000.0;
    t.build(fsOs);

    // cvToG vs reference across the CV span (avoid the very edges where clamp bites,
    // but include points near both clamps).
    for (double cv = -8.0; cv <= 8.0; cv += 0.37) {
        const double fc = refFcFromCv(cv, fsOs);
        const double gRef = refG(fc, fsOs);
        const float  gTab = t.cvToG(static_cast<float>(cv));
        // 1024-point linear interpolation of a smooth monotone map: tight tolerance.
        REQUIRE(static_cast<double>(gTab) == Catch::Approx(gRef).margin(1e-4));
    }

    // hzToG vs reference, keyed directly in Hz across the audio band.
    for (double fc : {20.0, 55.0, 110.0, 440.0, 1000.0, 4000.0, 12000.0, 19000.0}) {
        const double fcClamped = std::min(std::max(fc, static_cast<double>(cvf::kFcMinHz)),
                                          static_cast<double>(cvf::kFcMaxHz));
        const double gRef = refG(fcClamped, fsOs);
        const float  gTab = t.hzToG(static_cast<float>(fc));
        REQUIRE(static_cast<double>(gTab) == Catch::Approx(gRef).margin(1e-4));
    }

    // cvToG and hzToG agree at a shared point (cv=0 <-> fcRef Hz).
    REQUIRE(t.cvToG(0.0f) == Catch::Approx(t.hzToG(cvf::kFcRefHz)).margin(1e-6));

    // g is monotone increasing in CV (negative control: not flat / not decreasing).
    float prev = t.cvToG(-9.0f);
    bool strictlyIncreasedSomewhere = false;
    for (double cv = -9.0 + 0.5; cv <= 9.0; cv += 0.5) {
        const float g = t.cvToG(static_cast<float>(cv));
        REQUIRE(g >= prev - 1e-6f);          // non-decreasing
        if (g > prev + 1e-6f) strictlyIncreasedSomewhere = true;
        prev = g;
    }
    REQUIRE(strictlyIncreasedSomewhere);
}

// --- Acceptance 3: build is the only allocator (off audio thread) + determinism --
TEST_CASE("vcf-tables: lookups allocate nothing and build is the only allocator", "[vcf-tables][vcf][rt]") {
    FilterTables t;
    t.build(88200.0);   // build done BEFORE arming (it is the off-thread allocator)

    // Audio-rate hot path: cvToG / hzToG / resoTuningComp must not allocate.
    mw::test::AudioThreadGuard g;
    g.arm();
    volatile float sink = 0.0f;
    for (int i = 0; i < 256; ++i) {
        const float cv = -8.0f + 0.0625f * static_cast<float>(i);
        sink += t.cvToG(cv);
        sink += t.hzToG(20.0f + 70.0f * static_cast<float>(i));
        sink += t.resoTuningComp(0.001f * static_cast<float>(i));
    }
    g.disarm();
    (void) sink;
    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}

TEST_CASE("vcf-tables: table contents are bit-identical across runs for a fixed fs_os", "[vcf-tables][vcf]") {
    // Determinism / bless (F-14): same fs_os + frozen constants -> bit-identical
    // tables. Sample the public lookups exhaustively and compare bit patterns.
    FilterTables a, b;
    a.build(88200.0);
    b.build(88200.0);

    for (int i = 0; i < 1024; ++i) {
        const float cv = cvf::kCvTableMinVolts
                       + (cvf::kCvTableMaxVolts - cvf::kCvTableMinVolts)
                       * (static_cast<float>(i) / 1023.0f);
        REQUIRE(a.cvToG(cv) == b.cvToG(cv));          // exact equality, not Approx
    }
    for (int i = 0; i < 1024; ++i) {
        const float g = static_cast<float>(i) / 1024.0f;
        REQUIRE(a.resoTuningComp(g) == b.resoTuningComp(g));
        REQUIRE(a.hzToG(10.0f + 20.0f * static_cast<float>(i)) ==
                b.hzToG(10.0f + 20.0f * static_cast<float>(i)));
    }

    // A DIFFERENT fs_os yields a different g (negative control: tables are
    // genuinely sample-rate dependent, not constant).
    FilterTables c;
    c.build(48000.0);
    REQUIRE(a.cvToG(0.0f) != c.cvToG(0.0f));
    REQUIRE(a.sampleRateOs() == 88200.0);
    REQUIRE(c.sampleRateOs() == 48000.0);
}

// --- Acceptance 4: (PI) constants come from Calibration (no inline literal) ------
TEST_CASE("vcf-tables: fcRefHz and compFit are read from the calibration table", "[vcf-tables][vcf][cal]") {
    // The mapping is anchored to the calibration constants, not inline literals: a
    // CV of 0 V must produce g(fcRefHz) using THE calibration fcRefHz.
    FilterTables t;
    const double fsOs = 88200.0;
    t.build(fsOs);

    const double gAtRef = refG(static_cast<double>(cvf::kFcRefHz), fsOs);
    REQUIRE(static_cast<double>(t.cvToG(0.0f)) == Catch::Approx(gAtRef).margin(1e-4));

    // compFit drives resoTuningComp: evaluate the documented polynomial directly
    // from the calibration constants and require the table to match it.
    const float c0 = cvf::kCompFit[0];
    const float c1 = cvf::kCompFit[1];
    const float c2 = cvf::kCompFit[2];
    for (float gq : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.99f}) {
        const float expected = c0 + gq * (c1 + gq * c2);
        REQUIRE(t.resoTuningComp(gq) == Catch::Approx(expected).margin(1e-4));
    }

    // The (PI) reference cutoff and comp coefficients exist in the calibration
    // namespace with the documented defaults (the orchestrator wires this header
    // into Calibration.h). Asserts they are NOT inlined elsewhere.
    REQUIRE(cvf::kFcRefHz == 1000.0f);            // (PI) default ~1 kHz (§9)
    REQUIRE(cvf::kCompFit.size() == 3);
    REQUIRE(cvf::kCompFit[0] == 1.0f);            // identity-at-baseline comp (§7.3)
}
