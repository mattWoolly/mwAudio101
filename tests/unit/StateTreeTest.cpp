// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the state tree ids + Extras POD (task 017). Names begin
// with "statetree".

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <type_traits>

#include "state/Extras.h"
#include "state/StateTree.h"

TEST_CASE("statetree: root id and version attribute keys match sec 5.1", "[statetree]") {
    REQUIRE(std::string_view{mw::state::kRootId} == "MW101_STATE");
    REQUIRE(std::string_view{mw::state::kAttrSchemaVersion} == "schemaVersion");
    REQUIRE(std::string_view{mw::state::kAttrPluginVersion} == "pluginVersion");
    REQUIRE(std::string_view{mw::state::kAttrEngineVersion} == "engineVersion");
    REQUIRE(std::string_view{mw::state::kAttrRenderVersion} == "renderVersion");
    REQUIRE(std::string_view{mw::state::kParamsId} == "PARAMS");
    REQUIRE(std::string_view{mw::state::kExtrasId} == "extras");
}

TEST_CASE("statetree: Extras and SeqStep are trivially copyable, capacity 100", "[statetree]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::state::SeqStep>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::state::Extras>);
    STATIC_REQUIRE(mw::state::kMaxSeqSteps == 100);

    mw::state::Extras e;
    REQUIRE(e.stepCount == 0);
    REQUIRE(e.steps.size() == 100);
    REQUIRE_FALSE(e.arpLatch);
    REQUIRE(e.driftSeed == 0);
    REQUIRE_FALSE(e.seedLocked);
}

// Compile-time proof that SeqStep has NO accent member [ADR-025]. If a future edit
// added `accent`, this detection idiom would change result and the test would flip.
namespace {
template <typename T, typename = void>
struct has_accent : std::false_type {};
template <typename T>
struct has_accent<T, std::void_t<decltype(std::declval<T>().accent)>> : std::true_type {};
} // namespace

TEST_CASE("statetree: SeqStep carries NO per-step accent field (ADR-025)", "[statetree]") {
    STATIC_REQUIRE_FALSE(has_accent<mw::state::SeqStep>::value);
    // Positive control: the fields that MUST exist do.
    mw::state::SeqStep s{};
    s.noteSemitone = 7;
    s.gate = true;
    s.tie  = true;
    s.rest = false;
    REQUIRE(s.noteSemitone == 7);
    REQUIRE(s.gate);
    REQUIRE(s.tie);
    REQUIRE_FALSE(s.rest);
}
