// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/WrapperTargetTest.cpp — task-113 single-juce_add_plugin acceptance suite.
// Tag: [wrappers]; every test-case display name begins "wrappers" so `-R wrappers`
// selects exactly this suite (silent-pass rule, AGENTS.md "Tests").
//
// Task 113 is build-side wiring: one `juce_add_plugin(mwAudio101 ...)` target produces
// EVERY format (VST3 / AU / CLAP / Standalone / LV2) over the ONE shared
// `mw::plugin::MwAudioProcessor`, with no DSP fork [docs/design/09 §2.1, §3.1; ADR-011
// Decision + C11; ADR-024 C5]. The configure-time format-resolution / validator gate is
// covered by the `formats` / `validators` ctests (task 096); what THIS suite proves at
// runtime is the contract the build wiring exists to guarantee:
//
//   (A) No DSP fork — JUCE's plugin-instance factory `createPluginFilter()`, which is
//       the single entry point EVERY wrapper format (VST3/AU/CLAP/Standalone/LV2) calls
//       to obtain its processor, returns the one `MwAudioProcessor` type. There is no
//       per-format processor subclass [ADR-011 C11; §3.1].
//
//   (B) The shared processor carries the format-agnostic synth identity the single
//       `juce_add_plugin(... IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE ...)` declaration sets:
//       it is a synth (not a MIDI effect) that accepts MIDI input. Because the identity
//       lives on the one processor, every wrapper inherits it verbatim [§2.1; §3.1].
//
//   (C) One processor -> one render path: the same processor instance the factory hands
//       to any wrapper drives the three-call seam headlessly to finite output, so the
//       macOS-arm64 bit-exact bless reference is shared across formats [ADR-011 C11].
//
// Headless: a juce::ScopedJuceInitialiser_GUI brackets JUCE singletons / leak detector
// so the render path runs on a CI box with no message loop and no audio device, exactly
// as tests/plugin/PluginHarnessTest.cpp establishes.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"   // mw::plugin::MwAudioProcessor

// JUCE's wrapper-shared plugin-instance factory. Declared by the JUCE plugin client
// machinery and defined once in plugin/PluginProcessor.cpp; every format wrapper calls
// it to build its processor. We call it directly to prove the no-fork contract.
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

bool bufferIsAllFinite(const juce::AudioBuffer<float>& buffer) noexcept
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (! std::isfinite(data[n]))
                return false;
    }
    return true;
}

} // namespace

// (A) No DSP fork: the wrapper-shared factory yields the one MwAudioProcessor type.
TEST_CASE("wrappers createPluginFilter returns the one shared MwAudioProcessor with no per-format fork",
          "[wrappers]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<juce::AudioProcessor> fromFactory(createPluginFilter());
    REQUIRE(fromFactory != nullptr);

    // The single entry point every wrapper format calls must hand back the ONE
    // shared processor type — not a per-format subclass [ADR-011 C11; §3.1].
    auto* shared = dynamic_cast<mw::plugin::MwAudioProcessor*>(fromFactory.get());
    REQUIRE(shared != nullptr);

    // And its runtime type is EXACTLY MwAudioProcessor (no derived per-format shell).
    REQUIRE(typeid(*fromFactory) == typeid(mw::plugin::MwAudioProcessor));
}

// (B) Format-agnostic synth identity set by the single juce_add_plugin declaration.
TEST_CASE("wrappers the shared processor is a MIDI-accepting synth, not a MIDI effect",
          "[wrappers]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    REQUIRE(p != nullptr);

    // juce_add_plugin(... IS_SYNTH TRUE  NEEDS_MIDI_INPUT TRUE  IS_MIDI_EFFECT FALSE ...).
    // The identity lives on the one processor, so every wrapper inherits it.
    CHECK(p->getName() == juce::String("mwAudio101"));
    CHECK(p->acceptsMidi());          // NEEDS_MIDI_INPUT TRUE
    CHECK_FALSE(p->producesMidi());   // NEEDS_MIDI_OUTPUT FALSE
    CHECK_FALSE(p->isMidiEffect());   // IS_MIDI_EFFECT FALSE
}

// (C) One processor -> one render path: drive the factory-built instance to finite out.
TEST_CASE("wrappers the factory-built processor drives one finite render path for every format",
          "[wrappers]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    REQUIRE(p != nullptr);

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;
    constexpr int    numCh      = 2;

    p->prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(numCh, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);

    p->processBlock(buffer, midi);

    // The single shared render path never emits NaN/Inf; this output is what every
    // wrapper format produces verbatim (no DSP fork) [ADR-011 C11].
    REQUIRE(bufferIsAllFinite(buffer));

    p->releaseResources();
}
