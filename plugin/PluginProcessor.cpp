// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/PluginProcessor.cpp — MwAudioProcessor implementation. The processor
// convergence node (task 111).
//
// This TU is the single place that touches BOTH juce::* and the core POD seam. It wires
// the already-built components (it reimplements none of them) into the three-call seam:
//
//   prepareToPlay  -> Engine::prepare(sr, maxBlock, maxVoices); size the lock-free
//                     NormalizedEventBuffer + the core-event scratch + the ParamBridge
//                     table; resolve the per-format capability rungs; declare the
//                     CONSTANT PDC to the host via setLatencySamples ONCE [ADR-017 L10].
//   processBlock   -> drain the JUCE MidiBuffer (+ resolved note-expr rung) into the
//                     NormalizedEventBuffer (MidiFrontEnd), translate HostEvent ->
//                     mw::MidiEvent into the pre-sized scratch (EventTranslator), snapshot
//                     the APVTS atomics once (ParamBridge), recheck the transport rung
//                     (CapabilityShim), assemble the BlockContext, and drive
//                     Engine::process. Program Change is captured for message-thread
//                     preset recall — the audio thread only ever writes a POD. ZERO heap
//                     alloc, NO lock [docs/design/09 §3.2; ADR-011 C9; ADR-024 C7].
//   releaseResources -> Engine::reset.
//
// There is ONE Engine and ONE DSP path for every wrapper format [ADR-001 C14;
// ADR-011 Decision].

#include "PluginProcessor.h"

#include <cmath>   // std::fabs (telemetry peak-level metering)

#include <juce_audio_utils/juce_audio_utils.h>   // GenericAudioProcessorEditor

#include "../ui/MwAudioEditor.h"      // mw::ui::MwAudioEditor — the editor root (task 114)
#include "params/ParameterLayout.h"   // buildParameterLayout() — the full 91-param APVTS
#include "midi/EventTranslator.h"     // HostEvent -> mw::MidiEvent (§3.3)
#include "state/StateSerializer.h"    // canonical capture / write / read (task 023)
#include "state/LoadFailure.h"        // recoverState graded recovery ladder (task 024)
#include "state/Extras.h"             // mw::state::Extras (audio-thread POD payload)
#include "state/StateTree.h"          // mw::state::kParamsId etc.
#include "state/SeqPatternCodec.h"    // readSeqPattern: <extras><seq> tree -> Extras POD (111c)
#include "state/CcLearnCodec.h"       // write/readCcLearn: CC-learn bindings round-trip (023b)
#include "version/EngineVersion.h"    // schema / plugin / engine / render versions
#include "ui/EditorPrefsKeys.h"       // <extras> reduce-motion UI-preference key (task 115)

namespace mw::plugin {

// -----------------------------------------------------------------------------------

MwAudioProcessor::MwAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, /*undoManager*/ nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
MwAudioProcessor::createParameterLayout()
{
    // The FULL §4 layout: one juce parameter per LIVE kParamDefs entry, built mechanically
    // from the JUCE-free registry (task 020). Replaces the bootstrap single-gain stub.
    return buildParameterLayout();
}

PluginFormat MwAudioProcessor::resolveFormat() const noexcept
{
    // Map JUCE's wrapper type to the §8.2 capability matrix format. AudioUnitv3 maps to
    // AU; AAX is permanently excluded (it never reaches here). Undefined / Unity / the
    // headless test host fall back to Standalone (the universal floor) [docs/design/09
    // §8.1; ADR-024 C6].
    switch (wrapperType)
    {
        case wrapperType_VST3:        return PluginFormat::VST3;
        case wrapperType_AudioUnit:
        case wrapperType_AudioUnitv3: return PluginFormat::AU;
        case wrapperType_LV2:         return PluginFormat::LV2;
        case wrapperType_Standalone:  return PluginFormat::Standalone;
        case wrapperType_Undefined:
        case wrapperType_VST:
        case wrapperType_AAX:
        case wrapperType_Unity:
        default:                      return PluginFormat::Standalone;
    }
}

void MwAudioProcessor::declareLatencyFromPrepare(double sampleRate)
{
    // The single CONSTANT worst-case PDC value, padded across ALL FX-on/off and Quality
    // tiers, in base-rate samples. Computed + declared ONLY here (prepare), NEVER mutated
    // from processBlock [ADR-017 L4/L10; docs/design/09 §8.3].
    reportedLatencySamples_ = latencyReporter_.computeWorstCaseLatency(sampleRate);
    setLatencySamples(reportedLatencySamples_);
    ++latencySetCalls_;
}

// --- The three-call seam ------------------------------------------------------------

void MwAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // == THE SOLE ALLOCATION / SIZING SITE ==  Everything below allocates/sizes here so
    // processBlock stays allocation-free [docs/design/00 §5.5; ADR-001 C2; ADR-011 C9].

    // 1. The DSP core: prepare(sr, maxBlock, maxVoices). The per-voice 2x-oversampled
    //    zone is selected/clamped inside the engine [ADR-023].
    engine_.prepare(sampleRate, samplesPerBlock, kMaxVoices);

    // 2. The lock-free host-event surface: capacity = kEventBufferBlockFactor*maxBlock +
    //    kEventBufferSlack (the (PI) factors centralized in Calibration.h) [§3.2].
    hostEvents_.prepare(samplesPerBlock);

    // 3. The pre-sized core-event scratch the EventTranslator writes into. Sized to the
    //    same §3.2 capacity so a fully-translated buffer never overflows [ADR-011 C9].
    events_.assign(static_cast<std::size_t>(eventBufferCapacityFor(samplesPerBlock)),
                   mw::MidiEvent{});

    // 4. The MIDI front-end de-zipper coefficients (sole sizing point) [docs/design/09 §1.3].
    midiFrontEnd_.prepare(sampleRate, samplesPerBlock);

    // 5. The full APVTS -> normalized-POD bridge table (string lookups happen here, off
    //    the audio thread; snapshot() is pure arithmetic) [docs/design/00 §5.4].
    paramBridge_.prepare(apvts_);

    // 6. Resolve the per-format capability rungs ONCE from the §8.1 matrix + the current
    //    host transport query; cache the note-expr rung for the block path [§7-§8].
    resolvedCaps_ = capabilityShim_.resolve(resolveFormat(), /*mpeLite*/ false, getPlayHead());
    capabilityShim_.publishToUi(resolvedCaps_);

    // 7. Size the constant-PDC padding lines and declare the latency to the host ONCE.
    latencyReporter_.preparePadding(latencyReporter_.computeWorstCaseLatency(sampleRate),
                                    getTotalNumOutputChannels());
    declareLatencyFromPrepare(sampleRate);

    // 8. Attach the audio->GUI telemetry Producer to the pre-allocated SPSC Buffer (the
    //    Buffer storage is allocated by construction; prepare() just stores the pointer,
    //    off the audio thread). processBlock is the SINGLE producer thereafter — it only
    //    push()es a byte copy, never allocates [docs/design/10-ui.md §8.3; ADR-015 C5].
    telemetryProducer_.prepare(telemetryBuffer_);
}

void MwAudioProcessor::releaseResources()
{
    engine_.reset();
    ++resetCalls_;
}

bool MwAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Synth: no input bus; mono or stereo output only.
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void MwAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numFrames   = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Clear any output channels the engine will not write (it writes ch0 and, if present,
    // ch1). No alloc.
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numFrames);

    // 1. Per-block, branch-free transport recheck (falls to Free-run when the host stops
    //    reporting a transport, re-locks when it reappears) — no alloc, no lock [§8.2].
    const ResolvedCapabilities caps = capabilityShim_.recheckPerBlock(getPlayHead());

    // 2. Drain the JUCE MidiBuffer (+ the resolved note-expression rung) into the
    //    pre-sized, drop-never-grow lock-free NormalizedEventBuffer. The CcLearnMap is
    //    read through its single atomic-pointer load. No alloc, no lock [§4/§6.3].
    hostEvents_.clear();
    midiFrontEnd_.processMidi(midi, ccLearnMap_, resolvedCaps_.noteExpr, hostEvents_);

    // 3. Capture the LAST Program Change in this block for message-thread preset recall.
    //    The front-end does NOT forward Program Change to the engine (consumed in plugin/).
    //    The audio thread only ever writes a POD (an atomic int); it NEVER calls into the
    //    PresetManager / APVTS [docs/design/09 §3.3; ADR-008 C19]. When a Program Change is
    //    seen, hand it off to the message thread via triggerAsyncUpdate() — a lock-free flag
    //    set (no heap alloc, no lock): handleAsyncUpdate() reads+clears the atomic and does
    //    the actual recall off the audio thread [ADR-001 C3/C4].
    bool sawProgramChange = false;
    for (const auto meta : midi)
    {
        const juce::MidiMessage& m = meta.getMessage();
        if (m.isProgramChange())
        {
            lastProgramChange_ = m.getProgramChangeNumber();
            pendingProgramChange_.store(lastProgramChange_, std::memory_order_relaxed);
            sawProgramChange = true;
        }
    }
    if (sawProgramChange)
        triggerAsyncUpdate();   // RT-safe: sets a lock-free flag; no alloc, no lock.

    // 4. Translate the HostEvent surface -> the JUCE-free core mw::MidiEvent scratch,
    //    field-for-field (§3.3). ProgramChange + unmapped CCs are skipped at the boundary.
    //    Writes into the pre-sized scratch; surplus is dropped (drop-never-grow) [ADR-011 C9].
    const int coreEventCount = translateBlock(hostEvents_.begin(), hostEvents_.end(),
                                              ccLearnMap_,
                                              events_.data(),
                                              static_cast<int>(events_.size()));
    lastCoreEventCount_ = coreEventCount;

    // 5. Snapshot the APVTS automation atomics ONCE into the normalized POD the engine
    //    reads (one atomic load per param; pure arithmetic) [docs/design/00 §5.4]. The
    //    bridge emits the interim NormalizedParamSnapshot; copy it into the core-owned
    //    mw::ParamSnapshot the BlockContext borrows (same registry-index shape).
    const NormalizedParamSnapshot bridged = paramBridge_.snapshot();
    paramSnapshot_.normalizedValues = bridged.normalizedValues;
    paramSnapshot_.indexValues      = bridged.indexValues;

    // 6. Decode the host transport into the POD TransportInfo (§5.3). Absent playhead
    //    (e.g. Standalone) -> sane stopped defaults; the recheck above already classified
    //    the transport rung. No alloc.
    mw::TransportInfo transport{};
    transport.sampleRate  = getSampleRate();
    transport.bpm         = 120.0;
    transport.ppqPosition = 0.0;
    transport.isPlaying   = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())          transport.bpm         = *bpm;
            if (auto ppq = pos->getPpqPosition())  transport.ppqPosition = *ppq;
            transport.isPlaying = pos->getIsPlaying();
        }
    }
    juce::ignoreUnused(caps);

    // 7. Build the borrowed AudioBlockView over the host channel pointers (§5.3) and
    //    assemble the BlockContext. params points at the stable member snapshot.
    float* const* chans = buffer.getArrayOfWritePointers();
    mw::BlockContext ctx{};
    ctx.audio     = mw::AudioBlockView{ chans, numChannels, numFrames };
    ctx.params    = &paramSnapshot_;
    ctx.transport = transport;
    ctx.midi      = mw::MidiEventView{ events_.data(), coreEventCount };

    // 7b. CONTINUOUS-CONTROLLER INGRESS (task 162d — the plugin half of the 162c seam).
    //     Feed the LIVE host pitch-bend + CC1 (mod-wheel) position the MidiFrontEnd tracked
    //     while draining this block's MidiBuffer (step 2) into BlockContext::controllers, so
    //     the engine's 162c legs activate END-TO-END: pitchBend (centered unit [-1,+1]) bends
    //     {VCO,VCF} per mod.bend_dest / mod.bend_range_*, and modWheel ([0,1]) scales the LFO
    //     depth per mod.lfo_mod_wheel. Before this task the processor built BlockContext WITHOUT
    //     these, so bend/wheel were inert in the real plugin despite working in the core test.
    //     The front-end holds each position across blocks (a real wheel keeps its position until
    //     the next message), so a block with no controller message keeps the last held value.
    //     CC1 reaches the engine EXACTLY ONCE — via this controller ingress, NOT also as a
    //     mod.lfo_mod_wheel ParamValue (the front-end no longer emits that for CC1) — so there
    //     is no double-application [docs/design/09 §4.4/§6.2; ADR-028 item 3]. RT-safe: two POD
    //     reads + two POD writes; no alloc, no lock.
    ctx.controllers.pitchBend = midiFrontEnd_.liveBendUnit();
    ctx.controllers.modWheel  = midiFrontEnd_.liveModWheelNorm();
    lastBendUnit_ = ctx.controllers.pitchBend;   // test introspection (plain member write)
    lastModWheel_ = ctx.controllers.modWheel;

    // 8. Pure render through the seam (the engine writes its stereo output directly into
    //    the borrowed host channels) [ADR-001 C3].
    engine_.process(ctx);
    ++processCalls_;

    // 8b. ADOPT the latest message-thread-published seq pattern: ONE ACQUIRE atomic-
    //     pointer load + a trivial POD copy. The audio thread NEVER parses a tree,
    //     allocates, or locks — it only adopts a published POD [docs/design/10-ui.md §9.3;
    //     docs/design/00 §5.4; ADR-008 C19]. (Held for the seq subsystem + test
    //     introspection; the live playhead step authority is the control core.)
    lastAdoptedSeq_ = seqPatternHandoff_.adopt();

    // 8c. PUBLISH one audio->GUI telemetry frame for this block via the 107 SPSC Producer
    //     — a seqlock byte copy: NO heap alloc, NO lock [docs/design/10-ui.md §8.3, §8.4;
    //     docs/design/00 §5.4; ADR-015 C5; ADR-001 C3/C4]. The Snapshot POD is filled on the
    //     stack (no alloc) and pushed by value. snapshot() math is pure arithmetic over the
    //     just-rendered output + a handful of engine getters.
    //
    //     Task 118d COMPLETES this publish (it previously filled ONLY vcaLevelL/R + a
    //     MONOTONIC display step, leaving scope[256], vcfCutoffDisplay, lfoPhase at ZERO so
    //     127's ScopeMeterOverlay rendered a FLAT scope + empty cutoff indicator). Now it fills:
    //       - vcaLevelL/R       : the post-VCA peak level per channel (as before);
    //       - scope[kScopePoints]: a DECIMATED tap of the post-VCA rendered wave for this block
    //                              (every kScopeDecimation-th sample of mono (L+R)/2), so the
    //                              UI scope shows the live waveform instead of a flat line;
    //       - vcfCutoffDisplay  : the dispatched/modulated cutoff (engine_.currentCutoffDisplay,
    //                              0..1) so the cutoff indicator tracks the filter;
    //       - lfoPhase          : the engine LFO display phase (engine_.currentLfoPhase), which
    //                              advances while the LFO runs — the §8.4 mod-source indicator;
    //       - seqStep           : the REAL live sequencer slot (engine_.currentSeqStep), the
    //                              actual playhead the SequencerGrid (126) highlights — replacing
    //                              the 111c monotonic counter (closes the 111c/118c QA MEDIUM).
    //     RT-safe: getters are plain reads, the scope decimation is arithmetic over the output
    //     buffer; NO heap alloc, NO lock added (the publish is already on the audio thread).
    mw::ui::Telemetry::Snapshot frame{};
    float peakL = 0.0f;
    float peakR = 0.0f;
    if (numChannels > 0 && numFrames > 0)
    {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(numChannels > 1 ? 1 : 0);
        for (int n = 0; n < numFrames; ++n)
        {
            const float al = std::fabs(l[n]);
            const float ar = std::fabs(r[n]);
            if (al > peakL) peakL = al;
            if (ar > peakR) peakR = ar;
        }

        // Decimated scope tap: walk the block in kScopeDecimation strides, writing the mono
        // (L+R)/2 sample at each stride into the next scope point until the block is consumed
        // or the fixed kScopePoints array fills. A short block simply fills fewer points (the
        // remainder stays at the zero-initialized default); a long block is decimated to fit.
        constexpr int kPoints = mw::cal::telemetry::kScopePoints;
        constexpr int kStride = mw::cal::telemetry::kScopeDecimation > 0
                                    ? mw::cal::telemetry::kScopeDecimation : 1;
        int p = 0;
        for (int n = 0; n < numFrames && p < kPoints; n += kStride, ++p)
            frame.scope[static_cast<std::size_t>(p)] = 0.5f * (l[n] + r[n]);
    }
    frame.vcaLevelL        = peakL;
    frame.vcaLevelR        = peakR;
    frame.vcfCutoffDisplay = engine_.currentCutoffDisplay();   // dispatched/modulated cutoff, 0..1
    frame.lfoPhase         = engine_.currentLfoPhase();        // advancing LFO display phase (§8.4)
    frame.seqStep          = static_cast<std::uint64_t>(       // REAL live playhead slot (118d);
        static_cast<std::int64_t>(engine_.currentSeqStep()));  // -1 (no step) widens to all-ones
    telemetryProducer_.push(frame);           // RT-safe: seqlock byte copy, no alloc/lock.

    // 9. Align the (constant) configuration up to the reported worst-case latency so the
    //    PDC number declared in prepare is honored bit-for-bit. Uses the preallocated
    //    padding lines (no alloc, no lock); latency is NEVER re-declared here [ADR-017 L10].
    //    Pad amount is 0 in this build (the engine already runs the worst-case zone), but
    //    the call is on the constant path so a future shorter config aligns without a
    //    latency-mutation from process.
    latencyReporter_.padBlock(chans, numChannels, /*padSamples*/ 0, numFrames);
}

// --- Editor -------------------------------------------------------------------------

juce::AudioProcessorEditor* MwAudioProcessor::createEditor()
{
    // The real editor root: the format-agnostic, aspect-locked, AffineTransform-scaled
    // MwAudioEditor shared by every wrapper (task 114) [docs/design/10-ui.md §4, §5.2;
    // ADR-015 Decision; ADR-011].
    return new mw::ui::MwAudioEditor(*this);
}

// --- Programs (the factory preset bank surfaced as host programs) -------------------

int MwAudioProcessor::getNumPrograms()
{
    // JUCE requires >= 1. The factory bank is empty at this stage (no embedded files yet,
    // tasks 131/144-150) — surface a single default program so hosts are happy [§10.2].
    return juce::jmax(1, presetManager_.getNumPresets());
}

void MwAudioProcessor::setCurrentProgram(int index)
{
    // Message-thread preset recall: apply the bank slot through the SAME migrate+recover
    // chain as session state, into APVTS + <extras>. Out-of-range / empty bank is a safe
    // no-op [docs/design/06 §10.1; ADR-021]. The audio thread is untouched here.
    if (presetManager_.getNumPresets() <= 0)
    {
        currentProgram_ = 0;
        return;
    }
    if (index < 0 || index >= presetManager_.getNumPresets())
        return;

    mw::state::Extras extras{};
    mw::plugin::state::RecoveryReport report{};
    presetManager_.loadPreset(index, apvts_, extras, report);
    currentProgram_ = index;
}

const juce::String MwAudioProcessor::getProgramName(int index)
{
    if (presetManager_.getNumPresets() <= 0)
        return "Default";
    return presetManager_.getName(index);
}

// --- MIDI Program Change -> preset recall (message-thread consumer) -----------------

void MwAudioProcessor::handleAsyncUpdate()
{
    // MESSAGE THREAD. processBlock captured a Program Change into pendingProgramChange_
    // (a lock-free POD store) and fired triggerAsyncUpdate(); read + clear the atomic
    // here and apply the recall off the audio thread. exchange(-1) atomically consumes
    // the pending value so a re-fire that races the clear is not lost (it re-stores a
    // fresh value and re-triggers) and a spurious wake with no pending PC is a no-op
    // [docs/design/09 §3.3; docs/design/06 §10; ADR-008 C19].
    const int index = pendingProgramChange_.exchange(-1, std::memory_order_relaxed);
    if (index < 0)
        return;   // no pending Program Change (already consumed / spurious wake).

    // Clamp to the surfaced program range; an out-of-range Program Change is IGNORED
    // (no recall, APVTS untouched) [docs/design/06 §10.1]. getNumPrograms() is the
    // surfaced bank size (>= 1) — a PC of 0 over the empty default bank is a safe no-op
    // inside setCurrentProgram itself.
    if (index >= getNumPrograms())
        return;

    setCurrentProgram(index);   // routes to PresetManager::loadPreset (message thread).
    lastRecalledProgram_ = index;
    ++programRecalls_;
}

// --- State (canonical serializer round-trip + graded recovery) ----------------------

void MwAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Capture the canonical MW101_STATE tree (APVTS <PARAMS> + the <extras> POD) and
    // serialize it to the host's opaque blob via the ONE canonical serializer (task 023).
    // The <extras> seq pattern is now OWNED here (task 111c): the message-thread canonical
    // copy of the edited 100-step pattern persists so the saved session restores the user's
    // sequence, not an empty default [docs/design/10-ui.md §9.3; docs/design/06 §5.4].
    // The advisory editor size (a message-thread <extras> UI preference, NOT a host
    // parameter) is threaded through so it PERSISTS in the canonical state and round-trips
    // on reload [docs/design/10-ui.md §4.4; ADR-015 C2; ADR-008 §4/§5 C8]. A {0,0} stored
    // size (no editor opened yet) is omitted by captureState.
    const mw::state::Extras extras = seqPattern_;
    const juce::Point<int> uiSize = storedEditorSize_;
    const juce::ValueTree canonical = mw::plugin::state::captureState(
        apvts_, extras,
        mw101::version::kCurrentSchemaVersion,
        juce::String(mw101::version::kPluginVersion),
        juce::String(mw101::version::kEngineVersion),
        mw101::version::kCurrentRenderVersion,
        mw::plugin::state::UiEditorSize{ uiSize.x, uiSize.y });

    // Persist the reduce-motion / low-CPU UI preference directly on the canonical
    // <extras> node (a UI preference, NOT a host parameter, exactly like the editor
    // size) [docs/design/10-ui.md §10; ADR-008 §4/§5 C8]. Written here (not threaded
    // through the canonical serializer) to keep this a localized, conflict-free edit:
    // the key is owned by plugin/ui/EditorPrefsKeys.h. Only written when ON, so a
    // default-OFF session stays byte-compatible with pre-115 blobs. Message-thread only.
    if (storedReduceMotion_)
    {
        auto extrasNode = canonical.getChildWithName(
            juce::Identifier{ mw::state::kExtrasId });
        if (extrasNode.isValid())
            extrasNode.setProperty(
                juce::Identifier{ mw::plugin::ui::prefs::kExtrasReduceMotion }, true, nullptr);
    }

    // Persist the OpenGL render-backend opt-in directly on the canonical <extras> node,
    // mirroring the reduce-motion write above (a UI preference, NOT a host parameter)
    // [docs/design/10-ui.md §11; ADR-015 C9; ADR-008 §4/§5]. ONLY written when ON, so a
    // default-OFF session stays BYTE-COMPATIBLE with pre-130 blobs (the software path is
    // the default and writes no key). Uses the DEDICATED UI-preference key owned by
    // plugin/ui/EditorPrefsKeys.h (kExtrasOpenGlOptIn) — deliberately distinct from the
    // core §9 sticky AUDIO renderVersion opt-in (kExtrasRenderOptIn) so this UI toggle can
    // never collide with the renderVersion-migration opt-in. Message-thread.
    if (storedOpenGl_)
    {
        auto extrasNode = canonical.getChildWithName(
            juce::Identifier{ mw::state::kExtrasId });
        if (extrasNode.isValid())
            extrasNode.setProperty(
                juce::Identifier{ mw::plugin::ui::prefs::kExtrasOpenGlOptIn }, true, nullptr);
    }

    // Persist the user MIDI-learn (CC-learn) bindings directly on the canonical <extras>
    // node (task 023b), mirroring the reduce-motion / OpenGL writes above. writeCcLearn
    // emits a <ccLearn> child holding ONLY the NON-DEFAULT rows (diffed against the §6.2
    // seed), so a never-learned session writes NO node and the blob stays byte-compatible
    // with pre-023b state. The live ccLearnMap_ is read on the message thread here (the
    // single writer); the audio thread only ever reads its atomic live pointer
    // [docs/design/06 §5.4; docs/design/09 §6.3; ADR-012 C16; ADR-008 C19].
    if (auto extrasNode = canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
        extrasNode.isValid())
        mw::plugin::state::writeCcLearn(extrasNode, ccLearnMap_);

    mw::plugin::state::writeToBlob(canonical, destData);
}

void MwAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Run the host blob through the NEVER-throwing graded recovery ladder (task 024):
    // it migrates to CURRENT, defaults missing, clamps out-of-range, and ALWAYS returns a
    // complete valid canonical tree. Then bind the recovered <PARAMS> into APVTS (the
    // §5.3 message-thread path; same shape PresetManager uses) [ADR-021].
    mw::plugin::state::RecoveryReport report{};
    const juce::ValueTree recovered =
        mw::plugin::state::recoverState(data, sizeInBytes, report);

    // Restore the advisory editor size from the recovered <extras> UI node so a freshly
    // created editor opens at the persisted size; absent/zero/garbage -> {0,0}, which the
    // editor reads as "no stored size" and falls back to the default design scale
    // [docs/design/10-ui.md §4.4; ADR-015 C2; ADR-021 fallback]. Message-thread only.
    const mw::plugin::state::UiEditorSize uiSize =
        mw::plugin::state::readUiEditorSize(recovered);
    storedEditorSize_ = { uiSize.width, uiSize.height };

    // Restore the reduce-motion / low-CPU UI preference from the recovered <extras> node
    // (the inverse of getStateInformation's write). Absent / garbage -> false (animation
    // on), so pre-115 blobs and never-toggled sessions keep the default [docs/design/
    // 10-ui.md §10; ADR-008 C8; ADR-021 fallback]. Message-thread only.
    storedReduceMotion_ = false;
    storedOpenGl_       = false;
    if (const auto extrasNode = recovered.getChildWithName(
            juce::Identifier{ mw::state::kExtrasId });
        extrasNode.isValid())
    {
        storedReduceMotion_ = static_cast<bool>(extrasNode.getProperty(
            juce::Identifier{ mw::plugin::ui::prefs::kExtrasReduceMotion }, false));

        // Restore the OpenGL render-backend opt-in from the recovered <extras> node (the
        // inverse of getStateInformation's write). Reads the DEDICATED UI-preference key
        // (kExtrasOpenGlOptIn), NOT the core §9 sticky audio renderVersion opt-in. Absent /
        // garbage -> false (software path), so pre-130 blobs and never-opted-in sessions
        // keep the default [docs/design/10-ui.md §11; ADR-015 C9; ADR-021 fallback].
        // Message-thread only.
        storedOpenGl_ = static_cast<bool>(extrasNode.getProperty(
            juce::Identifier{ mw::plugin::ui::prefs::kExtrasOpenGlOptIn }, false));
    }

    // Restore the EDITED <extras><seq> 100-step pattern from the recovered tree into the
    // message-thread canonical copy, and republish it to the audio thread via the RT-safe
    // handoff so the restored session's audio path adopts the user's sequence (the inverse
    // of captureState's <seq> write) [docs/design/10-ui.md §9.3; docs/design/06 §5.4;
    // ADR-008 C19]. recoverState always returns a valid tree, so this never throws; a
    // missing/garbage <seq> decodes to an empty default pattern [ADR-021 fallback].
    seqPattern_ = mw::plugin::state::readSeqPattern(recovered);
    seqPatternHandoff_.publish(seqPattern_);

    // Restore the user MIDI-learn (CC-learn) bindings from the recovered <extras><ccLearn>
    // node into the live map (the inverse of getStateInformation's writeCcLearn). The map
    // starts from its §6.2 default seed and each well-formed, validated <binding> is applied
    // over it, then published in one atomic swap. An absent / garbage <ccLearn> leaves the
    // map at the default seed WITHOUT failing the load, so pre-023b blobs still restore
    // [docs/design/06 §5.4; docs/design/09 §6.3; ADR-012 C16; ADR-021]. Message-thread only.
    mw::plugin::state::readCcLearn(recovered, ccLearnMap_);

    const auto params = recovered.getChildWithName(juce::Identifier{ mw::state::kParamsId });
    if (! params.isValid())
        return;

    juce::ValueTree apvtsState{ apvts_.state.getType() };
    apvtsState.copyPropertiesAndChildrenFrom(params, nullptr);
    apvts_.replaceState(apvtsState);
}

} // namespace mw::plugin

// -----------------------------------------------------------------------------------
// The JUCE plugin-instance factory. JUCE's wrappers (incl. Standalone) call this to
// construct the one shared processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mw::plugin::MwAudioProcessor();
}
