// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the FxParams POD snapshot (task 088). Names begin with
// "fxparams" so the `-R fxparams` selector matches.
//
// Verifies docs/design/07 §7 layout + ADR-010 FX-13 (engine-default OFF/dry) and
// ADR-010 FX-9 (Mono Output flag) and the trivially-copyable POD contract (§7,
// §3.1) consumed lock-free by the FX audio thread.

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "dsp/fx/FxParams.h"

using mw::fx::FxParams;

// Test-case NAMES begin with "fxparams" so the name-based `-R fxparams` selector
// matches (AGENTS.md: discovery registers names). No new bracket [tag] is
// introduced, so the checked-in tests/golden/corpus/ctest-labels.snapshot (which
// diffs TAGS) stays unchanged — this task owns no shared golden file.
TEST_CASE("fxparams: default-constructed snapshot is FX OFF / dry per doc-07 sec7 and ADR-010 FX-13") {
    constexpr FxParams p {};

    STATIC_REQUIRE(p.masterBypass == true);   // engine default OFF -> bypass (FX-13)
    STATIC_REQUIRE(p.monoOutput == false);    // global Mono Output collapse (FX-9)
    STATIC_REQUIRE(p.hostBpm == 120.0);       // from plugin/ AudioPlayHead (ADR-001)

    STATIC_REQUIRE(p.drive.on == false);      // per-block bypass default
    STATIC_REQUIRE(p.chorus.mode == 0);       // Mode::Off == 0
    STATIC_REQUIRE(p.delay.on == false);

    // Runtime mirror so the case asserts something even if STATIC_REQUIRE folds.
    FxParams q {};
    CHECK(q.masterBypass == true);
    CHECK(q.monoOutput == false);
    CHECK(q.hostBpm == 120.0);
    CHECK(q.drive.on == false);
    CHECK(q.chorus.mode == 0);
    CHECK(q.delay.on == false);
}

TEST_CASE("fxparams: snapshot is a trivially-copyable POD per doc-07 sec7") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<FxParams>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<FxParams::DriveP>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<FxParams::ChorusP>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<FxParams::DelayP>);

    // Runtime check too (acceptance criterion explicitly allows runtime check).
    CHECK(std::is_trivially_copyable_v<FxParams>);
}

TEST_CASE("fxparams: nested struct field set + types match doc-07 sec7 layout") {
    // DriveP{ on, amount, tone, output }
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DriveP::on), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DriveP::amount), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DriveP::tone), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DriveP::output), float>);

    // ChorusP{ mode, rate, depth, width, mix } — mode is enum int
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::ChorusP::mode), int>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::ChorusP::rate), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::ChorusP::depth), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::ChorusP::width), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::ChorusP::mix), float>);

    // DelayP{ on, sync, pingpong, division, timeMs, feedback, damp, width, mix }
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::on), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::sync), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::pingpong), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::division), int>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::timeMs), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::feedback), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::damp), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::width), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::DelayP::mix), float>);

    // Top-level master fields + nested members are present with the doc-07 types.
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::masterBypass), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::monoOutput), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::hostBpm), double>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::drive), FxParams::DriveP>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::chorus), FxParams::ChorusP>);
    STATIC_REQUIRE(std::is_same_v<decltype(FxParams::delay), FxParams::DelayP>);
}
