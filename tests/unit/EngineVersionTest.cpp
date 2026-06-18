// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the version constants (task 016). Names begin with
// "engineversion".

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <type_traits>

#include "version/EngineVersion.h"

TEST_CASE("engineversion: the four version constants have the sec 9.1 values", "[engineversion]") {
    REQUIRE(mw101::version::kCurrentSchemaVersion == 1);
    REQUIRE(mw101::version::kCurrentRenderVersion == 1);
    REQUIRE(std::string_view{mw101::version::kEngineVersion} == "1.0.0");
    REQUIRE(std::string_view{mw101::version::kPluginVersion} == "1.0.0");
}

TEST_CASE("engineversion: types are int (schema/render) and string (engine/plugin)", "[engineversion]") {
    STATIC_REQUIRE(std::is_same_v<std::remove_cv_t<decltype(mw101::version::kCurrentSchemaVersion)>, int>);
    STATIC_REQUIRE(std::is_same_v<std::remove_cv_t<decltype(mw101::version::kCurrentRenderVersion)>, int>);
    // engineVersion / pluginVersion are MAJOR.MINOR.PATCH-style C strings.
    REQUIRE(std::string_view{mw101::version::kEngineVersion}.find('.') != std::string_view::npos);
}
