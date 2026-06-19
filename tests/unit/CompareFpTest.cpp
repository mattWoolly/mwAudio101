// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-2 golden support: the CLASS-FP two-stage comparer (task 043). Test-case
// names begin with "golden" so `ctest -R golden` (and `-R class-fp`) selects them
// (silent-pass rule, AGENTS.md / docs/design/11 §8.3). This realizes the CLASS-FP
// compare contract: docs/design/11 §6.1 (Stage gating), §6.3 (Stage-1 fingerprint +
// Stage-2 NMSE/alias-floor + FpTolerance), and ADR-013 C6/C7/C9/C22 + ADR-023 V11.
//
// Acceptance coverage (plan/backlog/043):
//  - Stage 2 is SKIPPED when Stage 1 is within tolerance and full==false, and RUN on
//    a Stage-1 flag or full==true [ADR-013 C9; §6.1]
//  - With tol.maxAbsErr==0 a 1-ULP diff FAILS (arm64 bit-exact); with a band the same
//    diff PASSES inside band and FAILS outside (paired) [ADR-013 C6, C7]
//  - compareFp REFUSES (no pass) when the blessed EngineTag differs in ladder /
//    oversample / renderVersion [ADR-013 C22; ADR-023 V11]
//  - oracle: Stage-2 windowed-FFT NMSE detects a spectral difference an identical
//    signal does not, and the alias-floor metric flags energy above the perceptual
//    limit [docs/design/11 §6.3; docs/research/10 §8]
//
// Tagged [golden] (already in the snapshot) AND [class-fp] (a NEW tag the orchestrator
// will pick up at wave integration — an expected, transient red on labels_snapshot
// ONLY, never on the scoped -R class-fp / -R golden selection).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "../../core/calibration/CompareFpConstants.h"
#include "../golden/CompareFp.h"
#include "../golden/GoldenKey.h"

namespace {

using mw::golden::compareFp;
using mw::golden::DeterminismClass;
using mw::golden::EngineTag;
using mw::golden::FpResult;
using mw::golden::FpTolerance;
using mw::golden::LadderEngine;
using mw::golden::RenderResult;

constexpr double kPi = 3.14159265358979323846;

// A canonical blessed engine tag (matches the GoldenKeyTest canonical key).
EngineTag canonicalEngine() noexcept {
    return EngineTag{LadderEngine::Huovilainen, /*oversampleFactor=*/2, /*renderVersion=*/1};
}

// Synthesize a sine of `freqHz` at `sampleRate` for `n` samples.
RenderResult makeSine(double freqHz, double sampleRate, int n,
                      const EngineTag& eng, float amp = 0.5f) {
    RenderResult r{};
    r.sampleRate = sampleRate;
    r.engine     = eng;
    r.samples.resize(static_cast<std::size_t>(n));
    const double w = 2.0 * kPi * freqHz / sampleRate;
    for (int i = 0; i < n; ++i)
        r.samples[static_cast<std::size_t>(i)] =
            amp * static_cast<float>(std::sin(w * static_cast<double>(i)));
    return r;
}

// The arm64 bless tolerance: bit-exact (maxAbsErr == 0). [docs/design/11 §6.3; C6]
FpTolerance arm64Tolerance() noexcept {
    FpTolerance t{};
    t.maxAbsErr           = 0.0;       // bit-exact gate
    t.rmsErr              = 0.0;
    t.nmseDbCeiling       = -120.0;
    t.aliasFloorDbCeiling = -60.0;
    return t;
}

// A representative Linux/Windows band (the §6.4 (PI) seed values). [C7]
FpTolerance bandedTolerance() noexcept {
    FpTolerance t{};
    t.maxAbsErr           = 1.0e-6;    // banded
    t.rmsErr              = 1.0e-7;
    t.nmseDbCeiling       = -120.0;
    t.aliasFloorDbCeiling = -60.0;
    return t;
}

} // namespace

// --- C9 / §6.1: Stage-2 gating ----------------------------------------------------

TEST_CASE("golden: class-fp Stage 2 is SKIPPED when Stage 1 is within tolerance and "
          "full is false [ADR-013 C9]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    const auto blessed = makeSine(220.0, 48000.0, 8192, eng);
    const auto got     = blessed;   // identical -> Stage 1 well within any band

    const FpResult res = compareFp(got, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE(res.pass);
    REQUIRE_FALSE(res.ranStage2);     // cheap path: no Stage 2
    REQUIRE_FALSE(res.s2.has_value());
}

TEST_CASE("golden: class-fp Stage 2 RUNS when full is true even on an identical signal "
          "[ADR-013 C9]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    const auto blessed = makeSine(220.0, 48000.0, 8192, eng);
    const auto got     = blessed;

    const FpResult res = compareFp(got, blessed, bandedTolerance(), /*full=*/true);
    REQUIRE(res.ranStage2);           // forced full -> Stage 2 ran
    REQUIRE(res.s2.has_value());
    REQUIRE(res.pass);                // identical signal still passes
}

TEST_CASE("golden: class-fp Stage 2 RUNS on a Stage-1 flag (out-of-band difference) "
          "[ADR-013 C9]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    const auto blessed = makeSine(220.0, 48000.0, 8192, eng);
    auto got = blessed;
    // A gross perturbation that blows the Stage-1 band -> Stage 1 flags -> Stage 2 runs.
    for (auto& s : got.samples) s += 0.05f;

    const FpResult res = compareFp(got, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE(res.ranStage2);           // Stage-1 flag escalated to Stage 2
    REQUIRE(res.s2.has_value());
    REQUIRE_FALSE(res.pass);          // out of band -> FAILS
}

// --- C6: arm64 bit-exact (maxAbsErr == 0) -----------------------------------------

TEST_CASE("golden: class-fp with maxAbsErr==0 FAILS on a 1-ULP diff (arm64 bit-exact) "
          "[ADR-013 C6]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    const auto blessed = makeSine(220.0, 48000.0, 4096, eng);
    auto got = blessed;

    // Perturb exactly one sample by one ULP.
    float& v = got.samples[1000];
    v = std::nextafter(v, std::numeric_limits<float>::infinity());
    REQUIRE(v != blessed.samples[1000]);   // sanity: it really changed

    const FpResult res = compareFp(got, blessed, arm64Tolerance(), /*full=*/false);
    REQUIRE_FALSE(res.pass);          // bit-exact gate: any nonzero diff FAILS
    REQUIRE(res.s1.maxAbsErr > 0.0);

    // Paired positive control: the SAME signal vs itself passes bit-exact.
    const FpResult same = compareFp(blessed, blessed, arm64Tolerance(), /*full=*/false);
    REQUIRE(same.pass);
    REQUIRE(same.s1.maxAbsErr == 0.0);
}

// --- C7: banded — same 1-ULP diff passes inside band, fails outside (paired) ------

TEST_CASE("golden: class-fp 1-ULP diff PASSES inside a band and FAILS outside it "
          "(paired) [ADR-013 C7]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    const auto blessed = makeSine(220.0, 48000.0, 4096, eng);
    auto got = blessed;

    float& v = got.samples[1000];
    v = std::nextafter(v, std::numeric_limits<float>::infinity());
    const double ulp = std::abs(static_cast<double>(v) -
                                static_cast<double>(blessed.samples[1000]));
    REQUIRE(ulp > 0.0);

    // Inside band: maxAbsErr band (1e-6) comfortably exceeds a single ~3e-8 float ULP.
    FpResult inBand = compareFp(got, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE(inBand.pass);

    // Outside band: tighten the band BELOW the diff -> the same diff now FAILS.
    FpTolerance tight = bandedTolerance();
    tight.maxAbsErr = ulp * 0.5;      // band tighter than the 1-ULP error
    FpResult outBand = compareFp(got, blessed, tight, /*full=*/false);
    REQUIRE_FALSE(outBand.pass);
}

// --- C22 / V11: engine-context refusal --------------------------------------------

TEST_CASE("golden: class-fp REFUSES when the blessed engine tag differs in ladder "
          "[ADR-013 C22]",
          "[golden][class-fp]") {
    const auto blessed = makeSine(220.0, 48000.0, 4096, canonicalEngine());
    auto got = blessed;
    got.engine.ladder = LadderEngine::ZDF;   // cross-engine

    const FpResult res = compareFp(got, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE(res.refused);             // refusal is explicit
    REQUIRE_FALSE(res.pass);          // a refusal is NOT a pass
}

TEST_CASE("golden: class-fp REFUSES when oversample factor differs [ADR-013 C22]",
          "[golden][class-fp]") {
    const auto blessed = makeSine(220.0, 48000.0, 4096, canonicalEngine());
    auto got = blessed;
    got.engine.oversampleFactor = 1;   // 2 -> 1

    const FpResult res = compareFp(got, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE(res.refused);
    REQUIRE_FALSE(res.pass);
}

TEST_CASE("golden: class-fp REFUSES when renderVersion differs [ADR-023 V11]",
          "[golden][class-fp]") {
    const auto blessed = makeSine(220.0, 48000.0, 4096, canonicalEngine());
    auto got = blessed;
    got.engine.renderVersion = 2;      // 1 -> 2

    const FpResult res = compareFp(got, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE(res.refused);
    REQUIRE_FALSE(res.pass);

    // Paired positive control: identical engine tag is NOT refused.
    const FpResult ok = compareFp(blessed, blessed, bandedTolerance(), /*full=*/false);
    REQUIRE_FALSE(ok.refused);
    REQUIRE(ok.pass);
}

// --- Stage-2 spectral oracle ------------------------------------------------------

TEST_CASE("golden: class-fp Stage-2 NMSE is very low for identical spectra and high "
          "for a different tone (oracle) [docs/design/11 6.3]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    const auto blessed = makeSine(440.0, 48000.0, 8192, eng);

    // Identical: NMSE floor (well below the ceiling); a stub returning a constant
    // would not distinguish this from the divergent case below.
    const FpResult same = compareFp(blessed, blessed, bandedTolerance(), /*full=*/true);
    REQUIRE(same.s2.has_value());
    REQUIRE(same.s2->nmseDb < -120.0);

    // Different tone (660 Hz vs 440 Hz) at the same RMS: a large spectral mismatch.
    const auto other = makeSine(660.0, 48000.0, 8192, eng);
    const FpResult diff = compareFp(other, blessed, bandedTolerance(), /*full=*/true);
    REQUIRE(diff.s2.has_value());
    REQUIRE(diff.s2->nmseDb > -20.0);          // grossly different spectrum
    REQUIRE(diff.s2->nmseDb > same.s2->nmseDb); // strictly worse than identical
}

TEST_CASE("golden: class-fp Stage-2 alias-floor flags energy above the perceptual "
          "limit (oracle) [docs/design/11 6.3; research/10 8]",
          "[golden][class-fp]") {
    const auto eng = canonicalEngine();
    // Blessed: a low tone with negligible energy above the alias limit.
    const auto blessed = makeSine(220.0, 48000.0, 8192, eng);

    // Got: a high tone (12 kHz) ENTIRELY above the ~2135 Hz perceptual limit, so the
    // residual (got - blessed) carries strong above-limit energy.
    const auto aliasy = makeSine(12000.0, 48000.0, 8192, eng);

    const FpResult clean   = compareFp(blessed, blessed, bandedTolerance(), /*full=*/true);
    const FpResult dirty   = compareFp(aliasy, blessed, bandedTolerance(), /*full=*/true);
    REQUIRE(clean.s2.has_value());
    REQUIRE(dirty.s2.has_value());

    // The above-limit residual energy is far higher when a 12 kHz residual is present.
    REQUIRE(dirty.s2->aliasFloorDb > clean.s2->aliasFloorDb + 40.0);
}
