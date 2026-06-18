// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the stateless closed-form PolyBLEP residual
// (task 026, core/dsp/PolyBlep.h). Test-case names begin with "polyblep" so
// `-R polyblep` selects them (silent-pass rule). Each case asserts an Acceptance
// criterion of backlog/026 verbatim against the ADR-002 Contract residual and
// docs/design/01 §3.1 / §10.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <type_traits>

#include "dsp/PolyBlep.h"

using Catch::Matchers::WithinAbs;
using mw101::dsp::polyBlep;

namespace {
// Reference leading-segment value: 2*(t/dt) - (t/dt)^2 - 1   [§3.1, §10; ADR-002 Contract]
constexpr float leadingRef(float t, float dt) {
    const float x = t / dt;
    return 2.0f * x - x * x - 1.0f;
}
// Reference trailing-segment value: ((t-1)/dt)^2 + 2*((t-1)/dt) + 1
constexpr float trailingRef(float t, float dt) {
    const float x = (t - 1.0f) / dt;
    return x * x + 2.0f * x + 1.0f;
}
constexpr float kEps = 1.0e-6f;  // float tolerance per the Acceptance criteria
} // namespace

TEST_CASE("polyblep: leading segment equals 2*(t/dt)-(t/dt)^2-1 for t<dt", "[polyblep]") {
    // [§3.1, §10; ADR-002 Contract C1-C2]
    const float dts[] = {0.001f, 0.01f, 0.05f, 0.1f, 0.25f, 0.49f};
    for (float dt : dts) {
        // Sample several phases strictly inside the leading window 0 <= t < dt.
        const float ts[] = {0.0f, 0.01f * dt, 0.1f * dt, 0.5f * dt, 0.9f * dt, 0.999f * dt};
        for (float t : ts) {
            REQUIRE(t < dt);
            REQUIRE_THAT(polyBlep(t, dt), WithinAbs(leadingRef(t, dt), kEps));
        }
    }
    // At t == 0 the residual is exactly -1 (2*0 - 0 - 1).
    REQUIRE_THAT(polyBlep(0.0f, 0.1f), WithinAbs(-1.0f, kEps));
}

TEST_CASE("polyblep: trailing segment equals ((t-1)/dt)^2+2*((t-1)/dt)+1 for t>1-dt", "[polyblep]") {
    // [§3.1, §10; ADR-002 Contract C1-C2]
    const float dts[] = {0.001f, 0.01f, 0.05f, 0.1f, 0.25f, 0.49f};
    for (float dt : dts) {
        // Sample several phases strictly inside the trailing window 1-dt < t < 1.
        const float fracs[] = {0.001f, 0.1f, 0.5f, 0.9f, 0.999f};
        for (float f : fracs) {
            const float t = (1.0f - dt) + f * dt;  // in (1-dt, 1)
            REQUIRE(t > 1.0f - dt);
            REQUIRE(t < 1.0f);
            REQUIRE_THAT(polyBlep(t, dt), WithinAbs(trailingRef(t, dt), kEps));
        }
    }
}

TEST_CASE("polyblep: interior returns exactly 0.0f", "[polyblep]") {
    // [§3.1, §10] dt <= t <= 1-dt is the interior; the residual is exactly 0.0f.
    const float dts[] = {0.001f, 0.01f, 0.05f, 0.1f, 0.25f};
    for (float dt : dts) {
        const float ts[] = {dt, dt + 0.001f, 0.5f, (1.0f - dt) - 0.001f, 1.0f - dt};
        for (float t : ts) {
            REQUIRE(t >= dt);
            REQUIRE(t <= 1.0f - dt);
            const float r = polyBlep(t, dt);
            REQUIRE(r == 0.0f);          // bit-exact zero, not merely near zero
            REQUIRE_FALSE(r != 0.0f);    // negative control
        }
    }
}

TEST_CASE("polyblep: segment boundaries select the correct branch", "[polyblep]") {
    // At the boundaries the branch conditions are t<dt (leading) and t>1-dt (trailing).
    constexpr float dt = 0.1f;
    // t == dt is NOT < dt -> interior -> 0.
    REQUIRE(polyBlep(dt, dt) == 0.0f);
    // t just below dt is leading.
    REQUIRE_THAT(polyBlep(dt * 0.999f, dt), WithinAbs(leadingRef(dt * 0.999f, dt), kEps));
    // t == 1-dt is NOT > 1-dt -> interior -> 0.
    REQUIRE(polyBlep(1.0f - dt, dt) == 0.0f);
    // t just above 1-dt is trailing.
    const float tHi = (1.0f - dt) + 0.001f * dt;
    REQUIRE_THAT(polyBlep(tHi, dt), WithinAbs(trailingRef(tHi, dt), kEps));
}

TEST_CASE("polyblep: function is constexpr / compile-time evaluable", "[polyblep]") {
    // [ADR-002 Contract] the residual MUST be constexpr (and noexcept). Evaluate in
    // a constant-expression context: this only compiles if polyBlep is constexpr.
    constexpr float leading  = polyBlep(0.0f, 0.1f);          // leading segment
    constexpr float interior = polyBlep(0.5f, 0.1f);          // interior
    constexpr float trailing = polyBlep(0.95f, 0.1f);         // trailing segment
    static_assert(leading == -1.0f, "leading at t=0 must be -1");
    static_assert(interior == 0.0f, "interior must be exactly 0");
    static_assert(trailing != 0.0f, "trailing must be non-zero");

    // Bind to constexpr locals so the static_asserts above are not optimized away,
    // and re-assert at runtime so this case has observable REQUIREs too.
    REQUIRE(leading == -1.0f);
    REQUIRE(interior == 0.0f);
    REQUIRE(trailing != 0.0f);
}

TEST_CASE("polyblep: function is noexcept", "[polyblep]") {
    // [ADR-002 Contract / docs/design/01 §3.1] hot-path residual is noexcept.
    STATIC_REQUIRE(noexcept(polyBlep(0.25f, 0.1f)));
    STATIC_REQUIRE(std::is_same_v<decltype(polyBlep(0.0f, 0.1f)), float>);
}
