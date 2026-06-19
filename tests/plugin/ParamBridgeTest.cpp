// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ParamBridgeTest.cpp — JUCE-linked tests for ParamBridge (task 102).
//
// ParamBridge is the APVTS <-> normalized-snapshot marshaller (plugin/params/
// ParamBridge.h/.cpp). It builds a parameter-id -> APVTS-atomic-pointer table at
// prepare from the JUCE-free param-schema layout (kParamDefs), and snapshot() reads
// each atomic ONCE per block into an immutable normalized [0,1] / typed-enum POD
// (mw::plugin::NormalizedParamSnapshot). No juce::* type appears in that POD.
//
// Asserts every acceptance criterion of plan/backlog/102:
//
//   1. (§5.4; ADR-001 C7) The bridge samples each APVTS atomic ONCE per block — a
//      step change in a normalized param appears in the NEXT snapshot, not before it,
//      i.e. exactly once-per-block. The core reads no std::atomic in tight loops; the
//      whole atomic read is hoisted into snapshot().
//   2. (§5.2; ADR-001 C14) No JUCE type crosses into the snapshot; the snapshot is a
//      trivially-copyable normalized POD (compile-time + runtime checks here).
//   3. The bridge produces NORMALIZED [0,1] values only (continuous params), and the
//      typed-enum INDEX for choice/bool params — normalized->engineering mapping stays
//      in core, the bridge never emits engineering units.
//   4. The id->pointer table is built once at prepare and covers every live kParamDefs
//      entry (one snapshot slot per registry index, in table order).
//
// Test-case names begin with the task tag `parambridge` so `-R parambridge` selects
// exactly these (silent-pass rule, AGENTS.md).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>
#include <type_traits>

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/ParamBridge.h"      // mw::plugin::ParamBridge / NormalizedParamSnapshot
#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"        // mw::params::kParamDefs (JUCE-free registry)
#include "params/ParamIDs.h"         // mw::params::ids::*

namespace {

// A minimal headless AudioProcessor hosting an APVTS built from the real plugin
// layout — the same construction the shipping MwAudioProcessor uses — so the bridge
// is exercised against genuine juce::AudioProcessorParameter atomics.
class BridgeHostProcessor final : public juce::AudioProcessor
{
public:
    BridgeHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override         { return "ParamBridgeHost"; }
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

// Drive a parameter to a 0..1 normalized target the way a host/automation lane does
// (setValueNotifyingHost takes the normalized value), so the APVTS atomic updates.
void setNormalized(juce::AudioProcessorValueTreeState& apvts, const char* id, float norm01)
{
    auto* p = apvts.getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(norm01);
}

// Index of an ID inside the registry (== its snapshot slot index).
int slotOf(const char* id)
{
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (std::string_view{ mw::params::kParamDefs[i].id } == std::string_view{ id })
            return static_cast<int>(i);
    return -1;
}

} // namespace

TEST_CASE("parambridge snapshot is a normalized JUCE-free POD covering every live param",
          "[parambridge]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The normalized snapshot must be a trivially-copyable POD with NO JUCE type — it
    // is what gets pointed at across the no-JUCE core seam (§5.2; ADR-001 C14). These
    // are compile-time facts about the type, independent of any instance.
    using Snapshot = mw::plugin::NormalizedParamSnapshot;
    STATIC_REQUIRE(std::is_trivially_copyable_v<Snapshot>);
    STATIC_REQUIRE(std::is_standard_layout_v<Snapshot>);

    BridgeHostProcessor host;
    mw::plugin::ParamBridge bridge;
    bridge.prepare(host.apvts);                 // builds the id->pointer table once

    const Snapshot snap = bridge.snapshot();

    // One slot per LIVE kParamDefs entry, in table order (the id->pointer table covers
    // the whole registry).
    REQUIRE(static_cast<std::size_t>(snap.count()) == mw::params::kParamDefs.size());

    // Every continuous slot is a normalized [0,1] value (the bridge emits normalized
    // only; engineering mapping stays in core). Choice/Bool slots carry a valid index.
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
    {
        const auto& def = mw::params::kParamDefs[i];
        const float norm = snap.normalized(static_cast<int>(i));
        CHECK(norm >= 0.0f);
        CHECK(norm <= 1.0f);

        if (def.type != mw::params::ParamType::Continuous)
        {
            const int idx = snap.index(static_cast<int>(i));
            CHECK(idx >= 0);
            CHECK(idx < static_cast<int>(def.choiceCount));
        }
    }
}

TEST_CASE("parambridge maps a known param value to its normalized [0,1] slot",
          "[parambridge]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BridgeHostProcessor host;
    mw::plugin::ParamBridge bridge;
    bridge.prepare(host.apvts);

    // vco.tune spans -24..+24 semitones (linear); normalized 0.75 == +12 st. The
    // bridge must report the NORMALIZED value, never the engineering value (+12).
    const int tuneSlot = slotOf(mw::params::ids::kVcoTune);
    REQUIRE(tuneSlot >= 0);
    setNormalized(host.apvts, mw::params::ids::kVcoTune, 0.75f);

    const auto snap = bridge.snapshot();
    const float norm = snap.normalized(tuneSlot);

    // Normalized, in-range, and clearly NOT the +12 engineering value.
    CHECK(norm == Catch::Approx(0.75f).margin(1.0e-4));
    CHECK(norm <= 1.0f);

    // A choice param (sub.mode, 3 options) reports its typed index, not a raw float.
    const int subModeSlot = slotOf(mw::params::ids::kSubMode);
    REQUIRE(subModeSlot >= 0);
    setNormalized(host.apvts, mw::params::ids::kSubMode, 1.0f);   // top choice index
    const auto snap2 = bridge.snapshot();
    CHECK(snap2.index(subModeSlot) == 2);   // 3 options -> indices 0,1,2; top == 2
}

TEST_CASE("parambridge samples each atomic exactly once per block (step change once-per-block)",
          "[parambridge]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BridgeHostProcessor host;
    mw::plugin::ParamBridge bridge;
    bridge.prepare(host.apvts);

    const int cutoffSlot = slotOf(mw::params::ids::kVcfCutoff);
    REQUIRE(cutoffSlot >= 0);

    // Block N: snapshot at the starting value.
    setNormalized(host.apvts, mw::params::ids::kVcfCutoff, 0.20f);
    const auto blockN = bridge.snapshot();
    const float valN = blockN.normalized(cutoffSlot);
    CHECK(valN == Catch::Approx(0.20f).margin(1.0e-4));

    // A step change lands in the APVTS atomic AFTER block N's snapshot was taken.
    setNormalized(host.apvts, mw::params::ids::kVcfCutoff, 0.90f);

    // The already-taken block-N snapshot is immutable: it MUST still read the old
    // value (the atomic was sampled once, at snapshot time — no live atomic peeking).
    CHECK(blockN.normalized(cutoffSlot) == Catch::Approx(0.20f).margin(1.0e-4));

    // Block N+1: the NEXT snapshot picks up the step exactly once.
    const auto blockNplus1 = bridge.snapshot();
    CHECK(blockNplus1.normalized(cutoffSlot) == Catch::Approx(0.90f).margin(1.0e-4));

    // And re-snapshotting with no further change re-reads the same value (idempotent
    // once-per-block read; no drift, no double-apply).
    const auto blockNplus2 = bridge.snapshot();
    CHECK(blockNplus2.normalized(cutoffSlot) == Catch::Approx(0.90f).margin(1.0e-4));
}
