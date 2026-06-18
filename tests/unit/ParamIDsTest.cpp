// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the parameter string-ID constants (task 014). Names begin
// with "paramids". Verifies the mw101. prefix discipline and uniqueness over the
// declared (foundation-scope) set.

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>
#include <vector>

#include "params/ParamIDs.h"

namespace ids = mw::params::ids;

namespace {
const std::vector<std::string_view>& allDeclaredIds() {
    static const std::vector<std::string_view> v = {
        ids::kVcoTune, ids::kVcoFine, ids::kVcoPw, ids::kVcoPwmDepth, ids::kVcoRange,
        ids::kSawLevel, ids::kPulseLevel, ids::kSubLevel, ids::kSubMode, ids::kNoiseLevel,
        ids::kVcfCutoff, ids::kVcfResonance, ids::kVcfEnvMod, ids::kVcfLfoMod, ids::kVcfKbdTrack,
        ids::kEnvAttack, ids::kEnvDecay, ids::kEnvSustain, ids::kEnvRelease,
        ids::kLfoRate, ids::kLfoShape,
        ids::kVcaLevel, ids::kVcaMode,
        ids::kGlideTime, ids::kGlideMode,
        ids::kTuneA4, ids::kTuneSlop,
        ids::kDeprecatedOsFactor,
    };
    return v;
}
} // namespace

TEST_CASE("paramids: every declared ID is mw101.-prefixed snake_case", "[paramids]") {
    for (auto id : allDeclaredIds()) {
        REQUIRE(id.substr(0, 6) == "mw101.");
        // snake_case: only lowercase letters, digits, '.', and '_'.
        for (char c : id) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
            REQUIRE(ok);
        }
    }
}

TEST_CASE("paramids: all declared IDs are unique", "[paramids]") {
    std::set<std::string_view> seen;
    for (auto id : allDeclaredIds()) {
        const bool inserted = seen.insert(id).second;
        REQUIRE(inserted);   // a duplicate ID would fail here
    }
    REQUIRE(seen.size() == allDeclaredIds().size());
}

TEST_CASE("paramids: no forbidden variant names are present", "[paramids]") {
    // sec 7.4: canonical IDs only — no amp.volume / tune.fine / sub.shape variants.
    for (auto id : allDeclaredIds()) {
        REQUIRE(id != std::string_view{"mw101.amp.volume"});
        REQUIRE(id != std::string_view{"mw101.tune.fine"});
        REQUIRE(id != std::string_view{"mw101.sub.shape"});
    }
    // The canonical sub mode ID IS present.
    REQUIRE(std::string_view{ids::kSubMode} == "mw101.sub.mode");
}
