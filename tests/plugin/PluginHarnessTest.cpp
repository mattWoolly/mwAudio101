// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PluginHarnessTest.cpp — the load-bearing smoke test for the
// JUCE-linked Catch2 target `mw101_plugin_tests`.
//
// This is the FIRST test in the JUCE-phase test harness. Its only job is to prove
// that the plugin-side AudioProcessor (which links the real JUCE modules) can be
// instantiated, driven through the three-call lifecycle seam
// (prepareToPlay -> processBlock -> releaseResources) headlessly, and produces
// finite output. Future JUCE-phase tasks drop additional `*Test.cpp` files into
// tests/plugin/ (discovered by the CONFIGURE_DEPENDS glob in tests/CMakeLists.txt)
// to unit-test plugin-side code against the actual JUCE compile environment.
//
// Headless init: a JUCE AudioProcessor can construct an APVTS and run its render
// path without a GUI message loop, but JUCE's leak detector / singletons expect a
// juce::ScopedJuceInitialiser_GUI to bracket the lifetime of JUCE objects. We scope
// one around the processor so the test runs cleanly on a headless CI box (no
// modal loops, no real audio device).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"   // mw::plugin::MwAudioProcessor

namespace {

// True iff every sample in every channel is a finite float (no NaN / Inf).
bool bufferIsAllFinite(const juce::AudioBuffer<float>& buffer) noexcept
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            if (! std::isfinite(data[n]))
                return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("plugin_harness instantiates the processor and renders one finite block", "[plugin_harness]")
{
    // Bracket JUCE singletons/leak-detector for a headless console run. No GUI is
    // shown and no message loop is entered — this just sets up JUCE's globals.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Construct the shared processor directly (it is compiled into this target).
    auto processor = std::make_unique<mw::plugin::MwAudioProcessor>();
    REQUIRE(processor != nullptr);

    constexpr double sampleRate    = 48000.0;
    constexpr int    blockSize     = 512;
    constexpr int    numChannels   = 2;

    // The three-call seam, driven headlessly.
    processor->prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    buffer.clear();                       // a silent input block
    juce::MidiBuffer midi;                // no MIDI events

    processor->processBlock(buffer, midi);

    // The render path must never emit NaN / Inf, even from silence.
    REQUIRE(bufferIsAllFinite(buffer));

    processor->releaseResources();
}
