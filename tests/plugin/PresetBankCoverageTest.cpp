// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetBankCoverageTest.cpp — the bank-level coverage manifest + full-bank
// validation (task 151). Where the per-category sibling tests (145-150) each walk ONE
// category folder, this test enumerates the WHOLE presets/ tree RECURSIVELY and asserts
// the entire factory bank is complete, traceable, honesty-compliant, and survives the
// SAME migration/recovery chain as host session state.
//
// Acceptance criteria asserted here (plan/backlog/151 Scope/Acceptance; the task file's
// `component: qa` / `cmake --preset default` block is STALE — this test runs the real
// §6.4 loader + the §8 recoverState ladder, both of which project/recover the canonical
// juce::ValueTree (tasks 023/024/025) and therefore REQUIRE JUCE, so it lives in
// tests/plugin and is verified with MW_BUILD_PLUGIN=ON, mirroring 145-150 / what QA
// accepted for 023):
//
//   (1) Bank totals ~64 presets across the 6 §6.5 categories, EACH non-empty
//       [research/11 §7.1; ADR-008 C14, C18].
//   (2) Every category-folder preset's meta.category == its FOLDER name AND is a valid
//       §6.5 enum value; CI mirrors presets/ 1:1 [ADR-008 C14, C18].
//   (3) NO preset metadata anywhere contains a "TB-303 filter" descriptor or an
//       "as used on track X" phrasing [ADR-008 C16; research/11 §6, §7.3].
//   (4) Every preset loads through the §6.4 loadPresetJson validator AND round-trips
//       through the SAME §8 recoverState migration/recovery chain as session state:
//       registry-complete, every value in range, every choice index valid, NO per-step
//       accent (ADR-025), and recovery reports a clean / migrated outcome (NOT a
//       clamp/INIT fallback — a faithful preset must not deviate) [ADR-008 C17; ADR-021].
//   (5) Traceability: every preset's meta carries a non-empty description AND >= 1 tag;
//       sound_ext is true IFF a software-only feature is used (vco.range >= 4 == 32'/64',
//       or lfo.shape == 4 == Sine), checked BOTH directions [ADR-008 C15; research/11
//       §6.1, §6.2, §7.1].
//   (6) A coverage MANIFEST (per-category counts + sound_ext distribution) the test
//       asserts against, so a dropped/added/mis-filed preset breaks the build.
//   (7) NON-VACUITY negative control: a crafted-bad preset (out-of-range value;
//       wrong-folder category; forbidden phrasing) is REJECTED by the SAME loader — so
//       the all-pass per-file loop cannot be silently vacuous [folded-in 150 QA note].
//
// INIT.mw101preset is the factory BASELINE and lives at the presets/ ROOT (no category
// folder). It is enumerated, loads, and migrates like any preset, but is EXCLUDED from
// the category-matches-FOLDER rule (it has no category folder; its meta.category is the
// "Lead" baseline category it was authored from) — exactly how it ships on disk.
//
// Test-case display names begin with the `presets_bank` tag and avoid the '[' character
// so `ctest -R presets_bank` selects exactly these and the silent-pass /
// --no-tests=error rule holds.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"        // mw::plugin::preset (the §6.4 loader/validator)
#include "state/StateSerializer.h"      // writeToBlob (blob the recovery ladder consumes)
#include "state/LoadFailure.h"          // mw::plugin::state::recoverState (the §8 ladder)
#include "params/ParamDefs.h"           // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"            // mw::state canonical keys
#include "state/Extras.h"               // mw::state::kMaxSeqSteps

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;
using mw::plugin::state::recoverState;
using mw::plugin::state::RecoveryReport;
using mw::plugin::state::RecoveryOutcome;

// The six §6.5 / research-11 §7.1 categories, in folder order. These strings are BOTH
// the on-disk folder names AND the meta.category enum values (ADR-008 C14/C18 — CI
// mirrors presets/ 1:1, so folder == category by construction).
constexpr std::array<const char*, 6> kCategories = {
    "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
};

// Software-only feature thresholds (re-derived independently of the loader so a flipped
// loader rule cannot hide here): vco.range indices >= 4 are the 32'/64' software
// registers; lfo.shape index 4 is the software Sine shape [ADR-008 C15; research/11 §6].
constexpr int kVcoRangeSwFirst = 4;
constexpr int kLfoShapeSine     = 4;

// Locate the repository's presets/ root by walking up from this test source file's
// compile-time path (__FILE__ == .../tests/plugin/PresetBankCoverageTest.cpp). Robust to
// the build dir's cwd and identical local vs CI (CI mirrors the tree 1:1) [ADR-008 C18].
// Falls back to walking up from the cwd.
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

// Every .mw101preset anywhere under presets/ (RECURSIVE), so the scan sees the whole bank
// — every category folder AND the top-level INIT baseline.
juce::Array<juce::File> allPresetFiles()
{
    juce::Array<juce::File> files;
    const auto root = findPresetsRoot();
    if (root.isDirectory())
        files = root.findChildFiles(juce::File::findFiles, /*recursive=*/true,
                                    "*.mw101preset");
    return files;
}

// Files inside one category folder only (non-recursive), for the per-category manifest.
juce::Array<juce::File> categoryFiles(const char* category)
{
    juce::Array<juce::File> files;
    const auto dir = findPresetsRoot().getChildFile(category);
    if (dir.isDirectory())
        files = dir.findChildFiles(juce::File::findFiles, /*recursive=*/false,
                                   "*.mw101preset");
    return files;
}

// True iff this file is the top-level INIT baseline (presets/INIT.mw101preset), which has
// no category FOLDER and is exempt from the folder-match rule.
bool isInitBaseline(const juce::File& f)
{
    return f.getFileNameWithoutExtension() == "INIT"
        && f.getParentDirectory().getFileName() == "presets";
}

// The immediate parent folder name (the on-disk category folder for a category preset).
juce::String folderOf(const juce::File& f)
{
    return f.getParentDirectory().getFileName();
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

bool usesSoftwareExt(const std::map<std::string, double>& values)
{
    return choiceIndex(values, "mw101.vco.range") >= kVcoRangeSwFirst
        || choiceIndex(values, "mw101.lfo.shape") == kLfoShapeSine;
}

bool containsForbiddenPhrase(const juce::String& s)
{
    const auto lower = s.toLowerCase();
    return lower.contains("as used on track") || lower.contains("tb-303 filter");
}

// The expected coverage manifest. A dropped/added/mis-filed preset, or a shift in the
// sound_ext distribution, breaks an assertion here — that is the point of the manifest.
struct CategoryManifest { const char* name; int total; int soundExt; };

// Counts taken from the authored bank on disk. The per-category test owns the deep
// musical assertions; this manifest owns the bank-level shape: every category populated,
// the total ~64, and a real (non-zero) software-extension footprint spread across the
// bank without over-claiming.
constexpr std::array<CategoryManifest, 6> kManifest = {{
    { "AcidBassLead", 14, 3 },
    { "SubBass",      12, 3 },   // +1: task 131b 'VR-7 Reference Sub' (a4=442, sound_ext=false)
    { "Lead",         10, 2 },
    { "PWMStrings",    8, 0 },
    { "BlipsFX",       8, 2 },
    { "SeqArpRiff",   12, 2 },
}};

} // namespace

TEST_CASE("presets_bank the bank totals about 64 presets with all six categories populated",
          "[presets_bank]")
{
    const auto root = findPresetsRoot();
    INFO("expected presets/ at the repo root; resolved to: " << root.getFullPathName());
    REQUIRE(root.isDirectory());

    // Every category folder exists and is non-empty [research/11 §7.1].
    for (const auto* cat : kCategories)
    {
        const auto dir = root.getChildFile(cat);
        INFO("category folder: " << cat);
        REQUIRE(dir.isDirectory());
        REQUIRE_FALSE(categoryFiles(cat).isEmpty());
    }

    // The full recursive bank is ~64 presets (category folders + the INIT baseline). A
    // tight band so the selector can never silently pass with a half-authored tree.
    const auto all = allPresetFiles();
    INFO("recursive .mw101preset count: " << all.size());
    CHECK(all.size() >= 60);
    CHECK(all.size() <= 70);

    // Exactly one top-level INIT baseline is present.
    int initCount = 0;
    for (const auto& f : all)
        if (isInitBaseline(f))
            ++initCount;
    CHECK(initCount == 1);
}

TEST_CASE("presets_bank the coverage manifest matches the authored bank exactly",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int manifestTotal = 0;
    int manifestSoundExt = 0;

    for (const auto& entry : kManifest)
    {
        const auto files = categoryFiles(entry.name);
        INFO("category: " << entry.name << " (on-disk count " << files.size() << ")");

        // Per-category file count matches the manifest exactly (1:1 with disk).
        CHECK(files.size() == entry.total);

        // Count sound_ext presets in this category by loading each through the validator
        // (which proves sound_ext == used-software-ext) and inspecting the recovered meta.
        int catSoundExt = 0;
        for (const auto& file : files)
        {
            PresetMeta meta;
            const auto canonical = loadPresetJson(file, meta);
            INFO("preset: " << file.getFileName());
            REQUIRE(canonical.has_value());
            if (meta.soundExt)
                ++catSoundExt;
        }
        CHECK(catSoundExt == entry.soundExt);

        manifestTotal    += entry.total;
        manifestSoundExt += entry.soundExt;
    }

    // The manifest's category folders sum to the authored bank total (excludes INIT, which
    // lives at the root), and the bank carries a real, non-trivial software-extension
    // footprint without every preset claiming it (honesty: ADR-008 C15).
    CHECK(manifestTotal == 64);
    CHECK(manifestSoundExt == 12);
    CHECK(manifestSoundExt > 0);
    CHECK(manifestSoundExt < manifestTotal);
}

TEST_CASE("presets_bank every category preset's meta category matches its folder and is a valid enum",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto all = allPresetFiles();
    REQUIRE_FALSE(all.isEmpty());

    const std::set<std::string> validEnum = [] {
        std::set<std::string> s;
        for (const auto* c : kCategories) s.insert(c);
        return s;
    }();

    for (const auto& file : all)
    {
        INFO("preset: " << file.getFullPathName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        // meta.category is always one of the six §6.5 enum values (the loader enforces
        // this; re-checked here independent of the loader) [ADR-008 C14].
        CHECK(validEnum.count(meta.category.toStdString()) == 1);

        if (isInitBaseline(file))
        {
            // INIT is the baseline at the presets/ root — no category folder. It still
            // carries a valid enum category (the "Lead" baseline) but is exempt from the
            // folder-match rule.
            CHECK(meta.category == juce::String{ "Lead" });
            continue;
        }

        // Every OTHER preset's category MUST equal its containing folder name (CI mirrors
        // presets/ 1:1) [ADR-008 C14, C18].
        CHECK(meta.category == folderOf(file));
    }
}

TEST_CASE("presets_bank whole-bank scan finds zero forbidden attribution phrasing",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto all = allPresetFiles();
    REQUIRE_FALSE(all.isEmpty());

    for (const auto& file : all)
    {
        INFO("preset: " << file.getFullPathName());

        // Scan BOTH the raw on-disk text (catches forbidden phrases anywhere — including
        // fields the loader does not surface) AND the loader-parsed human-facing meta
        // (name/description/inspiredBy/tags) [ADR-008 C16; research/11 §6, §7.3].
        const auto raw = file.loadFileAsString();
        CHECK_FALSE(containsForbiddenPhrase(raw));

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        for (const auto* text : { &meta.name, &meta.description, &meta.inspiredBy })
            CHECK_FALSE(containsForbiddenPhrase(*text));
        for (const auto& tag : meta.tags)
            CHECK_FALSE(containsForbiddenPhrase(tag));
    }
}

TEST_CASE("presets_bank every preset loads is registry-complete in-range and traceable",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto all = allPresetFiles();
    REQUIRE_FALSE(all.isEmpty());

    for (const auto& file : all)
    {
        INFO("preset: " << file.getFullPathName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // A nullopt means a §6.4 rule failed: missing/out-of-range registry ID, invalid
        // choice index, category outside the §6.5 enum, sound_ext mismatch, a per-step
        // accent (ADR-025), or forbidden attribution. Loading == every rule passes.
        REQUIRE(canonical.has_value());

        // Registry-complete: one <PARAM> per live ID, none missing/extra.
        const auto values = recoveredParamValues(*canonical);
        REQUIRE(values.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            CHECK(values.find(def.id) != values.end());

        // Every recovered value sits in its registry range / valid choice-index set,
        // re-derived here independent of the loader's own check [ADR-008 C18; §6.4].
        for (const auto& def : mw::params::kParamDefs)
        {
            const auto it = values.find(def.id);
            REQUIRE(it != values.end());
            const double v = it->second;
            if (def.type == mw::params::ParamType::Continuous)
            {
                CHECK(v >= static_cast<double>(def.minValue));
                CHECK(v <= static_cast<double>(def.maxValue));
            }
            else
            {
                const int idx = static_cast<int>(std::lround(v));
                CHECK(idx >= 0);
                CHECK(idx < static_cast<int>(def.choiceCount));
            }
        }

        // Traceability: every preset carries a non-empty description AND at least one tag
        // (the §7.1 idiom citation lives in this human-facing text) [ADR-008 C16/C17].
        CHECK(meta.description.isNotEmpty());
        CHECK(meta.tags.size() >= 1);

        // sound_ext is true IFF a software-only feature is engaged — checked BOTH
        // directions, re-derived from the recovered params [ADR-008 C15; research/11 §6].
        CHECK(meta.soundExt == usesSoftwareExt(values));
    }
}

TEST_CASE("presets_bank every preset survives the same migration and recovery chain as session state",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto all = allPresetFiles();
    REQUIRE_FALSE(all.isEmpty());

    for (const auto& file : all)
    {
        INFO("preset: " << file.getFullPathName());

        // Load via the §6.4 validator, then serialize the recovered canonical tree to the
        // SAME opaque host blob a session save produces, and run it through the SAME §8
        // recoverState migration/recovery ladder host state goes through [ADR-008 C17;
        // ADR-021]. A faithful factory preset must come back CLEAN — never clamped, never
        // reset to INIT, no missing/out-of-range value.
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        juce::MemoryBlock blob;
        mw::plugin::state::writeToBlob(*canonical, blob);
        REQUIRE(blob.getSize() > 0);

        RecoveryReport report;
        juce::ValueTree recovered;
        REQUIRE_NOTHROW(recovered = recoverState(blob.getData(),
                                                 static_cast<int>(blob.getSize()), report));

        // A complete valid canonical tree comes back: right root, all live params, both
        // subtrees present, stamped at CURRENT schema.
        REQUIRE(recovered.hasType(juce::Identifier{ mw::state::kRootId }));
        const auto params = recovered.getChildWithName(juce::Identifier{ mw::state::kParamsId });
        REQUIRE(params.isValid());
        CHECK(params.getNumChildren() == static_cast<int>(mw::params::kParamDefs.size()));
        CHECK(recovered.getChildWithName(juce::Identifier{ mw::state::kExtrasId }).isValid());

        // The factory bank is authored AT the current schema with in-range values, so the
        // recovery ladder must NOT have to clamp anything or fall to INIT — a clean or
        // migrated outcome only [ADR-021 L4/L5: clamp/INIT signal a corrupt/foreign load].
        CHECK(report.outcome != RecoveryOutcome::ClampedValues);
        CHECK(report.outcome != RecoveryOutcome::InitFallback);
        CHECK(report.outcome != RecoveryOutcome::NewerDownInterpreted);

        // The stored sequence (if any) fits the fixed 100-step capacity after recovery.
        const auto seq = recovered.getChildWithName(juce::Identifier{ mw::state::kExtrasId })
                                  .getChildWithName(juce::Identifier{ mw::state::kSeqId });
        if (seq.isValid())
        {
            CHECK(static_cast<int>(seq.getProperty(juce::Identifier{ "stepCount" }, 0))
                  <= mw::state::kMaxSeqSteps);
            CHECK(seq.getNumChildren() <= mw::state::kMaxSeqSteps);
        }
    }
}

TEST_CASE("presets_bank negative control proves the validator rejects bad presets so the suite is non-vacuous",
          "[presets_bank]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Take a REAL, valid factory preset, prove it loads, then craft three corrupted
    // variants and prove the SAME loader REJECTS each. Without this control the all-pass
    // per-file loops above could pass vacuously if the validator were ever neutered.
    const auto all = allPresetFiles();
    REQUIRE_FALSE(all.isEmpty());

    // Pick a known, simple base: the first AcidBassLead preset (it loads cleanly).
    const auto baseFiles = categoryFiles("AcidBassLead");
    REQUIRE_FALSE(baseFiles.isEmpty());
    const auto baseFile = baseFiles.getFirst();

    PresetMeta baseMeta;
    const auto baseOk = loadPresetJson(baseFile, baseMeta);
    REQUIRE(baseOk.has_value());   // the positive control: a real preset DOES load

    const juce::var baseRoot = juce::JSON::parse(baseFile.loadFileAsString());
    REQUIRE(baseRoot.isObject());

    // Helper: serialize a mutated JSON var to a temp .mw101preset, load it, return whether
    // the loader accepted it, and clean up.
    auto loadsMutated = [](const juce::var& root) {
        auto tmp = juce::File::createTempFile(".mw101preset");
        const bool wrote = tmp.replaceWithText(juce::JSON::toString(root));
        PresetMeta m;
        const auto res = wrote ? loadPresetJson(tmp, m) : std::optional<juce::ValueTree>{};
        tmp.deleteFile();
        return res.has_value();
    };

    SECTION("an out-of-range continuous value is rejected")
    {
        juce::var root = baseRoot.clone();
        // mw101.vcf.cutoff range is [0,1]; 9.0 is a §6.4 out-of-range failure.
        root.getProperty("params", juce::var()).getDynamicObject()
            ->setProperty("mw101.vcf.cutoff", 9.0);
        CHECK_FALSE(loadsMutated(root));
    }

    SECTION("a category that does not match the section-6.5 enum is rejected")
    {
        juce::var root = baseRoot.clone();
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("category", "NotARealCategory");
        CHECK_FALSE(loadsMutated(root));
    }

    SECTION("forbidden as-used-on-track attribution is rejected")
    {
        juce::var root = baseRoot.clone();
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("description", "The exact patch as used on track Voodoo Ray.");
        CHECK_FALSE(loadsMutated(root));
    }

    SECTION("a TB-303 filter descriptor is rejected")
    {
        juce::var root = baseRoot.clone();
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("description", "A classic TB-303 filter squelch.");
        CHECK_FALSE(loadsMutated(root));
    }

    SECTION("a per-step accent field is rejected per ADR-025")
    {
        juce::var root = baseRoot.clone();
        auto* seqObj = new juce::DynamicObject();
        seqObj->setProperty("stepCount", 1);
        juce::Array<juce::var> steps;
        auto* step = new juce::DynamicObject();
        step->setProperty("note", 0);
        step->setProperty("gate", true);
        step->setProperty("tie", false);
        step->setProperty("rest", false);
        step->setProperty("accent", true);   // FORBIDDEN [ADR-025]
        steps.add(juce::var{ step });
        seqObj->setProperty("steps", steps);
        root.getDynamicObject()->setProperty("seq", juce::var{ seqObj });
        CHECK_FALSE(loadsMutated(root));
    }
}
