// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetSubBassTest.cpp — JUCE-linked enumeration test for the SubBass
// factory preset category (task 146). It walks the on-disk presets/SubBass/ folder and
// runs EVERY .mw101preset file through the real §6.4 loader/validator
// (mw::plugin::preset::loadPresetJson), asserting each acceptance criterion of
// plan/backlog/146:
//
//   - the selector matches (>=1 file present), so `ctest -R presets_subbass
//     --no-tests=error` cannot silently pass [AGENTS.md silent-pass rule];
//   - each file loads (loadPresetJson returns a value) — which alone proves every live
//     registry ID is present + in range, every choice index is valid, the category is
//     in the §6.5 enum, sound_ext == used-software-ext, and no forbidden attribution
//     phrasing is present (those are exactly the §6.4 rules loadPresetJson enforces)
//     [docs/design/06 §6.4; ADR-008 C13-C18];
//   - meta.category == "SubBass" [ADR-008 C14; research/11 §7.1(2)];
//   - the recovered <PARAMS> subtree carries all 91 IDs (registry-complete) and the
//     voicing is a genuine sub-bass: MONO voice mode, a prominent independent sub-osc
//     level, and a canonical sub-mode index (research/11 §4.4: -1 Oct Sq / -2 Oct Sq /
//     -2 Oct Pulse == indices 0/1/2);
//   - sound_ext is true IFF the preset uses a software-only feature (vco.range >= 4 ==
//     32'/64', or lfo.shape == 4 == Sine) — re-checked here independently of the loader
//     [ADR-008 C15; research/11 §6.1, §6.2];
//   - no preset meta carries an "as used on track" or "TB-303 filter" descriptor
//     [ADR-008 C16; research/11 §4.2, §7.3].
//
// WHY plugin/ (not core/) and MW_BUILD_PLUGIN: the loader projects/recovers the
// canonical juce::ValueTree (task 023/025), so it fundamentally requires JUCE and cannot
// build under the JUCE-free core/`default` preset. The task file's `component: core` /
// `cmake --preset default` verification block is STALE for that reason; this test lives
// in tests/plugin (compiled into mw101_plugin_tests) and is verified with
// MW_BUILD_PLUGIN=ON, mirroring what QA accepted for tasks 023/025. Behavior asserted is
// exactly task 146's Scope/Acceptance — only the build location/commands are corrected.
//
// Test-case display names begin with the `presets_subbass` tag and avoid the '['
// character so `ctest -R presets_subbass` selects exactly these.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"   // mw::plugin::preset (the unit under test)
#include "params/ParamDefs.h"      // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"       // mw::state canonical keys

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;

// Locate the repository's presets/SubBass directory by walking up from this test source
// file's compile-time path (__FILE__ is .../tests/plugin/PresetSubBassTest.cpp). This is
// robust to the build directory's working directory and is identical local vs CI because
// CI mirrors the source tree 1:1 [ADR-008 C18]. Falls back to walking up from the cwd if
// the source path is somehow unavailable.
juce::File findSubBassDir()
{
    const juce::File thisSource{ juce::String::fromUTF8(__FILE__) };
    if (thisSource.existsAsFile())
    {
        // .../tests/plugin/PresetSubBassTest.cpp -> repo root is two parents up from
        // tests/.
        const auto repoRoot = thisSource.getParentDirectory()    // tests/plugin
                                        .getParentDirectory()    // tests
                                        .getParentDirectory();   // repo root
        const auto dir = repoRoot.getChildFile("presets").getChildFile("SubBass");
        if (dir.isDirectory())
            return dir;
    }

    // Fallback: search upward from the current working directory.
    for (auto dir = juce::File::getCurrentWorkingDirectory();
         dir.exists() && dir != dir.getParentDirectory();
         dir = dir.getParentDirectory())
    {
        const auto candidate = dir.getChildFile("presets").getChildFile("SubBass");
        if (candidate.isDirectory())
            return candidate;
    }
    return {};
}

// All *.mw101preset files in presets/SubBass, sorted for deterministic iteration.
juce::Array<juce::File> subBassPresetFiles()
{
    juce::Array<juce::File> files;
    const auto dir = findSubBassDir();
    if (dir.isDirectory())
        files = dir.findChildFiles(juce::File::findFiles, /*recursive=*/false,
                                   "*.mw101preset");
    return files;
}

// Recover the <PARAMS> subtree into an id -> modeled value map.
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

int choiceIndex(const std::map<std::string, double>& m, const char* id)
{
    const auto it = m.find(id);
    return it == m.end() ? -1 : static_cast<int>(std::lround(it->second));
}

} // namespace

TEST_CASE("presets_subbass folder is present and non-empty", "[presets_subbass]")
{
    const auto dir = findSubBassDir();
    INFO("expected presets/SubBass next to the repo root; resolved to: "
         << dir.getFullPathName());
    REQUIRE(dir.isDirectory());

    // The category ships ~10 presets; require a healthy, non-trivial set so this selector
    // can never silently pass with an empty/half-authored folder.
    const auto files = subBassPresetFiles();
    REQUIRE(files.size() >= 8);
}

TEST_CASE("presets_subbass every preset loads through the section-6.4 validator",
          "[presets_subbass]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = subBassPresetFiles();
    REQUIRE_FALSE(files.isEmpty());

    for (const auto& file : files)
    {
        INFO("SubBass preset: " << file.getFileName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // A nullopt means the file failed a §6.4 rule: missing/out-of-range registry ID,
        // invalid choice index, category outside the §6.5 enum, sound_ext mismatch, a
        // per-step accent, or forbidden attribution phrasing. Loading == all of those pass.
        REQUIRE(canonical.has_value());

        // Category MUST be SubBass [ADR-008 C14; research/11 §7.1(2)].
        CHECK(meta.category == juce::String{ "SubBass" });

        // Registry-complete: one <PARAM> per live ID, none missing/extra.
        const auto values = recoveredParamValues(*canonical);
        REQUIRE(values.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            CHECK(values.find(def.id) != values.end());

        // Sub-bass identity: MONO voice (research/11 §7.1(2) — the synth is monophonic),
        // a prominent independent sub-osc level, and a canonical sub-mode index
        // (research/11 §4.4: -1 Oct Sq / -2 Oct Sq / -2 Oct Pulse == 0 / 1 / 2).
        CHECK(choiceIndex(values, "mw101.voice.mode") == 0);

        const auto subLevelIt = values.find("mw101.sub.level");
        REQUIRE(subLevelIt != values.end());
        CHECK(subLevelIt->second >= 0.5);

        const int subMode = choiceIndex(values, "mw101.sub.mode");
        CHECK((subMode == 0 || subMode == 1 || subMode == 2));

        // sound_ext is true IFF a software-only feature is engaged (vco.range >= 4 == the
        // 32'/64' registers, or lfo.shape == 4 == the Sine shape) — re-derived here from
        // the recovered params, independent of the loader's own check [ADR-008 C15;
        // research/11 §6.1, §6.2].
        const bool usesSoftwareExt =
               choiceIndex(values, "mw101.vco.range") >= 4
            || choiceIndex(values, "mw101.lfo.shape") == 4;
        CHECK(meta.soundExt == usesSoftwareExt);

        // Attribution discipline (re-checked on the human-facing meta text) [ADR-008 C16;
        // research/11 §4.2, §7.3].
        const auto lower = [](const juce::String& s) { return s.toLowerCase(); };
        for (const auto* text : { &meta.name, &meta.description, &meta.inspiredBy })
        {
            CHECK_FALSE(lower(*text).contains("as used on track"));
            CHECK_FALSE(lower(*text).contains("tb-303 filter"));
        }
        for (const auto& tag : meta.tags)
        {
            CHECK_FALSE(lower(tag).contains("as used on track"));
            CHECK_FALSE(lower(tag).contains("tb-303 filter"));
        }
    }
}

TEST_CASE("presets_subbass the category covers the three canonical sub-osc modes",
          "[presets_subbass]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = subBassPresetFiles();
    REQUIRE_FALSE(files.isEmpty());

    bool seen[3] = { false, false, false };  // -1 Oct Sq / -2 Oct Sq / -2 Oct Pulse
    for (const auto& file : files)
    {
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        const auto values = recoveredParamValues(*canonical);
        const int subMode = choiceIndex(values, "mw101.sub.mode");
        if (subMode >= 0 && subMode < 3)
            seen[subMode] = true;
    }

    // research/11 §4.4 names exactly three sub modes; the bank should exercise all three.
    CHECK(seen[0]);
    CHECK(seen[1]);
    CHECK(seen[2]);
}
