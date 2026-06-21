// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/TelemetryDatapathsTest.cpp — the JUCE-linked telemetry data-path tests
// (task 118d). Test-case names begin with "gui_datapaths" so `ctest -R gui_datapaths`
// selects exactly these under the silent-pass rule (AGENTS.md "Tests"); the display
// text avoids '[' so Catch2 does not mis-parse a tag and break ctest -R selection.
//
// Covers every acceptance criterion of plan/backlog/118d (the spec + both addenda):
//
//   PART A — REAL SEQ STEP (closes the 118c QA MEDIUM):
//     (A1) StepSequencer::currentSlot()/playPos() track the REAL playhead: currentSlot()
//          is -1 before any edge, then the slot each advanceOnEdge() actually played, and
//          resetToStart() rewinds it to -1 / slot 0 (the rewind the 118c mirror missed).
//     (A2) Engine::currentSeqStep() == the StepSequencer's real currentSlot() as a playing
//          pattern advances, INCLUDING after a clock-reset-on-keypress (LFO-trig mode +
//          clockResetOnKeypress) rewinds the sequencer mid-playback — the divergence path
//          the reconstructed (playPos+1)%count mirror got WRONG and that was previously
//          UNTESTED.
//     (A3) The processor publishes Snapshot.seqStep == engine_.currentSeqStep() (the live
//          step), not a monotonic counter — pulled through the real 107 SPSC telemetry path.
//
//   PART B — FILL THE ZERO FIELDS (closes the 127 QA MEDIUM):
//     (B1) Snapshot.scope[kScopePoints] is non-zero/varying when the engine produces sound
//          (the decimated post-VCA tap), instead of a flat zero line.
//     (B2) Snapshot.vcfCutoffDisplay tracks the dispatched cutoff: a high cutoff knob
//          publishes a higher display value than a low cutoff knob.
//     (B3) Snapshot.lfoPhase ADVANCES across blocks while the LFO runs (a moving indicator),
//          instead of staying at zero.
//
//   RT-SAFETY:
//     (RT) The telemetry-publishing processBlock is steady-state stable: repeated blocks
//          after a warm-up render finite output with no per-block growth (the publish is a
//          seqlock byte copy + getter reads + arithmetic over the output buffer — it adds
//          NO heap alloc / NO lock). The alloc/lock SENTINEL for the engine-side seq path
//          (the StepSequencer::currentSlot() read this task wires in) is asserted under an
//          armed AudioThreadGuard by the engine_seq RT test in the JUCE-free mw101_tests
//          target (that global-new-override sentinel only compiles into that binary).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"   // mw::plugin::MwAudioProcessor

#include "Engine.h"
#include "BlockContext.h"
#include "control/ControlTypes.h"
#include "control/SequencerEngine.h"
#include "control/StepSequencer.h"
#include "voice/VoiceManager.h"
#include "voice/VoiceTypes.h"
#include "params/ParamIDs.h"
#include "calibration/SequencerRoutingConstants.h"
#include "calibration/TelemetryConstants.h"
#include "ui/Telemetry.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 4096;
constexpr int    kMaxVoices = mw::kMaxVoices;

using mw::control::ControlSnapshot;
using mw::control::SeqStep;
using mw::control::ClockSource;
using mw::control::TrigMode;
using mw::control::kMaxSteps;

// Mutable handle to the engine's hosted SequencerEngine (the same const_cast pattern the
// engine_seq tests use). Off-audio-thread config only.
mw::seq::SequencerEngine& seqOf(mw::Engine& e) noexcept {
    return const_cast<mw::seq::SequencerEngine&>(e.sequencer());
}

// An all-note ascending pattern of `n` slots, internal clock at `rateHz`.
ControlSnapshot pattern(int n, float rateHz, bool resetOnKeypress, TrigMode trig) {
    ControlSnapshot s{};
    s.clockSource = ClockSource::Internal;
    s.internalRateHz = rateHz;
    s.clockResetOnKeypress = resetOnKeypress;
    s.trigMode = trig;
    s.seqCount = n;
    for (int i = 0; i < n && i < kMaxSteps; ++i)
        s.seq[static_cast<std::size_t>(i)].bits =
            static_cast<std::uint8_t>((i * 5 + 1) & SeqStep::kPitchMask);
    return s;
}

// A self-contained core block driver (matches the engine_seq suite).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];
    explicit Block(int n) : L(static_cast<std::size_t>(n), 0.0f),
                            R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }
    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n, bool playing) {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = nullptr;
        c.transport         = { 120.0, 0.0, playing, kSr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOn;
    e.noteId = static_cast<std::int16_t>(note);
    e.data0 = static_cast<float>(note);
    e.value = vel;
    e.sampleOffset = offset;
    return e;
}

// Set an APVTS param from its engineering value (drives the processor's dispatch).
void setParam(mw::plugin::MwAudioProcessor& proc, const char* id, float engineeringValue) {
    auto* p = proc.apvts().getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(engineeringValue));
}

} // namespace

// ===========================================================================
// PART A
// ===========================================================================

// (A1) StepSequencer::currentSlot()/playPos() track the REAL playhead, incl. the rewind.
TEST_CASE("gui_datapaths: StepSequencer currentSlot tracks the real playhead and the rewind",
          "[gui_datapaths]") {
    mw::control::StepSequencer seq;
    seq.prepare();

    // A 4-slot pattern.
    mw::control::SeqBuffer buf{};
    for (int i = 0; i < 4; ++i)
        buf[static_cast<std::size_t>(i)].bits =
            static_cast<std::uint8_t>((i + 1) & SeqStep::kPitchMask);
    seq.loadBuffer(buf, 4);
    seq.setPlay(true);

    // Before any edge: no step has played.
    REQUIRE(seq.currentSlot() == -1);
    REQUIRE(seq.playPos() == 0);

    seq.advanceOnEdge();                  // plays slot 0, advances playPos to 1
    REQUIRE(seq.currentSlot() == 0);
    REQUIRE(seq.playPos() == 1);

    seq.advanceOnEdge();                  // plays slot 1
    REQUIRE(seq.currentSlot() == 1);
    REQUIRE(seq.playPos() == 2);

    // The clock-reset rewind: currentSlot() reverts to "no step", playPos to 0 — the path
    // the 118c reconstructed mirror could not observe.
    seq.resetToStart();
    REQUIRE(seq.currentSlot() == -1);
    REQUIRE(seq.playPos() == 0);

    seq.advanceOnEdge();                  // first edge after the rewind plays slot 0 again
    REQUIRE(seq.currentSlot() == 0);
}

// (A2) Engine::currentSeqStep() == the real playhead as a pattern advances.
TEST_CASE("gui_datapaths: Engine currentSeqStep equals the real StepSequencer slot as it advances",
          "[gui_datapaths]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // 3-slot pattern, internal period == sr/2 == 24000 (exactly one edge per block).
    auto& seq = seqOf(eng);
    seq.publishSnapshot(pattern(3, 2.0f, /*resetOnKeypress=*/false, TrigMode::GateTrig));
    seq.setSeqPlay(true);
    const int period = static_cast<int>(std::lround(kSr / 2.0));

    REQUIRE(eng.currentSeqStep() == -1);

    auto stepAfterOneEdge = [&]() {
        Block blk(period);
        std::vector<mw::MidiEvent> none;
        auto c = blk.ctx(none, period, /*playing=*/true);
        eng.process(c);
        // The engine's published step must equal the StepSequencer's OWN real slot.
        REQUIRE(eng.currentSeqStep() == eng.sequencer().seq().currentSlot());
        return eng.currentSeqStep();
    };

    REQUIRE(stepAfterOneEdge() == 0);
    REQUIRE(stepAfterOneEdge() == 1);
    REQUIRE(stepAfterOneEdge() == 2);
    REQUIRE(stepAfterOneEdge() == 0);   // wraps
}

// (A2b) The DIVERGENCE PATH: a keypress mid-playback with clockResetOnKeypress=true (and
// LFO-trig, the mode that arms the reset) rewinds the sequencer to slot 0 — currentSeqStep()
// must report the REAL rewound slot, NOT the stale value the reconstructed mirror produced.
TEST_CASE("gui_datapaths: Engine currentSeqStep follows a clock-reset-on-keypress rewind",
          "[gui_datapaths]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // LFO-trig mode + clockResetOnKeypress=true: a gate-on key inside processBlock fires
    // clock_.resetToKeypress() + seq_.resetToStart(), rewinding playPos to 0.
    auto& seq = seqOf(eng);
    seq.publishSnapshot(pattern(5, 2.0f, /*resetOnKeypress=*/true, TrigMode::Lfo));
    seq.setSeqPlay(true);
    const int period = static_cast<int>(std::lround(kSr / 2.0));   // one edge / block

    // Advance several edges so the playhead is deep in the pattern (slot > 0).
    for (int i = 0; i < 3; ++i) {
        Block blk(period);
        std::vector<mw::MidiEvent> none;
        auto c = blk.ctx(none, period, /*playing=*/true);
        eng.process(c);
    }
    REQUIRE(eng.currentSeqStep() == eng.sequencer().seq().currentSlot());
    REQUIRE(eng.currentSeqStep() > 0);   // genuinely mid-pattern

    // Press a key at the very head of the next block: the keypress rewinds the sequencer,
    // and the single edge in this block then plays slot 0. A reconstructed (playPos+1)%count
    // mirror would have reported the WRONG (un-rewound) slot here.
    {
        const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;
        Block blk(period);
        std::vector<mw::MidiEvent> ev{ noteOn(base + 2, 1.0f, 0) };
        auto c = blk.ctx(ev, period, /*playing=*/true);
        eng.process(c);
    }

    // currentSeqStep() must equal the StepSequencer's REAL slot, which the rewind drove to 0.
    REQUIRE(eng.currentSeqStep() == eng.sequencer().seq().currentSlot());
    REQUIRE(eng.currentSeqStep() == 0);
}

// (A3) The PROCESSOR publishes Snapshot.seqStep == engine_.currentSeqStep() through the real
// 107 SPSC telemetry path (a running transport + a playing seq routed through the processor).
TEST_CASE("gui_datapaths: the processor publishes the live seq step through telemetry",
          "[gui_datapaths]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    // Configure the engine's sequencer to play a pattern (the processor routes through it only
    // when the host transport is playing — which the headless host reports as stopped, so we
    // drive the engine's seq path config directly and assert the PUBLISHED value mirrors the
    // engine's currentSeqStep regardless of which slot it lands on).
    auto& eng = const_cast<mw::Engine&>(proc.engineForTest());
    auto& seq = seqOf(eng);
    seq.publishSnapshot(pattern(4, 2.0f, /*resetOnKeypress=*/false, TrigMode::GateTrig));
    seq.setSeqPlay(true);

    auto consumer = proc.telemetryConsumer();

    juce::AudioBuffer<float> buffer(2, kMaxBlock);
    juce::MidiBuffer midi;
    buffer.clear();
    proc.processBlock(buffer, midi);

    mw::ui::Telemetry::Snapshot frame{};
    REQUIRE(consumer.pull(frame));

    // The published step is the engine's REAL live step widened to the uint64 field — NOT a
    // monotonic per-block counter.
    const std::uint64_t expected = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(eng.currentSeqStep()));
    REQUIRE(frame.seqStep == expected);

    proc.releaseResources();
}

// ===========================================================================
// PART B
// ===========================================================================

// (B1) scope[kScopePoints] is non-zero/varying when the engine produces sound.
TEST_CASE("gui_datapaths: the published scope is a non-flat tap of the rendered output",
          "[gui_datapaths]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    // Open the VCA + saw level so a held note produces sound through the dispatch.
    setParam(proc, mw::params::ids::kVcaLevel, 0.8f);
    setParam(proc, mw::params::ids::kSawLevel, 0.8f);
    setParam(proc, mw::params::ids::kVcfCutoff, 0.9f);

    auto consumer = proc.telemetryConsumer();

    juce::AudioBuffer<float> buffer(2, kMaxBlock);
    juce::MidiBuffer midi;

    // Render several blocks with a held note so the voice opens and sounds.
    mw::ui::Telemetry::Snapshot frame{};
    bool sawNonFlatScope = false;
    bool gotFrame = false;
    for (int blk = 0; blk < 8; ++blk) {
        buffer.clear();
        midi.clear();
        if (blk == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        proc.processBlock(buffer, midi);

        if (consumer.pull(frame)) {
            gotFrame = true;
            float mn = frame.scope[0], mx = frame.scope[0];
            for (int i = 0; i < mw::cal::telemetry::kScopePoints; ++i) {
                mn = std::min(mn, frame.scope[static_cast<std::size_t>(i)]);
                mx = std::max(mx, frame.scope[static_cast<std::size_t>(i)]);
            }
            // A flat zero line would give mn == mx == 0; a real tap of an audible block
            // varies across the array.
            if (mx > mn && (std::fabs(mx) > 1.0e-6f || std::fabs(mn) > 1.0e-6f))
                sawNonFlatScope = true;
        }
    }

    REQUIRE(gotFrame);
    REQUIRE(sawNonFlatScope);

    proc.releaseResources();
}

// (B2) vcfCutoffDisplay tracks the dispatched cutoff (high knob -> higher display).
TEST_CASE("gui_datapaths: the published vcfCutoffDisplay tracks the dispatched cutoff",
          "[gui_datapaths]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    auto displayForCutoff = [](float cutoff01) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kMaxBlock);
        setParam(proc, mw::params::ids::kVcaLevel, 0.8f);
        setParam(proc, mw::params::ids::kSawLevel, 0.8f);
        setParam(proc, mw::params::ids::kVcfCutoff, cutoff01);

        auto consumer = proc.telemetryConsumer();
        juce::AudioBuffer<float> buffer(2, kMaxBlock);
        juce::MidiBuffer midi;
        mw::ui::Telemetry::Snapshot frame{};
        float last = 0.0f;
        for (int blk = 0; blk < 4; ++blk) {
            buffer.clear();
            midi.clear();
            if (blk == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            proc.processBlock(buffer, midi);
            if (consumer.pull(frame))
                last = frame.vcfCutoffDisplay;
        }
        proc.releaseResources();
        return last;
    };

    const float lowDisplay  = displayForCutoff(0.1f);
    const float highDisplay = displayForCutoff(0.9f);

    // The display is a 0..1 value and a higher cutoff knob publishes a higher display value.
    REQUIRE(highDisplay > lowDisplay);
    REQUIRE(highDisplay <= 1.0f);
    REQUIRE(lowDisplay >= 0.0f);
}

// (B3) lfoPhase ADVANCES across blocks while the LFO runs.
TEST_CASE("gui_datapaths: the published lfoPhase advances while the LFO runs",
          "[gui_datapaths]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    setParam(proc, mw::params::ids::kVcaLevel, 0.8f);
    setParam(proc, mw::params::ids::kSawLevel, 0.8f);
    setParam(proc, mw::params::ids::kLfoRate, 20.0f);   // a fast, non-zero LFO rate

    auto consumer = proc.telemetryConsumer();
    juce::AudioBuffer<float> buffer(2, kMaxBlock);
    juce::MidiBuffer midi;
    mw::ui::Telemetry::Snapshot frame{};

    // First block: hold a note so the dispatch runs the LFO; capture the phase.
    buffer.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    proc.processBlock(buffer, midi);
    REQUIRE(consumer.pull(frame));
    const std::uint32_t phase0 = frame.lfoPhase;

    // A later block: the phase must have ADVANCED (it would stay 0 if the field were unfilled).
    std::uint32_t phase1 = phase0;
    for (int blk = 0; blk < 4; ++blk) {
        buffer.clear();
        midi.clear();
        proc.processBlock(buffer, midi);
        if (consumer.pull(frame))
            phase1 = frame.lfoPhase;
    }

    REQUIRE(phase1 != phase0);

    proc.releaseResources();
}

// ===========================================================================
// RT-SAFETY (steady-state stability)
// ===========================================================================

// (RT) The telemetry-publishing processBlock is steady-state stable: after a warm-up block,
// repeated blocks publish finite frames with a continuously-advancing telemetry counter and
// no torn / non-finite data — consistent with a seqlock byte copy + getter reads + arithmetic
// over the output buffer (NO heap alloc / NO lock added). The hard alloc/lock SENTINEL for the
// engine-side seq path this task wires in (StepSequencer::currentSlot()) is asserted under an
// armed AudioThreadGuard by the engine_seq RT test in the JUCE-free mw101_tests target.
TEST_CASE("gui_datapaths: the telemetry-publishing processBlock is steady-state stable",
          "[gui_datapaths]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    setParam(proc, mw::params::ids::kVcaLevel, 0.8f);
    setParam(proc, mw::params::ids::kSawLevel, 0.8f);
    setParam(proc, mw::params::ids::kLfoRate, 10.0f);

    auto consumer = proc.telemetryConsumer();
    juce::AudioBuffer<float> buffer(2, kMaxBlock);
    juce::MidiBuffer midi;

    // Warm once so any one-time lazy realization happens before the steady-state loop.
    buffer.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    proc.processBlock(buffer, midi);
    mw::ui::Telemetry::Snapshot frame{};
    consumer.pull(frame);

    // Every subsequent block must publish a fresh, finite frame (a new frame each block proves
    // the producer keeps committing; finite scope proves no torn / NaN data).
    for (int blk = 0; blk < 16; ++blk) {
        buffer.clear();
        midi.clear();
        proc.processBlock(buffer, midi);
        REQUIRE(consumer.pull(frame));   // a NEW frame was published this block
        for (int i = 0; i < mw::cal::telemetry::kScopePoints; ++i)
            REQUIRE(std::isfinite(frame.scope[static_cast<std::size_t>(i)]));
        REQUIRE(std::isfinite(frame.vcfCutoffDisplay));
    }

    proc.releaseResources();
}
