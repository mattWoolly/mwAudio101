// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/PluginProcessor.h — MwAudioProcessor, the single JUCE AudioProcessor over
// the shared mw::Engine (task 111, MINIMAL-bootstrap version).
//
// BOOTSTRAP SCOPE: this is the thin JUCE shell from docs/design/09 §3.1, reduced to
// the minimum that proves the build+link works: it owns a mw::Engine, drives the
// three-call seam [docs/design/00 §5.1; ADR-001 C2-C5] —
//   prepareToPlay(sr, block) -> engine.prepare(sr, maxBlockSize=block, maxVoices=kMaxVoices)
//   processBlock(buffer, midi) -> translate JUCE MIDI + playhead into the core POD
//                                  BlockContext / mw::MidiEvent seam, call
//                                  engine.process(ctx), write the engine's stereo
//                                  output into the host buffer channels
//   releaseResources()        -> engine.reset()
// — and exposes a MINIMAL APVTS (a single master-gain param) and a trivial generic
// editor. The core render path is JUCE-free and format-agnostic (ADR-001 C1/C14).
//
// DEFERRED to leaf tasks (NOT implemented here):
//   - full 91-param APVTS layout ........................ task 020
//   - MidiFrontEnd / HostEventNormalizer (CC/learn map,
//     MPE, note-expression rungs) ........................ task 104 / 111 (full)
//   - CapabilityShim (per-format transport/note-expr) ... task 102
//   - LatencyReporter + setLatencySamples (PDC) ......... task 105
//   - the real UI (replaces GenericAudioProcessorEditor) . ui stream (108+)
//   - the lock-free NormalizedEventBuffer + the compiled
//     no-allocation assertion around processBlock ........ task 111 (full)
//
// The minimal MIDI translation here is a direct, allocation-free JUCE-MidiBuffer ->
// mw::MidiEvent walk into a pre-sized member vector (reserved in prepareToPlay), a
// stand-in for the full §3.2 NormalizedEventBuffer. Only NoteOn/NoteOff/PitchBend/
// pressure/CC are mapped; the full CC->param learn map is task 104.

#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine.h"          // mwcore (JUCE-free); the three-call seam
#include "BlockContext.h"    // mwcore POD seam: AudioBlockView / TransportInfo / MidiEvent

namespace mw::plugin {

class MwAudioProcessor final : public juce::AudioProcessor {
public:
    MwAudioProcessor();
    ~MwAudioProcessor() override = default;

    // --- AudioProcessor lifecycle (the three-call seam) ---------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    using juce::AudioProcessor::processBlock; // bring in the double-precision overload

    bool isBusesLayoutSupported(const BusesLayout&) const override;

    // --- Editor (trivial generic editor for the bootstrap) ------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- Identity -----------------------------------------------------------------
    const juce::String getName() const override { return "mwAudio101"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // --- Programs (single empty program in the bootstrap) -------------------------
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // --- State (APVTS round-trip; full 91-param layout is task 020) ---------------
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // The single JUCE-free DSP core, driven through prepare/process/reset only.
    mw::Engine engine_{};

    // MINIMAL parameter state: one master-gain param. The full §2/§3.0 layout (91
    // live IDs) is task 020 — OUT OF SCOPE here.
    juce::AudioProcessorValueTreeState apvts_;

    // Pre-sized scratch for the JUCE-MidiBuffer -> mw::MidiEvent translation. Sized
    // in prepareToPlay; never grown from processBlock (stand-in for the §3.2
    // NormalizedEventBuffer; ADR-011 C9). Mutable size only; reserve happens off the
    // audio thread.
    std::vector<mw::MidiEvent> events_{};

    // The voice cap the engine is prepared with (bootstrap value; the real cap is a
    // param in the voice stream).
    static constexpr int kMaxVoices = 8;

    // The render-time oversampling factor handed to the seam (2x per the seam doc;
    // the engine clamps to 1x above its OS ceiling internally) [§8.5; ADR-023].
    static constexpr int kOversample = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MwAudioProcessor)
};

} // namespace mw::plugin
