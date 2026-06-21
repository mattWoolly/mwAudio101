// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetManagerTest.cpp — JUCE-linked tests for the in-memory preset bank
// + per-slot INIT fallback (task 119). Asserts every acceptance criterion of
// plan/backlog/119 against docs/design/06 §10.1, §10.2, §10.3, §8.3 L9 and ADR-021.
//
//   1. The default constructor builds the embedded BinaryData bank gracefully:
//      construction never crashes and a far out-of-range query is a safe no-op. (Task 131
//      wired the embedded bank; the presets/ <-> BinaryData 1:1 mirror + the real
//      ~64-preset load are asserted in tests/plugin/FactoryPresetCorpusTest.cpp.) [§10.2].
//   2. The bank loads a set of in-memory presets (injected as JSON fixtures): each slot
//      decodes via the task-025 loadPresetJson and exposes name/category/index queries
//      [§10.1; §10.2].
//   3. §8.3 L9: a deliberately-undecodable slot resolves to INIT and warns NAMING it,
//      while the REST of the bank still loads — construction never aborts/empties the
//      bank [§8.3 L9; ADR-021 L9].
//   4. loadPreset runs the SAME migration (§7) + recovery (§8) chain as session state and
//      applies the slot's canonical tree to APVTS + Extras via the §5.3 path; an INIT
//      slot binds the §11 INIT poles [§10.2].
//   5. indicesForCategory groups the bank by §6.5 category correctly [§10.1].
//
// Test names begin with the `presetmgr` tag so `ctest -R presetmgr` selects exactly
// these and the silent-pass / --no-tests=error rule holds. No '[' appears in any
// display name. A juce::ScopedJuceInitialiser_GUI brackets every case (JUCE singletons
// + leak detector), matching tests/plugin/PluginHarnessTest.cpp.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetManager.h"       // mw::plugin::preset (the unit under test)
#include "preset/PresetFormat.h"        // writePresetJson (build valid fixtures)
#include "params/ParameterLayout.h"     // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"           // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"            // mw::state canonical keys
#include "state/Extras.h"               // mw::state::Extras
#include "state/InitPatch.h"            // mw::state::buildInitPatch (expected INIT poles)
#include "state/LoadFailure.h"          // RecoveryReport / RecoveryOutcome

namespace {

using mw::plugin::preset::PresetManager;
using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::writePresetJson;
using mw::plugin::state::RecoveryReport;
using mw::plugin::state::RecoveryOutcome;

// A minimal headless AudioProcessor hosting the FULL 91-param APVTS layout so loadPreset
// has a real tree to bind into, and writePresetJson has a real <PARAMS> subtree.
class PresetMgrHostProcessor final : public juce::AudioProcessor
{
public:
    PresetMgrHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "PresetMgrHost"; }
    void prepareToPlay(double, int) override            {}
    void releaseResources() override                    {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram(int) override                {}
    const juce::String getProgramName(int) override     { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

// Build a canonical MW101_STATE tree whose <PARAMS> mirrors the engine-default APVTS
// state, optionally overriding ONE id with a modeled value, so writePresetJson has a
// complete real tree to project into a valid .mw101preset JSON string.
juce::ValueTree makeCanonicalTree(const char* overrideId = nullptr, double overrideValue = 0.0)
{
    PresetMgrHostProcessor host;
    juce::ValueTree root{ juce::Identifier{ mw::state::kRootId } };
    root.setProperty(mw::state::kAttrSchemaVersion, 1, nullptr);

    juce::ValueTree params{ juce::Identifier{ mw::state::kParamsId } };
    params.copyPropertiesAndChildrenFrom(host.apvts.copyState(), nullptr);

    if (overrideId != nullptr)
    {
        auto node = params.getChildWithProperty("id", juce::String(overrideId));
        if (node.isValid())
            node.setProperty("value", overrideValue, nullptr);
    }
    root.appendChild(params, nullptr);
    return root;
}

// Author a VALID .mw101preset JSON string with the given meta name/category (every
// §6.4 rule satisfied; modern-default poles). `overrideId`/`overrideValue` let a test
// stamp a recognisable non-default value to prove it lands in APVTS after loadPreset.
juce::String validPresetJson(const juce::String& name, const juce::String& category,
                             const char* overrideId = nullptr, double overrideValue = 0.0)
{
    PresetMeta meta;
    meta.name     = name;
    meta.author   = "Matt Woolly";
    meta.category = category;
    meta.soundExt = false;  // no software-only feature in these fixtures
    return writePresetJson(makeCanonicalTree(overrideId, overrideValue), meta);
}

// The modeled value a loadPreset of an INIT slot must land on for a given id (the §11
// INIT patch poles, e.g. mw101.voice.mode == Mono(0), mw101.fx.bypass == true).
float initValueFor(const char* id)
{
    const auto patch = mw::state::buildInitPatch();
    const auto v = patch.valueFor(id);
    REQUIRE(v.has_value());
    return *v;
}

// Read the modeled value of an APVTS parameter (denormalised engineering units / index).
float apvtsModeledValue(const PresetMgrHostProcessor& host, const char* id)
{
    auto* p = host.apvts.getParameter(id);
    REQUIRE(p != nullptr);
    return p->getNormalisableRange().convertFrom0to1(p->getValue());
}

} // namespace

// --- (1) graceful construction of the embedded bank ----------------------------------

TEST_CASE("presetmgr default constructor builds the embedded bank gracefully",
          "[presetmgr]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Task 131 wired the default ctor to enumerate + decode the embedded BinaryData
    // factory bank (the presets/ <-> BinaryData mirror lives in FactoryPresetCorpusTest).
    // Construction must never abort or crash regardless of the embedded set; the loaded
    // count is >= 0 and a far out-of-range query is always a safe no-op [§10.2; §8.3 L9].
    REQUIRE_NOTHROW([] { PresetManager pm; }());

    PresetManager pm;
    CHECK(pm.getNumPresets() >= 0);
    // Out-of-range queries are safe no-ops, never a crash/abort (use an index far past
    // any plausible bank size so this holds whether or not the bank is populated).
    CHECK(pm.getName(100000).isEmpty());
    CHECK(pm.getCategory(100000).isEmpty());
    CHECK(pm.indicesForCategory("NotACategory").isEmpty());
}

// --- (2) the bank loads injected JSON fixtures + name/category/index queries ----------

TEST_CASE("presetmgr loads an injected in-memory bank and exposes name and category",
          "[presetmgr]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::vector<PresetManager::SlotSource> sources{
        { "Acid Squelch", validPresetJson("Acid Squelch", "AcidBassLead") },
        { "Sub Pulse",    validPresetJson("Sub Pulse",    "SubBass") },
        { "Bright Lead",  validPresetJson("Bright Lead",  "Lead") },
    };

    PresetManager pm{ sources };

    REQUIRE(pm.getNumPresets() == 3);
    CHECK(pm.getName(0) == "Acid Squelch");
    CHECK(pm.getName(1) == "Sub Pulse");
    CHECK(pm.getName(2) == "Bright Lead");
    // Category comes from the decoded JSON meta block (§6.2), not the slot name.
    CHECK(pm.getCategory(0) == "AcidBassLead");
    CHECK(pm.getCategory(1) == "SubBass");
    CHECK(pm.getCategory(2) == "Lead");
    // Every slot decoded cleanly — no per-slot fallback warning was raised.
    CHECK(pm.constructionReport().notes.isEmpty());
}

// --- (3) §8.3 L9: an undecodable slot resolves to INIT, warns naming it, bank survives -

TEST_CASE("presetmgr resolves an undecodable slot to INIT and warns naming it without emptying the bank",
          "[presetmgr]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::vector<PresetManager::SlotSource> sources{
        { "Good One",   validPresetJson("Good One",   "Lead") },
        { "Broken Two", "{ this is not valid preset json" },   // deliberately undecodable
        { "Good Three", validPresetJson("Good Three", "SubBass") },
    };

    PresetManager pm;
    REQUIRE_NOTHROW([&] { pm = PresetManager{ sources }; }());

    // L9: the bank still loads every slot — it is NOT aborted/emptied by one bad preset.
    REQUIRE(pm.getNumPresets() == 3);
    CHECK(pm.getName(0) == "Good One");
    CHECK(pm.getName(1) == "Broken Two");   // the slot keeps its declared NAME
    CHECK(pm.getName(2) == "Good Three");

    // The undecodable slot resolved to INIT; the good slots kept their decoded category.
    CHECK(pm.getCategory(0) == "Lead");
    CHECK(pm.getCategory(2) == "SubBass");

    // §8.3 L9: a warning is recorded that NAMES the offending slot, without storming.
    const auto& report = pm.constructionReport();
    CHECK_FALSE(report.notes.isEmpty());
    CHECK(report.notes.joinIntoString(" ").contains("Broken Two"));

    // Loading the INIT-resolved slot binds the §11 INIT poles into APVTS (a real INIT,
    // not garbage): mw101.voice.mode == Mono(0), mw101.fx.bypass == true(1).
    PresetMgrHostProcessor host;
    mw::state::Extras extras{};
    RecoveryReport loadReport;
    pm.loadPreset(1, host.apvts, extras, loadReport);

    CHECK(apvtsModeledValue(host, "mw101.voice.mode") == initValueFor("mw101.voice.mode"));
    CHECK(apvtsModeledValue(host, "mw101.fx.bypass")  == initValueFor("mw101.fx.bypass"));
}

// --- (4) loadPreset runs the shared migration + recovery chain and applies via §5.3 ---

TEST_CASE("presetmgr loadPreset runs the shared recovery chain and binds the preset to APVTS",
          "[presetmgr]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A valid preset that stamps a recognisable non-default cutoff so we can prove the
    // decoded value reaches APVTS through the §5.3 binding.
    constexpr double kCutoff = 0.42;
    std::vector<PresetManager::SlotSource> sources{
        { "Recognisable", validPresetJson("Recognisable", "AcidBassLead",
                                          "mw101.vcf.cutoff", kCutoff) },
    };

    PresetManager pm{ sources };
    REQUIRE(pm.getNumPresets() == 1);

    PresetMgrHostProcessor host;
    mw::state::Extras extras{};
    RecoveryReport report;
    REQUIRE_NOTHROW([&] { pm.loadPreset(0, host.apvts, extras, report); }());

    // The preset's cutoff reached APVTS (the shared §8 recovery + §5.3 bind path); a
    // clean valid preset produces a CleanLoad / MigratedAndBound outcome (never INIT).
    CHECK(apvtsModeledValue(host, "mw101.vcf.cutoff")
              == Catch::Approx(kCutoff).margin(1.0e-4));
    CHECK(report.outcome != RecoveryOutcome::InitFallback);

    // An out-of-range index is a safe no-op (never a crash/abort) — APVTS is untouched.
    const float beforeCutoff = apvtsModeledValue(host, "mw101.vcf.cutoff");
    REQUIRE_NOTHROW([&] { pm.loadPreset(99, host.apvts, extras, report); }());
    CHECK(apvtsModeledValue(host, "mw101.vcf.cutoff") == beforeCutoff);
}

// --- (5) indicesForCategory groups the bank by §6.5 category --------------------------

TEST_CASE("presetmgr indicesForCategory groups the bank correctly",
          "[presetmgr]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::vector<PresetManager::SlotSource> sources{
        { "Lead A",  validPresetJson("Lead A",  "Lead") },
        { "Bass A",  validPresetJson("Bass A",  "SubBass") },
        { "Lead B",  validPresetJson("Lead B",  "Lead") },
        { "Acid A",  validPresetJson("Acid A",  "AcidBassLead") },
        { "Lead C",  validPresetJson("Lead C",  "Lead") },
    };

    PresetManager pm{ sources };
    REQUIRE(pm.getNumPresets() == 5);

    const auto leads = pm.indicesForCategory("Lead");
    REQUIRE(leads.size() == 3);
    CHECK(leads.contains(0));
    CHECK(leads.contains(2));
    CHECK(leads.contains(4));

    const auto bass = pm.indicesForCategory("SubBass");
    REQUIRE(bass.size() == 1);
    CHECK(bass.contains(1));

    const auto acid = pm.indicesForCategory("AcidBassLead");
    REQUIRE(acid.size() == 1);
    CHECK(acid.contains(3));

    // A category with no members returns an empty array (never a crash).
    CHECK(pm.indicesForCategory("BlipsFX").isEmpty());
}
