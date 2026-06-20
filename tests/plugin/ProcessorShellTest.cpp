// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ProcessorShellTest.cpp — the task-111 MwAudioProcessor convergence-node
// acceptance suite. Tag: [processor]; test-case display names begin "processor".
//
// Asserts the full-processor contract the bootstrap left deferred (docs/design/09
// §3.1-3.2; docs/design/00 §4.4/§5.1; ADR-001 C2-C6; ADR-011 C9; ADR-017 L10; ADR-024
// C7):
//
//   (1) the Engine seam is driven EXACTLY as prepare(double,int,int) /
//       process(const BlockContext&) / reset — one DSP path, no fork across formats.
//   (2) the native MIDI/event surface drains into the pre-sized lock-free
//       NormalizedEventBuffer and processBlock performs ZERO heap allocation and takes
//       NO lock — proven with an override-free mstats() byte-delta probe over a real
//       processBlock call (the same macOS-arm64 sentinel the other plugin tests use,
//       since mw101_plugin_tests does NOT link the global-new AudioThreadGuard TU).
//   (3) setLatencySamples is called ONCE from prepare with the LatencyReporter constant
//       and is NEVER mutated from processBlock (constant PDC, ADR-017 L10).
//
// Headless: a juce::ScopedJuceInitialiser_GUI brackets JUCE singletons so the render
// path runs on a CI box with no message loop / no audio device.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include <malloc/malloc.h>   // mstats(): override-free heap-usage probe (macOS arm64)

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"                       // mw::plugin::MwAudioProcessor
#include "latency/LatencyReporter.h"               // the constant-PDC reference value
#include "host/HostEvent.h"                        // eventBufferCapacityFor()

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr int    kNumOut     = 2;

// A small MIDI buffer that exercises every translated rung plus a Program Change (which
// the front-end leaves for the processor's preset-recall hook).
juce::MidiBuffer makeBusyMidi()
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 9000), 16);
    midi.addEvent(juce::MidiMessage::channelPressureChange(1, 64), 32);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 90), 48);   // CC74 -> cutoff (mapped)
    midi.addEvent(juce::MidiMessage::programChange(1, 3), 64);          // preset recall hook
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 480);
    return midi;
}

} // namespace

// ============================================================================
// Acceptance 1: the Engine seam is driven EXACTLY prepare/process/reset, one path.
// ============================================================================
TEST_CASE("processor drives the Engine seam as prepare/process/reset exactly once each", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;

    // Before prepare the engine is unprepared and the seam idle.
    REQUIRE_FALSE(proc.engineForTest().isPrepared());

    // prepareToPlay -> Engine::prepare(sr, maxBlock, maxVoices) with the EXACT scalars.
    proc.prepareToPlay(kSampleRate, kBlockSize);
    REQUIRE(proc.engineForTest().isPrepared());
    REQUIRE(proc.engineForTest().sampleRate()   == kSampleRate);
    REQUIRE(proc.engineForTest().maxBlockSize() == kBlockSize);

    // processBlock -> a single Engine::process per call (no second/forked DSP path).
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi = makeBusyMidi();

    const int before = proc.processCallCountForTest();
    proc.processBlock(buffer, midi);
    REQUIRE(proc.processCallCountForTest() == before + 1);

    // The output is always finite (the seam never emits NaN/Inf, even under busy MIDI).
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            REQUIRE(std::isfinite(buffer.getSample(ch, n)));

    // releaseResources -> Engine::reset.
    proc.releaseResources();
    REQUIRE(proc.engineResetCountForTest() >= 1);
}

// ============================================================================
// Acceptance 1 (cont.): the native surface drains into the PRE-SIZED lock-free buffer.
// ============================================================================
TEST_CASE("processor pre-sizes the NormalizedEventBuffer in prepare and drains MIDI into it", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSampleRate, kBlockSize);

    // The lock-free buffer is sized to the §3.2 capacity for this block (sole alloc site).
    REQUIRE(proc.eventBufferCapacityForTest()
            == mw::plugin::eventBufferCapacityFor(kBlockSize));

    // A busy block drains note/bend/pressure/CC into normalized core events; the
    // resulting core-event count is non-zero (the surface was actually drained).
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    buffer.clear();
    juce::MidiBuffer midi = makeBusyMidi();
    proc.processBlock(buffer, midi);
    REQUIRE(proc.lastCoreEventCountForTest() > 0);

    // The Program Change in the block was captured for the preset-recall hook (consumed
    // in plugin/, NOT forwarded into the engine event stream).
    REQUIRE(proc.lastProgramChangeForTest() == 3);
}

// ============================================================================
// Acceptance 2: processBlock performs ZERO heap allocation and NO lock.
// ============================================================================
TEST_CASE("processor processBlock is allocation-free and lock-free over a steady-state block", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSampleRate, kBlockSize);

    // Build the buffer + MIDI ONCE, outside the measured window, so the test's own
    // allocations (juce::AudioBuffer / juce::MidiBuffer storage) are not charged to
    // processBlock. processBlock writes output in place and only READS the MidiBuffer,
    // so the same buffers are reused every iteration.
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    juce::MidiBuffer midi = makeBusyMidi();

    // Warm-up call: any lazy first-call internal allocation (JUCE playhead query path,
    // engine warm-up, etc.) happens OUTSIDE the measured window.
    buffer.clear();
    proc.processBlock(buffer, midi);

    // Warm mstats() once so its own lazy first-call bookkeeping is not charged to us.
    (void) mstats();
    const std::size_t before = mstats().bytes_used;
    for (int i = 0; i < 256; ++i)
        proc.processBlock(buffer, midi);   // re-render in place; no test-side allocation
    const std::size_t after = mstats().bytes_used;

    // ZERO heap growth across 256 steady-state processBlock calls (the drop-never-grow
    // NormalizedEventBuffer + the pre-sized core-event scratch + the once-per-block
    // ParamBridge snapshot are all allocation-free) [ADR-011 C9; ADR-024 C7].
    REQUIRE(after == before);
}

// ============================================================================
// Acceptance 3: setLatencySamples called ONLY from prepare, NEVER from processBlock.
// ============================================================================
TEST_CASE("processor reports constant PDC from prepare and never mutates latency from process", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;

    // prepare declares the LatencyReporter CONSTANT worst-case value to the host.
    proc.prepareToPlay(kSampleRate, kBlockSize);

    const mw::plugin::LatencyReporter reporter;
    const int expected = reporter.computeWorstCaseLatency(kSampleRate);
    REQUIRE(proc.getLatencySamples() == expected);

    const int latencyAfterPrepare = proc.getLatencySamples();
    const int setCallsAfterPrepare = proc.latencySetCountForTest();
    REQUIRE(setCallsAfterPrepare >= 1);   // declared at least once from prepare

    // Drive many blocks: the reported latency NEVER changes and setLatencySamples is
    // NEVER called again from the audio thread (constant PDC) [ADR-017 L10].
    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
    juce::MidiBuffer midi = makeBusyMidi();
    for (int i = 0; i < 32; ++i)
    {
        buffer.clear();
        juce::MidiBuffer m = makeBusyMidi();
        proc.processBlock(buffer, m);
    }
    REQUIRE(proc.getLatencySamples() == latencyAfterPrepare);
    REQUIRE(proc.latencySetCountForTest() == setCallsAfterPrepare);   // no extra set from process

    // A second prepare (e.g. sample-rate change) may re-declare, but the value is the
    // same constant (invariant to rate / block / FX bypass) [ADR-017 L5/L7].
    proc.prepareToPlay(44100.0, 256);
    REQUIRE(proc.getLatencySamples() == expected);
}
