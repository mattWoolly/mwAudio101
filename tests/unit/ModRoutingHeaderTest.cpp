// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the fixed-routing combiner PODs in core/dsp/ModRouting.h
// (task 053). Realizes docs/design/03 §5.1: declares ModDepths / VelocityRouting /
// ModBus as POD with the §5.1 fields + defaults, plus the combiner entry points
// (prepare sizes ModBus; per-tick combine). Velocity/LPF MATH is out of scope here
// (task 12) — these cases pin struct SHAPE/DEFAULTS and RT-safety only.
//
// Test-case NAMES begin with "modrouting_header" so `ctest -R modrouting_header`
// selects them. The Catch2 [tag] is the pre-existing [core] tag so no new label is
// introduced into tests/golden/corpus/ctest-labels.snapshot.

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "dsp/ModRouting.h"

using mw101::dsp::ModBus;
using mw101::dsp::ModDepths;
using mw101::dsp::VelocityRouting;

// --- PODs are standard-layout + trivially-copyable (RT-safe state) -------------
TEST_CASE("modrouting_header: the three structs are POD (standard-layout, trivially copyable)", "[core]") {
    STATIC_REQUIRE(std::is_standard_layout_v<ModDepths>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModDepths>);
    STATIC_REQUIRE(std::is_standard_layout_v<VelocityRouting>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<VelocityRouting>);
    STATIC_REQUIRE(std::is_standard_layout_v<ModBus>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModBus>);
}

// --- ModDepths: §5.1 fields all present, all default to 0 ----------------------
TEST_CASE("modrouting_header: ModDepths has the seven §5.1 depth scalars, default 0", "[core]") {
    ModDepths d;
    REQUIRE(d.lfoToPitch == 0.0f);
    REQUIRE(d.lfoToCutoff == 0.0f);
    REQUIRE(d.lfoToPw == 0.0f);
    REQUIRE(d.lfoToVca == 0.0f);
    REQUIRE(d.envToCutoff == 0.0f);
    REQUIRE(d.envToPw == 0.0f);
    REQUIRE(d.keyFollow == 0.0f);

    // The fields are float scalars (no heap-owning / non-trivial members).
    STATIC_REQUIRE(std::is_same_v<decltype(d.lfoToPitch), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(d.keyFollow), float>);
}

// --- VelocityRouting: ADR-016 R-2 — enabled defaults true ----------------------
TEST_CASE("modrouting_header: VelocityRouting defaults to enabled (ADR-016 R-2)", "[core]") {
    VelocityRouting v;
    REQUIRE(v.enabled == true);          // out-of-box ON (positive)
    REQUIRE_FALSE(v.enabled == false);   // negative control
    REQUIRE(v.toVcaAmount == 1.0f);
    REQUIRE(v.toCutoffAmount == 1.0f);

    STATIC_REQUIRE(std::is_same_v<decltype(v.enabled), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(v.toVcaAmount), float>);
}

// --- ModBus: §5.1 lpState/lpCoeff, no heap members -----------------------------
TEST_CASE("modrouting_header: ModBus is POD with lpState/lpCoeff and no heap members", "[core]") {
    ModBus b;
    REQUIRE(b.lpState == 0.0f);
    REQUIRE(b.lpCoeff == 0.0f);

    // No heap-owning members: the struct is trivially copyable AND its size is just
    // its two float fields (a heap member — e.g. a pointer/vector — would enlarge it
    // or break trivial-copyability). ADR-020 S14: sized in prepare, no heap members.
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModBus>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<ModBus>);
    STATIC_REQUIRE(sizeof(ModBus) == 2 * sizeof(float));
    STATIC_REQUIRE(std::is_same_v<decltype(b.lpState), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(b.lpCoeff), float>);
}

// --- Combiner entry points: prepare SIZES the ModBus; per-tick combine exists ---
TEST_CASE("modrouting_header: prepare sizes the ModBus state (no heap, deterministic)", "[core]") {
    mw101::dsp::ModRoutingCombiner c;
    // prepare(...) is the sizing seam (ADR-020 S14): it resets/derives the ModBus
    // one-pole state from the sample rate; nothing is sized on the audio thread.
    c.prepare(48000.0);
    const ModBus& bus = c.modBus();
    REQUIRE(bus.lpState == 0.0f);          // state reset to zero by prepare
    REQUIRE(bus.lpCoeff >= 0.0f);          // a valid one-pole coefficient is derived
    REQUIRE(bus.lpCoeff < 1.0f);

    // prepare is callable again (re-prepare on sample-rate change) and stays reset.
    c.prepare(96000.0);
    REQUIRE(c.modBus().lpState == 0.0f);
}

TEST_CASE("modrouting_header: per-tick combine entry point is noexcept and present", "[core]") {
    mw101::dsp::ModRoutingCombiner c;
    c.prepare(48000.0);

    ModDepths depths;          // all-zero depths => no depth contribution
    VelocityRouting vel;       // default ON
    vel.enabled = false;       // also disable velocity so the all-zero shape holds

    // The per-tick combiner is the hot-path entry point. With zero depths, a zero
    // envelope/LFO and velocity OFF, every per-destination contribution is zero
    // (shape check only; the calibrated depth/velocity/LPF MATH is task 057's scope,
    // asserted numerically in ModRoutingCombineTest, not here).
    const mw101::dsp::ModContributions mods =
        c.combine(depths, vel, /*envLevel=*/0.0f, /*lfoValue=*/0.0f, /*velNorm=*/0.0f);
    REQUIRE(mods.pitchMod == 0.0f);
    REQUIRE(mods.cutoffMod == 0.0f);
    REQUIRE(mods.pwMod == 0.0f);
    REQUIRE(mods.vcaControl == 0.0f);

    // Hot-path RT contract: the combine entry point is noexcept (ADR-001 / ADR-020).
    STATIC_REQUIRE(noexcept(c.combine(depths, vel, 0.0f, 0.0f, 1.0f)));
}
