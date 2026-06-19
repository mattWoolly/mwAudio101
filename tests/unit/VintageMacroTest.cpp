// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/VintageMacroTest.cpp — Layer-1 unit tests for the HOST-THREAD Vintage
// (Age) macro -> target mapping (task 066).
//
// Test-case names begin with "vintage_macro" so `ctest -R vintage_macro` selects them
// (silent-pass discipline, AGENTS.md). They cover every Acceptance criterion of
// plan/backlog/066:
//   - age=0 maps to ZERO added group offset (in tune on load) per §10.2 / VV-1;
//   - the kAgeCurve mapping is MONOTONIC and scales each target WITHIN its schema
//     range (§10.1) — verified against the authoritative docs/design/06 §3 ranges in
//     kParamDefs, not hand-typed numbers;
//   - apply() touches ONLY the param TARGETS and performs NO audio-thread DSP
//     (host-thread path) per §3.2 / §Decision 7 — i.e. it writes the smoother TARGET
//     without advancing the smoothed current value (no process()).
//
// All figures here trace to the (PI) curve/ceilings in VintageMacroConstants.h or to
// the param schema; none is a measured SH-101 spec [docs/design/08 §10.1; ADR-009 §8].

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <string_view>
#include <type_traits>

#include "dsp/drift/VintageMacro.h"
#include "calibration/VintageMacroConstants.h"
#include "params/ParamDefs.h"
#include "params/Smoother.h"

using mw::dsp::drift::VintageMacro;
using mw::dsp::drift::VintageTargets;
using mw::params::OnePoleSmoother;
namespace cal = mw::cal::drift;

namespace {

// Resolve a parameter's [min,max] schema range from the authoritative kParamDefs
// table (docs/design/06 §3). Asserts the ID exists so a typo can't silently pass.
struct Range { float lo; float hi; };
Range schemaRange(std::string_view id) {
    for (const auto& d : mw::params::kParamDefs) {
        if (std::string_view{d.id} == id) return Range{ d.minValue, d.maxValue };
    }
    FAIL("schema ID not found in kParamDefs: " << id);
    return Range{ 0.0f, 0.0f };
}

// A fresh smoother whose current value is parked AWAY from any macro target, so we can
// prove apply() moves only the TARGET and never the smoothed current value.
OnePoleSmoother parkedSmoother(double parkedValue) {
    OnePoleSmoother s;
    s.prepare(/*timeConstantSeconds=*/0.015, /*tickRateHz=*/1000.0); // real de-zipper
    s.reset(parkedValue);
    return s;
}

} // namespace

// --- Acceptance 1: age=0 => zero added group offset (in tune on load) §10.2/VV-1 ---

TEST_CASE("vintage_macro age zero yields zero added group offset, in tune on load", "[vintage_macro]") {
    const VintageTargets t = VintageMacro::computeTargets(0.0f);

    // The Age=0 mapping is the zero-perturbation baseline: depth/slop/var all 0, and
    // drift.rate at its schema MIN baseline. Nothing the macro scales adds any offset.
    REQUIRE(t.driftDepthCents == Catch::Approx(0.0f));
    REQUIRE(t.tuneSlopCents   == Catch::Approx(0.0f));
    REQUIRE(t.varCutoff       == Catch::Approx(0.0f));
    REQUIRE(t.varEnvTime      == Catch::Approx(0.0f));
    REQUIRE(t.varPw           == Catch::Approx(0.0f));
    REQUIRE(t.varGlide        == Catch::Approx(0.0f));
    // Drift rate baseline is the schema MIN (slowest), not an "added" offset.
    REQUIRE(t.driftRateHz == Catch::Approx(cal::kAgeDriftRateMinHz));

    // The (PI) curve endpoint is exactly 0 at age 0 (VV-1).
    REQUIRE(VintageMacro::curve(0.0f) == Catch::Approx(0.0f));
}

TEST_CASE("vintage_macro negative or below-zero age clamps to the in-tune baseline", "[vintage_macro]") {
    // Defensive clamp: an out-of-range age never produces a negative / sub-baseline
    // offset (the param default is 0 / in tune on load, §10.2).
    const VintageTargets t = VintageMacro::computeTargets(-0.5f);
    REQUIRE(t.driftDepthCents == Catch::Approx(0.0f));
    REQUIRE(t.tuneSlopCents   == Catch::Approx(0.0f));
    REQUIRE(t.varCutoff       == Catch::Approx(0.0f));
    REQUIRE(t.driftRateHz     == Catch::Approx(cal::kAgeDriftRateMinHz));
    REQUIRE(VintageMacro::curve(-1.0f) == Catch::Approx(0.0f));
}

// --- Acceptance 2: monotonic kAgeCurve scales each target within its schema range ---

TEST_CASE("vintage_macro curve is monotonic non-decreasing with endpoints 0 and 1", "[vintage_macro]") {
    REQUIRE(VintageMacro::curve(0.0f) == Catch::Approx(0.0f));
    REQUIRE(VintageMacro::curve(1.0f) == Catch::Approx(1.0f));

    // Dense monotonicity sweep over [0,1]: the curve never decreases.
    float prev = VintageMacro::curve(0.0f);
    for (int i = 1; i <= 200; ++i) {
        const float a = static_cast<float>(i) / 200.0f;
        const float s = VintageMacro::curve(a);
        REQUIRE(s >= prev - 1.0e-6f);            // monotonic non-decreasing
        REQUIRE(s >= 0.0f);
        REQUIRE(s <= 1.0f + 1.0e-6f);            // stays in [0,1]
        prev = s;
    }
    // Above 1.0 saturates at the top (no overshoot past full vintage).
    REQUIRE(VintageMacro::curve(1.5f) == Catch::Approx(1.0f));
}

TEST_CASE("vintage_macro each scaled target stays within its docs/design/06 schema range", "[vintage_macro]") {
    const Range depth = schemaRange("mw101.drift.depth");
    const Range rate  = schemaRange("mw101.drift.rate");
    const Range slop  = schemaRange("mw101.tune.slop");
    const Range vcut  = schemaRange("mw101.var.cutoff");
    const Range venv  = schemaRange("mw101.var.env_time");
    const Range vpw   = schemaRange("mw101.var.pw");
    const Range vgl   = schemaRange("mw101.var.glide");

    // Sweep the whole Age domain; every scaled target must land inside its schema
    // [min,max]. The macro's per-target ceilings mirror the schema maxima (§10.1).
    for (int i = 0; i <= 100; ++i) {
        const float age = static_cast<float>(i) / 100.0f;
        const VintageTargets t = VintageMacro::computeTargets(age);

        REQUIRE(t.driftDepthCents >= depth.lo); REQUIRE(t.driftDepthCents <= depth.hi);
        REQUIRE(t.driftRateHz     >= rate.lo);  REQUIRE(t.driftRateHz     <= rate.hi);
        REQUIRE(t.tuneSlopCents   >= slop.lo);  REQUIRE(t.tuneSlopCents   <= slop.hi);
        REQUIRE(t.varCutoff       >= vcut.lo);  REQUIRE(t.varCutoff       <= vcut.hi);
        REQUIRE(t.varEnvTime      >= venv.lo);  REQUIRE(t.varEnvTime      <= venv.hi);
        REQUIRE(t.varPw           >= vpw.lo);   REQUIRE(t.varPw           <= vpw.hi);
        REQUIRE(t.varGlide        >= vgl.lo);   REQUIRE(t.varGlide        <= vgl.hi);
    }

    // At full Age the targets reach the TOP of their schema range (the macro spans the
    // whole range, §10.1): depth->max, slop->max, var.*->max, rate->max.
    const VintageTargets full = VintageMacro::computeTargets(1.0f);
    REQUIRE(full.driftDepthCents == Catch::Approx(depth.hi));
    REQUIRE(full.driftRateHz     == Catch::Approx(rate.hi));
    REQUIRE(full.tuneSlopCents   == Catch::Approx(slop.hi));
    REQUIRE(full.varCutoff       == Catch::Approx(vcut.hi));
    REQUIRE(full.varEnvTime      == Catch::Approx(venv.hi));
    REQUIRE(full.varPw           == Catch::Approx(vpw.hi));
    REQUIRE(full.varGlide        == Catch::Approx(vgl.hi));
}

TEST_CASE("vintage_macro group targets are monotonic in Age and scale with the curve", "[vintage_macro]") {
    // Each scaled target tracks the (monotonic) curve, so a heavier Age never reduces
    // any member of the group it scales (§10.1).
    VintageTargets prev = VintageMacro::computeTargets(0.0f);
    for (int i = 1; i <= 100; ++i) {
        const float age = static_cast<float>(i) / 100.0f;
        const VintageTargets t = VintageMacro::computeTargets(age);
        REQUIRE(t.driftDepthCents >= prev.driftDepthCents - 1.0e-4f);
        REQUIRE(t.driftRateHz     >= prev.driftRateHz     - 1.0e-6f);
        REQUIRE(t.tuneSlopCents   >= prev.tuneSlopCents   - 1.0e-4f);
        REQUIRE(t.varCutoff       >= prev.varCutoff       - 1.0e-6f);
        prev = t;
    }

    // Targets are exactly the unit scale times the per-target ceiling (rate lerped
    // from its min baseline). Check at the curve mid-knot value.
    const float age = 0.5f;
    const float s   = VintageMacro::curve(age);
    const VintageTargets t = VintageMacro::computeTargets(age);
    REQUIRE(t.driftDepthCents == Catch::Approx(s * cal::kAgeDriftDepthMaxCents));
    REQUIRE(t.tuneSlopCents   == Catch::Approx(s * cal::kAgeTuneSlopMaxCents));
    REQUIRE(t.varCutoff       == Catch::Approx(s * cal::kAgeVarMax));
    REQUIRE(t.driftRateHz == Catch::Approx(
        cal::kAgeDriftRateMinHz + s * (cal::kAgeDriftRateMaxHz - cal::kAgeDriftRateMinHz)));
}

// --- Acceptance 3: apply() touches only param TARGETS, no audio-thread DSP §3.2/D7 --

TEST_CASE("vintage_macro apply writes only the smoother targets and runs no audio-thread DSP", "[vintage_macro]") {
    // Park every smoother's CURRENT value away from any macro target. apply() must move
    // only the TARGET (the already-smoothed target store the audio thread reads); it
    // must NOT advance the smoothed current value — that would be audio-thread DSP
    // (a process() tick), which the host-thread macro never does (§3.2, §Decision 7).
    constexpr double kParked = -999.0;
    std::array<OnePoleSmoother, 7> sm = {
        parkedSmoother(kParked), parkedSmoother(kParked), parkedSmoother(kParked),
        parkedSmoother(kParked), parkedSmoother(kParked), parkedSmoother(kParked),
        parkedSmoother(kParked),
    };

    const float age = 0.7f;
    VintageMacro::apply(age, sm[0], sm[1], sm[2], sm[3], sm[4], sm[5], sm[6]);

    const VintageTargets t = VintageMacro::computeTargets(age);
    const std::array<float, 7> expectTargets = {
        t.driftDepthCents, t.driftRateHz, t.tuneSlopCents,
        t.varCutoff, t.varEnvTime, t.varPw, t.varGlide,
    };

    for (std::size_t i = 0; i < sm.size(); ++i) {
        // TARGET == the mapped value ...
        REQUIRE(sm[i].target() == Catch::Approx(static_cast<double>(expectTargets[i])));
        // ... and the smoothed CURRENT value is UNTOUCHED (still parked): apply() did
        // NOT advance the de-zipper, i.e. it performed no audio-thread DSP.
        REQUIRE(sm[i].current() == Catch::Approx(kParked));
        // The smoother is therefore left in the "still smoothing toward the new target"
        // state — the audio thread will de-zipper it on its own ticks later.
        REQUIRE(sm[i].isSmoothing());
    }
}

TEST_CASE("vintage_macro apply at age zero leaves an in-tune baseline target set", "[vintage_macro]") {
    // The host-thread apply at the default Age (0) writes the zero-offset baseline as
    // the target on every group smoother (in tune on load, §10.2 / VV-1).
    std::array<OnePoleSmoother, 7> sm = {
        parkedSmoother(7.0), parkedSmoother(7.0), parkedSmoother(7.0),
        parkedSmoother(7.0), parkedSmoother(7.0), parkedSmoother(7.0),
        parkedSmoother(7.0),
    };
    VintageMacro::apply(0.0f, sm[0], sm[1], sm[2], sm[3], sm[4], sm[5], sm[6]);

    REQUIRE(sm[0].target() == Catch::Approx(0.0));                       // drift.depth
    REQUIRE(sm[1].target() == Catch::Approx(cal::kAgeDriftRateMinHz));   // drift.rate baseline (min)
    REQUIRE(sm[2].target() == Catch::Approx(0.0));                       // tune.slop
    REQUIRE(sm[3].target() == Catch::Approx(0.0));                       // var.cutoff
    REQUIRE(sm[4].target() == Catch::Approx(0.0));                       // var.env_time
    REQUIRE(sm[5].target() == Catch::Approx(0.0));                       // var.pw
    REQUIRE(sm[6].target() == Catch::Approx(0.0));                       // var.glide
}

// --- POD / host-thread discipline ---------------------------------------------------

TEST_CASE("vintage_macro VintageTargets is a trivially-copyable POD", "[vintage_macro]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<VintageTargets>);
    STATIC_REQUIRE(std::is_standard_layout_v<VintageTargets>);
    // Default-constructed targets are the in-tune baseline (matches age=0 mapping).
    const VintageTargets def{};
    const VintageTargets zero = VintageMacro::computeTargets(0.0f);
    REQUIRE(def.driftDepthCents == Catch::Approx(zero.driftDepthCents));
    REQUIRE(def.driftRateHz     == Catch::Approx(zero.driftRateHz));
    REQUIRE(def.varCutoff       == Catch::Approx(zero.varCutoff));
}
