// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for OnePoleSmoother (task 008). Names begin with "smooth" and
// (separately) "smoother" — both match `-R smooth`. Paired positive/negative
// controls per docs/design/11 sec 4.2.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "params/Smoother.h"

TEST_CASE("smooth: a step change is de-zippered (monotone, not instantaneous)", "[smooth][smoother]") {
    mw::params::OnePoleSmoother s;
    s.prepare(/*tau=*/0.010, /*tickHz=*/500.0); // S2-like 10 ms at a ~500 Hz control tick
    s.reset(0.0);
    s.setTarget(1.0);

    const double t1 = s.process();
    // POSITIVE: first tick must move toward, but not reach, the target (smoothing).
    REQUIRE(t1 > 0.0);
    REQUIRE(t1 < 1.0);

    // Monotone approach over subsequent ticks.
    double prev = t1;
    for (int i = 0; i < 50; ++i) {
        const double v = s.process();
        REQUIRE(v >= prev);   // monotone non-decreasing toward target
        prev = v;
    }
    REQUIRE(prev > t1);       // it actually advanced
    REQUIRE(prev <= 1.0);
}

TEST_CASE("smooth: a constant input passes through unchanged (negative control)", "[smooth][smoother]") {
    mw::params::OnePoleSmoother s;
    s.prepare(0.010, 500.0);
    s.reset(0.42);
    s.setTarget(0.42);
    // No step => the value never deviates; a "smoother" that injected motion fails here.
    for (int i = 0; i < 100; ++i) {
        REQUIRE(s.process() == 0.42);
    }
    REQUIRE_FALSE(s.isSmoothing());
}

TEST_CASE("smooth: NoSmooth time-constant snaps to target each tick", "[smooth][smoother]") {
    mw::params::OnePoleSmoother s;
    s.prepare(/*tau=*/0.0, /*tickHz=*/500.0); // tau<=0 => snap (the NoSmooth class)
    s.reset(0.0);
    s.setTarget(1.0);
    REQUIRE(s.process() == 1.0);              // snapped immediately, no de-zipper
}
