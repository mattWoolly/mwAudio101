// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetSeqArpRiffTest.cpp — JUCE-linked enumeration test for the
// SeqArpRiff factory preset category (task 150). It walks the on-disk
// presets/SeqArpRiff/ folder and runs EVERY .mw101preset file through the real §6.4
// loader/validator (mw::plugin::preset::loadPresetJson), asserting each acceptance
// criterion of plan/backlog/150:
//
//   - the selector matches (>=1 file present), so `ctest -R presets_seqarpriff
//     --no-tests=error` cannot silently pass [AGENTS.md silent-pass rule];
//   - each file loads (loadPresetJson returns a value) — which alone proves every live
//     registry ID is present + in range, every choice index is valid, the category is
//     in the §6.5 enum, sound_ext == used-software-ext, NO per-step accent is present
//     (ADR-025: the loader rejects any accent field), and no forbidden attribution
//     phrasing is present (those are exactly the §6.4 rules loadPresetJson enforces)
//     [docs/design/06 §6.4; ADR-008 C13-C18; ADR-025];
//   - meta.category == "SeqArpRiff" [ADR-008 C14; research/11 §7.1(6)];
//   - the recovered <PARAMS> subtree carries all 91 IDs (registry-complete);
//   - sound_ext is true IFF the preset uses a software-only feature (vco.range >= 4 ==
//     32'/64', or lfo.shape == 4 == Sine) — re-checked here independently of the loader
//     [ADR-008 C15; research/11 §6.1, §6.2];
//   - no preset meta carries an "as used on track" or "TB-303 filter" descriptor
//     [ADR-008 C16; research/11 §4.2, §7.3];
//   - the SeqArpRiff identity holds: each preset carries a stored <seq> pattern AND/OR
//     arp settings (008 §C13); every stored sequence fits the fixed 100-step capacity
//     (008 §C8/§C20) and round-trips exactly through writePresetJson -> loadPresetJson
//     (note/gate/tie/rest only, never accent);
//   - the bank exercises the canonical arp modes Up/Down/Up-Down (research/11 §4.7) and
//     ships at least one stored-sequence preset and at least one arp preset (non-clone
//     coverage), with at least one inspired-by attribution present (Voodoo-Ray-style
//     homage framed inspired-by only — never "as used on track X") [§4.8].
//
// WHY plugin/ (not core/) and MW_BUILD_PLUGIN: the loader projects/recovers the
// canonical juce::ValueTree (task 023/025), so it fundamentally requires JUCE and cannot
// build under the JUCE-free core/`default` preset. The task file's `component: docs/core`
// / `cmake --preset default` verification block is STALE for that reason; this test lives
// in tests/plugin (compiled into mw101_plugin_tests) and is verified with
// MW_BUILD_PLUGIN=ON, mirroring what QA accepted for tasks 023/025/146. Behavior asserted
// is exactly task 150's Scope/Acceptance — only the build location/commands are corrected.
//
// Test-case display names begin with the `presets_seqarpriff` tag and avoid the '['
// character so `ctest -R presets_seqarpriff` selects exactly these.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"   // mw::plugin::preset (the unit under test)
#include "params/ParamDefs.h"      // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"       // mw::state canonical keys
#include "state/Extras.h"          // mw::state::kMaxSeqSteps

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;
using mw::plugin::preset::writePresetJson;

// Locate the repository's presets/SeqArpRiff directory by walking up from this test
// source file's compile-time path (__FILE__ is .../tests/plugin/PresetSeqArpRiffTest.cpp).
// Robust to the build directory's cwd and identical local vs CI (CI mirrors the source
// tree 1:1) [ADR-008 C18]. Falls back to walking up from the cwd.
juce::File findCategoryDir()
{
    const juce::File thisSource{ juce::String::fromUTF8(__FILE__) };
    if (thisSource.existsAsFile())
    {
        const auto repoRoot = thisSource.getParentDirectory()    // tests/plugin
                                        .getParentDirectory()    // tests
                                        .getParentDirectory();   // repo root
        const auto dir = repoRoot.getChildFile("presets").getChildFile("SeqArpRiff");
        if (dir.isDirectory())
            return dir;
    }

    for (auto dir = juce::File::getCurrentWorkingDirectory();
         dir.exists() && dir != dir.getParentDirectory();
         dir = dir.getParentDirectory())
    {
        const auto candidate = dir.getChildFile("presets").getChildFile("SeqArpRiff");
        if (candidate.isDirectory())
            return candidate;
    }
    return {};
}

juce::Array<juce::File> categoryPresetFiles()
{
    juce::Array<juce::File> files;
    const auto dir = findCategoryDir();
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

// Recover the <extras>/<seq> subtree: stepCount + the per-step note/gate/tie/rest values.
struct RecoveredStep { int note = 0; bool gate = true, tie = false, rest = false; };
struct RecoveredSeq  { int stepCount = 0; std::vector<RecoveredStep> steps; };

RecoveredSeq recoveredSeq(const juce::ValueTree& canonical)
{
    RecoveredSeq out;
    const auto extras = canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    if (! extras.isValid())
        return out;
    const auto seq = extras.getChildWithName(juce::Identifier{ mw::state::kSeqId });
    if (! seq.isValid())
        return out;
    out.stepCount = static_cast<int>(seq.getProperty(juce::Identifier{ "stepCount" }, 0));
    for (int i = 0; i < seq.getNumChildren(); ++i)
    {
        const auto s = seq.getChild(i);
        RecoveredStep r;
        r.note = static_cast<int>(s.getProperty("note", 0));
        r.gate = static_cast<bool>(s.getProperty("gate", true));
        r.tie  = static_cast<bool>(s.getProperty("tie", false));
        r.rest = static_cast<bool>(s.getProperty("rest", false));
        out.steps.push_back(r);
    }
    return out;
}

bool recoveredArpLatch(const juce::ValueTree& canonical)
{
    const auto extras = canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    return extras.isValid()
        && static_cast<bool>(extras.getProperty(juce::Identifier{ mw::state::kExtrasArpLatch }, false));
}

} // namespace

TEST_CASE("presets_seqarpriff folder is present and non-empty", "[presets_seqarpriff]")
{
    const auto dir = findCategoryDir();
    INFO("expected presets/SeqArpRiff next to the repo root; resolved to: "
         << dir.getFullPathName());
    REQUIRE(dir.isDirectory());

    // The category ships ~12 presets; require a healthy, non-trivial set so this selector
    // can never silently pass with an empty/half-authored folder.
    const auto files = categoryPresetFiles();
    REQUIRE(files.size() >= 10);
}

TEST_CASE("presets_seqarpriff every preset loads through the section-6.4 validator",
          "[presets_seqarpriff]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = categoryPresetFiles();
    REQUIRE_FALSE(files.isEmpty());

    for (const auto& file : files)
    {
        INFO("SeqArpRiff preset: " << file.getFileName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);

        // A nullopt means the file failed a §6.4 rule: missing/out-of-range registry ID,
        // invalid choice index, category outside the §6.5 enum, sound_ext mismatch, a
        // per-step accent (ADR-025), or forbidden attribution phrasing. Loading == all pass.
        REQUIRE(canonical.has_value());

        // Category MUST be SeqArpRiff [ADR-008 C14; research/11 §7.1(6)].
        CHECK(meta.category == juce::String{ "SeqArpRiff" });

        // Registry-complete: one <PARAM> per live ID, none missing/extra.
        const auto values = recoveredParamValues(*canonical);
        REQUIRE(values.size() == mw::params::kParamDefs.size());
        for (const auto& def : mw::params::kParamDefs)
            CHECK(values.find(def.id) != values.end());

        // Every recovered value sits in its registry range / valid choice-index set —
        // re-derived here, independent of the loader's own check [ADR-008 C18; §6.4].
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

        // SeqArpRiff identity: the preset MUST carry a stored <seq> pattern AND/OR arp
        // settings beyond the params (008 §C13). We treat "carries a riff/arp identity"
        // as: a non-empty stored sequence, OR an active arp mode, OR arp latch engaged.
        const auto seq = recoveredSeq(*canonical);
        const int arpMode = choiceIndex(values, "mw101.arp.mode");
        const bool hasSeq = seq.stepCount > 0;
        const bool hasArp = arpMode > 0 || recoveredArpLatch(*canonical);
        CHECK((hasSeq || hasArp));

        // Stored sequences fit the fixed 100-step capacity (008 §C8/§C20).
        CHECK(seq.stepCount <= mw::state::kMaxSeqSteps);
        CHECK(static_cast<int>(seq.steps.size()) <= mw::state::kMaxSeqSteps);
        CHECK(static_cast<int>(seq.steps.size()) == seq.stepCount);

        // sound_ext is true IFF a software-only feature is engaged (vco.range >= 4 == the
        // 32'/64' registers, or lfo.shape == 4 == the Sine shape) — re-derived here from
        // the recovered params [ADR-008 C15; research/11 §6.1, §6.2].
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

TEST_CASE("presets_seqarpriff seq/arp sections round-trip through the loader",
          "[presets_seqarpriff]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = categoryPresetFiles();
    REQUIRE_FALSE(files.isEmpty());

    for (const auto& file : files)
    {
        INFO("SeqArpRiff round-trip: " << file.getFileName());

        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        const auto seq0 = recoveredSeq(*canonical);
        const bool latch0 = recoveredArpLatch(*canonical);

        // Project the recovered canonical tree back to JSON and re-load it. The seq/arp
        // sections (stepCount + per-step note/gate/tie/rest, plus arp latch) MUST survive
        // the round-trip exactly — proving the stored riff is faithfully persisted
        // (008 §C8/§C20; ADR-025: note/gate/tie/rest only).
        const juce::String json = writePresetJson(*canonical, meta);

        auto tmp = juce::File::createTempFile(".mw101preset");
        REQUIRE(tmp.replaceWithText(json));

        PresetMeta meta2;
        const auto canonical2 = loadPresetJson(tmp, meta2);
        tmp.deleteFile();
        REQUIRE(canonical2.has_value());

        const auto seq1 = recoveredSeq(*canonical2);
        const bool latch1 = recoveredArpLatch(*canonical2);

        CHECK(seq1.stepCount == seq0.stepCount);
        REQUIRE(seq1.steps.size() == seq0.steps.size());
        for (size_t i = 0; i < seq0.steps.size(); ++i)
        {
            CHECK(seq1.steps[i].note == seq0.steps[i].note);
            CHECK(seq1.steps[i].gate == seq0.steps[i].gate);
            CHECK(seq1.steps[i].tie  == seq0.steps[i].tie);
            CHECK(seq1.steps[i].rest == seq0.steps[i].rest);
        }
        CHECK(latch1 == latch0);
        CHECK(meta2.category == meta.category);
    }
}

TEST_CASE("presets_seqarpriff bank covers stored-seq + arp modes + inspired-by framing",
          "[presets_seqarpriff]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto files = categoryPresetFiles();
    REQUIRE_FALSE(files.isEmpty());

    bool sawStoredSeq = false;
    bool sawArpPreset = false;
    bool sawInspiredBy = false;
    bool sawArpMode[4] = { false, false, false, false }; // Off / Up / Down / Up-Down
    int distinctNonEmptySeqHashes = 0;
    std::map<std::string, int> seqSignatures;

    for (const auto& file : files)
    {
        PresetMeta meta;
        const auto canonical = loadPresetJson(file, meta);
        REQUIRE(canonical.has_value());

        const auto values = recoveredParamValues(*canonical);
        const auto seq = recoveredSeq(*canonical);
        const int arpMode = choiceIndex(values, "mw101.arp.mode");

        if (seq.stepCount > 0)
        {
            sawStoredSeq = true;
            // Build a cheap signature of the stored pattern so we can prove the riffs are
            // genuinely distinct (not clones): notes + flags + count.
            std::string sig = std::to_string(seq.stepCount) + ":";
            for (const auto& s : seq.steps)
                sig += std::to_string(s.note) + (s.gate ? "g" : "-")
                     + (s.tie ? "t" : "-") + (s.rest ? "r" : "-") + ",";
            ++seqSignatures[sig];
        }
        if (arpMode > 0 || recoveredArpLatch(*canonical))
            sawArpPreset = true;
        if (arpMode >= 0 && arpMode < 4)
            sawArpMode[arpMode] = true;
        if (meta.inspiredBy.isNotEmpty())
            sawInspiredBy = true;
    }

    for (const auto& [sig, count] : seqSignatures)
    {
        juce::ignoreUnused(sig);
        if (count == 1)
            ++distinctNonEmptySeqHashes;
    }

    // The category is defined by the riff: at least one stored-sequence preset and at
    // least one arp preset (008 §C13; research/11 §7.1(6)).
    CHECK(sawStoredSeq);
    CHECK(sawArpPreset);

    // The arp idiom is up/down/up-down (research/11 §4.7) — exercise all three.
    CHECK(sawArpMode[1]); // Up
    CHECK(sawArpMode[2]); // Down
    CHECK(sawArpMode[3]); // Up-Down

    // At least one inspired-by attribution (the Voodoo-Ray-style homage, framed
    // inspired-by only) [§4.8; ADR-008 C16].
    CHECK(sawInspiredBy);

    // Stored riffs are genuinely distinct (no two share the same step signature):
    // every distinct stored pattern appears exactly once.
    CHECK(distinctNonEmptySeqHashes == static_cast<int>(seqSignatures.size()));
    CHECK(seqSignatures.size() >= 6);
}
