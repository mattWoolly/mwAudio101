// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/PluginProcessor.h — MwAudioProcessor, the single JUCE AudioProcessor over the
// shared mw::Engine. THE PROCESSOR CONVERGENCE NODE (task 111).
//
// This is the full §3.1 processor shell: ONE AudioProcessor, ONE mw::Engine, ONE DSP
// path for every wrapper format [docs/design/09 §3.1; ADR-001 C14; ADR-011 Decision —
// no DSP fork across formats]. It owns and wires the already-built plugin components
// (it does NOT reimplement any of them):
//
//   - mw::Engine                       the JUCE-free DSP core, driven through the
//                                       three-call seam prepare/process/reset ONLY
//                                       [docs/design/00 §5.1; ADR-001 C2-C5].
//   - ParamBridge + ParamSnapshot      the full 91-param APVTS<->normalized-POD bridge;
//                                       the once-per-block atomic snapshot feeds
//                                       BlockContext.params [docs/design/00 §5.4;
//                                       ADR-001 C7]. (replaces the bootstrap single-gain
//                                       stub.)
//   - MidiFrontEnd + CcLearnMap        the note/gate/bend/pressure/CC translator that
//                                       drains the JUCE MidiBuffer + the resolved
//                                       note-expression rung into the lock-free
//                                       NormalizedEventBuffer [docs/design/09 §4/§6].
//   - EventTranslator                  the field-for-field HostEvent -> mw::MidiEvent
//                                       boundary crossing into the pre-sized core-event
//                                       scratch [docs/design/09 §3.3; ADR-011 C11].
//   - NormalizedEventBuffer            the fixed-capacity, drop-never-grow lock-free
//                                       event surface; sole alloc in prepare
//                                       [docs/design/09 §3.2; ADR-011 C9; ADR-024 C7].
//   - CapabilityShim                   the per-format note-expr/transport rung resolve +
//                                       per-block branch-free transport recheck
//                                       [docs/design/09 §7-§8; ADR-022].
//   - LatencyReporter                  the CONSTANT plugin-delay-compensation reporter;
//                                       setLatencySamples is called ONCE from prepare
//                                       with this constant and NEVER mutated from
//                                       processBlock [docs/design/09 §8.3; ADR-017 L10].
//   - PresetManager                    consumes Program Change for preset recall on the
//                                       message thread; the audio thread only ever sees
//                                       PODs [docs/design/06 §10; ADR-008 C19].
//
// REAL-TIME CONTRACT [docs/design/09 §1.3; ADR-011 C9; ADR-024 C7]. prepareToPlay is the
// SOLE allocation/sizing site (Engine::prepare, the NormalizedEventBuffer, the core-event
// scratch, the ParamBridge table, the LatencyReporter padding). processBlock performs
// ZERO heap allocation and takes NO lock: it drains into the pre-sized buffers, reads the
// CcLearnMap through its single atomic-pointer load, snapshots the APVTS atomics once, and
// drives Engine::process. The constant latency is declared in prepare and never touched
// from the audio thread.
//
// OUT OF SCOPE (other tasks): the Engine DSP internals (full-engine); the format-target
// CMake wiring (task 113); the real UI editor (task 114 — the GenericAudioProcessorEditor
// is retained so the Standalone still builds).

#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine.h"          // mwcore (JUCE-free); the three-call seam
#include "BlockContext.h"    // mwcore POD seam: AudioBlockView / TransportInfo / MidiEvent
#include "params/ParamSnapshot.h"  // mwcore (JUCE-free): mw::ParamSnapshot (BlockContext.params)

#include "host/HostEvent.h"          // NormalizedEventBuffer / HostEvent (§3.2)
#include "host/Capabilities.h"       // PluginFormat / ResolvedCapabilities (§7.2)
#include "host/CapabilityShim.h"     // per-format rung resolve + per-block recheck (112)
#include "midi/CcLearnMap.h"         // double-buffered CC/learn map (§6.3)
#include "midi/MidiFrontEnd.h"       // JUCE MidiBuffer -> NormalizedEventBuffer (104)
#include "params/ParamBridge.h"      // APVTS <-> NormalizedParamSnapshot marshaller (102)
#include "latency/LatencyReporter.h" // constant PDC reporter (105; ADR-017 L10)
#include "preset/PresetManager.h"    // factory preset bank + ProgramChange recall (119)

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

    // --- Editor (trivial generic editor; the real UI is task 114) -----------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- Identity -----------------------------------------------------------------
    const juce::String getName() const override { return "mwAudio101"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // --- Programs (preset bank surfaced as host programs) -------------------------
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // --- State (APVTS round-trip via the canonical serializer) --------------------
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }

    // --- Test introspection (NO audio-thread mutation; for tests/plugin only) -----
    [[nodiscard]] const mw::Engine& engineForTest() const noexcept { return engine_; }
    [[nodiscard]] int  processCallCountForTest() const noexcept { return processCalls_; }
    [[nodiscard]] int  engineResetCountForTest() const noexcept { return resetCalls_; }
    // The pre-sized core-event scratch capacity (== assigned size; deterministic, unlike
    // vector::capacity()). The host-event surface is sized to the same §3.2 capacity.
    [[nodiscard]] int  eventBufferCapacityForTest() const noexcept {
        return static_cast<int>(events_.size());
    }
    [[nodiscard]] int  lastCoreEventCountForTest() const noexcept { return lastCoreEventCount_; }
    [[nodiscard]] int  lastProgramChangeForTest() const noexcept { return lastProgramChange_; }
    [[nodiscard]] int  latencySetCountForTest() const noexcept { return latencySetCalls_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Map JUCE's wrapper type to the §8.2 PluginFormat enum the CapabilityShim resolves
    // against. Pure; runs at prepare time (off the audio thread).
    [[nodiscard]] PluginFormat resolveFormat() const noexcept;

    // Declare the constant PDC to the host. Called ONLY from prepareToPlay [ADR-017 L10].
    void declareLatencyFromPrepare(double sampleRate);

    // The single JUCE-free DSP core, driven through prepare/process/reset only.
    mw::Engine engine_{};

    // The full APVTS layout (91 live params, built from the JUCE-free registry).
    juce::AudioProcessorValueTreeState apvts_;

    // --- Wired plugin components --------------------------------------------------
    ParamBridge      paramBridge_{};   // APVTS -> normalized POD (once-per-block snapshot)
    MidiFrontEnd     midiFrontEnd_{};  // JUCE MidiBuffer -> NormalizedEventBuffer
    CcLearnMap       ccLearnMap_{};    // §6.2-seeded CC -> param-index map (lock-free)
    CapabilityShim   capabilityShim_{};// per-format rung resolve + per-block recheck
    LatencyReporter  latencyReporter_{};// constant PDC reporter
    preset::PresetManager presetManager_{}; // factory bank + ProgramChange recall

    // The resolved per-format note-expression rung (set once in prepare); the transport
    // rung is rechecked per block on the audio thread.
    ResolvedCapabilities resolvedCaps_{ NoteExpressionRung::Collapsed, TransportRung::FreeRun };

    // The fixed-capacity, drop-never-grow lock-free host-event surface (§3.2). Sized in
    // prepareToPlay; drained in processBlock; never grown on the audio thread.
    NormalizedEventBuffer hostEvents_{};

    // Pre-sized core-event scratch the EventTranslator writes into (HostEvent ->
    // mw::MidiEvent). Reserved in prepareToPlay; never grown from processBlock.
    std::vector<mw::MidiEvent> events_{};

    // The once-per-block normalized parameter snapshot the engine reads through
    // BlockContext.params. A stable member so the borrowed pointer outlives process().
    // Filled off the host's automation atomics each block (one atomic read per param).
    mw::ParamSnapshot paramSnapshot_{};

    // The constant worst-case latency declared to the host (base-rate samples). Cached
    // in prepare; never recomputed on the audio thread [ADR-017 L10].
    int reportedLatencySamples_ = 0;

    // --- Preset recall handoff (audio thread -> message thread) -------------------
    // processBlock captures the LAST Program Change number seen this block into this
    // atomic (a POD write, no lock); the message thread applies the recall. The audio
    // thread NEVER calls into PresetManager / APVTS replaceState.
    std::atomic<int> pendingProgramChange_{ -1 };
    int currentProgram_ = 0;

    // --- Test counters (audio-thread writes are plain; read off-thread in tests) ---
    int processCalls_       = 0;
    int resetCalls_         = 0;
    int latencySetCalls_    = 0;
    int lastCoreEventCount_ = 0;
    int lastProgramChange_  = -1;

    // The voice cap the engine is prepared with (bootstrap value; the real cap is a
    // param in the voice stream).
    static constexpr int kMaxVoices = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MwAudioProcessor)
};

} // namespace mw::plugin
