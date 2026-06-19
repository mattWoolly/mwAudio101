// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/PluginProcessor.cpp — MwAudioProcessor implementation (task 111, MINIMAL).
//
// See PluginProcessor.h for the bootstrap scope and the deferred-leaf-task list.
// This TU is the single place that touches BOTH juce::* and the core POD seam: it
// marshals the host's JUCE MidiBuffer + playhead into a JUCE-free mw::BlockContext
// (docs/design/09 §3.2/§3.3; docs/design/00 §5.2-§5.4) and drives the engine.

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>   // GenericAudioProcessorEditor

#include "params/ParamIDs.h"   // mwcore: the stable mw101.* string IDs

namespace mw::plugin {

namespace {

// Translate one JUCE MIDI message into the core POD mw::MidiEvent (docs/design/09
// §3.3 field-for-field map). Returns false for messages the engine does not ingest
// in the bootstrap (everything but note/bend/pressure/CC). Allocation-free.
bool toMidiEvent(const juce::MidiMessage& m, int sampleOffset, mw::MidiEvent& out) noexcept
{
    out.channel      = static_cast<std::int8_t>(m.getChannel());  // 1..16; 0 if none
    out.noteId       = -1;                                        // MIDI-derived
    out.sampleOffset = sampleOffset;

    if (m.isNoteOn())
    {
        out.type  = mw::NormalizedType::NoteOn;
        out.data0 = static_cast<float>(m.getNoteNumber());
        out.value = m.getFloatVelocity();
        return true;
    }
    if (m.isNoteOff())
    {
        out.type  = mw::NormalizedType::NoteOff;
        out.data0 = static_cast<float>(m.getNoteNumber());
        out.value = m.getFloatVelocity();
        return true;
    }
    if (m.isPitchWheel())
    {
        out.type  = mw::NormalizedType::PitchBend;
        out.data0 = 0.0f;
        // Center the 14-bit wheel to a signed [-1, 1] offset.
        out.value = (static_cast<float>(m.getPitchWheelValue()) - 8192.0f) / 8192.0f;
        return true;
    }
    if (m.isChannelPressure())
    {
        out.type  = mw::NormalizedType::ChannelPressure;
        out.data0 = 0.0f;
        out.value = static_cast<float>(m.getChannelPressureValue()) / 127.0f;
        return true;
    }
    if (m.isAftertouch())
    {
        out.type  = mw::NormalizedType::PolyPressure;
        out.data0 = static_cast<float>(m.getNoteNumber());
        out.value = static_cast<float>(m.getAfterTouchValue()) / 127.0f;
        return true;
    }
    if (m.isController())
    {
        // The full CC -> mw101.<id> learn map is task 104; the bootstrap forwards the
        // raw CC number/value so the seam is exercised end to end.
        out.type  = mw::NormalizedType::ControlChange;
        out.data0 = static_cast<float>(m.getControllerNumber());
        out.value = static_cast<float>(m.getControllerValue()) / 127.0f;
        return true;
    }
    return false;
}

} // namespace

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
    // MINIMAL layout: one master-gain param. The full 91-param §3.0 catalogue is
    // task 020 — OUT OF SCOPE here. The ID is taken VERBATIM from core ParamIDs.h so
    // the bootstrap does not hand-mint a string.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ mw::params::ids::kVcaLevel, 1 },
        "Master Gain",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        1.0f));
    return layout;
}

// --- The three-call seam ------------------------------------------------------------

void MwAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Off-the-audio-thread setup. prepare() is the engine's ONLY allocation site
    // (ADR-001 C2). The seam signature is prepare(double, int maxBlock, int maxVoices);
    // the per-voice 2x-oversampled zone is selected/clamped inside the engine.
    engine_.prepare(sampleRate, samplesPerBlock, kMaxVoices);
    juce::ignoreUnused(kOversample); // documented seam intent; engine owns the factor

    // Pre-size the MIDI translation scratch off the audio thread (stand-in for the
    // §3.2 NormalizedEventBuffer; ADR-011 C9). Generous head room for dense input.
    events_.reserve(static_cast<size_t>(samplesPerBlock) * 4 + 256);
}

void MwAudioProcessor::releaseResources()
{
    engine_.reset();
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

    // Clear any output channels the engine will not write (the engine writes ch0 and,
    // if present, ch1).
    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numFrames);

    // 1. Drain the JUCE MidiBuffer into the pre-sized mw::MidiEvent scratch, ordered
    //    by sampleOffset (JUCE delivers it ordered). No allocation on the hot path:
    //    clear() keeps capacity; push_back stays within the reserved head room.
    events_.clear();
    for (const auto meta : midi)
    {
        mw::MidiEvent e{};
        if (toMidiEvent(meta.getMessage(), meta.samplePosition, e)
            && events_.size() < events_.capacity())
        {
            events_.push_back(e);
        }
    }

    // 2. Decode the host transport into the POD TransportInfo (§5.3). Absent playhead
    //    (e.g. Standalone) -> sane stopped defaults.
    mw::TransportInfo transport{};
    transport.sampleRate = getSampleRate();
    transport.bpm        = 120.0;
    transport.ppqPosition = 0.0;
    transport.isPlaying  = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())               transport.bpm        = *bpm;
            if (auto ppq = pos->getPpqPosition())        transport.ppqPosition = *ppq;
            transport.isPlaying = pos->getIsPlaying();
        }
    }

    // 3. Build the borrowed AudioBlockView over the host's channel pointers (§5.3) and
    //    assemble the BlockContext. params is null in the bootstrap — the full
    //    ParamSnapshot bridge is task 020; the engine never dereferences it.
    float* const* chans = buffer.getArrayOfWritePointers();
    mw::BlockContext ctx{};
    ctx.audio       = mw::AudioBlockView{ chans, numChannels, numFrames };
    ctx.params      = nullptr;
    ctx.transport   = transport;
    ctx.midi        = mw::MidiEventView{ events_.data(), static_cast<int>(events_.size()) };

    // 4. Pure render through the seam. The engine writes its stereo output directly
    //    into the borrowed host channels (ADR-001 C3).
    engine_.process(ctx);
}

// --- Editor -------------------------------------------------------------------------

juce::AudioProcessorEditor* MwAudioProcessor::createEditor()
{
    // Trivial generic editor for the bootstrap; the real UI is the ui stream (108+).
    return new juce::GenericAudioProcessorEditor(*this);
}

// --- State --------------------------------------------------------------------------

void MwAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // APVTS round-trip only (the master-gain param). Full preset/state serialization
    // is the state-presets stream.
    if (auto state = apvts_.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void MwAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(apvts_.state.getType()))
            apvts_.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

} // namespace mw::plugin

// -----------------------------------------------------------------------------------
// The JUCE plugin-instance factory. JUCE's wrappers (incl. Standalone) call this to
// construct the one shared processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mw::plugin::MwAudioProcessor();
}
