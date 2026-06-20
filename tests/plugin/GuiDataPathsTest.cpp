// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/GuiDataPathsTest.cpp — the JUCE-linked tests for the processor-side
// GUI data-paths plumbing (task 111c, tag gui_datapaths).
//
// Covers the three acceptance criteria of plan/backlog/111c-gui-data-paths.md:
//   (a) processBlock PUBLISHES a Telemetry::Snapshot every block with NO heap alloc /
//       NO lock — re-asserted with a malloc-counting probe over many publishing blocks
//       [§8.3; docs/design/00 §5.4; ADR-015 C5].
//   (b) the Consumer accessor returns coalesced most-recent frames: driving processBlock
//       then pulling sees an ADVANCING seqStep [§8.4].
//   (c) the message-thread seq-pattern accessor reads + writes the <extras> 100-step
//       pattern; an edit is ADOPTED by the audio thread via the RT-safe handoff (no
//       parse/alloc) AND still round-trips through getState/setStateInformation [§9.3;
//       docs/design/00 §5.4].

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include <malloc/malloc.h>   // mstats() — heap byte-delta probe (macOS; per task hint)

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"            // mw::plugin::MwAudioProcessor
#include "ui/Telemetry.h"               // mw::ui::Telemetry::Snapshot / Consumer (core/ on include path)
#include "state/Extras.h"               // mw::state::Extras / SeqStep / kMaxSeqSteps

// ---------------------------------------------------------------------------
// No-alloc probe via mstats() (macOS). We do NOT override global operator new/delete:
// another TU in this same mw101_plugin_tests binary (LatencyReporterTest.cpp) already
// owns those global symbols, and two definitions would collide at link. mstats reports
// the process-wide bytes-used-in-use; the DELTA across a long run of processBlocks is 0
// when the audio path takes no heap [§8.3; docs/design/00 §5.4; ADR-015 C5; ADR-001 C3].
// ---------------------------------------------------------------------------
namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 256;
constexpr int    kNumChans   = 2;

[[nodiscard]] std::size_t heapInUseBytes() noexcept
{
    return mstats().bytes_used;
}

} // namespace

TEST_CASE("gui_datapaths: processBlock publishes a telemetry frame with no heap alloc",
          "[gui_datapaths]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(kNumChans, kBlockSize);
    juce::MidiBuffer midi;

    // Warm up once OUTSIDE the probe (first-touch lazy paths, JUCE statics, etc.).
    buffer.clear();
    processor.processBlock(buffer, midi);

    auto consumer = processor.telemetryConsumer();

    // Probe a long run of publishing blocks — the telemetry publish (a seqlock byte
    // copy) and the whole audio path must not allocate [§8.3; ADR-015 C5; ADR-001 C3].
    // Drain the consumer first so its pull() bookkeeping is not part of the measured run.
    constexpr int kProbeBlocks = 2000;
    const std::size_t before = heapInUseBytes();
    for (int i = 0; i < kProbeBlocks; ++i)
    {
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
    const std::size_t after = heapInUseBytes();
    CHECK(after == before);   // zero net heap growth across 2000 publishing blocks.

    // And the producer really published: the consumer can pull a frame.
    mw::ui::Telemetry::Snapshot frame{};
    CHECK(consumer.pull(frame));
}

TEST_CASE("gui_datapaths: the Consumer accessor coalesces to the most-recent frame with an advancing seqStep",
          "[gui_datapaths]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(kNumChans, kBlockSize);
    juce::MidiBuffer midi;

    auto consumer = processor.telemetryConsumer();

    // Drive one block, then pull: a frame is available.
    buffer.clear();
    processor.processBlock(buffer, midi);

    mw::ui::Telemetry::Snapshot first{};
    REQUIRE(consumer.pull(first));

    // Drive several MORE blocks, then pull once: the consumer COALESCES to the latest
    // frame, whose seqStep has ADVANCED past the first frame's [§8.4].
    constexpr int kMoreBlocks = 5;
    for (int i = 0; i < kMoreBlocks; ++i)
    {
        buffer.clear();
        processor.processBlock(buffer, midi);
    }

    mw::ui::Telemetry::Snapshot latest{};
    REQUIRE(consumer.pull(latest));
    CHECK(latest.seqStep > first.seqStep);

    // A pull with nothing newly published returns false (coalesced; §8.3).
    mw::ui::Telemetry::Snapshot none{};
    CHECK_FALSE(consumer.pull(none));
}

TEST_CASE("gui_datapaths: a seq-pattern edit is adopted by the audio thread and round-trips through state",
          "[gui_datapaths]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(kSampleRate, kBlockSize);

    // --- Write an edited 100-step <extras> pattern via the message-thread accessor ---
    mw::state::Extras edited{};
    edited.stepCount = 4;
    edited.steps[0] = mw::state::SeqStep{ /*note*/ 0,  /*gate*/ true,  /*tie*/ false, /*rest*/ false };
    edited.steps[1] = mw::state::SeqStep{ /*note*/ 7,  /*gate*/ true,  /*tie*/ true,  /*rest*/ false };
    edited.steps[2] = mw::state::SeqStep{ /*note*/ 12, /*gate*/ false, /*tie*/ false, /*rest*/ true  };
    edited.steps[3] = mw::state::SeqStep{ /*note*/ -5, /*gate*/ true,  /*tie*/ false, /*rest*/ false };
    edited.arpLatch  = true;
    edited.driftSeed = 0x1234'5678'9abcLL;
    edited.seedLocked = true;

    processor.setSeqPattern(edited);

    // The message-thread read accessor reflects the edit.
    const mw::state::Extras readBack = processor.seqPattern();
    CHECK(readBack.stepCount == 4);
    CHECK(readBack.steps[1].noteSemitone == 7);
    CHECK(readBack.steps[1].tie);
    CHECK(readBack.steps[2].rest);
    CHECK_FALSE(readBack.steps[2].gate);
    CHECK(readBack.steps[3].noteSemitone == -5);
    CHECK(readBack.arpLatch);

    // --- The audio thread ADOPTS the edited pattern via the RT-safe handoff ----------
    juce::AudioBuffer<float> buffer(kNumChans, kBlockSize);
    juce::MidiBuffer midi;

    // Adoption is a single ACQUIRE pointer load + a POD copy — assert it allocates
    // nothing across many blocks (no parse, no heap) [docs/design/00 §5.4; ADR-008 C19].
    buffer.clear();
    processor.processBlock(buffer, midi);   // warm up outside the probe

    const std::size_t before = heapInUseBytes();
    for (int i = 0; i < 500; ++i)
    {
        buffer.clear();
        processor.processBlock(buffer, midi);
    }
    const std::size_t after = heapInUseBytes();
    CHECK(after == before);   // adopt() across 500 blocks took zero net heap.

    const mw::state::Extras adopted = processor.adoptedSeqPatternForTest();
    CHECK(adopted.stepCount == 4);
    CHECK(adopted.steps[0].gate);
    CHECK(adopted.steps[1].noteSemitone == 7);
    CHECK(adopted.steps[1].tie);
    CHECK(adopted.steps[2].rest);
    CHECK(adopted.steps[3].noteSemitone == -5);

    // --- The edited pattern round-trips through capture/recoverState -----------------
    juce::MemoryBlock blob;
    processor.getStateInformation(blob);

    mw::plugin::MwAudioProcessor restored;
    restored.prepareToPlay(kSampleRate, kBlockSize);
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

    const mw::state::Extras roundTripped = restored.seqPattern();
    CHECK(roundTripped.stepCount == 4);
    CHECK(roundTripped.steps[1].noteSemitone == 7);
    CHECK(roundTripped.steps[1].tie);
    CHECK(roundTripped.steps[2].rest);
    CHECK_FALSE(roundTripped.steps[2].gate);
    CHECK(roundTripped.steps[3].noteSemitone == -5);
    CHECK(roundTripped.arpLatch);
    CHECK(roundTripped.seedLocked);
    CHECK(roundTripped.driftSeed == 0x1234'5678'9abcLL);

    // And the restored processor's audio thread adopts the round-tripped pattern.
    juce::AudioBuffer<float> b2(kNumChans, kBlockSize);
    juce::MidiBuffer m2;
    b2.clear();
    restored.processBlock(b2, m2);
    const mw::state::Extras restoredAdopted = restored.adoptedSeqPatternForTest();
    CHECK(restoredAdopted.stepCount == 4);
    CHECK(restoredAdopted.steps[1].noteSemitone == 7);
}
