// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the COMPLETE parameter string-ID catalogue (task 014b).
// Test-case names begin with "paramids"; tag is [paramids]. This closes task 014's
// deferred scope: every one of the 91 live canonical IDs in kParamDefs (plus the
// deprecated mw101.os.factor alias) MUST have a named ids:: constant, matching the
// authoritative registry VERBATIM [docs/design/06 §3.0; ParamDefs.h].
//
// Coverage maps 1:1 to the acceptance criteria:
//  - every kParamDefs[i].id has a matching ids:: constant (1:1 coverage)         [§3.0]
//  - no ids:: live constant names an ID that is absent from kParamDefs           [§3.0]
//  - the live-id count is exactly 91, and exactly one alias (os.factor)          [§3.0]
//  - the alias constant equals kOsFactorAlias.id and is NOT one of the 91 live   [§7.4]
//  - the full catalogue is unique + mw101.-prefixed snake_case                   [§3.1]
//
// The "would fail if a registry id lacked a constant" guarantee is the set-equality
// assertion below: it is built from the registry on one side and the hand-named
// constants on the other, so adding a row to kParamDefs without a matching ids::
// constant (or vice versa) turns this test red.

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>
#include <vector>

#include "params/ParamDefs.h"
#include "params/ParamIDs.h"

namespace ids = mw::params::ids;
namespace pd = mw::params;

namespace {

// Every LIVE ids:: constant, named explicitly. Referencing each by name means a
// missing/renamed constant fails to COMPILE; the set comparison below proves each
// string matches the registry VERBATIM. This vector MUST contain exactly the 91
// live constants (the alias kOsFactorAlias is asserted separately, not here).
const std::vector<std::string_view>& allLiveConstantIds() {
    static const std::vector<std::string_view> v = {
        // --- VCO / oscillator ---
        ids::kVcoTune, ids::kVcoFine, ids::kVcoPw, ids::kVcoPwmDepth, ids::kVcoRange,
        // --- Source mixer / sub / noise ---
        ids::kSawLevel, ids::kPulseLevel, ids::kSubLevel, ids::kSubMode, ids::kNoiseLevel,
        // --- VCF ---
        ids::kVcfCutoff, ids::kVcfResonance, ids::kVcfEnvMod, ids::kVcfLfoMod, ids::kVcfKbdTrack,
        // --- Envelope ---
        ids::kEnvAttack, ids::kEnvDecay, ids::kEnvSustain, ids::kEnvRelease,
        // --- LFO ---
        ids::kLfoRate, ids::kLfoShape, ids::kLfoDest, ids::kLfoDelay,
        ids::kLfoDepthPitch, ids::kLfoDepthPwm, ids::kLfoDepthCutoff,
        ids::kLfoTempoSync, ids::kLfoSyncDiv,
        // --- VCA ---
        ids::kVcaLevel, ids::kVcaMode,
        // --- Glide ---
        ids::kGlideTime, ids::kGlideMode,
        // --- Mod / bend ---
        ids::kModBendRangeVco, ids::kModBendRangeVcf, ids::kModBendDest, ids::kModLfoModWheel,
        // --- Arp ---
        ids::kArpMode, ids::kArpRange, ids::kArpTempoSync, ids::kArpSyncDiv, ids::kArpLatch,
        // --- Seq ---
        ids::kSeqMode, ids::kSeqTempoSync, ids::kSeqSyncDiv,
        // --- Key / trigger ---
        ids::kKeyTriggerPriority,
        // --- Tuning ---
        ids::kTuneA4, ids::kTuneSlop,
        // --- Pitch / velocity / expression / MPE ---
        ids::kPitchModernUnquantized, ids::kVelEnable, ids::kVelDepth, ids::kAmpExpression,
        ids::kMpeEnable, ids::kMpeBendRange, ids::kMpePressureDest,
        // --- Vintage / drift / variance / warm-up ---
        ids::kVintageAge, ids::kVintageEnable, ids::kVintageCalSpread, ids::kVintageDetuneAmt,
        ids::kDriftDepth, ids::kDriftRate, ids::kWarmupTime,
        ids::kVarCutoff, ids::kVarEnvTime, ids::kVarPw, ids::kVarGlide,
        // --- FX: Drive ---
        ids::kFxBypass, ids::kFxDriveEnable, ids::kFxDriveAmount, ids::kFxDriveTone, ids::kFxDriveOutput,
        // --- FX: Chorus ---
        ids::kFxChorusEnable, ids::kFxChorusMode, ids::kFxChorusRate, ids::kFxChorusDepth,
        ids::kFxChorusWidth, ids::kFxChorusMix,
        // --- FX: Delay ---
        ids::kFxDelayEnable, ids::kFxDelaySync, ids::kFxDelayDivision, ids::kFxDelayTime,
        ids::kFxDelayFeedback, ids::kFxDelayDamp, ids::kFxDelayWidth, ids::kFxDelayMix,
        ids::kFxDelayPingpong,
        // --- Output ---
        ids::kOutMono,
        // --- Structural (non-automatable) ---
        ids::kQuality, ids::kVoiceMode, ids::kVoiceCount, ids::kUnisonCount, ids::kControlVintage,
    };
    return v;
}

std::set<std::string_view> registryLiveIds() {
    std::set<std::string_view> s;
    for (const auto& d : pd::kParamDefs) s.insert(std::string_view{d.id});
    return s;
}

std::set<std::string_view> constantLiveIds() {
    std::set<std::string_view> s;
    for (auto id : allLiveConstantIds()) s.insert(id);
    return s;
}

} // namespace

TEST_CASE("paramids: every kParamDefs registry ID has a matching ids constant", "[paramids]") {
    // 1:1 coverage, registry -> constant. A registry row added without an ids::
    // constant turns THIS red (its ID would be missing from constantLiveIds()).
    const auto consts = constantLiveIds();
    for (const auto& d : pd::kParamDefs) {
        INFO("registry ID missing a matching ids:: constant: " << d.id);
        REQUIRE(consts.count(std::string_view{d.id}) == 1);
    }
}

TEST_CASE("paramids: every live ids constant names a real kParamDefs ID", "[paramids]") {
    // 1:1 coverage, constant -> registry. A constant typo (not VERBATIM) or a stale
    // constant naming a removed ID turns THIS red.
    const auto registry = registryLiveIds();
    for (auto id : allLiveConstantIds()) {
        INFO("ids:: constant names an ID absent from kParamDefs: " << id);
        REQUIRE(registry.count(id) == 1);
    }
}

TEST_CASE("paramids: live constant set equals the 91-entry registry set exactly", "[paramids]") {
    // The objective exact-count / exact-coverage assertion. Both sides are 91 and
    // identical; any divergence (extra, missing, or mistyped ID) fails here.
    REQUIRE(pd::kParamDefs.size() == 91);
    REQUIRE(allLiveConstantIds().size() == 91);
    REQUIRE(constantLiveIds().size() == 91);   // no duplicate constants
    REQUIRE(constantLiveIds() == registryLiveIds());
}

TEST_CASE("paramids: the os.factor alias constant is present and is NOT a live param", "[paramids]") {
    // §7.4: the deprecated alias has its own clearly-named constant matching
    // kOsFactorAlias.id verbatim, and it must NOT appear among the 91 live IDs.
    REQUIRE(std::string_view{ids::kOsFactorAlias} == std::string_view{pd::kOsFactorAlias.id});
    REQUIRE(std::string_view{ids::kOsFactorAlias} == "mw101.os.factor");
    REQUIRE(registryLiveIds().count("mw101.os.factor") == 0);
    REQUIRE(constantLiveIds().count("mw101.os.factor") == 0);
}

TEST_CASE("paramids: the original foundation alias name kDeprecatedOsFactor is unchanged", "[paramids]") {
    // task 014b is purely additive: the foundation-era alias constant keeps its name
    // and value, and the new clearer kOsFactorAlias points at the same string.
    REQUIRE(std::string_view{ids::kDeprecatedOsFactor} == "mw101.os.factor");
    REQUIRE(std::string_view{ids::kOsFactorAlias} == std::string_view{ids::kDeprecatedOsFactor});
}

TEST_CASE("paramids: the full catalogue (91 live + alias) is unique and mw101 snake_case", "[paramids]") {
    // §3.1 discipline over the COMPLETE set, including the alias.
    std::vector<std::string_view> all = allLiveConstantIds();
    all.push_back(ids::kOsFactorAlias);

    std::set<std::string_view> seen;
    for (auto id : all) {
        INFO("offending ID: " << id);
        REQUIRE(id.substr(0, 6) == "mw101.");
        for (char c : id) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                         || c == '.' || c == '_';
            REQUIRE(ok);
        }
        REQUIRE(seen.insert(id).second);   // a duplicate string fails here
    }
    REQUIRE(seen.size() == 92);            // 91 live + 1 alias, all distinct
}

TEST_CASE("paramids: the 31 foundation constants still hold their §3.0 values", "[paramids]") {
    // Purely-additive guard: the original representative-set constants are unchanged.
    REQUIRE(std::string_view{ids::kVcoTune}      == "mw101.vco.tune");
    REQUIRE(std::string_view{ids::kVcoFine}      == "mw101.vco.fine");
    REQUIRE(std::string_view{ids::kVcoPw}        == "mw101.vco.pw");
    REQUIRE(std::string_view{ids::kVcoPwmDepth}  == "mw101.vco.pwm_depth");
    REQUIRE(std::string_view{ids::kVcoRange}     == "mw101.vco.range");
    REQUIRE(std::string_view{ids::kSawLevel}     == "mw101.saw.level");
    REQUIRE(std::string_view{ids::kPulseLevel}   == "mw101.pulse.level");
    REQUIRE(std::string_view{ids::kSubLevel}     == "mw101.sub.level");
    REQUIRE(std::string_view{ids::kSubMode}      == "mw101.sub.mode");
    REQUIRE(std::string_view{ids::kNoiseLevel}   == "mw101.noise.level");
    REQUIRE(std::string_view{ids::kVcfCutoff}    == "mw101.vcf.cutoff");
    REQUIRE(std::string_view{ids::kVcfResonance} == "mw101.vcf.resonance");
    REQUIRE(std::string_view{ids::kVcfEnvMod}    == "mw101.vcf.env_mod");
    REQUIRE(std::string_view{ids::kVcfLfoMod}    == "mw101.vcf.lfo_mod");
    REQUIRE(std::string_view{ids::kVcfKbdTrack}  == "mw101.vcf.kbd_track");
    REQUIRE(std::string_view{ids::kEnvAttack}    == "mw101.env.attack");
    REQUIRE(std::string_view{ids::kEnvDecay}     == "mw101.env.decay");
    REQUIRE(std::string_view{ids::kEnvSustain}   == "mw101.env.sustain");
    REQUIRE(std::string_view{ids::kEnvRelease}   == "mw101.env.release");
    REQUIRE(std::string_view{ids::kLfoRate}      == "mw101.lfo.rate");
    REQUIRE(std::string_view{ids::kLfoShape}     == "mw101.lfo.shape");
    REQUIRE(std::string_view{ids::kVcaLevel}     == "mw101.vca.level");
    REQUIRE(std::string_view{ids::kVcaMode}      == "mw101.vca.mode");
    REQUIRE(std::string_view{ids::kGlideTime}    == "mw101.glide.time");
    REQUIRE(std::string_view{ids::kGlideMode}    == "mw101.glide.mode");
    REQUIRE(std::string_view{ids::kTuneA4}       == "mw101.tune.a4");
    REQUIRE(std::string_view{ids::kTuneSlop}     == "mw101.tune.slop");
    REQUIRE(std::string_view{ids::kLfoDepthPitch}  == "mw101.lfo.depth_pitch");
    REQUIRE(std::string_view{ids::kLfoDepthPwm}    == "mw101.lfo.depth_pwm");
    REQUIRE(std::string_view{ids::kLfoDepthCutoff} == "mw101.lfo.depth_cutoff");
}
