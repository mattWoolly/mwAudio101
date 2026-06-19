// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the declarative parameter registry kParamDefs (task 019).
// Test-case names begin with "paramdefs"; tag is [paramdefs]. The §3.1 invariants are
// additionally enforced at COMPILE time by static_assert inside ParamDefs.h — these
// runtime cases re-verify them objectively and pin the §3.0 contract counts/values so
// a silent drift in the table fails CI.
//
// Coverage maps 1:1 to the task acceptance criteria:
//  - 91 live entries + the os.factor alias slot; IDs/types/ranges/defaults match §3.0
//  - structural params => isAutomatable==false && SmoothingClass::NoSmooth   [§3.7/§3.8]
//  - choice indices / canonical counts; software-ext indices >= canonicalCount [§3.4]
//  - skews/defaults/time-constants come from Calibration.h, never inlined       [§3.10]
//  - §3.1 invariants (unique + mw101.-prefixed; choices!=nullptr; ...)          [§3.1]

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <set>
#include <string_view>

#include "calibration/ParamDefsConstants.h"
#include "params/ParamDefs.h"
#include "params/SmoothingClass.h"

namespace pd = mw::params;

namespace {

// Find a live def by ID (the alias slot is excluded from kParamDefs deliberately).
const pd::ParamDef* find(std::string_view id) {
    for (const auto& d : pd::kParamDefs) {
        if (id == d.id) return &d;
    }
    return nullptr;
}

bool isStructural(std::string_view id) {
    return id == "mw101.quality" || id == "mw101.voice.mode"
        || id == "mw101.voice.count" || id == "mw101.unison.count"
        || id == "mw101.control.vintage";
}

} // namespace

TEST_CASE("paramdefs: kParamDefs has exactly 91 live entries", "[paramdefs]") {
    // §3.0: 91 live AudioProcessorParameters (the alias slot is NOT counted live).
    REQUIRE(pd::kParamDefs.size() == 91);
}

TEST_CASE("paramdefs: the os.factor alias slot is present and is NOT a live entry",
          "[paramdefs]") {
    // The deprecated alias is retained for migration only (§3.0, §7.4): it must not
    // appear among the 91 live defs, but the registry exposes its slot.
    REQUIRE(find("mw101.os.factor") == nullptr);
    REQUIRE(std::string_view{pd::kOsFactorAlias.id} == "mw101.os.factor");
    REQUIRE(std::string_view{pd::kOsFactorAlias.migratesTo} == "mw101.quality");
}

TEST_CASE("paramdefs: every ID is unique and mw101.-prefixed snake_case", "[paramdefs]") {
    // §3.1 invariant: IDs unique and mw101.-prefixed.
    std::set<std::string_view> seen;
    for (const auto& d : pd::kParamDefs) {
        std::string_view id{d.id};
        REQUIRE(id.substr(0, 6) == "mw101.");
        for (char c : id) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                         || c == '.' || c == '_';
            REQUIRE(ok);
        }
        REQUIRE(seen.insert(id).second);  // duplicate ID fails here
    }
    REQUIRE(seen.size() == pd::kParamDefs.size());
}

TEST_CASE("paramdefs: exactly 5 structural params, all non-automatable + NoSmooth",
          "[paramdefs]") {
    // §3.7/§3.8/§3.1: the five structural params carry isAutomatable==false and
    // SmoothingClass::NoSmooth. 86 of the 91 live params are automatable.
    int structural = 0;
    int automatable = 0;
    for (const auto& d : pd::kParamDefs) {
        if (isStructural(d.id)) {
            ++structural;
            REQUIRE(d.isAutomatable == false);
            REQUIRE(d.smoothing == pd::SmoothingClass::NoSmooth);
        } else {
            REQUIRE(d.isAutomatable == true);
        }
        if (d.isAutomatable) ++automatable;
    }
    REQUIRE(structural == 5);
    REQUIRE(automatable == 86);
}

TEST_CASE("paramdefs: choice params have non-null labels and choiceCount >= canonical",
          "[paramdefs]") {
    // §3.1 invariant: choice params have choices != nullptr and
    // choiceCount >= canonicalChoiceCount.
    for (const auto& d : pd::kParamDefs) {
        if (d.type == pd::ParamType::Choice) {
            REQUIRE(d.choices != nullptr);
            REQUIRE(d.choiceCount >= d.canonicalChoiceCount);
            REQUIRE(d.canonicalChoiceCount >= 1);
            REQUIRE(d.isDiscrete == true);
            // Every choice label slot is non-null.
            for (std::uint8_t i = 0; i < d.choiceCount; ++i) {
                REQUIRE(d.choices[i] != nullptr);
            }
        }
    }
}

TEST_CASE("paramdefs: bool params are two-option discrete choices", "[paramdefs]") {
    // §3.5: index 0=false, 1=true; modeled as a 2-label discrete control.
    for (const auto& d : pd::kParamDefs) {
        if (d.type == pd::ParamType::Bool) {
            REQUIRE(d.choices != nullptr);
            REQUIRE(d.choiceCount == 2);
            REQUIRE(d.isDiscrete == true);
        }
    }
}

TEST_CASE("paramdefs: software-ext indices sit at or above the canonical count",
          "[paramdefs]") {
    // §3.4: the only software extensions are vco.range indices >= 4 and lfo.shape
    // index 4 (Sine). Any def flagged isSoftwareExt must expose choices ABOVE its
    // canonical count, and only those two params are flagged.
    const auto* range = find("mw101.vco.range");
    const auto* shape = find("mw101.lfo.shape");
    REQUIRE(range != nullptr);
    REQUIRE(shape != nullptr);

    // vco.range: canonical 4 (16'/8'/4'/2'), software 32'/64' at indices 4,5.
    REQUIRE(range->canonicalChoiceCount == 4);
    REQUIRE(range->choiceCount == 6);
    REQUIRE(range->isSoftwareExt == true);

    // lfo.shape: canonical 4 (Tri/Sq/Random/Noise), software Sine at index 4.
    REQUIRE(shape->canonicalChoiceCount == 4);
    REQUIRE(shape->choiceCount == 5);
    REQUIRE(shape->isSoftwareExt == true);

    // Exactly these two params carry the software-ext flag, and the flag is only set
    // when choiceCount > canonicalChoiceCount (extras live above the canon).
    int extCount = 0;
    for (const auto& d : pd::kParamDefs) {
        if (d.isSoftwareExt) {
            ++extCount;
            REQUIRE(d.type == pd::ParamType::Choice);
            REQUIRE(d.choiceCount > d.canonicalChoiceCount);
        }
    }
    REQUIRE(extCount == 2);
}

TEST_CASE("paramdefs: continuous defaults lie within their declared range", "[paramdefs]") {
    for (const auto& d : pd::kParamDefs) {
        if (d.type == pd::ParamType::Continuous) {
            REQUIRE(d.minValue < d.maxValue);
            REQUIRE(d.defaultValue >= d.minValue);
            REQUIRE(d.defaultValue <= d.maxValue);
        }
    }
}

TEST_CASE("paramdefs: choice defaults are valid in-range indices", "[paramdefs]") {
    for (const auto& d : pd::kParamDefs) {
        if (d.type == pd::ParamType::Choice || d.type == pd::ParamType::Bool) {
            const int def = static_cast<int>(d.defaultValue);
            REQUIRE(def >= 0);
            REQUIRE(def < d.choiceCount);
        }
    }
}

TEST_CASE("paramdefs: spot-check §3.0 contract values for representative IDs",
          "[paramdefs]") {
    // A representative cross-section of §3.0 rows: type, range, default, automatable,
    // smoothing class — verbatim from the master index.
    {
        const auto* p = find("mw101.vco.tune");  // -24..+24 semis, lin, def 0, Pitch
        REQUIRE(p != nullptr);
        REQUIRE(p->type == pd::ParamType::Continuous);
        REQUIRE(p->minValue == -24.0f);
        REQUIRE(p->maxValue == 24.0f);
        REQUIRE(p->defaultValue == 0.0f);
        REQUIRE(p->smoothing == pd::SmoothingClass::Pitch);
    }
    {
        const auto* p = find("mw101.vco.fine");  // symmetric skew
        REQUIRE(p != nullptr);
        REQUIRE(p->symmetricSkew == true);
        REQUIRE(p->minValue == -1.0f);
        REQUIRE(p->maxValue == 1.0f);
    }
    {
        const auto* p = find("mw101.vcf.cutoff");  // def 1.0, Fast
        REQUIRE(p != nullptr);
        REQUIRE(p->defaultValue == 1.0f);
        REQUIRE(p->smoothing == pd::SmoothingClass::Fast);
    }
    {
        const auto* p = find("mw101.vca.level");  // def 0.8, Level
        REQUIRE(p != nullptr);
        REQUIRE(p->defaultValue == 0.8f);
        REQUIRE(p->smoothing == pd::SmoothingClass::Level);
    }
    {
        const auto* p = find("mw101.lfo.rate");  // 0.1..30 Hz, def 5, Fast
        REQUIRE(p != nullptr);
        REQUIRE(p->minValue == 0.1f);
        REQUIRE(p->maxValue == 30.0f);
        REQUIRE(p->defaultValue == 5.0f);
    }
    {
        const auto* p = find("mw101.glide.time");  // 0..5 s, Glide
        REQUIRE(p != nullptr);
        REQUIRE(p->maxValue == 5.0f);
        REQUIRE(p->smoothing == pd::SmoothingClass::Glide);
    }
    {
        const auto* p = find("mw101.fx.bypass");  // bool, default TRUE (bypassed)
        REQUIRE(p != nullptr);
        REQUIRE(p->type == pd::ParamType::Bool);
        REQUIRE(p->defaultValue == 1.0f);
    }
    {
        const auto* p = find("mw101.quality");  // choice, def 1 Standard, structural
        REQUIRE(p != nullptr);
        REQUIRE(p->type == pd::ParamType::Choice);
        REQUIRE(p->defaultValue == 1.0f);
        REQUIRE(p->isAutomatable == false);
        REQUIRE(p->canonicalChoiceCount == 3);
    }
}

TEST_CASE("paramdefs: tempo-sync subdivision choices have 6 options and §3.4 defaults",
          "[paramdefs]") {
    // §3.4: the four *.sync_div choice sets are the 6-entry division ladder.
    struct Row { const char* id; int def; };
    const Row rows[] = {
        {"mw101.lfo.sync_div", 1},
        {"mw101.arp.sync_div", 1},
        {"mw101.seq.sync_div", 3},
        {"mw101.fx.delay_division", 1},
    };
    for (const auto& r : rows) {
        const auto* p = find(r.id);
        REQUIRE(p != nullptr);
        REQUIRE(p->type == pd::ParamType::Choice);
        REQUIRE(p->choiceCount == 6);
        REQUIRE(p->canonicalChoiceCount == 6);
        REQUIRE(static_cast<int>(p->defaultValue) == r.def);
    }
}

TEST_CASE("paramdefs: skews and (PI) defaults are wired from the calibration table",
          "[paramdefs]") {
    // §3.10: skews/defaults/ceilings are referenced from Calibration.h, not inlined.
    // We assert the registry's stored value EQUALS the calibration constant, which is
    // the observable, objective form of "never inlined".
    {
        const auto* p = find("mw101.vcf.cutoff");  // log-ish skew
        REQUIRE(p != nullptr);
        REQUIRE(p->skew == mw::cal::skew::kCutoff);
    }
    {
        const auto* p = find("mw101.vco.tune");  // linear
        REQUIRE(p != nullptr);
        REQUIRE(p->skew == mw::cal::skew::kLinear);
    }
    {
        const auto* p = find("mw101.lfo.rate");
        REQUIRE(p != nullptr);
        REQUIRE(p->skew == mw::cal::skew::kLfoRate);
    }
    {
        const auto* p = find("mw101.vel.depth");  // (PI) default 0.5
        REQUIRE(p != nullptr);
        REQUIRE(p->defaultValue == mw::cal::paramdefault::kVelDepth);
    }
    {
        const auto* p = find("mw101.fx.delay_feedback");  // (PI) 0.95 ceiling
        REQUIRE(p != nullptr);
        REQUIRE(p->maxValue == mw::cal::paramrange::kDelayFeedbackMax);
    }
}

TEST_CASE("paramdefs: smoothing-class assignment matches the §3.9 mapping", "[paramdefs]") {
    // §3.9 class-to-param mapping spot checks across each non-default class.
    REQUIRE(find("mw101.vco.tune")->smoothing       == pd::SmoothingClass::Pitch);
    REQUIRE(find("mw101.tune.slop")->smoothing      == pd::SmoothingClass::Pitch);
    REQUIRE(find("mw101.vcf.resonance")->smoothing  == pd::SmoothingClass::Fast);
    REQUIRE(find("mw101.lfo.depth_pwm")->smoothing  == pd::SmoothingClass::Fast);
    REQUIRE(find("mw101.vco.pw")->smoothing         == pd::SmoothingClass::PulseWidth);
    REQUIRE(find("mw101.vco.pwm_depth")->smoothing  == pd::SmoothingClass::PulseWidth);
    REQUIRE(find("mw101.saw.level")->smoothing      == pd::SmoothingClass::Level);
    REQUIRE(find("mw101.glide.time")->smoothing     == pd::SmoothingClass::Glide);

    // §3.9: envelope A/D/R times are explicitly NoSmooth (the DSP re-reads per stage).
    REQUIRE(find("mw101.env.attack")->smoothing  == pd::SmoothingClass::NoSmooth);
    REQUIRE(find("mw101.env.decay")->smoothing   == pd::SmoothingClass::NoSmooth);
    REQUIRE(find("mw101.env.release")->smoothing == pd::SmoothingClass::NoSmooth);
    // env.sustain is a LEVEL, so it IS de-zippered.
    REQUIRE(find("mw101.env.sustain")->smoothing == pd::SmoothingClass::Level);
}

TEST_CASE("paramdefs: every entry has a non-empty label and a versionAdded", "[paramdefs]") {
    for (const auto& d : pd::kParamDefs) {
        REQUIRE(d.label != nullptr);
        REQUIRE(std::strlen(d.label) > 0);
        REQUIRE(d.versionAdded >= 1);
    }
}
