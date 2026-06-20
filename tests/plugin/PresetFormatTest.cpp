// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetFormatTest.cpp — JUCE-linked tests for the .mw101preset JSON
// projection + validator (task 025). Asserts every acceptance criterion of
// plan/backlog/025:
//
//   1. A VALID preset round-trips: writePresetJson(canonical, meta) -> loadPresetJson
//      reproduces every param value into the canonical <PARAMS> tree, and the parsed
//      PresetMeta matches [docs/design/06 §6.2, §6.3].
//   2. loadPresetJson returns nullopt on malformed JSON and on each §6.4 validation
//      failure [docs/design/06 §6.3, §6.4; ADR-021 L11]:
//        - missing schemaVersion / meta.name / meta.author / meta.category
//        - category not in the §6.5 enum
//        - a registry ID missing from params
//        - a continuous value out of its NormalisableRange
//        - a choice index >= choiceCount
//        - sound_ext mismatch (false when a software-ext index is used; true when none)
//        - a per-step `accent` field present in seq.steps
//        - attribution discipline: "as used on track X" phrasing; "TB-303 filter"
//          descriptor; inspired_by carrying an "as used on track" string
//   3. writePresetJson emits §6.2-shaped JSON: schemaVersion, meta, params, seq, arp;
//      per-step objects carry note/gate/tie/rest ONLY (never accent) [docs/design/06
//      §6.2; ADR-025].
//   4. sound_ext is forced true by a software-ext index (vco.range >= 4 or
//      lfo.shape == 4) and the projection round-trips it [docs/design/06 §6.4].
//
// Test names begin with the `presetfmt` tag so `ctest -R presetfmt` selects exactly
// these and the silent-pass / --no-tests=error rule holds. The display text avoids
// the '[' character.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "preset/PresetFormat.h"        // mw::plugin::preset (the unit under test)
#include "params/ParameterLayout.h"     // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"           // mw::params::kParamDefs (JUCE-free registry)
#include "state/StateTree.h"            // mw::state canonical keys

namespace {

using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::loadPresetJson;
using mw::plugin::preset::writePresetJson;

// A minimal headless AudioProcessor hosting the FULL APVTS layout so we can grab a
// real canonical <PARAMS> subtree (every one of the 91 live IDs) to project.
class PresetHostProcessor final : public juce::AudioProcessor
{
public:
    PresetHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "PresetHost"; }
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

// Build a canonical MW101_STATE tree with a <PARAMS> subtree mirroring the APVTS state
// (one <PARAM id= value=> child per live ID at its engine default), so writePresetJson
// has a real, complete tree to project. Optionally override a single id's value.
juce::ValueTree makeCanonicalTree()
{
    PresetHostProcessor host;
    juce::ValueTree root{ juce::Identifier{ mw::state::kRootId } };
    root.setProperty(mw::state::kAttrSchemaVersion, 1, nullptr);

    juce::ValueTree params{ juce::Identifier{ mw::state::kParamsId } };
    params.copyPropertiesAndChildrenFrom(host.apvts.copyState(), nullptr);
    root.appendChild(params, nullptr);
    return root;
}

// Default modeled value for a registry entry (continuous => defaultValue; choice/bool
// => the default index). This is exactly what writePresetJson must emit per id.
double defaultModeledValue(const mw::params::ParamDef& def)
{
    return static_cast<double>(def.defaultValue);
}

// A canonical tree like makeCanonicalTree() but carrying an <extras>/<seq> with two
// active steps (note/gate/tie/rest only, matching the StateSerializer shape) so the
// preset projection has stored steps to emit.
juce::ValueTree makeCanonicalTreeWithSeq()
{
    auto root = makeCanonicalTree();

    juce::ValueTree extras{ juce::Identifier{ mw::state::kExtrasId } };
    extras.setProperty(mw::state::kExtrasArpLatch, false, nullptr);

    juce::ValueTree seq{ juce::Identifier{ mw::state::kSeqId } };
    seq.setProperty("stepCount", 2, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        juce::ValueTree step{ juce::Identifier{ mw::state::kStepId } };
        step.setProperty("note", i * 7, nullptr);
        step.setProperty("gate", true, nullptr);
        step.setProperty("tie", i == 1, nullptr);
        step.setProperty("rest", false, nullptr);
        seq.appendChild(step, nullptr);
    }
    extras.appendChild(seq, nullptr);
    root.appendChild(extras, nullptr);
    return root;
}

// Parse JSON text -> the params object as an id->value map (modeled values).
std::map<std::string, juce::var> jsonParams(const juce::String& text)
{
    std::map<std::string, juce::var> out;
    const juce::var root = juce::JSON::parse(text);
    const auto* obj = root.getProperty("params", juce::var()).getDynamicObject();
    if (obj != nullptr)
        for (const auto& prop : obj->getProperties())
            out[prop.name.toString().toStdString()] = prop.value;
    return out;
}

// A complete, VALID preset JSON string built from the registry defaults plus the
// caller-supplied meta block, params, seq and arp sub-objects. The starting point for
// "mutate one thing to make it invalid" cases.
juce::String validPresetJson()
{
    const auto canonical = makeCanonicalTree();
    PresetMeta meta;
    meta.name        = "Round Trip";
    meta.author      = "Matt Woolly";
    meta.category    = "AcidBassLead";
    meta.description = "Inspired-by acid lead; not a track reconstruction.";
    meta.tags        = { "acid", "resonant" };
    meta.soundExt    = false;
    return writePresetJson(canonical, meta);
}

// Write `text` to a temp .mw101preset file and run it through loadPresetJson.
std::optional<juce::ValueTree> loadFromText(const juce::String& text, PresetMeta& outMeta)
{
    auto file = juce::File::createTempFile(".mw101preset");
    file.replaceWithText(text);
    auto result = loadPresetJson(file, outMeta);
    file.deleteFile();
    return result;
}

} // namespace

TEST_CASE("presetfmt writePresetJson emits the section-6.2 shape with note gate tie rest steps",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto json = validPresetJson();
    const juce::var root = juce::JSON::parse(json);
    REQUIRE(root.isObject());

    // schemaVersion present and == 1.
    CHECK(static_cast<int>(root.getProperty("schemaVersion", juce::var())) == 1);

    // meta block present with the required fields.
    const auto meta = root.getProperty("meta", juce::var());
    REQUIRE(meta.isObject());
    CHECK(meta.getProperty("name", juce::var()).toString() == "Round Trip");
    CHECK(meta.getProperty("author", juce::var()).toString() == "Matt Woolly");
    CHECK(meta.getProperty("category", juce::var()).toString() == "AcidBassLead");

    // params block holds EVERY live registry ID.
    const auto params = jsonParams(json);
    CHECK(params.size() == mw::params::kParamDefs.size());
    for (const auto& def : mw::params::kParamDefs)
        CHECK(params.find(def.id) != params.end());

    // seq + arp sub-objects present.
    CHECK(root.getProperty("seq", juce::var()).isObject());
    CHECK(root.getProperty("arp", juce::var()).isObject());

    // Build a tree carrying a stored seq so steps are emitted, and assert no accent.
    // (The default tree has no <extras>/<seq>, so seq.steps is empty; this asserts the
    // per-step SHAPE when steps exist — done via a dedicated tree below.)
}

TEST_CASE("presetfmt a valid preset round-trips through the canonical tree",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto json = validPresetJson();

    PresetMeta meta;
    const auto recovered = loadFromText(json, meta);
    REQUIRE(recovered.has_value());

    // The recovered canonical tree carries a <PARAMS> child with every live ID, each at
    // its registry default modeled value.
    const auto params = recovered->getChildWithName(mw::state::kParamsId);
    REQUIRE(params.isValid());

    std::map<std::string, double> recoveredValues;
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto child = params.getChild(i);
        recoveredValues[child.getProperty("id").toString().toStdString()] =
            static_cast<double>(child.getProperty("value"));
    }
    REQUIRE(recoveredValues.size() == mw::params::kParamDefs.size());
    for (const auto& def : mw::params::kParamDefs)
    {
        const auto it = recoveredValues.find(def.id);
        REQUIRE(it != recoveredValues.end());
        if (def.type == mw::params::ParamType::Continuous)
        {
            // JSON is a human-readable text format: a continuous value round-trips to
            // within text-serialization precision, not bit-for-bit (that exactness is
            // the BINARY blob serializer's contract, not the preset's) [§6.2, §6.3].
            CHECK(std::abs(it->second - defaultModeledValue(def)) < 1.0e-5);
        }
        else
        {
            // Choice / Bool indices are integers and MUST round-trip exactly.
            CHECK(static_cast<int>(it->second) == static_cast<int>(defaultModeledValue(def)));
        }
    }

    // The meta round-trips.
    CHECK(meta.name == "Round Trip");
    CHECK(meta.author == "Matt Woolly");
    CHECK(meta.category == "AcidBassLead");
    CHECK(meta.soundExt == false);
}

TEST_CASE("presetfmt loadPresetJson rejects malformed JSON", "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    SECTION("syntactically broken JSON")
    {
        const auto r = loadFromText("{ this is not json ]]", meta);
        CHECK_FALSE(r.has_value());
    }

    SECTION("JSON that is not an object")
    {
        const auto r = loadFromText("[1, 2, 3]", meta);
        CHECK_FALSE(r.has_value());
    }

    SECTION("empty text")
    {
        const auto r = loadFromText("", meta);
        CHECK_FALSE(r.has_value());
    }
}

TEST_CASE("presetfmt loadPresetJson enforces schemaVersion and required meta fields",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    const auto base = validPresetJson();

    auto stripField = [&](const juce::String& topKey, const juce::String& field) {
        juce::var root = juce::JSON::parse(base);
        if (field.isEmpty())
        {
            root.getDynamicObject()->removeProperty(topKey);
        }
        else
        {
            auto* m = root.getProperty(topKey, juce::var()).getDynamicObject();
            m->removeProperty(field);
        }
        return juce::JSON::toString(root);
    };

    SECTION("missing schemaVersion")
    {
        CHECK_FALSE(loadFromText(stripField("schemaVersion", {}), meta).has_value());
    }
    SECTION("missing meta.name")
    {
        CHECK_FALSE(loadFromText(stripField("meta", "name"), meta).has_value());
    }
    SECTION("missing meta.author")
    {
        CHECK_FALSE(loadFromText(stripField("meta", "author"), meta).has_value());
    }
    SECTION("missing meta.category")
    {
        CHECK_FALSE(loadFromText(stripField("meta", "category"), meta).has_value());
    }
}

TEST_CASE("presetfmt loadPresetJson rejects a category outside the section-6.5 enum",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    juce::var root = juce::JSON::parse(validPresetJson());
    root.getProperty("meta", juce::var()).getDynamicObject()
        ->setProperty("category", "NotARealCategory");
    CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());

    // Each of the six enum values IS accepted (the valid base uses AcidBassLead; spot
    // check one more so a typo in the enum table is caught).
    root.getProperty("meta", juce::var()).getDynamicObject()
        ->setProperty("category", "SeqArpRiff");
    PresetMeta meta2;
    CHECK(loadFromText(juce::JSON::toString(root), meta2).has_value());
}

TEST_CASE("presetfmt loadPresetJson rejects a missing registry ID in params",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    juce::var root = juce::JSON::parse(validPresetJson());
    root.getProperty("params", juce::var()).getDynamicObject()
        ->removeProperty("mw101.vcf.cutoff");
    CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
}

TEST_CASE("presetfmt loadPresetJson rejects an out-of-range continuous value",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    juce::var root = juce::JSON::parse(validPresetJson());
    // mw101.vco.tune range is [-24, +24]; 999 is out of range.
    root.getProperty("params", juce::var()).getDynamicObject()
        ->setProperty("mw101.vco.tune", 999.0);
    CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
}

TEST_CASE("presetfmt loadPresetJson rejects a choice index at or above choiceCount",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    juce::var root = juce::JSON::parse(validPresetJson());
    // mw101.sub.mode has 3 choices (indices 0..2); index 3 is out of range.
    root.getProperty("params", juce::var()).getDynamicObject()
        ->setProperty("mw101.sub.mode", 3);
    CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
}

TEST_CASE("presetfmt sound_ext is forced true by a software-ext index",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    // Engage a software-only feature (vco.range = 4 == 32') but leave sound_ext false:
    // the projection MUST reject the inconsistency.
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("params", juce::var()).getDynamicObject()
            ->setProperty("mw101.vco.range", 4);  // 32' (software-only)
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("sound_ext", false);
        CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
    }

    // The SAME params with sound_ext = true is accepted, and the flag round-trips.
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("params", juce::var()).getDynamicObject()
            ->setProperty("mw101.vco.range", 4);
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("sound_ext", true);
        PresetMeta extMeta;
        const auto r = loadFromText(juce::JSON::toString(root), extMeta);
        REQUIRE(r.has_value());
        CHECK(extMeta.soundExt == true);
    }

    // Conversely, claiming sound_ext == true while using NO software-only feature is
    // ALSO rejected (the rule is "iff").
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("sound_ext", true);
        CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
    }

    // lfo.shape == 4 (Sine) is the other software-only index.
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("params", juce::var()).getDynamicObject()
            ->setProperty("mw101.lfo.shape", 4);  // Sine (software-only)
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("sound_ext", true);
        PresetMeta sineMeta;
        CHECK(loadFromText(juce::JSON::toString(root), sineMeta).has_value());
    }
}

TEST_CASE("presetfmt loadPresetJson rejects a per-step accent field",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    juce::var root = juce::JSON::parse(validPresetJson());

    // Inject a seq with one step carrying a forbidden `accent` field [ADR-025].
    auto* seqObj = new juce::DynamicObject();
    seqObj->setProperty("stepCount", 1);
    juce::Array<juce::var> steps;
    auto* step = new juce::DynamicObject();
    step->setProperty("note", 0);
    step->setProperty("gate", true);
    step->setProperty("tie", false);
    step->setProperty("rest", false);
    step->setProperty("accent", true);   // FORBIDDEN
    steps.add(juce::var{ step });
    seqObj->setProperty("steps", steps);
    root.getDynamicObject()->setProperty("seq", juce::var{ seqObj });

    CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
}

TEST_CASE("presetfmt loadPresetJson enforces attribution discipline",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PresetMeta meta;

    SECTION("as-used-on-track phrasing in the description is rejected")
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("description", "The exact patch as used on track Voodoo Ray.");
        CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
    }

    SECTION("TB-303 filter descriptor is rejected")
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("description", "A classic TB-303 filter squelch.");
        CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
    }

    SECTION("forbidden phrasing in a tag is rejected")
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        juce::Array<juce::var> tags{ juce::var{ "TB-303 filter" } };
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("tags", tags);
        CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
    }

    SECTION("inspired_by carrying an as-used-on-track string is rejected")
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("inspired_by", "as used on track A Guy Called Gerald");
        CHECK_FALSE(loadFromText(juce::JSON::toString(root), meta).has_value());
    }

    SECTION("inspired_by as a plain inspired-by reference is accepted and round-trips")
    {
        juce::var root = juce::JSON::parse(validPresetJson());
        root.getProperty("meta", juce::var()).getDynamicObject()
            ->setProperty("inspired_by", "acid-house idiom");
        PresetMeta okMeta;
        const auto r = loadFromText(juce::JSON::toString(root), okMeta);
        REQUIRE(r.has_value());
        CHECK(okMeta.inspiredBy == "acid-house idiom");
    }
}

TEST_CASE("presetfmt a stored seq pattern round-trips note gate tie rest with no accent",
          "[presetfmt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Build a canonical tree carrying an <extras>/<seq> with a couple of steps, project
    // it to JSON, and assert the emitted per-step objects carry note/gate/tie/rest only.
    auto canonical = makeCanonicalTreeWithSeq();

    PresetMeta meta;
    meta.name     = "Riff";
    meta.author   = "Matt Woolly";
    meta.category = "SeqArpRiff";

    const auto json = writePresetJson(canonical, meta);
    const juce::var root = juce::JSON::parse(json);
    const auto seq = root.getProperty("seq", juce::var());
    REQUIRE(seq.isObject());
    CHECK(static_cast<int>(seq.getProperty("stepCount", juce::var())) == 2);

    const auto* steps = seq.getProperty("steps", juce::var()).getArray();
    REQUIRE(steps != nullptr);
    REQUIRE(steps->size() == 2);
    for (const auto& s : *steps)
    {
        auto* obj = s.getDynamicObject();
        REQUIRE(obj != nullptr);
        CHECK(obj->hasProperty("note"));
        CHECK(obj->hasProperty("gate"));
        CHECK(obj->hasProperty("tie"));
        CHECK(obj->hasProperty("rest"));
        CHECK_FALSE(obj->hasProperty("accent"));
    }
}
