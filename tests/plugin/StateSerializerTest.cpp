// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/StateSerializerTest.cpp — JUCE-linked tests for the canonical state
// (de)serializer (task 023). Asserts every acceptance criterion of plan/backlog/023:
//
//   1. captureState -> writeToBlob -> readFromBlob round-trips EVERY param value and
//      the FULL <extras> (a 100-step note/rest/tie/gate sequence, drift seed + lock,
//      arp latch) bit-for-bit [docs/design/06 §5; Acceptance hooks round-trip].
//   2. readFromBlob returns nullopt on a STRUCTURAL parse failure (garbage bytes,
//      truncated blob, wrong-root tree) [docs/design/06 §5.2; ADR-021 L1].
//   3. <seq> carries note/gate/tie/rest ONLY — there is NO accent attribute on any
//      <step> element [docs/design/06 §5.5; ADR-025].
//   4. The root carries schemaVersion/pluginVersion/engineVersion/renderVersion and a
//      <PARAMS> subtree mirroring the APVTS state [docs/design/06 §5.1].
//
// Test names begin with the `serializer` tag so `ctest -R serializer` selects exactly
// these and the silent-pass / --no-tests=error rule holds.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/StateSerializer.h"      // mw::plugin::state (the unit under test)
#include "params/ParameterLayout.h"     // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"           // mw::params::kParamDefs (JUCE-free registry)
#include "state/Extras.h"               // mw::state::Extras / SeqStep / kMaxSeqSteps
#include "state/StateTree.h"            // mw::state canonical keys

namespace {

// A minimal headless AudioProcessor hosting the FULL APVTS layout, so the serializer
// runs against the real 91-param tree the plugin ships.
class SerializerHostProcessor final : public juce::AudioProcessor
{
public:
    SerializerHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "SerializerHost"; }
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

// Drive every parameter to a deterministic non-default normalized value so the
// round-trip has to carry the WHOLE tree, not just the defaults.
void scrambleAllParams(juce::AudioProcessor& processor)
{
    std::mt19937 rng{ 0xC0FFEEu };
    std::uniform_real_distribution<float> dist{ 0.05f, 0.95f };
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*>(p))
            withId->setValueNotifyingHost(dist(rng));
}

// Build a fully-populated 100-step Extras with a distinct value per step, plus a
// non-trivial drift seed/lock and arp latch, so the <extras>/<seq> round-trip is
// exercised at full capacity.
mw::state::Extras makeFullExtras()
{
    mw::state::Extras e{};
    e.stepCount  = mw::state::kMaxSeqSteps;          // all 100 steps active
    e.arpLatch   = true;
    e.driftSeed  = static_cast<std::int64_t>(0x0123456789ABCDEFLL);
    e.seedLocked = true;

    for (int i = 0; i < mw::state::kMaxSeqSteps; ++i)
    {
        auto& s = e.steps[static_cast<std::size_t>(i)];
        s.noteSemitone = static_cast<std::int8_t>((i % 25) - 12);  // span [-12, +12]
        s.gate = (i % 2) == 0;
        s.tie  = (i % 3) == 0;
        s.rest = (i % 5) == 0;
    }
    return e;
}

// Snapshot every param value off an APVTS state subtree into an id->normalized map.
std::map<std::string, double> paramValues(const juce::ValueTree& paramsSubtree)
{
    std::map<std::string, double> out;
    for (int i = 0; i < paramsSubtree.getNumChildren(); ++i)
    {
        const auto child = paramsSubtree.getChild(i);
        const auto id    = child.getProperty("id").toString().toStdString();
        if (! id.empty())
            out[id] = static_cast<double>(child.getProperty("value"));
    }
    return out;
}

constexpr int    kSchema  = 1;
constexpr int    kRender  = 1;
const juce::String kPlugin = "1.2.3";
const juce::String kEngine = "4.5.6";

} // namespace

TEST_CASE("serializer round-trips every param and the full extras bit-for-bit",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SerializerHostProcessor host;
    scrambleAllParams(host);
    // Flush the scrambled parameter atomics into the backing state ValueTree that
    // captureState reads (setValueNotifyingHost updates the atomic; copyState() commits
    // those pending writes into apvts.state on the message thread).
    host.apvts.copyState();
    const auto extras = makeFullExtras();

    // Capture -> write -> read.
    const auto captured = mw::plugin::state::captureState(
        host.apvts, extras, kSchema, kPlugin, kEngine, kRender);

    juce::MemoryBlock blob;
    mw::plugin::state::writeToBlob(captured, blob);
    REQUIRE(blob.getSize() > 0);

    const auto restored = mw::plugin::state::readFromBlob(blob.getData(),
                                                          static_cast<int>(blob.getSize()));
    REQUIRE(restored.has_value());

    // --- Root attributes survive verbatim -------------------------------------
    CHECK(static_cast<int>(restored->getProperty(mw::state::kAttrSchemaVersion)) == kSchema);
    CHECK(static_cast<int>(restored->getProperty(mw::state::kAttrRenderVersion)) == kRender);
    CHECK(restored->getProperty(mw::state::kAttrPluginVersion).toString() == kPlugin);
    CHECK(restored->getProperty(mw::state::kAttrEngineVersion).toString() == kEngine);

    // --- Every param value round-trips bit-for-bit ----------------------------
    const auto before = paramValues(host.apvts.copyState());
    const auto params = restored->getChildWithName(mw::state::kParamsId);
    REQUIRE(params.isValid());
    const auto after = paramValues(params);

    REQUIRE(after.size() == before.size());
    REQUIRE(after.size() == mw::params::kParamDefs.size());
    for (const auto& [id, value] : before)
    {
        const auto it = after.find(id);
        REQUIRE(it != after.end());
        // Same float bits: JUCE stores param values as float; equality is exact.
        CHECK(static_cast<float>(it->second) == static_cast<float>(value));
    }

    // --- Full <extras> round-trips bit-for-bit --------------------------------
    const auto restoredExtras = restored->getChildWithName(mw::state::kExtrasId);
    REQUIRE(restoredExtras.isValid());

    CHECK(static_cast<bool>(restoredExtras.getProperty(mw::state::kExtrasArpLatch))
              == extras.arpLatch);
    CHECK(static_cast<bool>(restoredExtras.getProperty(mw::state::kExtrasSeedLocked))
              == extras.seedLocked);
    CHECK(static_cast<std::int64_t>(restoredExtras.getProperty(mw::state::kExtrasDriftSeed))
              == extras.driftSeed);

    const auto seq = restoredExtras.getChildWithName(mw::state::kSeqId);
    REQUIRE(seq.isValid());
    CHECK(static_cast<int>(seq.getProperty(mw::plugin::state::kSeqAttrStepCount))
              == extras.stepCount);
    REQUIRE(seq.getNumChildren() == extras.stepCount);

    for (int i = 0; i < extras.stepCount; ++i)
    {
        const auto step = seq.getChild(i);
        const auto& src = extras.steps[static_cast<std::size_t>(i)];
        CHECK(static_cast<int>(step.getProperty(mw::plugin::state::kSeqStepAttrNote))
                  == static_cast<int>(src.noteSemitone));
        CHECK(static_cast<bool>(step.getProperty(mw::plugin::state::kSeqStepAttrGate))
                  == src.gate);
        CHECK(static_cast<bool>(step.getProperty(mw::plugin::state::kSeqStepAttrTie))
                  == src.tie);
        CHECK(static_cast<bool>(step.getProperty(mw::plugin::state::kSeqStepAttrRest))
                  == src.rest);
    }
}

TEST_CASE("serializer seq steps carry note gate tie rest only and never accent",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SerializerHostProcessor host;
    const auto extras = makeFullExtras();

    const auto captured = mw::plugin::state::captureState(
        host.apvts, extras, kSchema, kPlugin, kEngine, kRender);

    const auto seq = captured.getChildWithName(mw::state::kExtrasId)
                             .getChildWithName(mw::state::kSeqId);
    REQUIRE(seq.isValid());
    REQUIRE(seq.getNumChildren() > 0);

    // No <step> element may carry an `accent` attribute under any casing [ADR-025].
    for (int i = 0; i < seq.getNumChildren(); ++i)
    {
        const auto step = seq.getChild(i);
        CHECK_FALSE(step.hasProperty("accent"));
        CHECK_FALSE(step.hasProperty("Accent"));
        // The four contracted attributes ARE present.
        CHECK(step.hasProperty(mw::plugin::state::kSeqStepAttrNote));
        CHECK(step.hasProperty(mw::plugin::state::kSeqStepAttrGate));
        CHECK(step.hasProperty(mw::plugin::state::kSeqStepAttrTie));
        CHECK(step.hasProperty(mw::plugin::state::kSeqStepAttrRest));
    }
}

TEST_CASE("serializer captures only the active step count out of the fixed capacity",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SerializerHostProcessor host;

    // A short 16-step pattern inside the 100-step fixed-capacity array: only the
    // active steps are written [docs/design/06 §5.5].
    mw::state::Extras extras{};
    extras.stepCount = 16;
    for (int i = 0; i < extras.stepCount; ++i)
        extras.steps[static_cast<std::size_t>(i)].noteSemitone = static_cast<std::int8_t>(i);

    const auto captured = mw::plugin::state::captureState(
        host.apvts, extras, kSchema, kPlugin, kEngine, kRender);

    const auto seq = captured.getChildWithName(mw::state::kExtrasId)
                             .getChildWithName(mw::state::kSeqId);
    REQUIRE(seq.isValid());
    CHECK(static_cast<int>(seq.getProperty(mw::plugin::state::kSeqAttrStepCount)) == 16);
    CHECK(seq.getNumChildren() == 16);   // one <step> per ACTIVE step, not 100
}

TEST_CASE("serializer readFromBlob returns nullopt on structural parse failure",
          "[serializer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION("garbage bytes")
    {
        const unsigned char garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42, 0x99 };
        const auto parsed = mw::plugin::state::readFromBlob(garbage, (int) sizeof garbage);
        CHECK_FALSE(parsed.has_value());
    }

    SECTION("empty / zero-length blob")
    {
        const auto parsed = mw::plugin::state::readFromBlob(nullptr, 0);
        CHECK_FALSE(parsed.has_value());
    }

    SECTION("truncated valid blob")
    {
        SerializerHostProcessor host;
        const auto captured = mw::plugin::state::captureState(
            host.apvts, mw::state::Extras{}, kSchema, kPlugin, kEngine, kRender);

        juce::MemoryBlock blob;
        mw::plugin::state::writeToBlob(captured, blob);
        REQUIRE(blob.getSize() > 8);

        // Hand back only the first few bytes: structurally unparseable.
        const auto parsed = mw::plugin::state::readFromBlob(blob.getData(), 4);
        CHECK_FALSE(parsed.has_value());
    }

    SECTION("well-formed JUCE blob whose root is not MW101_STATE")
    {
        // A perfectly serializable ValueTree, but with the wrong root tag: structurally
        // invalid as a canonical state blob.
        juce::ValueTree wrongRoot{ "SOMETHING_ELSE" };
        wrongRoot.setProperty("x", 1, nullptr);

        juce::MemoryBlock blob;
        mw::plugin::state::writeToBlob(wrongRoot, blob);
        REQUIRE(blob.getSize() > 0);

        const auto parsed = mw::plugin::state::readFromBlob(
            blob.getData(), static_cast<int>(blob.getSize()));
        CHECK_FALSE(parsed.has_value());
    }
}
