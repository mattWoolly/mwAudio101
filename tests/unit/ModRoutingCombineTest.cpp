// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the fixed-routing COMBINE math (task 057), realizing
// docs/design/03 §5.1 (depth scaling), §3.5 (mod-bus kModBusLpHz LPF) and §5.2
// (velocity routing to VCA level + VCF cutoff amount; velocity ON by default per
// ADR-016 R-2). The struct SHAPE/DEFAULTS + the pitch-only stub were pinned by task
// 053 (ModRoutingHeaderTest); this suite pins the calibrated (PI) combine MATH that
// task 057 implements as the body of ModRoutingCombiner::combine in
// core/dsp/ModRouting.cpp.
//
// Test-case NAMES begin with "modrouting_combine" so `ctest -R modrouting_combine`
// selects them (catch_discover_tests registers names, not tags). AVOID '[' in the
// display text — Catch2 parses it as a tag and breaks ctest -R selection. The Catch2
// [tag] is [modrouting_combine] (the task tag); the orchestrator regenerates the
// label snapshot at wave integration, so a new [tag] is expected and fine.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <type_traits>

#include "dsp/ModRouting.h"
#include "calibration/EnvLfoVcaConstants.h"

using Catch::Matchers::WithinAbs;
using mw101::dsp::ModBus;
using mw101::dsp::ModContributions;
using mw101::dsp::ModDepths;
using mw101::dsp::ModRoutingCombiner;
using mw101::dsp::VelocityRouting;

namespace {
// Mirror the design §3.5 one-pole corner derivation used by prepare so the test is an
// independent oracle, not a tautology against the implementation's own arithmetic.
float expectedLpCoeff(double fc, double fs) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    return static_cast<float>(1.0 - std::exp(-kTwoPi * fc / fs));
}
}  // namespace

// --- The combine result is a POD (RT-safe, no heap members) --------------------
TEST_CASE("modrouting_combine: ModContributions is POD with the per-destination fields",
          "[modrouting_combine]") {
    STATIC_REQUIRE(std::is_standard_layout_v<ModContributions>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModContributions>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<ModContributions>);

    ModContributions c;
    // Per-destination contributions default to a benign zero.
    REQUIRE(c.pitchMod == 0.0f);
    REQUIRE(c.cutoffMod == 0.0f);
    REQUIRE(c.pwMod == 0.0f);
    REQUIRE(c.vcaControl == 0.0f);

    STATIC_REQUIRE(std::is_same_v<decltype(c.pitchMod), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(c.vcaControl), float>);
}

// --- prepare wires the (PI) kModBusLpHz one-pole coefficient (§3.5) -------------
// The depth scalars / LPF corner are (PI) from Calibration.h, NEVER inlined
// (acceptance: §3.5, §5.3, ADR-020 S13).
TEST_CASE("modrouting_combine: prepare derives the kModBusLpHz one-pole coeff from Calibration",
          "[modrouting_combine]") {
    ModRoutingCombiner e;
    e.prepare(48000.0);

    const ModBus& bus = e.modBus();
    REQUIRE(bus.lpState == 0.0f);              // running state reset
    REQUIRE(bus.lpCoeff > 0.0f);               // a real LPF, not a pass-through
    REQUIRE(bus.lpCoeff < 1.0f);               // a valid one-pole coefficient

    // The coefficient traces to the (PI) kModBusLpHz corner, not an inlined literal.
    const float want = expectedLpCoeff(mw::cal::lfo::kModBusLpHz, 48000.0);
    REQUIRE_THAT(bus.lpCoeff, WithinAbs(want, 1.0e-7f));

    // Re-prepare on sample-rate change re-derives and stays reset.
    e.prepare(96000.0);
    REQUIRE(e.modBus().lpState == 0.0f);
    const float want96 = expectedLpCoeff(mw::cal::lfo::kModBusLpHz, 96000.0);
    REQUIRE_THAT(e.modBus().lpCoeff, WithinAbs(want96, 1.0e-7f));
    // Lower corner-to-fs ratio at the higher SR => smaller coefficient.
    REQUIRE(e.modBus().lpCoeff < want);
}

// --- Depth scaling: env/LFO scaled by per-destination depths (§5.1) ------------
TEST_CASE("modrouting_combine: env and LFO are scaled by the per-destination depths",
          "[modrouting_combine]") {
    ModRoutingCombiner e;
    e.prepare(48000.0);

    ModDepths d;
    d.lfoToPitch = 0.5f;
    d.lfoToCutoff = 0.25f;
    d.lfoToPw = 0.4f;
    d.envToCutoff = 0.8f;
    d.envToPw = 0.3f;

    VelocityRouting vel;
    vel.enabled = false;   // isolate the depth-scaling path from velocity here

    const float envLevel = 0.6f;
    const float lfoValue = 1.0f;   // unity so the LPF leaves the steady-state value

    // Drive the mod bus to steady state so the one-pole has settled on lfoValue; the
    // depth scaling is then a clean multiply we can assert against.
    ModContributions c{};
    for (int i = 0; i < 4096; ++i)
        c = e.combine(d, vel, envLevel, lfoValue, /*velNorm=*/1.0f);

    // Pitch = lfoToPitch * (filtered LFO ~= lfoValue).
    REQUIRE_THAT(c.pitchMod, WithinAbs(d.lfoToPitch * lfoValue, 1.0e-3f));
    // Cutoff = envToCutoff*env + lfoToCutoff*lfo (velocity OFF removes its term).
    REQUIRE_THAT(c.cutoffMod,
                 WithinAbs(d.envToCutoff * envLevel + d.lfoToCutoff * lfoValue, 1.0e-3f));
    // PW = envToPw*env + lfoToPw*lfo.
    REQUIRE_THAT(c.pwMod,
                 WithinAbs(d.envToPw * envLevel + d.lfoToPw * lfoValue, 1.0e-3f));

    // Doubling a depth doubles only its destination contribution (linear scaling).
    ModDepths d2 = d;
    d2.lfoToPitch = 1.0f;
    ModRoutingCombiner e2;
    e2.prepare(48000.0);
    ModContributions c2{};
    for (int i = 0; i < 4096; ++i)
        c2 = e2.combine(d2, vel, envLevel, lfoValue, 1.0f);
    REQUIRE_THAT(c2.pitchMod, WithinAbs(2.0f * c.pitchMod, 1.0e-3f));
}

// --- Mod-bus LPF actually filters the modulation signal (§3.5) -----------------
// Oracle: a one-pole step response y[n] = y[n-1] + a*(x - y[n-1]) approaches the
// target geometrically; the first tick must NOT already equal the target (proves a
// real filter sits on the bus, not a pass-through).
TEST_CASE("modrouting_combine: the mod bus applies the fixed kModBusLpHz one-pole LPF",
          "[modrouting_combine]") {
    ModRoutingCombiner e;
    e.prepare(48000.0);
    const float a = e.modBus().lpCoeff;

    ModDepths d;
    d.lfoToPitch = 1.0f;        // unity depth => pitchMod == filtered LFO directly
    VelocityRouting vel;
    vel.enabled = false;

    // Step the LFO input from 0 to 1; track the filtered pitch output.
    const float step = 1.0f;
    float oracle = 0.0f;        // independent one-pole state

    ModContributions c0 = e.combine(d, vel, /*env=*/0.0f, /*lfo=*/step, 1.0f);
    oracle += a * (step - oracle);
    // First tick is the partial approach, strictly below the target — a real LPF.
    REQUIRE(c0.pitchMod < step);
    REQUIRE(c0.pitchMod > 0.0f);
    REQUIRE_THAT(c0.pitchMod, WithinAbs(oracle, 1.0e-6f));

    // Track several more ticks against the independent one-pole oracle.
    for (int i = 0; i < 64; ++i) {
        ModContributions c = e.combine(d, vel, 0.0f, step, 1.0f);
        oracle += a * (step - oracle);
        REQUIRE_THAT(c.pitchMod, WithinAbs(oracle, 1.0e-5f));
    }
    // After many ticks it has converged toward the target.
    ModContributions cN{};
    for (int i = 0; i < 4096; ++i) cN = e.combine(d, vel, 0.0f, step, 1.0f);
    REQUIRE_THAT(cN.pitchMod, WithinAbs(step, 1.0e-3f));
}

// --- Velocity ON routes to VCA level + VCF cutoff amount (§5.2 / ADR-016 R-2) --
// The headline acceptance pair: velocity ON adds both contributions; OFF removes
// BOTH (the faithful no-velocity pole).
TEST_CASE("modrouting_combine: velocity ON routes to VCA level and VCF cutoff amount",
          "[modrouting_combine]") {
    ModRoutingCombiner eOn;
    eOn.prepare(48000.0);
    ModRoutingCombiner eOff;
    eOff.prepare(48000.0);

    ModDepths d;
    d.envToCutoff = 0.3f;   // a baseline cutoff contribution independent of velocity

    VelocityRouting vOn;    // default ON (ADR-016 R-2)
    REQUIRE(vOn.enabled == true);
    vOn.toVcaAmount = mw::cal::vel::kVelToVca;       // (PI) defaults, never inlined
    vOn.toCutoffAmount = mw::cal::vel::kVelToCutoff;

    VelocityRouting vOff = vOn;
    vOff.enabled = false;   // the faithful no-velocity pole, one switch away

    const float envLevel = 1.0f;   // baseAmp source level (ENV)
    const float lfoValue = 0.0f;   // no tremolo, isolate velocity
    const float velNorm = 0.5f;    // a half-velocity press

    // Settle both buses (lfo=0 so the bus stays at 0; one tick suffices, loop for safety).
    ModContributions on{}, off{};
    for (int i = 0; i < 8; ++i) {
        on = eOn.combine(d, vOn, envLevel, lfoValue, velNorm);
        off = eOff.combine(d, vOff, envLevel, lfoValue, velNorm);
    }

    // VCA level (§5.2): vcaControl = baseAmp * lerp(1, velNorm, toVca*enabled).
    const float baseAmp = envLevel;  // ENV source + zero tremolo
    const float lerpOn = 1.0f + (velNorm - 1.0f) * (vOn.toVcaAmount * 1.0f);
    REQUIRE_THAT(on.vcaControl, WithinAbs(baseAmp * lerpOn, 1.0e-5f));
    // OFF: lerp factor collapses to 1.0 => vcaControl == baseAmp (no velocity scaling).
    REQUIRE_THAT(off.vcaControl, WithinAbs(baseAmp, 1.0e-5f));

    // At less-than-full velocity, ON scales the VCA level DOWN vs OFF (positive pole).
    REQUIRE(on.vcaControl < off.vcaControl);

    // VCF cutoff amount (§5.2): cutoffMod += velNorm * toCutoff * enabled (additive).
    const float velCutoff = velNorm * vOn.toCutoffAmount;  // enabled=1
    REQUIRE_THAT(on.cutoffMod - off.cutoffMod, WithinAbs(velCutoff, 1.0e-5f));
    REQUIRE(on.cutoffMod > off.cutoffMod);

    // OFF removes BOTH contributions: cutoffMod is exactly the non-velocity baseline.
    REQUIRE_THAT(off.cutoffMod, WithinAbs(d.envToCutoff * envLevel, 1.0e-5f));
}

// --- Velocity full-press is a no-op on VCA level (§5.2 lerp endpoint) -----------
TEST_CASE("modrouting_combine: full velocity leaves VCA level unchanged",
          "[modrouting_combine]") {
    ModRoutingCombiner e;
    e.prepare(48000.0);

    ModDepths d;
    VelocityRouting v;             // ON
    v.toVcaAmount = mw::cal::vel::kVelToVca;

    const float baseAmp = 0.8f;    // ENV level
    ModContributions cFull{}, cLow{};
    for (int i = 0; i < 4; ++i) {
        cFull = e.combine(d, v, baseAmp, /*lfo=*/0.0f, /*velNorm=*/1.0f);
    }
    ModRoutingCombiner e2;
    e2.prepare(48000.0);
    for (int i = 0; i < 4; ++i) {
        cLow = e2.combine(d, v, baseAmp, 0.0f, /*velNorm=*/0.0f);
    }
    // Full velocity (velNorm=1) => lerp(1, 1, ...) = 1 => unchanged baseAmp.
    REQUIRE_THAT(cFull.vcaControl, WithinAbs(baseAmp, 1.0e-6f));
    // Zero velocity scales down by toVcaAmount: baseAmp * (1 - toVcaAmount).
    REQUIRE_THAT(cLow.vcaControl, WithinAbs(baseAmp * (1.0f - v.toVcaAmount), 1.0e-6f));
    REQUIRE(cLow.vcaControl < cFull.vcaControl);
}

// --- LFO tremolo folds into the VCA control alongside the amplitude source ------
TEST_CASE("modrouting_combine: LFO tremolo (lfoToVca) is summed into the VCA control",
          "[modrouting_combine]") {
    ModRoutingCombiner e;
    e.prepare(48000.0);

    ModDepths d;
    d.lfoToVca = 0.2f;   // tremolo depth
    VelocityRouting v;
    v.enabled = false;   // isolate tremolo from velocity

    const float envLevel = 0.5f;
    const float lfoValue = 1.0f;

    ModContributions c{};
    for (int i = 0; i < 4096; ++i)        // settle the bus to lfoValue
        c = e.combine(d, v, envLevel, lfoValue, 1.0f);

    // baseAmp = ENV level + tremolo (lfoToVca * filtered LFO); velocity OFF => no scale.
    REQUIRE_THAT(c.vcaControl, WithinAbs(envLevel + d.lfoToVca * lfoValue, 1.0e-3f));
}

// --- RT contract: combine is noexcept (ADR-001 hot path) -----------------------
TEST_CASE("modrouting_combine: the per-tick combine entry point is noexcept",
          "[modrouting_combine]") {
    ModRoutingCombiner e;
    e.prepare(48000.0);
    ModDepths d;
    VelocityRouting v;
    STATIC_REQUIRE(noexcept(e.combine(d, v, 0.0f, 0.0f, 1.0f)));
    STATIC_REQUIRE(noexcept(e.prepare(48000.0)));
    STATIC_REQUIRE(noexcept(e.reset()));
    STATIC_REQUIRE(noexcept(e.modBus()));
}
