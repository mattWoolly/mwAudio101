// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/FactoryPresetCorpusTest.cpp — the presets/ <-> BinaryData 1:1 mirror +
// CI registry guard, and the embedded-bank load assertion (task 131).
//
// Where task 151 (PresetBankCoverageTest) validates the CONTENT of every on-disk preset,
// THIS test guards the SHIPPING boundary: it asserts the JUCE BinaryData set embedded by
// plugin/CMakeLists.txt's juce_add_binary_data target matches the on-disk presets/ tree
// EXACTLY 1:1 (every file <-> a BinaryData entry, no orphan, no missing) — so a preset
// added/removed without re-embedding fails the build LOUDLY rather than silently shipping
// a divergent bank. It also asserts the real PresetManager() default ctor now loads the
// embedded factory bank (~64 presets across the 6 §6.5 categories + INIT, all valid),
// fixing the market gap where the shipped plugin carried ZERO factory presets
// (getNumPresets()==0, MIDI ProgramChange a no-op) [docs/design/06 §6.4, §10.2, §10.3].
//
// The 1:1 mirror compares ORIGINAL FILENAMES (BinaryData::getNamedResourceOriginalFilename)
// against the on-disk basenames, sidestepping JUCE's resource-name sanitisation entirely —
// every basename in presets/ is unique, so a basename set comparison is an exact bijection.
//
// Acceptance criteria asserted here (plan/backlog/131 Scope/Acceptance, RE-SCOPED to the
// presets/ <-> BinaryData mirror + registry; the task file's `component: core` /
// `cmake --preset default` block is STALE — PresetManager + BinaryData require JUCE, so
// this lives in tests/plugin and is verified with MW_BUILD_PLUGIN=ON, mirroring 119/151
// and what QA accepted for task 023):
//
//   (1) The embedded BinaryData set <-> on-disk presets/ tree is EXACTLY 1:1: every file
//       has a BinaryData entry and every entry maps back to an on-disk file, with no
//       missing/orphan either way, failing on a divergence [§6.4; §10.2].
//   (2) PresetManager()'s DEFAULT (embedded) bank loads ~64 presets across the 6 §6.5
//       categories + INIT, all valid (no preset silently dropped, category index
//       populated, no per-slot INIT fallback storm) [§10.1; §10.2].
//   (3) A 'hardware-accurate' preset with tune.a4 == 442 exists in the bank (§10.3). Task
//       131b authored that preset (presets/SubBass/VR-7 Reference Sub.mw101preset), so the
//       former content-gap WARN is now a HARD assertion: a 442 reference preset MUST be
//       present and decode cleanly through loadPresetJson [ADR-012 C21-C22].
//
// Test-case display names begin with the `factorypresets` tag and avoid the '[' character
// so `ctest -R factorypresets` selects exactly these and the silent-pass /
// --no-tests=error rule holds. A juce::ScopedJuceInitialiser_GUI brackets every case
// (JUCE singletons + leak detector), matching tests/plugin/PluginHarnessTest.cpp.

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetManager.h"       // mw::plugin::preset::PresetManager (default ctor)
#include "preset/PresetFormat.h"        // loadPresetJson / PresetMeta (read tune.a4 / category)
#include "state/StateTree.h"            // mw::state canonical keys

#include "BinaryData.h"                 // the embedded factory bank (task 131)

namespace {

using mw::plugin::preset::PresetManager;
using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;

// The six §6.5 categories — the on-disk folder names AND meta.category enum values.
const juce::StringArray kCategories{
    "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
};

// Locate the repository's presets/ root by walking up from this test source file's
// compile-time path (__FILE__ == .../tests/plugin/FactoryPresetCorpusTest.cpp), with a
// cwd-walk fallback. Mirrors PresetBankCoverageTest::findPresetsRoot so the on-disk side
// of the mirror is resolved identically locally and in CI (CI mirrors the tree 1:1).
juce::File findPresetsRoot()
{
    const juce::File thisSource{ juce::String::fromUTF8(__FILE__) };
    if (thisSource.existsAsFile())
    {
        const auto repoRoot = thisSource.getParentDirectory()    // tests/plugin
                                        .getParentDirectory()    // tests
                                        .getParentDirectory();   // repo root
        const auto dir = repoRoot.getChildFile("presets");
        if (dir.isDirectory())
            return dir;
    }

    for (auto dir = juce::File::getCurrentWorkingDirectory();
         dir.exists() && dir != dir.getParentDirectory();
         dir = dir.getParentDirectory())
    {
        const auto candidate = dir.getChildFile("presets");
        if (candidate.isDirectory())
            return candidate;
    }
    return {};
}

// Every *.mw101preset filename (basename) anywhere under presets/ (RECURSIVE), so the
// on-disk side of the mirror sees the whole bank — every category folder AND the
// top-level INIT baseline.
std::set<std::string> onDiskPresetBasenames()
{
    std::set<std::string> names;
    const auto root = findPresetsRoot();
    if (root.isDirectory())
        for (const auto& f : root.findChildFiles(juce::File::findFiles, /*recursive=*/true,
                                                 "*.mw101preset"))
            names.insert(f.getFileName().toStdString());
    return names;
}

// Every embedded resource's ORIGINAL filename (the on-disk basename JUCE recorded at
// embed time), so the comparison is against the pre-sanitisation names — an exact
// bijection because all on-disk basenames are unique.
std::set<std::string> embeddedPresetOriginalFilenames()
{
    std::set<std::string> names;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* original =
            BinaryData::getNamedResourceOriginalFilename(BinaryData::namedResourceList[i]);
        if (original != nullptr)
            names.insert(juce::String{ juce::CharPointer_UTF8{ original } }.toStdString());
    }
    return names;
}

} // namespace

// --- (1) the presets/ <-> BinaryData 1:1 mirror (the CI registry guard) ---------------

TEST_CASE("factorypresets embedded BinaryData set mirrors the on-disk presets tree exactly 1 to 1",
          "[factorypresets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto onDisk   = onDiskPresetBasenames();
    const auto embedded = embeddedPresetOriginalFilenames();

    // The presets/ tree must actually resolve (the mirror is vacuous if it does not).
    INFO("expected presets/ at the repo root; resolved to: "
         << findPresetsRoot().getFullPathName());
    REQUIRE_FALSE(onDisk.empty());

    // Anything on disk but NOT embedded (a preset added without re-embedding) — fail loudly.
    juce::StringArray missingFromBinaryData;
    for (const auto& name : onDisk)
        if (embedded.find(name) == embedded.end())
            missingFromBinaryData.add(juce::String{ name });
    INFO("on disk but NOT embedded in BinaryData: " << missingFromBinaryData.joinIntoString(", "));
    CHECK(missingFromBinaryData.isEmpty());

    // Anything embedded but NOT on disk (a removed preset still embedded) — fail loudly.
    juce::StringArray orphanInBinaryData;
    for (const auto& name : embedded)
        if (onDisk.find(name) == onDisk.end())
            orphanInBinaryData.add(juce::String{ name });
    INFO("embedded in BinaryData but NOT on disk: " << orphanInBinaryData.joinIntoString(", "));
    CHECK(orphanInBinaryData.isEmpty());

    // The exact 1:1 bijection: same count, same set.
    CHECK(embedded.size() == onDisk.size());
    REQUIRE(embedded == onDisk);

    // The bank is the authored ~64-file corpus (63 category files + the INIT baseline).
    CHECK(onDisk.size() >= 60);
    CHECK(onDisk.size() <= 70);
}

// --- (2) PresetManager()'s DEFAULT (embedded) bank loads the real factory corpus ------

TEST_CASE("factorypresets default PresetManager loads the embedded factory bank across all categories",
          "[factorypresets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The market fix: BEFORE task 131 the default ctor was empty (getNumPresets()==0, a
    // DAW saw one blank program). It now enumerates the embedded BinaryData bank.
    PresetManager pm;

    // The embedded set EXACTLY mirrors presets/, so the loaded bank count equals the
    // embedded resource count — and is the authored ~64-file corpus.
    CHECK(pm.getNumPresets() == BinaryData::namedResourceListSize);
    CHECK(pm.getNumPresets() >= 60);
    CHECK(pm.getNumPresets() <= 70);

    // Every preset decoded cleanly into a NAMED slot — none silently dropped.
    int namedSlots = 0;
    for (int i = 0; i < pm.getNumPresets(); ++i)
        if (pm.getName(i).isNotEmpty())
            ++namedSlots;
    CHECK(namedSlots == pm.getNumPresets());

    // The §6.5 category index is populated: every one of the six categories has >= 1
    // member in the shipped bank (so the browser groups the real corpus, not nothing).
    for (const auto& cat : kCategories)
    {
        INFO("category with no members in the embedded bank: " << cat);
        CHECK_FALSE(pm.indicesForCategory(cat).isEmpty());
    }

    // The clean factory corpus must NOT trigger a per-slot INIT-fallback storm: a faithful
    // embedded preset decodes via loadPresetJson without resolving to INIT. A non-empty
    // construction report would mean an embedded preset failed to decode (§8.3 L9).
    INFO("construction report (per-slot INIT fallbacks): "
         << pm.constructionReport().notes.joinIntoString(" | "));
    CHECK(pm.constructionReport().notes.isEmpty());
}

// --- (3) §10.3 'hardware-accurate' tune.a4 == 442 preset presence (HARD assertion) ----

TEST_CASE("factorypresets bank carries a hardware-accurate 442 Hz reference preset",
          "[factorypresets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Scan every embedded preset's decoded canonical <PARAMS> for mw101.tune.a4 == 442.
    // Task 131b authored the §10.3 'hardware-accurate' reference preset, so an ABSENT 442
    // preset is now a BUILD-BREAKING failure (the former content-gap WARN was flipped to a
    // hard assertion): a 442 reference preset MUST exist and decode cleanly through the
    // same §6.4 loadPresetJson path [§10.3; ADR-012 C21-C22; task 131b].
    bool found442 = false;
    juce::String foundIn;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* resourceName = BinaryData::namedResourceList[i];
        int dataSize = 0;
        const char* data = BinaryData::getNamedResource(resourceName, dataSize);
        if (data == nullptr || dataSize <= 0)
            continue;

        const juce::String json{ juce::CharPointer_UTF8{ data },
                                 static_cast<std::size_t>(dataSize) };
        const juce::TemporaryFile temp{ ".mw101preset" };
        if (! temp.getFile().replaceWithText(json))
            continue;

        PresetMeta meta;
        const auto canonical = loadPresetJson(temp.getFile(), meta);
        if (! canonical.has_value())
            continue;

        const auto params =
            canonical->getChildWithName(juce::Identifier{ mw::state::kParamsId });
        const auto a4 = params.getChildWithProperty("id", "mw101.tune.a4");
        if (a4.isValid()
            && std::abs(static_cast<double>(a4.getProperty("value")) - 442.0) < 1.0e-6)
        {
            found442 = true;
            foundIn = juce::String{ juce::CharPointer_UTF8{
                BinaryData::getNamedResourceOriginalFilename(resourceName) } };
            break;
        }
    }

    // HARD assertion (task 131b): the §10.3 'hardware-accurate' 442 Hz reference preset
    // MUST exist in the embedded bank and decode cleanly. 442 is never the engine default
    // (440); it is surfaced in exactly this preset [ADR-012 C21-C22; docs/design/06 §10.3].
    INFO("hardware-accurate 442 Hz reference preset resolved to: " << foundIn);
    REQUIRE(found442);
    CHECK(foundIn.isNotEmpty());
}
