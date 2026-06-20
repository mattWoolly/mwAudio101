// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/AcidBassLeadPresetTest.cpp — JUCE-linked validation of the AcidBassLead
// factory category (task 145). Enumerates EVERY presets/AcidBassLead/*.mw101preset on
// disk and, for each, asserts it passes the real §6.4 loader/validator and the task's
// acceptance criteria:
//
//   1. loadPresetJson succeeds — i.e. schemaVersion + required meta present, EVERY one of
//      the 91 live registry IDs present, every continuous value in its NormalisableRange,
//      every choice/bool index a valid integer < choiceCount, no per-step accent, no
//      forbidden attribution [ADR-008 C13/C18; docs/design/06 §6.4].
//   2. meta.category == "AcidBassLead" (the one §6.5 enum value this category claims)
//      [ADR-008 C14; docs/design/06 §6.5].
//   3. sound_ext is CONSISTENT with the params: true iff the patch uses a software-only
//      feature (vco.range index >= 4 i.e. 32'/64', or lfo.shape index == 4 i.e. Sine)
//      [ADR-008 C15; research/11 §6.1, §6.2]. (The loader already rejects a mismatch, so a
//      successful load proves it; this re-derives it from the recovered params as a
//      belt-and-suspenders cross-check.)
//   4. HONESTY DISCIPLINE: no file's human-facing meta text (name/description/tags/
//      inspired_by) ships the forbidden "TB-303 filter" descriptor or an "as used on
//      track" track-reconstruction claim [ADR-008 C16; research/11 §4.2, §7.3]. inspired_by
//      is null (empty) or a careful inspired-by/disputed string, never a track claim.
//   5. The category is non-empty (>= 1 file) AND carries the §7.1(1)/§4.3 acid idioms in at
//      least one file: a high-resonance fast-decay zero-sustain filter-env acid shape, and
//      a 'filter-as-sine-oscillator' resonant variant (self-oscillating VCF) [research/11
//      §4.3, §7.1(1)].
//
// Each .mw101preset is read FROM DISK through the same loadPresetJson seam the PresetManager
// uses — this is the CI "mirror presets/ 1:1" gate (ADR-008 C18) operating on the authored
// files, not an embedded copy. The folder is located relative to this test's source file
// (__FILE__ is the absolute path the CMake glob compiled in), then by walking up from the
// test executable as a fallback, so it resolves regardless of the build directory.
//
// Test display names begin with the `presets_acidbasslead` tag and avoid the '[' character
// so `ctest -R presets_acidbasslead --no-tests=error` selects exactly these.
//
// WHY plugin/ AND NOT core/: this TU calls the JUCE loadPresetJson (juce::File / juce::var /
// juce::ValueTree) which CANNOT live in mwcore, so it is built into mw101_plugin_tests under
// MW_BUILD_PLUGIN=ON. The task file's `default`-preset Verification block is STALE — the
// projection fundamentally requires JUCE (mirrors what QA accepted for tasks 023/025); only
// the build location/commands are corrected, the asserted behavior is exactly the task scope.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"   // mw::plugin::preset::loadPresetJson / PresetMeta
#include "params/ParamDefs.h"      // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"       // mw::state canonical keys

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;

// The software-only choice indices that force sound_ext == true (§6.4)
// [docs/design/06 §3.4; ADR-008 C6/C15; research/11 §6.1, §6.2].
constexpr int kVcoRangeSwFirst = 4;  // mw101.vco.range indices >= 4 (32'/64') are sw-only
constexpr int kLfoShapeSwSine  = 4;  // mw101.lfo.shape index 4 (Sine) is software-only

// Locate the repo's presets/AcidBassLead directory. Primary: relative to this test source
// (__FILE__ -> .../tests/plugin/AcidBassLeadPresetTest.cpp; the repo root is two parents up
// from tests/). Fallback: walk up from the running executable until a presets/AcidBassLead
// child is found (covers an out-of-tree build that copied the source).
juce::File acidBassLeadDir()
{
    const juce::File self{ juce::String::fromUTF8(__FILE__) };
    if (self.existsAsFile())
    {
        // .../tests/plugin/AcidBassLeadPresetTest.cpp -> repo root is .../<root>
        const auto repoRoot = self.getParentDirectory()   // tests/plugin
                                  .getParentDirectory()    // tests
                                  .getParentDirectory();   // repo root
        const auto dir = repoRoot.getChildFile("presets").getChildFile("AcidBassLead");
        if (dir.isDirectory())
            return dir;
    }

    // Fallback: climb from the executable looking for presets/AcidBassLead.
    auto cur = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                   .getParentDirectory();
    for (int i = 0; i < 12 && cur.exists(); ++i)
    {
        const auto dir = cur.getChildFile("presets").getChildFile("AcidBassLead");
        if (dir.isDirectory())
            return dir;
        cur = cur.getParentDirectory();
    }
    return {};
}

// All *.mw101preset files in the AcidBassLead folder.
juce::Array<juce::File> acidBassLeadPresets()
{
    juce::Array<juce::File> out;
    const auto dir = acidBassLeadDir();
    if (dir.isDirectory())
        out = dir.findChildFiles(juce::File::findFiles, /*recursive=*/false, "*.mw101preset");
    return out;
}

// Recovered <PARAMS> subtree as id -> modeled value.
std::map<std::string, double> recoveredParamValues(const juce::ValueTree& canonical)
{
    std::map<std::string, double> out;
    const auto params = canonical.getChildWithName(juce::Identifier{ mw::state::kParamsId });
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto child = params.getChild(i);
        out[child.getProperty("id").toString().toStdString()] =
            static_cast<double>(child.getProperty("value"));
    }
    return out;
}

int idx(const std::map<std::string, double>& m, const char* id)
{
    const auto it = m.find(id);
    return it == m.end() ? -1 : static_cast<int>(std::lround(it->second));
}

double val(const std::map<std::string, double>& m, const char* id)
{
    const auto it = m.find(id);
    return it == m.end() ? std::nan("") : it->second;
}

bool usesSoftwareExt(const std::map<std::string, double>& m)
{
    return idx(m, "mw101.vco.range") >= kVcoRangeSwFirst
        || idx(m, "mw101.lfo.shape") == kLfoShapeSwSine;
}

} // namespace

TEST_CASE("presets_acidbasslead the category folder holds at least one authored preset",
          "[presets_acidbasslead]")
{
    const auto dir = acidBassLeadDir();
    INFO("Expected presets/AcidBassLead/ relative to " << __FILE__);
    REQUIRE(dir.isDirectory());

    const auto files = acidBassLeadPresets();
    INFO("Looked in: " << dir.getFullPathName());
    // ~14 files per the task scope; assert a healthy lower bound so a half-authored
    // category is caught, not just an empty one.
    REQUIRE(files.size() >= 10);
}

TEST_CASE("presets_acidbasslead every file loads through the validator with all IDs in range",
          "[presets_acidbasslead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = acidBassLeadPresets();
    REQUIRE(files.size() >= 10);

    for (const auto& file : files)
    {
        INFO("Validating " << file.getFileName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // A nullopt means this file failed a §6.4 rule (missing/extra ID, out-of-range
        // value, bad choice index, sound_ext mismatch, per-step accent, or a forbidden
        // attribution). All of those are hard failures for this category.
        REQUIRE(canonical.has_value());

        // Registry-complete: one recovered <PARAM> per live ID, none missing.
        const auto v = recoveredParamValues(*canonical);
        REQUIRE(v.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            REQUIRE(v.find(def.id) != v.end());

        // (2) category is exactly AcidBassLead.
        CHECK(meta.category == "AcidBassLead");

        // (3) sound_ext consistency: the recovered loader meta flag matches the params,
        // and equals our independent re-derivation from vco.range / lfo.shape.
        CHECK(meta.soundExt == usesSoftwareExt(v));

        // Belt-and-suspenders honesty: if NOT flagged sound_ext, the patch must use only
        // hardware registers/shapes; if flagged, it must actually engage one.
        if (! meta.soundExt)
        {
            CHECK(idx(v, "mw101.vco.range") < kVcoRangeSwFirst);
            CHECK(idx(v, "mw101.lfo.shape") != kLfoShapeSwSine);
        }
        else
        {
            CHECK((idx(v, "mw101.vco.range") >= kVcoRangeSwFirst
                   || idx(v, "mw101.lfo.shape") == kLfoShapeSwSine));
        }

        // (4) Honesty discipline — the loader already rejects forbidden phrases, but
        // re-assert directly on the recovered meta so a future loosening of the loader is
        // still caught by THIS category's test (case-insensitive).
        const auto nameL = meta.name.toLowerCase();
        const auto descL = meta.description.toLowerCase();
        const auto inspL = meta.inspiredBy.toLowerCase();
        for (const auto& s : { nameL, descL, inspL })
        {
            CHECK_FALSE(s.contains("tb-303 filter"));
            CHECK_FALSE(s.contains("as used on track"));
        }
        for (const auto& tag : meta.tags)
        {
            const auto t = tag.toLowerCase();
            CHECK_FALSE(t.contains("tb-303 filter"));
            CHECK_FALSE(t.contains("as used on track"));
        }
    }
}

TEST_CASE("presets_acidbasslead the category carries the section-4.3 and 7.1 acid idioms",
          "[presets_acidbasslead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = acidBassLeadPresets();
    REQUIRE(files.size() >= 10);

    bool sawAcidShape    = false;  // §4.3: high resonance + fast-decay zero-sustain filter env
    bool sawSineSelfOsc  = false;  // §7.1(1): a 'filter-as-sine-oscillator' resonant variant
    bool sawDriveBaked   = false;  // §4.3: overdrive grit baked into a gritty variant (ADR-016)
    bool sawGlide        = false;  // §4.3: auto/on portamento for techno slides

    for (const auto& file : files)
    {
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());
        const auto v = recoveredParamValues(*canonical);

        // §4.3 acid shape: high resonance (heading toward self-oscillation), the filter env
        // driven hard (env_mod up), Attack 0, Sustain 0, with the env reaching the cutoff.
        const bool acidShape =
            val(v, "mw101.vcf.resonance") >= 0.7
            && val(v, "mw101.vcf.env_mod") >= 0.5
            && val(v, "mw101.env.attack") <= 0.02
            && val(v, "mw101.env.sustain") <= 0.02;
        if (acidShape)
            sawAcidShape = true;

        // §7.1(1) filter-as-sine-oscillator: the VCF taken to (near) self-oscillation so it
        // sings as a sine source — extreme resonance with the audible source levels pulled
        // right down so the resonant peak IS the tone.
        const bool sineSelfOsc =
            val(v, "mw101.vcf.resonance") >= 0.95
            && val(v, "mw101.saw.level") <= 0.15
            && val(v, "mw101.pulse.level") <= 0.15
            && val(v, "mw101.sub.level") <= 0.15;
        if (sineSelfOsc)
            sawSineSelfOsc = true;

        // §4.3 grit: Drive baked in (FX un-bypassed + drive enabled with audible amount).
        const bool driveBaked =
            idx(v, "mw101.fx.bypass") == 0
            && idx(v, "mw101.fx.drive_enable") == 1
            && val(v, "mw101.fx.drive_amount") >= 0.2;
        if (driveBaked)
            sawDriveBaked = true;

        // §4.3 portamento: auto (1) or on (2) glide for slides.
        if (idx(v, "mw101.glide.mode") >= 1)
            sawGlide = true;
    }

    CHECK(sawAcidShape);
    CHECK(sawSineSelfOsc);
    CHECK(sawDriveBaked);
    CHECK(sawGlide);
}
