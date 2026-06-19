// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the INIT patch (out-of-box defaults, ADR-016 / task 021).
// Test-case names begin with "initpatch"; tag is [initpatch]. Coverage maps 1:1 to
// the task acceptance criteria (docs/design/06 §11, §3.10, §9.2):
//   - INIT applies EXACTLY the §11 overlay poles over the kParamDefs defaults
//   - param defaultValues in kParamDefs are NOT mutated (vintage.age stays 0;
//     fx.bypass stays true) — INIT is a patch overlay, not a default change
//   - (PI) INIT values are read from Calibration.h (the patch value == the named
//     calibration constant, byte-for-byte)
//   - renderVersion == kCurrentRenderVersion; <extras> sequence empty
//   - every live param appears exactly once in the patch (it is a complete tree)

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

#include "calibration/InitPatchConstants.h"
#include "params/ParamDefs.h"
#include "state/InitPatch.h"
#include "version/EngineVersion.h"

namespace st = mw::state;
namespace pd = mw::params;

namespace {

// The kParamDefs default for a live ID (the patch's pre-overlay seed value).
float paramDefault(std::string_view id) {
    for (const auto& d : pd::kParamDefs) {
        if (id == d.id) return d.defaultValue;
    }
    FAIL("unknown param id in test: " << std::string{id});
    return 0.0f;
}

} // namespace

// ---------------------------------------------------------------------------
// Structural: the INIT patch is a COMPLETE canonical tree — one entry per live
// param, no duplicates, no extras [§11; §5.1].
// ---------------------------------------------------------------------------
TEST_CASE("initpatch: covers every live param exactly once", "[initpatch]") {
    const auto patch = st::buildInitPatch();

    REQUIRE(patch.params.size() == pd::kParamDefs.size());

    std::set<std::string_view> seen;
    for (const auto& p : patch.params) {
        const bool inserted = seen.insert(p.id).second;
        REQUIRE(inserted);                 // no duplicate IDs
        REQUIRE(patch.valueFor(p.id).has_value());
    }
    // Every kParamDefs ID is present in the patch.
    for (const auto& d : pd::kParamDefs) {
        REQUIRE(seen.count(std::string_view{ d.id }) == 1);
    }
}

// ---------------------------------------------------------------------------
// Overlay correctness: each §11 surface lands on its specified pole [§11].
// ---------------------------------------------------------------------------
TEST_CASE("initpatch: applies the sec 11 overlay poles over ParamDefs defaults",
          "[initpatch]") {
    const auto patch = st::buildInitPatch();

    // Control rate / pitch quant -> MODERN (index 0) [ADR-016 R-1].
    REQUIRE(patch.valueFor("mw101.control.vintage")
            == static_cast<float>(st::initpole::kControlModern));
    REQUIRE(st::initpole::kControlModern == 0);

    // Velocity ON (-> VCA + VCF) [ADR-016 R-2].
    REQUIRE(patch.valueFor("mw101.vel.enable") == 1.0f);

    // Voice mode MONO (index 0) [ADR-016 R-3].
    REQUIRE(patch.valueFor("mw101.voice.mode")
            == static_cast<float>(st::initpole::kVoiceModeMono));
    REQUIRE(st::initpole::kVoiceModeMono == 0);

    // Analog drift subtle ON [ADR-016 R-4].
    REQUIRE(patch.valueFor("mw101.vintage.enable") == 1.0f);

    // FX engine OFF: bypass true, each per-engine enable false, chorus mode Off
    // [ADR-016 §Accepted; ADR-010 FX-13].
    REQUIRE(patch.valueFor("mw101.fx.bypass") == 1.0f);
    REQUIRE(patch.valueFor("mw101.fx.drive_enable") == 0.0f);
    REQUIRE(patch.valueFor("mw101.fx.chorus_enable") == 0.0f);
    REQUIRE(patch.valueFor("mw101.fx.delay_enable") == 0.0f);
    REQUIRE(patch.valueFor("mw101.fx.chorus_mode")
            == static_cast<float>(st::initpole::kChorusModeOff));
    REQUIRE(st::initpole::kChorusModeOff == 0);

    // Tuning A4 = 440 Hz [ADR-012 C21-C22].
    REQUIRE(patch.valueFor("mw101.tune.a4") == 440.0f);

    // MPE OFF [ADR-012 C10].
    REQUIRE(patch.valueFor("mw101.mpe.enable") == 0.0f);

    // Modern un-quantized pitch OFF [ADR-012 C7].
    REQUIRE(patch.valueFor("mw101.pitch.modern_unquantized") == 0.0f);
}

// ---------------------------------------------------------------------------
// (PI) discipline: the INIT (PI) overlay values come from Calibration.h by name,
// matching byte-for-byte [§3.10; §11; acceptance criterion 2].
// ---------------------------------------------------------------------------
TEST_CASE("initpatch: PI overlay values are read from Calibration.h",
          "[initpatch]") {
    const auto patch = st::buildInitPatch();

    // vintage.age low pole == the named (PI) calibration constant.
    REQUIRE(patch.valueFor("mw101.vintage.age")
            == mw::cal::initpatch::kVintageAgeLow);

    // vel.depth low-mid pole == the named (PI) calibration constant, which is the
    // single param-default source (so the two surfaces cannot drift) [§11; §3.3].
    REQUIRE(patch.valueFor("mw101.vel.depth")
            == mw::cal::initpatch::kVelDepthLowMid);
    REQUIRE(mw::cal::initpatch::kVelDepthLowMid == mw::cal::paramdefault::kVelDepth);

    // tune.a4 440 Hz reference is centralized too.
    REQUIRE(patch.valueFor("mw101.tune.a4") == mw::cal::initpatch::kTuneA4Hz);
}

// ---------------------------------------------------------------------------
// Critical acceptance: building the INIT patch does NOT mutate the kParamDefs
// PARAMETER defaults. vintage.age default stays 0 and fx.bypass default stays true;
// INIT is a patch OVERLAY, not a default change [§11; ADR-016 R-4; acceptance 1].
// ---------------------------------------------------------------------------
TEST_CASE("initpatch: param defaultValues are NOT mutated by INIT",
          "[initpatch]") {
    // Snapshot the registry defaults BEFORE building the patch.
    const float ageDefaultBefore    = paramDefault("mw101.vintage.age");
    const float bypassDefaultBefore = paramDefault("mw101.fx.bypass");
    const float velEnDefaultBefore  = paramDefault("mw101.vel.enable");
    const float voiceDefaultBefore  = paramDefault("mw101.voice.mode");

    const auto patch = st::buildInitPatch();

    // The PARAMETER defaults are unchanged by patch construction.
    REQUIRE(paramDefault("mw101.vintage.age")    == ageDefaultBefore);
    REQUIRE(paramDefault("mw101.fx.bypass")      == bypassDefaultBefore);
    REQUIRE(paramDefault("mw101.vel.enable")     == velEnDefaultBefore);
    REQUIRE(paramDefault("mw101.voice.mode")     == voiceDefaultBefore);

    // Concrete contract values from §3 / §11.
    REQUIRE(ageDefaultBefore == 0.0f);     // vintage.age param default stays 0 (in tune on load)
    REQUIRE(bypassDefaultBefore == 1.0f);  // fx.bypass param default already FX-off (true)

    // And the patch OVERLAY moved age away from the (unchanged) default.
    REQUIRE(patch.valueFor("mw101.vintage.age") != ageDefaultBefore);
    REQUIRE(patch.valueFor("mw101.vintage.age") == mw::cal::initpatch::kVintageAgeLow);

    // kParamDefs is constexpr/immutable; reaffirm the static facts.
    STATIC_REQUIRE(pd::kParamDefs.size() == 91);
}

// ---------------------------------------------------------------------------
// Non-overlaid params inherit their kParamDefs default verbatim (the overlay is a
// SPARSE patch, not a wholesale reset) [§11].
// ---------------------------------------------------------------------------
TEST_CASE("initpatch: non-overlaid params keep their ParamDefs default",
          "[initpatch]") {
    const auto patch = st::buildInitPatch();

    // A representative sample of params NOT touched by the §11 table.
    for (std::string_view id : {
             std::string_view{"mw101.vco.tune"},
             std::string_view{"mw101.vcf.cutoff"},
             std::string_view{"mw101.env.decay"},
             std::string_view{"mw101.saw.level"},
             std::string_view{"mw101.quality"},
             std::string_view{"mw101.tune.slop"} }) {
        REQUIRE(patch.valueFor(id) == paramDefault(id));
    }
}

// ---------------------------------------------------------------------------
// renderVersion == CURRENT and the <extras> sequence is empty by default
// [§11; §9.2; ADR-023 V9; acceptance criterion 3].
// ---------------------------------------------------------------------------
TEST_CASE("initpatch: renderVersion is CURRENT and extras sequence is empty",
          "[initpatch]") {
    const auto patch = st::buildInitPatch();

    REQUIRE(patch.renderVersion == mw101::version::kCurrentRenderVersion);
    REQUIRE(patch.schemaVersion == mw101::version::kCurrentSchemaVersion);

    // Empty <extras> sequence: no steps, latch off, seed 0/unlocked [§5.4; §11].
    REQUIRE(patch.extras.stepCount == 0);
    REQUIRE_FALSE(patch.extras.arpLatch);
    REQUIRE(patch.extras.driftSeed == 0);
    REQUIRE_FALSE(patch.extras.seedLocked);
}
