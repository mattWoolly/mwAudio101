// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetsLeadTest.cpp — JUCE-linked validation of the Lead category preset
// set (task 147). Enumerates presets/Lead/*.mw101preset and asserts EACH file:
//
//   1. loads through the real §6.4 loader/validator (loadPresetJson succeeds) — i.e.
//      every one of the 91 live registry IDs is present, every value is in range, every
//      choice index is valid, no per-step accent, no forbidden attribution [ADR-008
//      C13/C16/C18; docs/design/06 §6.4].
//   2. carries meta.category == "Lead" (the folder it lives in) [ADR-008 C14; §6.5].
//   3. has sound_ext == true IFF it uses a software-only feature (vco.range index >= 4,
//      i.e. 32'/64', OR lfo.shape == 4, i.e. Sine) — checked in BOTH directions, against
//      the recovered params, so the on-disk flag cannot lie [ADR-008 C15; research/11 §6.2].
//   4. ships no forbidden descriptor ("as used on track" / "tb-303 filter") in any meta
//      text — re-asserted here directly on the meta the loader returned [ADR-008 C16;
//      research/11 §4.2, §7.3].
//
// The set is also asserted to be a genuine, distinct showcase (not INIT clones): >= 10
// files, every required tag present, the §3.1/§7.3 documented Squarepusher exception cited
// as inspired-by only, the §4.10 chiptune homage labelled homage (not historical usage),
// and at least one software-ext (sound_ext true) AND at least one hardware (false) preset
// so the iff is exercised both ways.
//
// NON-VACUITY: a final case fabricates an INVALID Lead-shaped preset (an out-of-range
// vcf.cutoff) in a temp .mw101preset and asserts loadPresetJson REJECTS it (nullopt) while
// a clean copy loads — proving the loader gate is real and the per-file asserts above are
// not trivially satisfiable.
//
// The folder is located at runtime from __FILE__ (this TU lives at
// <repo>/tests/plugin/PresetsLeadTest.cpp, so the repo root is two levels up), so the test
// reads the SAME on-disk files that ship — it does not embed copies that could drift.
// Test display names begin with the `presets_lead` tag and avoid the '[' character so
// `ctest -R presets_lead --no-tests=error` selects exactly these.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"   // mw::plugin::preset::loadPresetJson / PresetMeta
#include "params/ParamDefs.h"      // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"       // mw::state canonical keys

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;

// Locate <repo>/presets/Lead from this TU's compile-time path. __FILE__ is the absolute
// source path the build feeds the compiler (CMake target_sources uses absolute paths), so
// <this file>/../../../presets/Lead is the shipping folder.
juce::File leadPresetDir()
{
    const juce::File thisFile{ juce::String::fromUTF8(__FILE__) };
    // tests/plugin/PresetsLeadTest.cpp -> tests/plugin -> tests -> <repo>.
    const juce::File repoRoot = thisFile.getParentDirectory()   // tests/plugin
                                        .getParentDirectory()   // tests
                                        .getParentDirectory();  // <repo>
    return repoRoot.getChildFile("presets").getChildFile("Lead");
}

std::vector<juce::File> leadPresetFiles()
{
    std::vector<juce::File> files;
    const auto dir = leadPresetDir();
    if (dir.isDirectory())
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.mw101preset"))
            files.push_back(f);
    return files;
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

// Mirror the §6.4 software-ext rule, computed from the recovered params: vco.range index
// >= 4 (32'/64') OR lfo.shape == 4 (Sine).
bool usesSoftwareExt(const std::map<std::string, double>& v)
{
    return idx(v, "mw101.vco.range") >= 4 || idx(v, "mw101.lfo.shape") == 4;
}

bool containsCaseInsensitive(const juce::String& haystack, const char* needle)
{
    return haystack.toLowerCase().contains(juce::String::fromUTF8(needle).toLowerCase());
}

} // namespace

TEST_CASE("presets_lead folder holds a distinct showcase set of Lead presets",
          "[presets_lead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto dir = leadPresetDir();
    INFO("expected Lead preset dir: " << dir.getFullPathName());
    REQUIRE(dir.isDirectory());

    const auto files = leadPresetFiles();
    // The scope is "~10" Lead presets; require the full set is present (not a stub).
    REQUIRE(files.size() >= 10);
}

TEST_CASE("presets_lead every file loads through the validator with all 91 IDs in range",
          "[presets_lead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = leadPresetFiles();
    REQUIRE(files.size() >= 10);

    for (const auto& file : files)
    {
        INFO("preset: " << file.getFileName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // A nullopt means the file failed a §6.4 rule (missing/out-of-range ID, bad choice
        // index, accent step, forbidden attribution, or sound_ext mismatch).
        REQUIRE(canonical.has_value());

        // Registry-complete: one recovered <PARAM> per live ID, none missing.
        const auto v = recoveredParamValues(*canonical);
        REQUIRE(v.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            REQUIRE(v.find(def.id) != v.end());

        // (2) category == Lead (the folder it lives in) [§6.5; ADR-008 C14].
        CHECK(meta.category == "Lead");

        // (3) sound_ext == used-software-feature, BOTH directions [§6.4; ADR-008 C15]. The
        // loader already rejects a mismatch, so meta.soundExt == the params-derived truth;
        // assert both equal the independent recomputation here.
        const bool sw = usesSoftwareExt(v);
        CHECK(meta.soundExt == sw);

        // (4) no forbidden descriptor in any meta text [§6.4; ADR-008 C16].
        for (const char* phrase : { "as used on track", "tb-303 filter" })
        {
            CHECK_FALSE(containsCaseInsensitive(meta.name, phrase));
            CHECK_FALSE(containsCaseInsensitive(meta.description, phrase));
            CHECK_FALSE(containsCaseInsensitive(meta.inspiredBy, phrase));
            for (const auto& t : meta.tags)
                CHECK_FALSE(containsCaseInsensitive(t, phrase));
        }

        // Author present (the validator requires it non-empty) + a "lead" tag (the set's
        // identity), so these are real Lead voicings, not arbitrary clones.
        CHECK(meta.author.isNotEmpty());
        CHECK(meta.tags.contains("lead"));
    }
}

TEST_CASE("presets_lead set exercises the software-ext iff in both directions",
          "[presets_lead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int swTrue = 0, swFalse = 0;
    for (const auto& file : leadPresetFiles())
    {
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());
        const bool sw = usesSoftwareExt(recoveredParamValues(*canonical));
        if (sw) ++swTrue; else ++swFalse;
    }

    // At least one of each so the iff is meaningfully tested both ways (a software-ext
    // 32'/64' or Sine-LFO lead, and a hardware-faithful lead) [ADR-008 C15; research/11 §6.2].
    CHECK(swTrue >= 1);
    CHECK(swFalse >= 1);
}

TEST_CASE("presets_lead honesty discipline: Squarepusher cited as inspired-by, chiptune is homage",
          "[presets_lead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    bool sawChiptune = false;
    bool sawSquarepusher = false;

    for (const auto& file : leadPresetFiles())
    {
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        const juce::String allText =
            meta.name + " " + meta.description + " " + meta.inspiredBy + " " + meta.tags.joinIntoString(" ");

        // §4.10 — any chiptune/game-music lead must be labelled a homage, NOT historical
        // SH-101 usage. If a preset mentions chiptune/game-music, it must say "homage".
        if (containsCaseInsensitive(allText, "chiptune") || containsCaseInsensitive(allText, "game"))
        {
            sawChiptune = true;
            CHECK(containsCaseInsensitive(allText, "homage"));
        }

        // §3.1/§7.3 — the documented Squarepusher exception, if cited, is inspired-by only
        // (never a literal "as used on track X" reconstruction). The forbidden-phrase gate
        // above already rejects the bad form; here we assert the citation lives in the
        // inspired_by field and is framed inspired-by.
        if (containsCaseInsensitive(allText, "squarepusher"))
        {
            sawSquarepusher = true;
            CHECK(containsCaseInsensitive(meta.inspiredBy, "squarepusher"));
            CHECK(containsCaseInsensitive(meta.inspiredBy, "inspired-by"));
        }
    }

    // The task scope authors both a chiptune homage (§4.10) and the one documented
    // Squarepusher exception (§3.1/§7.3) — confirm they are actually present so the above
    // discipline checks are not vacuous.
    CHECK(sawChiptune);
    CHECK(sawSquarepusher);
}

TEST_CASE("presets_lead loader gate is real: a tampered Lead preset is rejected",
          "[presets_lead]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Take a real, valid Lead preset, prove it loads, then corrupt one value out of range
    // and prove the SAME loader rejects it. This proves the per-file REQUIRE(has_value())
    // assertions above are a genuine gate, not trivially true.
    const auto files = leadPresetFiles();
    REQUIRE(! files.empty());

    const juce::String validText = files.front().loadFileAsString();

    auto cleanFile = juce::File::createTempFile(".mw101preset");
    cleanFile.replaceWithText(validText);
    PresetMeta cleanMeta;
    const auto clean = loadPresetJson(cleanFile, cleanMeta);
    cleanFile.deleteFile();
    REQUIRE(clean.has_value());   // the unmodified preset loads

    // Corrupt mw101.vcf.cutoff to 9.0 (its registry range is [0,1]) — a §6.4 out-of-range
    // failure the validator must reject.
    juce::var root;
    REQUIRE(juce::JSON::parse(validText, root).wasOk());
    auto* obj = root.getDynamicObject();
    REQUIRE(obj != nullptr);
    auto* params = obj->getProperty("params").getDynamicObject();
    REQUIRE(params != nullptr);
    params->setProperty(juce::Identifier{ "mw101.vcf.cutoff" }, 9.0);

    auto badFile = juce::File::createTempFile(".mw101preset");
    badFile.replaceWithText(juce::JSON::toString(root));
    PresetMeta badMeta;
    const auto bad = loadPresetJson(badFile, badMeta);
    badFile.deleteFile();

    CHECK_FALSE(bad.has_value());   // the tampered preset is rejected
}
