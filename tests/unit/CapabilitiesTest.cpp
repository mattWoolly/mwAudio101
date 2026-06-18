// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the capability-rung enums + ResolvedCapabilities POD
// (task 098). Names begin with "capabilities".

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

#include "host/Capabilities.h"

using namespace mw::plugin;

TEST_CASE("capabilities: NoteExpressionRung members and order match sec 7.2", "[capabilities]") {
    STATIC_REQUIRE(static_cast<std::uint8_t>(NoteExpressionRung::Native)      == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(NoteExpressionRung::MpeOverMidi) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(NoteExpressionRung::Collapsed)   == 2);
}

TEST_CASE("capabilities: TransportRung members and order match sec 7.2", "[capabilities]") {
    STATIC_REQUIRE(static_cast<std::uint8_t>(TransportRung::SampleAccurate) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TransportRung::BlockQuantized) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TransportRung::FreeRun)        == 2);
}

TEST_CASE("capabilities: PluginFormat covers all five wrappers (sec 8.2)", "[capabilities]") {
    STATIC_REQUIRE(static_cast<std::uint8_t>(PluginFormat::VST3)       == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(PluginFormat::AU)         == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(PluginFormat::CLAP)       == 2);
    STATIC_REQUIRE(static_cast<std::uint8_t>(PluginFormat::Standalone) == 3);
    STATIC_REQUIRE(static_cast<std::uint8_t>(PluginFormat::LV2)        == 4);
}

TEST_CASE("capabilities: ResolvedCapabilities is a trivially copyable POD", "[capabilities]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ResolvedCapabilities>);
    ResolvedCapabilities rc{NoteExpressionRung::Native, TransportRung::SampleAccurate};
    REQUIRE(rc.noteExpr == NoteExpressionRung::Native);
    REQUIRE(rc.transport == TransportRung::SampleAccurate);
}
