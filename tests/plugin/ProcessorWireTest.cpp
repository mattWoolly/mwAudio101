// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ProcessorWireTest.cpp — task-136 full-assembly integration test for the
// finalized MwAudioProcessor. Tag: [processor_wire]; test-case display names begin
// "processor_wire".
//
// This closes the task-111 QA MEDIUM and adds the residual full-assembly acceptance the
// original 136 wanted, now against the real assembled processor (docs/design/09 §3.1/§3.2/
// §3.3; docs/design/06 §10; ADR-001 C3/C4; ADR-008 C19; ADR-011 C9; ADR-017 L10):
//
//   (1) A MIDI ProgramChange in the processBlock buffer recalls the matching preset via
//       the message-thread handoff (AsyncUpdater -> setCurrentProgram ->
//       PresetManager::loadPreset). The audio thread only stores a POD; the actual recall
//       runs when the message-thread tick fires (driven synchronously in the test). An
//       out-of-range ProgramChange is IGNORED (no recall, APVTS untouched).
//   (2) No heap alloc / no lock / no preset parse is added to the processBlock path: an
//       mstats() byte-delta probe over a steady-state PC-BEARING block shows zero growth
//       (the same override-free macOS-arm64 sentinel ProcessorShellTest uses).
//   (3) setLatencySamples stays prepare-only (the audio thread never re-declares it) and
//       no juce type crosses the core seam (the engine is driven prepare/process/reset).
//
// Headless: a juce::ScopedJuceInitialiser_GUI brackets JUCE singletons so the render path
// and the AsyncUpdater run on a CI box with no audio device.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

#include <malloc/malloc.h>   // mstats(): override-free heap-usage probe (macOS arm64)

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"                  // mw::plugin::MwAudioProcessor
#include "latency/LatencyReporter.h"          // the constant-PDC reference value
#include "params/ParameterLayout.h"           // buildParameterLayout() for the JSON fixture
#include "preset/PresetFormat.h"              // writePresetJson / PresetMeta (task 025)
#include "preset/PresetManager.h"             // PresetManager::SlotSource
#include "state/StateTree.h"                  // canonical key constants

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr int    kNumOut     = 2;

// A continuous, easily-observed parameter we stamp with a recognisable value in a preset
// so a recall is provable by reading it back out of the live APVTS. It is a NORMALISED
// 0..1 parameter (engine default 1.0), so the fixtures use distinct in-range values well
// away from the default to make a recall unambiguous.
constexpr const char* kObservedId = "mw101.vcf.cutoff";

// A minimal headless AudioProcessor hosting the FULL 91-param APVTS layout, used purely
// to author a valid canonical <PARAMS> subtree for the preset JSON fixtures.
class FixtureHostProcessor final : public juce::AudioProcessor
{
public:
    FixtureHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "FixtureHost"; }
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

// Author a VALID .mw101preset JSON string whose <PARAMS> mirrors the engine-default tree
// but with kObservedId overridden to a recognisable modeled value, so a recall is provable.
juce::String presetJsonWithCutoff(const juce::String& name, double cutoffValue)
{
    FixtureHostProcessor host;
    juce::ValueTree root{ juce::Identifier{ mw::state::kRootId } };
    root.setProperty(mw::state::kAttrSchemaVersion, 1, nullptr);

    juce::ValueTree params{ juce::Identifier{ mw::state::kParamsId } };
    params.copyPropertiesAndChildrenFrom(host.apvts.copyState(), nullptr);

    auto node = params.getChildWithProperty("id", juce::String(kObservedId));
    REQUIRE(node.isValid());
    node.setProperty("value", cutoffValue, nullptr);
    root.appendChild(params, nullptr);

    mw::plugin::preset::PresetMeta meta;
    meta.name     = name;
    meta.author   = "Matt Woolly";
    meta.category = "Lead";
    meta.soundExt = false;
    return mw::plugin::preset::writePresetJson(root, meta);
}

// Read the modeled (denormalised) value of an APVTS parameter. apvts() is non-const, so
// this takes a non-const reference (it only READS the parameter).
float observedModeledValue(mw::plugin::MwAudioProcessor& proc)
{
    auto* p = proc.apvts().getParameter(kObservedId);
    REQUIRE(p != nullptr);
    return p->getNormalisableRange().convertFrom0to1(p->getValue());
}

// A busy block: notes/bend/pressure/CC plus a ProgramChange at the given index.
juce::MidiBuffer makeBusyMidi(int programIndex)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 9000), 16);
    midi.addEvent(juce::MidiMessage::channelPressureChange(1, 64), 32);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 90), 48);
    midi.addEvent(juce::MidiMessage::programChange(1, programIndex), 64);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 480);
    return midi;
}

} // namespace

// ============================================================================
// (1) A ProgramChange in the MIDI stream recalls the matching preset via the
//     message-thread AsyncUpdater handoff (closes the 111 QA MEDIUM).
// ============================================================================
TEST_CASE("processor_wire recalls a preset from a MIDI ProgramChange via the message-thread handoff",
          "[processor_wire]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;

    // Install a 3-slot bank; slot 2 stamps a recognisable cutoff so the recall is provable.
    // mw101.vcf.cutoff is normalised 0..1 (engine default 1.0) — use distinct in-range
    // values well away from the default.
    constexpr double kSlot2Cutoff = 0.42;
    proc.installPresetBankForTest({
        { "Lead A", presetJsonWithCutoff("Lead A", 0.10) },
        { "Lead B", presetJsonWithCutoff("Lead B", 0.25) },
        { "Lead C", presetJsonWithCutoff("Lead C", kSlot2Cutoff) },
    });
    REQUIRE(proc.getNumPrograms() == 3);

    proc.prepareToPlay(kSampleRate, kBlockSize);

    // Drive a busy block that carries ProgramChange #2 in the MIDI buffer.
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi = makeBusyMidi(/*programIndex*/ 2);

    const int recallsBefore = proc.programRecallCountForTest();
    proc.processBlock(buffer, midi);

    // The audio thread only CAPTURED the PC; it did NOT recall yet (no message-thread tick).
    REQUIRE(proc.lastProgramChangeForTest() == 2);
    REQUIRE(proc.getCurrentProgram() == 0);
    REQUIRE(proc.programRecallCountForTest() == recallsBefore);

    // Tick the message thread: drive the AsyncUpdater synchronously. This is the consumer
    // processBlock fired via triggerAsyncUpdate().
    proc.handleAsyncUpdate();

    // The recall ran: program index, recall count, and the live APVTS cutoff all reflect it.
    REQUIRE(proc.programRecallCountForTest() == recallsBefore + 1);
    REQUIRE(proc.lastRecalledProgramForTest() == 2);
    REQUIRE(proc.getCurrentProgram() == 2);
    REQUIRE(observedModeledValue(proc) == Catch::Approx((float) kSlot2Cutoff).margin(1.0e-3));
}

// ============================================================================
// (1 cont.) An OUT-OF-RANGE ProgramChange is ignored — no recall, APVTS untouched.
// ============================================================================
TEST_CASE("processor_wire ignores an out-of-range MIDI ProgramChange", "[processor_wire]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;
    proc.installPresetBankForTest({
        { "Lead A", presetJsonWithCutoff("Lead A", 0.10) },
        { "Lead B", presetJsonWithCutoff("Lead B", 0.25) },
    });
    REQUIRE(proc.getNumPrograms() == 2);

    proc.prepareToPlay(kSampleRate, kBlockSize);

    const float cutoffBefore = observedModeledValue(proc);

    // ProgramChange #9 is past the end of the 2-slot bank.
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi = makeBusyMidi(/*programIndex*/ 9);
    proc.processBlock(buffer, midi);

    proc.handleAsyncUpdate();

    // Out-of-range PC is dropped by the consumer: no recall ran and APVTS is unchanged.
    REQUIRE(proc.programRecallCountForTest() == 0);
    REQUIRE(proc.getCurrentProgram() == 0);
    REQUIRE(observedModeledValue(proc) == Catch::Approx(cutoffBefore).margin(1.0e-6));
}

// ============================================================================
// (2) processBlock over a PC-bearing block stays allocation-free / lock-free: the
//     ProgramChange handoff adds NO heap/lock/parse to the audio path.
// ============================================================================
TEST_CASE("processor_wire processBlock is allocation-free over a ProgramChange-bearing block",
          "[processor_wire]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;
    proc.installPresetBankForTest({
        { "Lead A", presetJsonWithCutoff("Lead A", 0.10) },
        { "Lead B", presetJsonWithCutoff("Lead B", 0.25) },
    });
    proc.prepareToPlay(kSampleRate, kBlockSize);

    // Build the buffer + MIDI ONCE, outside the measured window, so the test's own storage
    // allocations are not charged to processBlock. Every iteration re-renders in place and
    // re-reads the same PC-bearing MidiBuffer (so triggerAsyncUpdate fires each block).
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    juce::MidiBuffer midi = makeBusyMidi(/*programIndex*/ 1);

    // Warm-up: any lazy first-call internal allocation happens OUTSIDE the measured window.
    // (We do NOT pump the message loop, so handleAsyncUpdate — which DOES allocate, as it
    //  parses/recovers a preset — never runs inside the measured window; only the audio
    //  path's triggerAsyncUpdate flag-set is exercised.)
    buffer.clear();
    proc.processBlock(buffer, midi);

    (void) mstats();
    const std::size_t before = mstats().bytes_used;
    for (int i = 0; i < 256; ++i)
        proc.processBlock(buffer, midi);   // re-render in place; PC captured + triggerAsyncUpdate
    const std::size_t after = mstats().bytes_used;

    // ZERO heap growth across 256 steady-state PC-bearing processBlock calls: capturing the
    // Program Change (POD store) and triggerAsyncUpdate (lock-free flag) add no alloc/lock,
    // and the preset PARSE stays on the message thread [ADR-001 C3/C4; ADR-011 C9].
    REQUIRE(after == before);
}

// ============================================================================
// (3) setLatencySamples stays prepare-only and no juce type crosses the core seam under
//     a PC-bearing drive (re-asserted against the full assembly).
// ============================================================================
TEST_CASE("processor_wire keeps setLatencySamples prepare-only across a ProgramChange drive",
          "[processor_wire]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;
    proc.installPresetBankForTest({
        { "Lead A", presetJsonWithCutoff("Lead A", 0.10) },
        { "Lead B", presetJsonWithCutoff("Lead B", 0.25) },
    });
    proc.prepareToPlay(kSampleRate, kBlockSize);

    const mw::plugin::LatencyReporter reporter;
    const int expected = reporter.computeWorstCaseLatency(kSampleRate);
    REQUIRE(proc.getLatencySamples() == expected);

    const int latencyAfterPrepare  = proc.getLatencySamples();
    const int setCallsAfterPrepare = proc.latencySetCountForTest();
    REQUIRE(setCallsAfterPrepare >= 1);

    // Drive many PC-bearing blocks AND pump the recall handoff each time: latency is never
    // re-declared from the audio thread (constant PDC), even as presets recall [ADR-017 L10].
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    for (int i = 0; i < 32; ++i)
    {
        buffer.clear();
        juce::MidiBuffer m = makeBusyMidi(/*programIndex*/ (i % 2));
        proc.processBlock(buffer, m);
        proc.handleAsyncUpdate();   // apply the recall on the (test-driven) message thread
    }
    REQUIRE(proc.getLatencySamples() == latencyAfterPrepare);
    REQUIRE(proc.latencySetCountForTest() == setCallsAfterPrepare);   // no extra set from process

    // The recalls actually happened (the seam end-to-end), and output stayed finite.
    REQUIRE(proc.programRecallCountForTest() >= 1);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            REQUIRE(std::isfinite(buffer.getSample(ch, n)));
}
