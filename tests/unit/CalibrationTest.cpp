// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the calibration table + frozen-constant-set registry
// (task 005b). Names begin with "calibration" so `-R calibration` selects them.

#include <catch2/catch_test_macros.hpp>

#include "calibration/Calibration.h"
#include "version/EngineVersion.h"

TEST_CASE("calibration: single current renderVersion in the frozen registry", "[calibration][cal]") {
    int currentCount = 0;
    int currentRv = -1;
    for (const auto& e : mw::cal::kFrozenConstantSets) {
        if (e.isCurrent) {
            ++currentCount;
            currentRv = e.renderVersion;
        }
    }
    REQUIRE(currentCount == 1);                                   // exactly one CURRENT
    REQUIRE_FALSE(currentCount == 2);                             // negative control
    REQUIRE(currentRv == mw101::version::kCurrentRenderVersion);  // matches the version constant
}

TEST_CASE("calibration: registry keyed by renderVersion, only shipped versions retained", "[calibration][cal]") {
    for (const auto& e : mw::cal::kFrozenConstantSets) {
        REQUIRE(e.renderVersion >= 1);
        REQUIRE(e.renderVersion <= mw101::version::kCurrentRenderVersion);
    }
    // Lookup hits a shipped version and misses an unshipped one.
    REQUIRE(mw::cal::frozenSetFor(mw101::version::kCurrentRenderVersion) != nullptr);
    REQUIRE(mw::cal::frozenSetFor(0)   == nullptr);   // never-shipped (negative control)
    REQUIRE(mw::cal::frozenSetFor(999) == nullptr);   // future, not yet shipped
}

TEST_CASE("calibration: smoothing time constants are present and ordered as documented", "[calibration][cal]") {
    using namespace mw::cal::smoothing;
    // (PI) tunable defaults; assert the documented relative ordering, not measured facts.
    REQUIRE(kNoSmoothSeconds == 0.0);
    REQUIRE(kPitchSeconds      > 0.0);   // S1 ~2 ms
    REQUIRE(kPulseWidthSeconds > kPitchSeconds);      // S3 ~5 ms  > S1 ~2 ms
    REQUIRE(kFastSeconds       > kPulseWidthSeconds); // S2 ~10 ms > S3 ~5 ms
    REQUIRE(kLevelSeconds      > kFastSeconds);       // S4 ~15 ms > S2 ~10 ms
    REQUIRE(kGlideSeconds      > kLevelSeconds);      // S5 ~20 ms > S4 ~15 ms
}
