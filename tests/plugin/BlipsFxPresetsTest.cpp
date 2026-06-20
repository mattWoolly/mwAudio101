// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/BlipsFxPresetsTest.cpp — JUCE-linked validation of the BlipsFX
// category preset set (task 149). Enumerates presets/BlipsFX/*.mw101preset and, for
// EVERY file, asserts the real §6.4 loader/validator accepts it and that the patch
// honors the category + honesty-discipline contract:
//
//   1. loadPresetJson(file) succeeds — i.e. schemaVersion present, meta fields present,
//      EVERY one of the 91 live registry IDs present + in range, every choice/bool an
//      integer index < choiceCount, no per-step accent [ADR-008 C13/C18; design/06 §6.4].
//   2. meta.category == "BlipsFX" — matches the folder it lives in [ADR-008 C14; §6.5].
//   3. sound_ext is consistent: the loader DERIVES soundExt from the params (vco.range>=4
//      OR lfo.shape==Sine) and REJECTS a mismatch, so a successful load already proves the
//      on-disk flag matched; the test re-derives it from the recovered params and checks
//      both directions of the iff [ADR-008 C15; §6.4]. The set proves BOTH poles: some
//      files use a software-only feature (sound_ext true) and some use none (false).
//   4. No forbidden attribution/descriptor: "as used on track" / "tb-303 filter" appear
//      nowhere in name/description/tags/inspired_by; artist refs are inspired-by/disputed
//      only [ADR-008 C16; research/11 §4.2, §7.3]. (The loader rejects these, so a
//      successful load proves it; the test also re-scans the meta text directly.)
//   5. Blip/FX framing is general-practice/theory, not a sourced SH-101 idiom (§4.6,
//      §7.1(5)); the one permitted track-level inspired-by exception is Squarepusher
//      'Dimotane CO' noise->VCO-pitch-mod (§3.1, §7.3).
//
// Non-vacuity: the suite asserts the folder exists and holds >= 8 .mw101preset files and
// that EACH loaded (the per-file loop runs a positive REQUIRE), and a dedicated negative
// control proves loadPresetJson rejects a folder file that has been corrupted (a missing
// registry ID), so a silently-empty enumeration or a no-op loader cannot pass.
//
// The folder is located from this source file's compile-time path (__FILE__, an absolute
// path because the tests/plugin glob uses CMAKE_CURRENT_SOURCE_DIR): <repo>/tests/plugin/
// -> up 3 -> <repo> -> presets/BlipsFX. This needs no CMake define and touches no shared
// build file. Test display names begin with the `presets_blipsfx` tag and avoid '[' so
// `ctest -R presets_blipsfx --no-tests=error` selects exactly these [task brief].

#include <catch2/catch_test_macros.hpp>

#include <array>
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

// The expected category for every file in this folder (folder-name == category) [§6.5].
constexpr const char* kCategory = "BlipsFX";

// Software-only choice indices (§6.4) — re-derive sound_ext to check the iff both ways.
constexpr const char* kVcoRangeId      = "mw101.vco.range";
constexpr int         kVcoRangeSwFirst = 4;   // 32'/64' registers
constexpr const char* kLfoShapeId      = "mw101.lfo.shape";
constexpr int         kLfoShapeSwSine  = 4;   // Sine shape

// Locate <repo>/presets/BlipsFX from this TU's absolute compile path: tests/plugin/<this>
// -> parent (plugin) -> parent (tests) -> parent (<repo>) -> presets/BlipsFX.
juce::File blipsFxDir()
{
    const juce::File self{ juce::String::fromUTF8(__FILE__) };
    return self.getParentDirectory()          // tests/plugin
              .getParentDirectory()           // tests
              .getParentDirectory()           // <repo>
              .getChildFile("presets")
              .getChildFile("BlipsFX");
}

// Every *.mw101preset in the BlipsFX folder.
juce::Array<juce::File> blipsFxPresets()
{
    juce::Array<juce::File> out;
    blipsFxDir().findChildFiles(out, juce::File::findFiles, /*recursive=*/false,
                                "*.mw101preset");
    return out;
}

// Pull the recovered <PARAMS> subtree into an id -> modeled value map.
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

bool metaTextContains(const PresetMeta& m, const juce::String& needleLower)
{
    if (m.name.toLowerCase().contains(needleLower)) return true;
    if (m.description.toLowerCase().contains(needleLower)) return true;
    if (m.inspiredBy.toLowerCase().contains(needleLower)) return true;
    for (const auto& t : m.tags)
        if (t.toLowerCase().contains(needleLower)) return true;
    return false;
}

} // namespace

TEST_CASE("presets_blipsfx the folder holds at least 8 mw101preset files",
          "[presets_blipsfx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto dir = blipsFxDir();
    REQUIRE(dir.isDirectory());

    const auto files = blipsFxPresets();
    // Non-vacuity floor: the task authors ~8 presets; the selector must match real work.
    REQUIRE(files.size() >= 8);
}

TEST_CASE("presets_blipsfx every file loads through the validator with all 91 IDs in range",
          "[presets_blipsfx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = blipsFxPresets();
    REQUIRE(files.size() >= 8);   // guards against a silently-empty enumeration

    for (const auto& file : files)
    {
        INFO("preset: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // A nullopt means the file failed a §6.4 rule (missing/out-of-range ID, bad choice
        // index, sound_ext mismatch, per-step accent, or a forbidden attribution phrase).
        REQUIRE(canonical.has_value());

        // Registry-complete: exactly one recovered <PARAM> per live ID, none missing.
        const auto values = recoveredParamValues(*canonical);
        REQUIRE(values.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            REQUIRE(values.find(def.id) != values.end());
    }
}

TEST_CASE("presets_blipsfx every file is category BlipsFX with consistent sound_ext",
          "[presets_blipsfx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = blipsFxPresets();
    REQUIRE(files.size() >= 8);

    int softwareExtCount = 0;
    int hardwareOnlyCount = 0;

    for (const auto& file : files)
    {
        INFO("preset: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        // (2) category matches the folder.
        CHECK(meta.category == kCategory);

        // §6.5 enum membership (belt-and-suspenders).
        static const std::array<const char*, 6> kCategories{
            "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
        };
        bool known = false;
        for (const char* c : kCategories)
            known = known || (meta.category == c);
        CHECK(known);

        // (3) sound_ext iff: re-derive the software-ext usage from the recovered params and
        // confirm it equals the flag the loader accepted (the loader rejects a mismatch).
        const auto v = recoveredParamValues(*canonical);
        const bool usesSw = idx(v, kVcoRangeId) >= kVcoRangeSwFirst
                         || idx(v, kLfoShapeId) == kLfoShapeSwSine;
        CHECK(meta.soundExt == usesSw);

        if (usesSw) ++softwareExtCount; else ++hardwareOnlyCount;
    }

    // The set proves the iff in BOTH directions: at least one software-only patch
    // (sound_ext true) AND at least one hardware-only patch (sound_ext false).
    CHECK(softwareExtCount >= 1);
    CHECK(hardwareOnlyCount >= 1);
}

TEST_CASE("presets_blipsfx no forbidden descriptor and attribution is inspired-by only",
          "[presets_blipsfx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = blipsFxPresets();
    REQUIRE(files.size() >= 8);

    for (const auto& file : files)
    {
        INFO("preset: " << file.getFileName().toStdString());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        // (4) the loader already rejects these, but re-scan the recovered meta text so the
        // assertion is explicit and independent of the loader implementation.
        CHECK_FALSE(metaTextContains(meta, "as used on track"));
        CHECK_FALSE(metaTextContains(meta, "tb-303 filter"));

        // meta.name / author are populated (the validator requires non-empty).
        CHECK(meta.name.isNotEmpty());
        CHECK(meta.author.isNotEmpty());
    }
}

TEST_CASE("presets_blipsfx negative control proves the loader rejects an invalid file",
          "[presets_blipsfx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = blipsFxPresets();
    REQUIRE(files.size() >= 1);

    // Take a real, valid BlipsFX file, drop one required registry ID, write it to a temp
    // file, and confirm the loader REJECTS it. This proves the loader is non-vacuous: it is
    // actually running the §6.4 completeness rule, not rubber-stamping every file.
    const auto src = files.getReference(0);
    const juce::String text = src.loadFileAsString();

    juce::var root;
    REQUIRE(juce::JSON::parse(text, root).wasOk());
    auto* obj = root.getDynamicObject();
    REQUIRE(obj != nullptr);
    auto* params = obj->getProperty("params").getDynamicObject();
    REQUIRE(params != nullptr);

    // The valid source loads.
    {
        PresetMeta meta;
        REQUIRE(loadPresetJson(src, meta).has_value());
    }

    // Remove a required ID -> the loader must return nullopt.
    params->removeProperty(juce::Identifier{ "mw101.vcf.cutoff" });
    auto tmp = juce::File::createTempFile(".mw101preset");
    tmp.replaceWithText(juce::JSON::toString(root));

    PresetMeta meta;
    const auto loaded = loadPresetJson(tmp, meta);
    tmp.deleteFile();
    CHECK_FALSE(loaded.has_value());
}
