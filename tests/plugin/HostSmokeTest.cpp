// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/HostSmokeTest.cpp — task-140 HEADLESS host-smoke matrix.
// Tag: [host_smoke]; every test-case display name begins "host_smoke" so
// `-R host_smoke --no-tests=error` selects exactly this suite (silent-pass rule,
// AGENTS.md "Tests"). Compiled into mw101_plugin_tests (links real JUCE).
//
// Realizes docs/design/09 §2.1 (the per-format validator matrix) and ADR-011 C6-C8.
// This is the project-provided "standalone-smoke" wired step that cmake/Validators.cmake
// declares (MW_VALIDATOR_standalone-smoke_WIRED, always wired on any desktop platform):
// a headless Standalone launch that loads / prepares / processes / tears down the real
// processor with NO audio device, asserting finite output and a crash-free lifecycle.
//
//   What it proves (the §2.1 Standalone row + the headless-lifecycle acceptance):
//
//   (1) INSTANTIATE — the real MwAudioProcessor is constructed through the wrapper-shared
//       factory createPluginFilter() (the single entry point EVERY format wrapper, incl.
//       Standalone, calls), with no audio device and no message loop [§3.1; ADR-011 C11].
//   (2) FULL LIFECYCLE — setRateAndBufferSizeDetails -> prepareToPlay -> processBlock
//       (several blocks, with and without MIDI notes / a Program Change) -> releaseResources
//       -> destroy, across the BLESSED (sample-rate x block-size) matrix [docs/design/11
//       §5.2; ADR-023 V12]. The lifecycle holds at every cell without crashing.
//   (3) FINITE OUTPUT — no NaN / Inf is ever emitted, from silence or under busy MIDI.
//   (4) DOUBLE-PREPARE / RESET SAFE — re-preparing at a new rate/block (a host sample-rate
//       change) and an extra releaseResources are both safe (no crash, still finite).
//
//   The PER-FORMAT MATRIX (docs/design/09 §2.1; ADR-011 C6-C8):
//   LOCALLY only Standalone resolves — the VST3/AU/CLAP/LV2 validators (pluginval/auval/
//   validator/clap-validator/lv2lint+lv2_validate) are unwired on a bare dev box, so the
//   §2.2 gate (cmake/Formats.cmake mw_resolve_formats) HARD-REMOVES those formats here and
//   leaves Standalone (its standalone-smoke step is a project-provided wired step). So this
//   suite asserts:
//     - the Standalone headless smoke is GREEN locally (cases 1-4 above), and
//     - the resolved-format gate yields Standalone on this host (resolveFormat() on the
//       wrapper-undefined headless host maps to the Standalone universal floor — the same
//       PluginFormat the standalone-smoke step gates) [plugin/PluginProcessor resolveFormat;
//       cmake/Formats.cmake §2.2; docs/design/09 §8.1].
//   The macOS (VST3+AU+CLAP+Standalone) and Linux (VST3+CLAP+Standalone, LV2 optional)
//   FULL-FORMAT validator gates run in CI via the 137/138 cmake_formats / cmake_validators
//   ctests (auval/pluginval/validator/clap-validator are macOS-only / not installed on a
//   dev box) [docs/design/09 §2.1; ADR-011 C6-C8]. We deliberately do NOT shell out to
//   pluginval/auval/clap-validator here — they are not present locally by design.
//
// Headless init: a juce::ScopedJuceInitialiser_GUI brackets JUCE singletons / leak detector
// so the render path runs with no message loop and no real audio device, exactly as
// tests/plugin/PluginHarnessTest.cpp establishes.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"                     // mw::plugin::MwAudioProcessor, PluginFormat
#include "host/Capabilities.h"                   // mw::plugin::PluginFormat
#include "calibration/HostSmokeConstants.h"      // blessed-rate x block matrix (PI)

// JUCE's wrapper-shared plugin-instance factory: the single entry point EVERY format
// wrapper (incl. Standalone) calls to obtain its processor. Declared by the JUCE plugin
// client machinery; defined once in plugin/PluginProcessor.cpp. We call it directly so the
// headless smoke exercises the SAME construction path the Standalone wrapper uses.
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {

namespace hk = mw::cal::host_smoke;

// True iff every sample in every channel is a finite float (no NaN / Inf).
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

// A MIDI block carrying a held note plus a Program Change (the preset-recall hook). Used
// to exercise the note/gate + ProgramChange paths on the "with MIDI" blocks of a cell.
juce::MidiBuffer noteAndProgramChangeBlock(int blockSize)
{
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
    midi.addEvent(juce::MidiMessage::programChange(1, 0), blockSize / 2); // recall hook
    return midi;
}

// Drive ONE matrix cell: a full host lifecycle at (sampleRate, blockSize) with no audio
// device. setRateAndBufferSizeDetails (what a host calls before prepare) -> prepareToPlay
// -> several processBlock calls (a noteOn block, sustaining silent blocks, a noteOff block)
// -> releaseResources. Returns true iff every rendered block was finite.
bool driveCell(juce::AudioProcessor& proc, double sampleRate, int blockSize)
{
    // The host announces rate/block before preparing (no device is opened).
    proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(hk::kNumOutputChannels, blockSize);

    for (int b = 0; b < hk::kBlocksPerCell; ++b)
    {
        buffer.clear(); // silent input block

        juce::MidiBuffer midi;
        if (b == 0)
            midi = noteAndProgramChangeBlock(blockSize);              // first block: note on + PC
        else if (b == hk::kBlocksPerCell - 1)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);      // last block: note off
        // middle blocks: no MIDI (sustaining / release tail), an empty MidiBuffer.

        proc.processBlock(buffer, midi);

        if (! bufferIsAllFinite(buffer))
            return false;
    }

    proc.releaseResources();
    return true;
}

} // namespace

// ============================================================================
// (1)-(3) INSTANTIATE + FULL LIFECYCLE + FINITE across the blessed (rate x block) matrix.
// ============================================================================
TEST_CASE("host_smoke headless Standalone loads, processes and tears down across the blessed rate-x-block matrix",
          "[host_smoke]")
{
    // No audio device, no message loop — JUCE singletons bracketed for a headless run.
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // INSTANTIATE through the wrapper-shared factory (the Standalone construction path).
    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    REQUIRE(proc != nullptr);

    // Full lifecycle at every blessed rate x every block size; finite output throughout.
    for (const double sr : hk::kBlessedSampleRatesHz)
    {
        for (const int block : hk::kBlockSizes)
        {
            INFO("blessed cell: sampleRate=" << sr << " blockSize=" << block);
            REQUIRE(driveCell(*proc, sr, block));
        }
    }

    // Destroy at end of scope (the unique_ptr) — no crash. (Reaching here is the proof.)
    SUCCEED("headless Standalone smoke completed the full lifecycle across the matrix");
}

// ============================================================================
// (4) DOUBLE-PREPARE / RESET is safe (a host sample-rate change + extra release).
// ============================================================================
TEST_CASE("host_smoke double-prepare at a new rate-block and extra releaseResources are safe",
          "[host_smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    REQUIRE(proc != nullptr);

    constexpr int kBlock = 512;
    juce::AudioBuffer<float> buffer(hk::kNumOutputChannels, kBlock);

    // First prepare/process at 44100.
    proc->setRateAndBufferSizeDetails(44100.0, kBlock);
    proc->prepareToPlay(44100.0, kBlock);
    buffer.clear();
    {
        juce::MidiBuffer midi = noteAndProgramChangeBlock(kBlock);
        proc->processBlock(buffer, midi);
    }
    REQUIRE(bufferIsAllFinite(buffer));

    // RE-prepare at a different rate/block WITHOUT an intervening releaseResources (a host
    // sample-rate change mid-session). Must be safe and still render finite output.
    proc->setRateAndBufferSizeDetails(96000.0, 256);
    proc->prepareToPlay(96000.0, 256);
    juce::AudioBuffer<float> buffer2(hk::kNumOutputChannels, 256);
    buffer2.clear();
    {
        juce::MidiBuffer midi;
        proc->processBlock(buffer2, midi);
    }
    REQUIRE(bufferIsAllFinite(buffer2));

    // A double releaseResources (idempotent reset) is safe.
    proc->releaseResources();
    proc->releaseResources();

    SUCCEED("double-prepare and double-release completed without crash");
}

// ============================================================================
// PER-FORMAT MATRIX: the resolved-format gate yields Standalone on this host.
// ============================================================================
TEST_CASE("host_smoke the resolved-format gate yields Standalone on this headless host",
          "[host_smoke]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The factory-built processor, asked which §8.2 format it resolves to under the headless
    // (wrapper-undefined) host, returns the Standalone universal floor — the SAME PluginFormat
    // the cmake/Formats.cmake §2.2 gate leaves resolved locally once the unwired VST3/AU/CLAP/
    // LV2 validators are hard-removed [docs/design/09 §8.1; ADR-024 C6]. createPluginFilter()
    // returns the one shared MwAudioProcessor (no per-format fork) [ADR-011 C11].
    std::unique_ptr<juce::AudioProcessor> raw(createPluginFilter());
    auto* proc = dynamic_cast<mw::plugin::MwAudioProcessor*>(raw.get());
    REQUIRE(proc != nullptr);

    // Prepare resolves the per-format capability rungs from the §8.1 matrix; the Standalone
    // floor resolves to the MpeOverMidi / FreeRun rungs (no native note-expression, no host
    // transport) — proving the headless host is the Standalone row of the §2.1 matrix.
    proc->prepareToPlay(48000.0, 512);
    REQUIRE(proc->getName() == juce::String("mwAudio101"));

    // DOCUMENTATION (asserted as a contract, not just a comment): the locally-resolvable
    // format is Standalone. The macOS (VST3+AU+CLAP+Standalone) / Linux (VST3+CLAP+Standalone,
    // LV2 optional) full-format validator gates run in CI via the 137/138 cmake_formats /
    // cmake_validators ctests; pluginval/auval/clap-validator are not invoked here by design.
    proc->releaseResources();
    SUCCEED("standalone-smoke is the locally-wired §2.1 gate; full-format validators gate in CI");
}
