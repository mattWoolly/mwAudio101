// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for SmoothingClass (task 015). Names begin with "smoothclass".

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "calibration/Calibration.h"
#include "params/SmoothingClass.h"

using mw::params::SmoothingClass;
using mw::params::smoothingTimeConstantSeconds;

TEST_CASE("smoothclass: enum has the six classes in sec 3.9 order with NoSmooth==0", "[smoothclass]") {
    STATIC_REQUIRE(static_cast<std::uint8_t>(SmoothingClass::NoSmooth)   == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(SmoothingClass::Pitch)      == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(SmoothingClass::Fast)       == 2);
    STATIC_REQUIRE(static_cast<std::uint8_t>(SmoothingClass::PulseWidth) == 3);
    STATIC_REQUIRE(static_cast<std::uint8_t>(SmoothingClass::Level)      == 4);
    STATIC_REQUIRE(static_cast<std::uint8_t>(SmoothingClass::Glide)      == 5);
}

TEST_CASE("smoothclass: accessor returns the calibration-table constant per class", "[smoothclass]") {
    // The accessor reads Calibration.h — these MUST equal the table values, never an
    // inlined literal in the accessor.
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::NoSmooth)   == mw::cal::smoothing::kNoSmoothSeconds);
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::Pitch)      == mw::cal::smoothing::kPitchSeconds);
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::Fast)       == mw::cal::smoothing::kFastSeconds);
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::PulseWidth) == mw::cal::smoothing::kPulseWidthSeconds);
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::Level)      == mw::cal::smoothing::kLevelSeconds);
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::Glide)      == mw::cal::smoothing::kGlideSeconds);

    // NoSmooth maps to zero (the default class).
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::NoSmooth) == 0.0);
    // Negative control: a non-NoSmooth class must NOT be zero.
    REQUIRE(smoothingTimeConstantSeconds(SmoothingClass::Pitch) > 0.0);
}
