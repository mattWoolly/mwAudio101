// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DriftStateTest.cpp — Layer-1 unit tests for the per-voice DriftState
// POD and the Tier-1 / Tier-3 / variance draw helpers (task 065).
//
// Test-case names begin with "vintage_draws" so `ctest -R vintage_draws` selects
// them (silent-pass discipline, AGENTS.md). They cover every Acceptance criterion of
// plan/backlog/065:
//   - VR-8 is a SCALE on vcfWidthScale, NOT an offset, independent of cutoffOffset
//     (§4.1);
//   - the cutoff variance/offset band is the WIDEST of the variance/cal set for equal
//     spread (§4.1, §7.1);
//   - env-time/glide are MULTIPLICATIVE (1 + draw*band); cutoff/PW are ADDITIVE (§7.1);
//   - spread/var = 0 yields ZERO perturbation, and draws are deterministic for a
//     fixed seed (§4.2, §8.1/§8.2, VV-17).
//
// All figures here are reproducibility/statistical anchors, NOT measured SH-101 specs.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "dsp/drift/DriftState.h"
#include "calibration/DriftConstants.h"

using mw::dsp::drift::CalibrationDraw;
using mw::dsp::drift::NoteOnOffsets;
using mw::dsp::drift::DriftState;
using mw::dsp::drift::Xorshift128p;
using mw::dsp::drift::drawCalibration;
using mw::dsp::drift::drawNoteOn;
using mw::dsp::drift::drawSlopCents;
namespace cal = mw::cal::drift;

namespace {

// Largest |value| over many fixed-seed draws of a single field, to estimate the
// effective half-band of an additive draw (uniform => max -> band as N grows).
template <class Pick>
float observedHalfBand(std::uint64_t seed, int n, Pick pick) {
    Xorshift128p rng(seed);
    float maxAbs = 0.0f;
    for (int i = 0; i < n; ++i) maxAbs = std::max(maxAbs, std::abs(pick(rng)));
    return maxAbs;
}

} // namespace

// --- Acceptance: VR-8 is a SCALE on vcfWidthScale, NOT an offset (§4.1) -----------

TEST_CASE("vintage_draws VR-8 perturbs vcfWidthScale as a multiplier centered on one", "[vintage_draws]") {
    // Over many draws at full spread, vcfWidthScale must straddle 1.0 (a multiplier),
    // never behave like an additive offset centered on 0.
    Xorshift128p rng(0xC0FFEE1234567890ULL);
    float minS = 2.0f, maxS = 0.0f;
    double mean = 0.0;
    constexpr int kN = 50000;
    for (int i = 0; i < kN; ++i) {
        const CalibrationDraw d = drawCalibration(rng, 1.0f);
        minS = std::min(minS, d.vcfWidthScale);
        maxS = std::max(maxS, d.vcfWidthScale);
        mean += static_cast<double>(d.vcfWidthScale);
    }
    mean /= static_cast<double>(kN);

    REQUIRE(mean == Catch::Approx(1.0).margin(0.01));  // centered on UNITY, not zero
    REQUIRE(minS < 1.0f);                              // both sides of 1.0 reached
    REQUIRE(maxS > 1.0f);
    // Bounded within the (PI) VR-8 band: 1 +/- kCalBandVcfScale.
    REQUIRE(minS >= 1.0f - cal::kCalBandVcfScale - 1.0e-4f);
    REQUIRE(maxS <= 1.0f + cal::kCalBandVcfScale + 1.0e-4f);
}

TEST_CASE("vintage_draws VR-8 width scale is independent of the cutoff offset path", "[vintage_draws]") {
    // The VR-8 width SCALE and the uncalibrated cutoff OFFSET are distinct fields with
    // distinct domains: zeroing cal.spread leaves the scale at exactly 1.0 (identity)
    // while the offset is exactly 0.0 — proving they are separate, not the same path.
    Xorshift128p rng(0x1111222233334444ULL);
    const CalibrationDraw z = drawCalibration(rng, 0.0f);
    REQUIRE(z.vcfWidthScale == 1.0f);   // scale identity
    REQUIRE(z.cutoffOffset  == 0.0f);   // offset zero

    // At nonzero spread the two move on different scales (multiplicative ~1 vs an
    // additive cents-equiv offset that can be hundreds of cents).
    Xorshift128p r2(0x5555666677778888ULL);
    float maxScaleDev = 0.0f, maxOffsetMag = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        const CalibrationDraw d = drawCalibration(r2, 1.0f);
        maxScaleDev  = std::max(maxScaleDev, std::abs(d.vcfWidthScale - 1.0f));
        maxOffsetMag = std::max(maxOffsetMag, std::abs(d.cutoffOffset));
    }
    REQUIRE(maxScaleDev < 0.1f);        // scale stays near unity (a fractional trim)
    REQUIRE(maxOffsetMag > 10.0f);      // offset is a large additive cents-equiv band
}

// --- Acceptance: cutoff band is the WIDEST of the variance / cal set (§4.1, §7.1) --

TEST_CASE("vintage_draws cutoff offset is the widest Tier-1 additive band", "[vintage_draws]") {
    // §4.1: the uncalibrated cutoff offset legitimately gets the most generous Tier-1
    // band, wider than the additive Tune-cents band.
    const float cutoffHalf = observedHalfBand(0xAAAA, 200000,
        [](Xorshift128p& r) { return drawCalibration(r, 1.0f).cutoffOffset; });
    const float tuneHalf = observedHalfBand(0xBBBB, 200000,
        [](Xorshift128p& r) { return drawCalibration(r, 1.0f).tuneCents; });

    REQUIRE(cutoffHalf > tuneHalf);                     // cutoff offset is widest
    // And the (PI) constants encode that intent directly.
    REQUIRE(cal::kCalBandCutoffOffset
            > cal::kCalBandTuneCents + cal::kCalBandDacCents);
}

TEST_CASE("vintage_draws cutoff variance is the widest additive variance band", "[vintage_draws]") {
    // §7.1: the cutoff variance is the widest of the variance set; compare it against
    // the other ADDITIVE native-domain spread, PW (the only apples-to-apples one).
    const float cutoffHalf = observedHalfBand(0x1234, 200000,
        [](Xorshift128p& r) { return drawNoteOn(r, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f).varCutoff; });
    const float pwHalf = observedHalfBand(0x5678, 200000,
        [](Xorshift128p& r) { return drawNoteOn(r, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f).varPw; });

    REQUIRE(cutoffHalf > pwHalf);                       // cutoff is the widest band
    // The (PI) cutoff cents band exceeds every other variance band magnitude.
    REQUIRE(cal::kVarCutoffCents > cal::kVarEnvBand);
    REQUIRE(cal::kVarCutoffCents > cal::kVarPwFrac);
    REQUIRE(cal::kVarCutoffCents > cal::kVarGlideBand);
}

// --- Acceptance: env-time/glide MULTIPLICATIVE; cutoff/PW ADDITIVE (§7.1) ----------

TEST_CASE("vintage_draws env-time and glide apply as multiplicative scales about one", "[vintage_draws]") {
    Xorshift128p rng(0xDEADBEEF0BADF00DULL);
    double envMean = 0.0, glideMean = 0.0;
    float envMin = 2.0f, envMax = 0.0f, glMin = 2.0f, glMax = 0.0f;
    constexpr int kN = 50000;
    for (int i = 0; i < kN; ++i) {
        const NoteOnOffsets n = drawNoteOn(rng, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);
        envMean += n.varEnvScale; glideMean += n.varGlideScale;
        envMin = std::min(envMin, n.varEnvScale); envMax = std::max(envMax, n.varEnvScale);
        glMin  = std::min(glMin,  n.varGlideScale); glMax = std::max(glMax,  n.varGlideScale);
    }
    envMean /= static_cast<double>(kN); glideMean /= static_cast<double>(kN);

    // Multiplicative scales straddle UNITY (1 + draw*band), bounded by their (PI) band.
    REQUIRE(envMean   == Catch::Approx(1.0).margin(0.01));
    REQUIRE(glideMean == Catch::Approx(1.0).margin(0.01));
    REQUIRE(envMin < 1.0f); REQUIRE(envMax > 1.0f);
    REQUIRE(glMin  < 1.0f); REQUIRE(glMax  > 1.0f);
    REQUIRE(envMin >= 1.0f - cal::kVarEnvBand   - 1.0e-4f);
    REQUIRE(envMax <= 1.0f + cal::kVarEnvBand   + 1.0e-4f);
    REQUIRE(glMin  >= 1.0f - cal::kVarGlideBand - 1.0e-4f);
    REQUIRE(glMax  <= 1.0f + cal::kVarGlideBand + 1.0e-4f);
    // Multiplicative scales must stay strictly positive (a negative time constant is
    // nonsense): bands < 1 guarantee this.
    REQUIRE(envMin > 0.0f); REQUIRE(glMin > 0.0f);
}

TEST_CASE("vintage_draws cutoff and PW variance apply as additive offsets about zero", "[vintage_draws]") {
    Xorshift128p rng(0x0BADC0DE0BADC0DEULL);
    double cutoffMean = 0.0, pwMean = 0.0;
    bool cutNeg = false, cutPos = false, pwNeg = false, pwPos = false;
    constexpr int kN = 50000;
    for (int i = 0; i < kN; ++i) {
        const NoteOnOffsets n = drawNoteOn(rng, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
        cutoffMean += n.varCutoff; pwMean += n.varPw;
        if (n.varCutoff < -1.0f) cutNeg = true;
        if (n.varCutoff >  1.0f) cutPos = true;
        if (n.varPw < -0.001f)   pwNeg = true;
        if (n.varPw >  0.001f)   pwPos = true;
    }
    cutoffMean /= static_cast<double>(kN); pwMean /= static_cast<double>(kN);

    // Additive offsets are zero-mean and straddle zero (not centered on 1.0).
    REQUIRE(cutoffMean == Catch::Approx(0.0).margin(3.0));   // cents-equiv, near zero
    REQUIRE(pwMean     == Catch::Approx(0.0).margin(0.001));
    REQUIRE(cutNeg); REQUIRE(cutPos);
    REQUIRE(pwNeg);  REQUIRE(pwPos);
}

// --- Acceptance: spread/var = 0 => ZERO perturbation (§4.2, §8.1) -----------------

TEST_CASE("vintage_draws cal spread of zero yields the identity calibration draw", "[vintage_draws]") {
    // For ANY seed, spread01 = 0 must produce the exact identity (no perturbation):
    // tuneCents = 0, vcfWidthScale = 1, cutoffOffset = 0 [§4.2].
    for (std::uint64_t seed : {1ULL, 42ULL, 0xFFFFFFFFFFFFFFFFULL, 0xA5A5A5A5A5A5A5A5ULL}) {
        Xorshift128p rng(seed);
        for (int i = 0; i < 1000; ++i) {
            const CalibrationDraw d = drawCalibration(rng, 0.0f);
            REQUIRE(d.tuneCents     == 0.0f);
            REQUIRE(d.vcfWidthScale == 1.0f);
            REQUIRE(d.cutoffOffset  == 0.0f);
        }
    }
}

TEST_CASE("vintage_draws all-zero variance params yield identity note-on offsets but slop still applies", "[vintage_draws]") {
    // var.* = 0 => zero/unit variance offsets; but tune.slop (Tier 3) still applies per
    // its own param value [Acceptance hook: "zero defaults => in tune" except slop].
    Xorshift128p rng(0x1357246813572468ULL);
    const NoteOnOffsets n = drawNoteOn(rng, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(n.varCutoff     == 0.0f);
    REQUIRE(n.varEnvScale   == 1.0f);
    REQUIRE(n.varPw         == 0.0f);
    REQUIRE(n.varGlideScale == 1.0f);

    // With slopCents = 0 the whole draw is the strict identity (in tune on load).
    Xorshift128p r2(0x1357246813572468ULL);
    const NoteOnOffsets z = drawNoteOn(r2, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(z.slopCents     == 0.0f);
    REQUIRE(z.varCutoff     == 0.0f);
    REQUIRE(z.varEnvScale   == 1.0f);
    REQUIRE(z.varPw         == 0.0f);
    REQUIRE(z.varGlideScale == 1.0f);
}

TEST_CASE("vintage_draws slop scales linearly with slopCents and is zero at zero", "[vintage_draws]") {
    // drawSlopCents = slopCents * shape; shape independent of slopCents, so the draw
    // scales exactly linearly. slopCents = 0 => exactly 0.
    Xorshift128p r0(0x99AABBCCDDEEFF00ULL);
    REQUIRE(drawSlopCents(r0, 0.0f) == 0.0f);

    Xorshift128p ra(0x2468ACE013579BDFULL);
    Xorshift128p rb(0x2468ACE013579BDFULL);  // identical seed -> identical shape draw
    const float a = drawSlopCents(ra, 1.0f);
    const float b = drawSlopCents(rb, 4.0f);
    if (a != 0.0f) REQUIRE(b == Catch::Approx(4.0f * a).epsilon(1.0e-6));
}

// --- Acceptance: determinism for a fixed seed (§8.2, VV-17) ------------------------

TEST_CASE("vintage_draws are bit-identical for a fixed seed across runs", "[vintage_draws]") {
    const std::uint64_t seed = 0xABCDEF0123456789ULL;

    // Replay the SAME sequence of helper calls from two freshly-seeded PRNGs; every
    // field must match bit-for-bit (the macOS arm64 bless determinism guarantee).
    Xorshift128p p1(seed), p2(seed);
    for (int i = 0; i < 256; ++i) {
        const CalibrationDraw c1 = drawCalibration(p1, 0.6f);
        const CalibrationDraw c2 = drawCalibration(p2, 0.6f);
        REQUIRE(c1.tuneCents     == c2.tuneCents);
        REQUIRE(c1.vcfWidthScale == c2.vcfWidthScale);
        REQUIRE(c1.cutoffOffset  == c2.cutoffOffset);

        const NoteOnOffsets n1 = drawNoteOn(p1, 2.5f, 0.5f, 0.4f, 0.3f, 0.2f);
        const NoteOnOffsets n2 = drawNoteOn(p2, 2.5f, 0.5f, 0.4f, 0.3f, 0.2f);
        REQUIRE(n1.slopCents     == n2.slopCents);
        REQUIRE(n1.varCutoff     == n2.varCutoff);
        REQUIRE(n1.varEnvScale   == n2.varEnvScale);
        REQUIRE(n1.varPw         == n2.varPw);
        REQUIRE(n1.varGlideScale == n2.varGlideScale);
    }
}

TEST_CASE("vintage_draws spread scales the perturbation magnitude monotonically", "[vintage_draws]") {
    // Larger cal.spread => wider observed band (the (PI) width multiplier of §4.2/VV-6).
    const float halfQuarter = observedHalfBand(0x4242, 100000,
        [](Xorshift128p& r) { return drawCalibration(r, 0.25f).cutoffOffset; });
    const float halfFull = observedHalfBand(0x4242, 100000,
        [](Xorshift128p& r) { return drawCalibration(r, 1.0f).cutoffOffset; });
    REQUIRE(halfFull > halfQuarter);
    // ~4x band for 4x spread (uniform max scales with the band width).
    REQUIRE(halfFull == Catch::Approx(4.0f * halfQuarter).epsilon(0.05));
}

// --- POD / RT-safety layout (§8.1, §12.1) -----------------------------------------

TEST_CASE("vintage_draws state structs are trivially-copyable PODs for the pre-sized array", "[vintage_draws]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<CalibrationDraw>);
    STATIC_REQUIRE(std::is_standard_layout_v<CalibrationDraw>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<NoteOnOffsets>);
    STATIC_REQUIRE(std::is_standard_layout_v<NoteOnOffsets>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<DriftState>);
    STATIC_REQUIRE(std::is_standard_layout_v<DriftState>);
}

TEST_CASE("vintage_draws DriftState default-constructs to the identity personality", "[vintage_draws]") {
    // A default DriftState (before any draw) is in-tune / inactive: identity cal +
    // note-on offsets, inactive flag [§8.1 defaults].
    DriftState s;
    REQUIRE(s.active == false);
    REQUIRE(s.cal.tuneCents     == 0.0f);
    REQUIRE(s.cal.vcfWidthScale == 1.0f);
    REQUIRE(s.cal.cutoffOffset  == 0.0f);
    REQUIRE(s.noteOn.slopCents     == 0.0f);
    REQUIRE(s.noteOn.varCutoff     == 0.0f);
    REQUIRE(s.noteOn.varEnvScale   == 1.0f);
    REQUIRE(s.noteOn.varPw         == 0.0f);
    REQUIRE(s.noteOn.varGlideScale == 1.0f);
}
