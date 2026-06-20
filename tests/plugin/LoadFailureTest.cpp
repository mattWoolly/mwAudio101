// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/LoadFailureTest.cpp — JUCE-linked tests for the load-failure recovery
// ladder (task 024). Asserts every acceptance criterion of plan/backlog/024 against the
// §8.3 (ADR-021) failure-case contract. One fixture per ladder rung L1-L8 + L4/L6,
// asserting: no throw/crash, the correct fallback target, raw preservation on L6, and a
// sequence padded/clamped to the fixed 100-step capacity on L8 — plus that deviations
// coalesce into ONE RecoveryReport note list.
//
//   recoverState NEVER throws and ALWAYS returns a complete valid canonical tree + report
//   for any input [docs/design/06 §8.1, §8.2; ADR-021 L1, L13].
//
// Test names begin with the `loadfail` tag so `ctest -R loadfail` selects exactly these
// and the silent-pass / --no-tests=error rule holds. No '[' appears in any display name.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/LoadFailure.h"          // mw::plugin::state (the unit under test)
#include "state/StateSerializer.h"      // captureState / writeToBlob (build fixtures)
#include "params/ParameterLayout.h"     // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"           // mw::params::kParamDefs (JUCE-free registry)
#include "state/Extras.h"               // mw::state::Extras / kMaxSeqSteps
#include "state/StateTree.h"            // mw::state canonical keys
#include "version/EngineVersion.h"      // kCurrentSchemaVersion

namespace {

using namespace mw::plugin::state;

// A minimal headless AudioProcessor hosting the FULL 91-param APVTS layout so the
// fixtures serialize against the real tree the plugin ships.
class LoadFailHostProcessor final : public juce::AudioProcessor
{
public:
    LoadFailHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "LoadFailHost"; }
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

constexpr int      kRender  = mw101::version::kCurrentRenderVersion;
const juce::String kPlugin = "1.0.0";
const juce::String kEngine = "1.0.0";
constexpr int      kCurrent = mw101::version::kCurrentSchemaVersion;

// Serialize a canonical tree to a blob (the wire form recoverState consumes).
juce::MemoryBlock toBlob(const juce::ValueTree& tree)
{
    juce::MemoryBlock blob;
    writeToBlob(tree, blob);
    return blob;
}

// Find a <PARAM id="..."> child under <PARAMS>; invalid tree if absent.
juce::ValueTree findParam(const juce::ValueTree& canonical, const char* id)
{
    const auto params = canonical.getChildWithName(mw::state::kParamsId);
    for (int i = 0; i < params.getNumChildren(); ++i)
    {
        const auto child = params.getChild(i);
        if (child.getProperty("id").toString() == juce::String(id))
            return child;
    }
    return {};
}

double paramValue(const juce::ValueTree& canonical, const char* id)
{
    return static_cast<double>(findParam(canonical, id).getProperty("value"));
}

// Locate a registry entry by ID (linear scan; test-only).
const mw::params::ParamDef& def(const char* id)
{
    for (const auto& d : mw::params::kParamDefs)
        if (juce::String(d.id) == juce::String(id))
            return d;
    FAIL("unknown param id in test fixture");
    return mw::params::kParamDefs[0];
}

// A clean canonical tree at CURRENT schema (the happy path / fixture base).
juce::ValueTree makeCleanTree(LoadFailHostProcessor& host, const mw::state::Extras& extras)
{
    return captureState(host.apvts, extras, kCurrent, kPlugin, kEngine, kRender);
}

} // namespace

TEST_CASE("loadfail recoverState returns a complete valid tree on a clean current load",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    const auto blob = toBlob(makeCleanTree(host, mw::state::Extras{}));

    RecoveryReport report;
    juce::ValueTree out;
    REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                       static_cast<int>(blob.getSize()), report));

    // Complete valid canonical tree: right root, all 91 params, both subtrees present.
    REQUIRE(out.hasType(juce::Identifier{ mw::state::kRootId }));
    const auto params = out.getChildWithName(mw::state::kParamsId);
    REQUIRE(params.isValid());
    CHECK(params.getNumChildren() == static_cast<int>(mw::params::kParamDefs.size()));
    CHECK(out.getChildWithName(mw::state::kExtrasId).isValid());
    CHECK(static_cast<int>(out.getProperty(mw::state::kAttrSchemaVersion)) == kCurrent);

    // A clean load deviates from nothing.
    CHECK(report.outcome == RecoveryOutcome::CleanLoad);
    CHECK(report.notes.isEmpty());
}

TEST_CASE("loadfail L1 unparseable blob falls back to INIT with an empty sequence and one warning",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    SECTION("garbage bytes")
    {
        const unsigned char garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42, 0x99 };
        RecoveryReport report;
        juce::ValueTree out;
        REQUIRE_NOTHROW(out = recoverState(garbage, static_cast<int>(sizeof garbage), report));

        REQUIRE(out.hasType(juce::Identifier{ mw::state::kRootId }));
        const auto params = out.getChildWithName(mw::state::kParamsId);
        REQUIRE(params.isValid());
        CHECK(params.getNumChildren() == static_cast<int>(mw::params::kParamDefs.size()));

        // INIT defaults: vca.level default is 0.8, cutoff 1.0 (§3.0). The INIT patch
        // moves vintage.enable ON.
        CHECK(paramValue(out, "mw101.vca.level") == Catch::Approx(0.8));
        CHECK(static_cast<int>(findParam(out, "mw101.vintage.enable").getProperty("value")) == 1);

        // Empty <extras> sequence — no active steps.
        const auto seq = out.getChildWithName(mw::state::kExtrasId)
                            .getChildWithName(mw::state::kSeqId);
        REQUIRE(seq.isValid());
        CHECK(static_cast<int>(seq.getProperty(kSeqAttrStepCount)) == 0);
        CHECK(seq.getNumChildren() == 0);

        // INIT fallback outcome + exactly one coalesced warning, never reset-to-INIT silently.
        CHECK(report.outcome == RecoveryOutcome::InitFallback);
        CHECK(report.notes.size() == 1);
    }

    SECTION("null / zero-length blob")
    {
        RecoveryReport report;
        juce::ValueTree out;
        REQUIRE_NOTHROW(out = recoverState(nullptr, 0, report));
        REQUIRE(out.hasType(juce::Identifier{ mw::state::kRootId }));
        CHECK(report.outcome == RecoveryOutcome::InitFallback);
        CHECK(report.notes.size() == 1);
    }

    SECTION("well-formed JUCE blob with the wrong root tag falls to INIT")
    {
        juce::ValueTree wrongRoot{ "SOMETHING_ELSE" };
        wrongRoot.setProperty("x", 1, nullptr);
        const auto blob = toBlob(wrongRoot);

        RecoveryReport report;
        juce::ValueTree out;
        REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                           static_cast<int>(blob.getSize()), report));
        REQUIRE(out.hasType(juce::Identifier{ mw::state::kRootId }));
        CHECK(report.outcome == RecoveryOutcome::InitFallback);
        CHECK(report.notes.size() == 1);
    }
}

TEST_CASE("loadfail L2 schema at or below current migrates and defaults missing params",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    SECTION("clean current schema runs the chain with no deviation note")
    {
        const auto blob = toBlob(makeCleanTree(host, mw::state::Extras{}));
        RecoveryReport report;
        const auto out = recoverState(blob.getData(),
                                      static_cast<int>(blob.getSize()), report);
        CHECK(static_cast<int>(out.getProperty(mw::state::kAttrSchemaVersion)) == kCurrent);
        // No missing/clamped value => clean (CleanLoad) or MigratedAndBound with no notes.
        CHECK(report.notes.isEmpty());
    }

    SECTION("a missing param is filled with its registry default and noted")
    {
        auto tree = makeCleanTree(host, mw::state::Extras{});
        // Drop one known param child so recovery must default it.
        auto child = findParam(tree, "mw101.vca.level");
        REQUIRE(child.isValid());
        tree.getChildWithName(mw::state::kParamsId).removeChild(child, nullptr);

        const auto blob = toBlob(tree);
        RecoveryReport report;
        const auto out = recoverState(blob.getData(),
                                      static_cast<int>(blob.getSize()), report);

        // Every param present again, the missing one back at its registry default (0.8).
        const auto params = out.getChildWithName(mw::state::kParamsId);
        CHECK(params.getNumChildren() == static_cast<int>(mw::params::kParamDefs.size()));
        CHECK(paramValue(out, "mw101.vca.level") == Catch::Approx(0.8));

        CHECK(report.outcome == RecoveryOutcome::MigratedAndBound);
        CHECK(report.notes.size() == 1);   // one coalesced note for the deviation
    }

    SECTION("a missing schemaVersion attribute is treated as the v1 baseline")
    {
        auto tree = makeCleanTree(host, mw::state::Extras{});
        tree.removeProperty(mw::state::kAttrSchemaVersion, nullptr);

        const auto blob = toBlob(tree);
        RecoveryReport report;
        juce::ValueTree out;
        REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                           static_cast<int>(blob.getSize()), report));
        // Stamped to CURRENT by the migration chain; never INIT (params survived).
        CHECK(static_cast<int>(out.getProperty(mw::state::kAttrSchemaVersion)) == kCurrent);
        CHECK(out.getChildWithName(mw::state::kParamsId).getNumChildren()
              == static_cast<int>(mw::params::kParamDefs.size()));
        CHECK(report.outcome != RecoveryOutcome::InitFallback);
    }
}

TEST_CASE("loadfail L3 newer schema binds known ids defaults rest and retains the raw blob",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    // A blob a NEWER build wrote: schemaVersion above CURRENT, plus a future param the
    // current build does not know, and one known param at a non-default value.
    auto tree = makeCleanTree(host, mw::state::Extras{});
    tree.setProperty(mw::state::kAttrSchemaVersion, kCurrent + 5, nullptr);

    auto params = tree.getChildWithName(mw::state::kParamsId);
    juce::ValueTree future{ "PARAM" };
    future.setProperty("id", "mw101.future.unknown", nullptr);
    future.setProperty("value", 0.123, nullptr);
    params.appendChild(future, nullptr);

    // A known param carries a deliberate non-default value to prove "bind known ids".
    findParam(tree, "mw101.vca.level").setProperty("value", 0.42, nullptr);

    const auto blob = toBlob(tree);

    RecoveryReport report;
    juce::ValueTree out;
    REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                       static_cast<int>(blob.getSize()), report));

    // Known IDs bound (value preserved), never reset to INIT while interpretable.
    CHECK(paramValue(out, "mw101.vca.level") == Catch::Approx(0.42));
    CHECK(out.getChildWithName(mw::state::kParamsId).isValid());

    // Outcome is the newer-down-interpreted rung, with one coalesced warning.
    CHECK(report.outcome == RecoveryOutcome::NewerDownInterpreted);
    CHECK(report.notes.size() == 1);

    // L6: the original raw blob is retained on <extras> for round-trip.
    const auto extras = out.getChildWithName(mw::state::kExtrasId);
    REQUIRE(extras.isValid());
    REQUIRE(extras.hasProperty(mw::state::kExtrasRawNewerBlob));
    const auto* retained = extras.getProperty(mw::state::kExtrasRawNewerBlob)
                              .getBinaryData();
    REQUIRE(retained != nullptr);
    // Byte-for-byte equal to the blob we handed in (raw preservation).
    REQUIRE(retained->getSize() == blob.getSize());
    CHECK(std::memcmp(retained->getData(), blob.getData(), blob.getSize()) == 0);
}

TEST_CASE("loadfail L4 clamps out-of-range continuous values and resets invalid choice indices",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    auto tree = makeCleanTree(host, mw::state::Extras{});

    // Continuous: drive vco.tune far above its +24 ceiling and vca.level below 0.
    findParam(tree, "mw101.vco.tune").setProperty("value", 999.0, nullptr);
    findParam(tree, "mw101.vca.level").setProperty("value", -5.0, nullptr);
    // Choice: an out-of-range index on a 3-choice param (lfo.dest: 0..2).
    findParam(tree, "mw101.lfo.dest").setProperty("value", 99, nullptr);
    // Choice: a negative index on glide.mode (0..2).
    findParam(tree, "mw101.glide.mode").setProperty("value", -1, nullptr);

    const auto blob = toBlob(tree);

    RecoveryReport report;
    juce::ValueTree out;
    REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                       static_cast<int>(blob.getSize()), report));

    const auto& tune = def("mw101.vco.tune");
    const auto& lvl  = def("mw101.vca.level");
    // Continuous clamped INTO the modeled NormalisableRange [min, max].
    CHECK(paramValue(out, "mw101.vco.tune") == Catch::Approx(static_cast<double>(tune.maxValue)));
    CHECK(paramValue(out, "mw101.vca.level") == Catch::Approx(static_cast<double>(lvl.minValue)));

    // Invalid choice index reset to its registry default (lfo.dest default 0, glide.mode 0).
    CHECK(static_cast<int>(findParam(out, "mw101.lfo.dest").getProperty("value"))
          == static_cast<int>(def("mw101.lfo.dest").defaultValue));
    CHECK(static_cast<int>(findParam(out, "mw101.glide.mode").getProperty("value"))
          == static_cast<int>(def("mw101.glide.mode").defaultValue));

    // Coalesced into ONE warning (not one-per-param).
    CHECK(report.outcome == RecoveryOutcome::ClampedValues);
    CHECK(report.notes.size() == 1);
}

TEST_CASE("loadfail L5 prefers partial recovery over a full INIT reset",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    // A tree with most params dropped but one interpretable known value surviving:
    // recovery must NOT reset to INIT — it binds the survivor + defaults the rest.
    auto tree = makeCleanTree(host, mw::state::Extras{});
    auto params = tree.getChildWithName(mw::state::kParamsId);
    findParam(tree, "mw101.vcf.cutoff").setProperty("value", 0.33, nullptr);

    // Remove every child EXCEPT vcf.cutoff.
    for (int i = params.getNumChildren(); --i >= 0;)
    {
        const auto child = params.getChild(i);
        if (child.getProperty("id").toString() != "mw101.vcf.cutoff")
            params.removeChild(i, nullptr);
    }
    REQUIRE(params.getNumChildren() == 1);

    const auto blob = toBlob(tree);
    RecoveryReport report;
    const auto out = recoverState(blob.getData(),
                                  static_cast<int>(blob.getSize()), report);

    // Survivor bound; the rest defaulted; NOT INIT.
    CHECK(paramValue(out, "mw101.vcf.cutoff") == Catch::Approx(0.33));
    CHECK(out.getChildWithName(mw::state::kParamsId).getNumChildren()
          == static_cast<int>(mw::params::kParamDefs.size()));
    CHECK(report.outcome != RecoveryOutcome::InitFallback);
}

TEST_CASE("loadfail L8 pads and clamps a wrong-length stored sequence to the fixed capacity",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    SECTION("an over-length sequence is clamped to 100 steps")
    {
        // Hand-build a <seq> claiming 250 steps with 250 <step> children.
        auto tree = makeCleanTree(host, mw::state::Extras{});
        auto extras = tree.getChildWithName(mw::state::kExtrasId);
        auto seq = extras.getChildWithName(mw::state::kSeqId);
        seq.setProperty(kSeqAttrStepCount, 250, nullptr);
        for (int i = 0; i < 250; ++i)
        {
            juce::ValueTree step{ mw::state::kStepId };
            step.setProperty(kSeqStepAttrNote, (i % 25) - 12, nullptr);
            step.setProperty(kSeqStepAttrGate, true, nullptr);
            step.setProperty(kSeqStepAttrTie, false, nullptr);
            step.setProperty(kSeqStepAttrRest, false, nullptr);
            seq.appendChild(step, nullptr);
        }

        const auto blob = toBlob(tree);
        RecoveryReport report;
        juce::ValueTree out;
        REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                           static_cast<int>(blob.getSize()), report));

        const auto outSeq = out.getChildWithName(mw::state::kExtrasId)
                               .getChildWithName(mw::state::kSeqId);
        REQUIRE(outSeq.isValid());
        // Clamped to the fixed 100-step capacity — never crash, never over-allocate.
        CHECK(static_cast<int>(outSeq.getProperty(kSeqAttrStepCount)) == mw::state::kMaxSeqSteps);
        CHECK(outSeq.getNumChildren() == mw::state::kMaxSeqSteps);
    }

    SECTION("a negative step count is clamped up to zero")
    {
        auto tree = makeCleanTree(host, mw::state::Extras{});
        tree.getChildWithName(mw::state::kExtrasId)
            .getChildWithName(mw::state::kSeqId)
            .setProperty(kSeqAttrStepCount, -7, nullptr);

        const auto blob = toBlob(tree);
        RecoveryReport report;
        juce::ValueTree out;
        REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                           static_cast<int>(blob.getSize()), report));
        const auto outSeq = out.getChildWithName(mw::state::kExtrasId)
                               .getChildWithName(mw::state::kSeqId);
        CHECK(static_cast<int>(outSeq.getProperty(kSeqAttrStepCount)) == 0);
        CHECK(outSeq.getNumChildren() == 0);
    }

    SECTION("a short well-formed sequence round-trips unchanged")
    {
        mw::state::Extras e{};
        e.stepCount = 16;
        for (int i = 0; i < 16; ++i)
            e.steps[static_cast<std::size_t>(i)].noteSemitone = static_cast<std::int8_t>(i);
        const auto blob = toBlob(makeCleanTree(host, e));

        RecoveryReport report;
        const auto out = recoverState(blob.getData(),
                                      static_cast<int>(blob.getSize()), report);
        const auto outSeq = out.getChildWithName(mw::state::kExtrasId)
                               .getChildWithName(mw::state::kSeqId);
        CHECK(static_cast<int>(outSeq.getProperty(kSeqAttrStepCount)) == 16);
        CHECK(outSeq.getNumChildren() == 16);
    }
}

TEST_CASE("loadfail every recovery returns a complete valid tree and coalesces to one note",
          "[loadfail]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    LoadFailHostProcessor host;

    // Stack MULTIPLE sub-failures into ONE load: out-of-range continuous + invalid
    // choice + a missing param + a wrong-length sequence. All must coalesce into a
    // single RecoveryReport note list (never a storm), and the tree must be complete.
    auto tree = makeCleanTree(host, mw::state::Extras{});
    findParam(tree, "mw101.vco.tune").setProperty("value", 999.0, nullptr);
    findParam(tree, "mw101.lfo.dest").setProperty("value", 77, nullptr);
    {
        auto params = tree.getChildWithName(mw::state::kParamsId);
        params.removeChild(findParam(tree, "mw101.vca.level"), nullptr);
    }
    tree.getChildWithName(mw::state::kExtrasId)
        .getChildWithName(mw::state::kSeqId)
        .setProperty(kSeqAttrStepCount, 500, nullptr);

    const auto blob = toBlob(tree);
    RecoveryReport report;
    juce::ValueTree out;
    REQUIRE_NOTHROW(out = recoverState(blob.getData(),
                                       static_cast<int>(blob.getSize()), report));

    // Complete valid tree: all 91 params, both subtrees, clamped sequence.
    CHECK(out.getChildWithName(mw::state::kParamsId).getNumChildren()
          == static_cast<int>(mw::params::kParamDefs.size()));
    const auto outSeq = out.getChildWithName(mw::state::kExtrasId)
                           .getChildWithName(mw::state::kSeqId);
    CHECK(static_cast<int>(outSeq.getProperty(kSeqAttrStepCount)) <= mw::state::kMaxSeqSteps);

    // Multiple sub-failures coalesce into exactly ONE note (L12).
    CHECK(report.notes.size() == 1);
    // It is a deviating load, so NOT a clean outcome.
    CHECK(report.outcome != RecoveryOutcome::CleanLoad);
}
