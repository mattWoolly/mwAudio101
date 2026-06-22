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

#include "ui/Telemetry.h"            // mwcore (JUCE-free): audio->GUI SPSC telemetry (107)
#include "state/Extras.h"            // mwcore (JUCE-free): <extras> SeqStep[100] pattern POD (017)
#include "state/SeqPatternHandoff.h" // message->audio RT-safe seq-pattern double-buffer (111c)

namespace mw::plugin {

// Inherits juce::AsyncUpdater so a MIDI Program Change captured on the AUDIO thread
// (a lock-free POD store into pendingProgramChange_) is handed off to the MESSAGE
// thread for the actual preset recall: processBlock fires triggerAsyncUpdate() (a
// lock-free flag — no alloc/lock), and handleAsyncUpdate() reads+clears the atomic and
// applies the recall via setCurrentProgram -> PresetManager. The audio thread NEVER
// parses a preset or touches APVTS/PresetManager [docs/design/09 §3.2/§3.3; docs/design/
// 06 §10; ADR-001 C3/C4; ADR-008 C19].
class MwAudioProcessor final : public juce::AudioProcessor,
                               public juce::AsyncUpdater {
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

    // --- MIDI Program Change -> preset recall (audio -> message thread handoff) ----
    // Called on the MESSAGE thread by JUCE when processBlock fired triggerAsyncUpdate()
    // after seeing a Program Change. Reads + clears the audio-thread atomic, clamps the
    // index to [0, getNumPrograms()) and applies the recall via setCurrentProgram (which
    // routes to PresetManager::loadPreset). An out-of-range / stale index is ignored.
    // This is the SOLE consumer of pendingProgramChange_; the audio thread only ever
    // writes the POD [docs/design/09 §3.3; docs/design/06 §10; ADR-008 C19].
    void handleAsyncUpdate() override;

    // --- State (APVTS round-trip via the canonical serializer) --------------------
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() noexcept { return apvts_; }

    // --- Preset bank accessor (the narrow §9.1 ownership seam, tasks 128 / 114c) ----
    // The PresetManager lives in the PROCESSOR (§9.1); the editor's PresetBrowser (128) is
    // a THIN VIEW that holds a reference to it and only lists/filters/loads. The editor
    // assembly task (114c) needs this reference to CONSTRUCT the browser member — the
    // browser ctor takes a PresetManager&, so unlike the optional TransportModeBar
    // callback seams it cannot be deferred. The actual recall still routes through the
    // message-thread setCurrentProgram()/loadPreset() path; this accessor only exposes the
    // bank for the view's read/list surface, never an audio-thread handle
    // [docs/design/10-ui.md §9.1, §9.2; ADR-015 C6]. Message-thread only.
    [[nodiscard]] preset::PresetManager& presetManager() noexcept { return presetManager_; }

    // --- Editor size persistence (the narrow <extras>-UI seam, task 114) ----------
    // The editor root (MwAudioEditor) reads the stored size on construction and writes
    // the new size back on resize. The size genuinely PERSISTS: getStateInformation
    // writes it into the canonical <extras> uiWidth/uiHeight keys and setStateInformation
    // reads it back, so the last window size round-trips on session reload
    // [docs/design/10-ui.md §4.4; ADR-015 C2; ADR-008 §4/§5 — UI size is a <extras> UI
    // preference, NOT a host parameter]. Message-thread only; the audio thread never
    // touches this. A zero/default Point means "no stored size yet" -> default scale.
    [[nodiscard]] juce::Point<int> getStoredEditorSize() const noexcept { return storedEditorSize_; }
    void setStoredEditorSize(juce::Point<int> sizePx) noexcept { storedEditorSize_ = sizePx; }

    // --- Reduce-motion / low-CPU preference persistence (the <extras>-UI seam, 115) ---
    // The editor's reduce-motion toggle (a UI PREFERENCE, not a host parameter) restores
    // from this accessor on construction and writes back through it when toggled.
    // getStateInformation persists it into the canonical <extras> subtree and
    // setStateInformation reads it back, so it round-trips on session reload exactly like
    // the editor size [docs/design/10-ui.md §10; ADR-015 C8; ADR-008 §4/§5]. Message-
    // thread only; the audio thread never touches it. Default false (animation on).
    [[nodiscard]] bool getStoredReduceMotion() const noexcept { return storedReduceMotion_; }
    void setStoredReduceMotion(bool reduceMotion) noexcept { storedReduceMotion_ = reduceMotion; }

    // --- OpenGL render-backend opt-in persistence (the <extras>-UI seam, task 130) ----
    // The editor's OpenGL escape hatch is OFF by default (software/CPU render is primary);
    // it attaches a juce::OpenGLContext ONLY when this explicit advanced opt-in is ON. The
    // opt-in is a UI PREFERENCE, not a host parameter, so it restores from this accessor on
    // editor construction and writes back through it when toggled. getStateInformation
    // persists it into the DEDICATED canonical <extras> openGlOptIn key (ONLY when ON, so
    // pre-130 blobs stay byte-compatible) and setStateInformation reads it back, so it
    // round-trips on session reload exactly like the editor size / reduce-motion preference
    // [docs/design/10-ui.md §11; ADR-015 C9; ADR-008 §4/§5]. Message-thread only; the audio
    // thread never touches it. Default false (software path, no context attached).
    [[nodiscard]] bool getStoredOpenGl() const noexcept { return storedOpenGl_; }
    void setStoredOpenGl(bool openGlEnabled) noexcept { storedOpenGl_ = openGlEnabled; }

    // --- Audio -> GUI telemetry (the §8.3/§8.4 one-directional RT-safe read path) ---
    // processBlock PUBLISHES one Telemetry::Snapshot per block via the 107 SPSC Producer
    // (RT-safe: a seqlock byte copy, no heap alloc, no lock); the editor's Timer DRAINS
    // it through a Consumer view. This accessor hands the editor a freshly-prepared
    // Consumer attached to the processor-owned Buffer; the editor calls pull() on its
    // Timer to coalesce to the most-recent frame [docs/design/10-ui.md §8.3/§8.4; ADR-015
    // C5; docs/design/00 §5.4]. Message-thread only (constructs/prepares a Consumer);
    // the audio thread is the SINGLE producer and never touches this accessor.
    [[nodiscard]] mw::ui::Telemetry::Consumer telemetryConsumer() noexcept
    {
        mw::ui::Telemetry::Consumer c{};
        c.prepare(telemetryBuffer_);
        return c;
    }

    // --- <extras> seq-pattern editing (message thread) -> audio-thread handoff ------
    // The editor reads the current 100-step pattern via seqPattern(), edits it, and
    // writes it back via setSeqPattern(). setSeqPattern publishes the edited POD to the
    // audio thread through an RT-safe SPSC double-buffer swap: the audio thread only
    // ADOPTS the published POD each block (one ACQUIRE pointer load + a byte copy) — it
    // never parses a tree, allocates, or locks [docs/design/10-ui.md §9.3; docs/design/00
    // §5.4; ADR-008 C19; ADR-021 L7]. The same pattern persists through capture/
    // recoverState's <extras><seq> round-trip. Message-thread only.
    [[nodiscard]] mw::state::Extras seqPattern() const noexcept { return seqPattern_; }
    void setSeqPattern(const mw::state::Extras& pattern) noexcept
    {
        seqPattern_ = pattern;                 // message-thread canonical copy (for read + persist)
        seqPatternHandoff_.publish(pattern);   // RT-safe publish to the audio thread
    }

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
    // The live continuous-controller position the LAST processBlock fed into
    // BlockContext::controllers (task 162d). Lets a test prove the host pitch-bend / CC1
    // mod-wheel reached the controller-ingress seam through the real processor. Plain
    // members written on the audio thread; read off-thread in tests (no audio-thread mutation).
    [[nodiscard]] float lastBendUnitForTest() const noexcept { return lastBendUnit_; }
    [[nodiscard]] float lastModWheelForTest() const noexcept { return lastModWheel_; }
    // The number of times the message-thread handoff (handleAsyncUpdate) applied a
    // preset recall. Lets a test prove a Program Change in the MIDI stream actually
    // reached setCurrentProgram via the AsyncUpdater (NOT just a host setCurrentProgram).
    [[nodiscard]] int  programRecallCountForTest() const noexcept { return programRecalls_; }
    // The last Program Change index handed off to setCurrentProgram by the message-thread
    // consumer (-1 if none yet). Distinct from lastProgramChangeForTest(), which records
    // the raw audio-thread capture (including out-of-range values that the consumer drops).
    [[nodiscard]] int  lastRecalledProgramForTest() const noexcept { return lastRecalledProgram_; }
    // The seq-pattern POD the audio thread most recently ADOPTED in processBlock (a copy
    // of what adopt() returned). Lets a test prove the message-thread edit crossed the
    // RT-safe handoff and was observed on the audio thread. Plain member; written on the
    // audio thread, read off-thread in tests (no audio-thread mutation from the read).
    [[nodiscard]] mw::state::Extras adoptedSeqPatternForTest() const noexcept { return lastAdoptedSeq_; }
    // Install a non-empty preset bank for the full-assembly recall test (message thread
    // only; no audio-thread effect). The default-constructed bank is empty (no embedded
    // BinaryData yet, tasks 131/144-150), so this is the test seam that lets a ProgramChange
    // recall an actual slot. Mirrors PresetManager's injection constructor.
    void installPresetBankForTest(std::vector<preset::PresetManager::SlotSource> sources)
    {
        presetManager_ = preset::PresetManager{ sources };
        currentProgram_ = 0;
    }

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

    // --- Editor size persistence (message thread only; task 114) ------------------
    // The last editor window size in PIXELS, read by MwAudioEditor on construction and
    // written back on resize. Serialized into / restored from the canonical <extras>
    // uiWidth/uiHeight keys by get/setStateInformation so it survives a session reload.
    // Default {0,0} means "none stored yet" -> the editor falls back to its default
    // scale [docs/design/10-ui.md §4.4; ADR-015 C2].
    juce::Point<int> storedEditorSize_{ 0, 0 };

    // --- Reduce-motion / low-CPU preference (message thread only; task 115) -------
    // The persisted editor reduce-motion toggle state. Serialized into / restored from
    // the canonical <extras> reduceMotion key by get/setStateInformation so it survives a
    // session reload. Default false (animation on) [docs/design/10-ui.md §10; ADR-015 C8].
    bool storedReduceMotion_ = false;

    // --- OpenGL render-backend opt-in (message thread only; task 130) -------------
    // The persisted editor OpenGL opt-in. Serialized into / restored from the DEDICATED
    // canonical <extras> openGlOptIn key (plugin/ui/EditorPrefsKeys.h, distinct from the
    // core §9 sticky audio renderVersion opt-in) by get/setStateInformation (only written
    // when ON) so it survives a session reload. Default false (software path; no context
    // attached) [docs/design/10-ui.md §11; ADR-015 C9].
    bool storedOpenGl_ = false;

    // --- Audio -> GUI telemetry (107) ----------------------------------------------
    // The pre-allocated, fixed-capacity, lock-free SPSC ring (the shared state) — owned
    // here, constructed off the audio thread. processBlock is the SINGLE producer; the
    // editor's Timer receives a Consumer view via telemetryConsumer(). The Producer is
    // attached to the Buffer in prepareToPlay (off the audio thread) [§8.3; ADR-015 C5].
    mw::ui::Telemetry::Buffer   telemetryBuffer_{};
    mw::ui::Telemetry::Producer telemetryProducer_{};

    // --- <extras> seq-pattern handoff (111c) ---------------------------------------
    // The message-thread canonical copy of the editable 100-step pattern (read by the
    // editor, written into the canonical <extras><seq> by getStateInformation). The
    // RT-safe SPSC double-buffer hands an edited pattern to the audio thread, which only
    // ADOPTS the published POD each block [docs/design/10-ui.md §9.3; ADR-008 C19/C20].
    mw::state::Extras                       seqPattern_{};
    mw::plugin::state::SeqPatternHandoff    seqPatternHandoff_{};
    // The POD the audio thread last adopted (written on the audio thread; test-only read).
    mw::state::Extras                       lastAdoptedSeq_{};

    // --- Test counters (audio-thread writes are plain; read off-thread in tests) ---
    int processCalls_       = 0;
    int resetCalls_         = 0;
    int latencySetCalls_    = 0;
    int lastCoreEventCount_ = 0;
    int lastProgramChange_  = -1;
    int programRecalls_     = 0;   // message-thread handoff recall count (handleAsyncUpdate)
    int lastRecalledProgram_ = -1; // last index the consumer applied via setCurrentProgram

    // The live controller position the last processBlock fed BlockContext::controllers
    // (task 162d test introspection; written on the audio thread, read off-thread in tests).
    float lastBendUnit_ = 0.0f;
    float lastModWheel_ = 0.0f;

    // The voice cap the engine is prepared with (bootstrap value; the real cap is a
    // param in the voice stream).
    static constexpr int kMaxVoices = 8;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MwAudioProcessor)
};

} // namespace mw::plugin
