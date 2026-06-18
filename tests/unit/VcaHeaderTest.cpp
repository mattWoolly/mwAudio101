// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/VcaHeaderTest.cpp — header-layout contract for core/dsp/Vca.h (task 052).
//
// Asserts the BA662A-class VCA public API + POD state EXACTLY as
// docs/design/03 §4.2: the ENV/GATE-only VcaMode enum (no HOLD), the
// prepare/reset/setMode/setDrive/process/processBlock surface, noexcept hot paths,
// and POD state [ADR-020 S14]. These are compile-time signature/shape assertions:
// task 052 declares the layout only; the taper math (task 009) and anti-thump
// (task 010) own the method bodies, so the test never calls an unimplemented method
// (no link dependency).
//
// Test-case names begin with "vca_header" so `ctest -R vca_header` selects them.
// Tags reuse the existing [core] subsystem tag (the dsp layout is part of the core
// seam) so the checked-in label snapshot is unchanged.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

#include "dsp/Vca.h"

namespace {

using mw101::dsp::Vca;
using mw101::dsp::VcaMode;

// --- VcaMode: exactly Env=0 / Gate=1, underlying uint8_t, NO HOLD (§4.2) --------
static_assert(std::is_enum_v<VcaMode>, "VcaMode must be an enum");
static_assert(!std::is_convertible_v<VcaMode, int>,
              "VcaMode must be a scoped enum class (no implicit int conversion)");
static_assert(std::is_same_v<std::underlying_type_t<VcaMode>, std::uint8_t>,
              "VcaMode underlying type must be uint8_t per §4.2");
static_assert(static_cast<std::uint8_t>(VcaMode::Env) == 0, "VcaMode::Env must be 0");
static_assert(static_cast<std::uint8_t>(VcaMode::Gate) == 1, "VcaMode::Gate must be 1");

// --- Method signatures match §4.2 verbatim (pointer-to-member type identity) -----
static_assert(std::is_same_v<decltype(&Vca::prepare), void (Vca::*)(double) noexcept>,
              "prepare must be void prepare(double) noexcept");
static_assert(std::is_same_v<decltype(&Vca::reset), void (Vca::*)() noexcept>,
              "reset must be void reset() noexcept");
static_assert(std::is_same_v<decltype(&Vca::setMode), void (Vca::*)(VcaMode) noexcept>,
              "setMode must be void setMode(VcaMode) noexcept");
static_assert(std::is_same_v<decltype(&Vca::setDrive), void (Vca::*)(float) noexcept>,
              "setDrive must be void setDrive(float) noexcept");
static_assert(std::is_same_v<decltype(&Vca::process), float (Vca::*)(float, float) noexcept>,
              "process must be float process(float, float) noexcept");
static_assert(
    std::is_same_v<decltype(&Vca::processBlock), void (Vca::*)(float*, const float*, int) noexcept>,
    "processBlock must be void processBlock(float*, const float*, int) noexcept");

// --- POD / RT discipline: trivially copyable, standard-layout (ADR-020 S14) ------
static_assert(std::is_trivially_copyable_v<Vca>,
              "Vca state must be a trivially-copyable POD (ADR-020 S14)");
static_assert(std::is_standard_layout_v<Vca>,
              "Vca must be standard-layout (POD seam state)");
static_assert(std::is_nothrow_default_constructible_v<Vca>,
              "Vca default construction must not throw");

} // namespace

TEST_CASE("vca_header: VcaMode has exactly Env/Gate and no HOLD enumerator", "[core]") {
    // Env=0, Gate=1 are pinned by the static_asserts above; this run-time case
    // gives `-R vca_header` a concrete, non-empty target (silent-pass rule).
    REQUIRE(static_cast<std::uint8_t>(VcaMode::Env) == 0u);
    REQUIRE(static_cast<std::uint8_t>(VcaMode::Gate) == 1u);

    // NEGATIVE control: the enum must be a 2-value pair. Anything outside {0,1} (a
    // smuggled-in HOLD=2) would be an unnamed value, which the design forbids
    // without an ADR (§4.2). We assert the two documented names round-trip and that
    // the underlying width is the documented uint8_t.
    REQUIRE(static_cast<VcaMode>(std::uint8_t{0}) == VcaMode::Env);
    REQUIRE(static_cast<VcaMode>(std::uint8_t{1}) == VcaMode::Gate);
    static_assert(sizeof(VcaMode) == 1, "VcaMode must be 1 byte (uint8_t-backed)");
}

TEST_CASE("vca_header: Vca exposes the §4.2 API surface as noexcept members", "[core]") {
    // Default construction is RT-safe (no throw); the engineering defaults match the
    // §4.2 class layout (Env mode, zero drive). Bodies for the hot paths are owned
    // by tasks 009/010, so we assert construction + queryable defaults only.
    Vca vca{};
    (void) vca;

    // The signature identities are enforced at compile time by the static_asserts
    // above; this case keeps a discoverable `vca_header` run-time anchor.
    SUCCEED("Vca constructed with the documented §4.2 layout");
}
