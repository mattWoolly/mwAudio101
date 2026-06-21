// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/SequencerGridTest.cpp — JUCE-linked Catch2 tests for the 100-step
// SequencerGrid editor (task 126, ui/modules/SequencerGrid.h), compiled into
// mw101_plugin_tests. Constructed against a REAL mw::plugin::MwAudioProcessor under a
// juce::ScopedJuceInitialiser_GUI (the sibling-UI-test pattern). Test-case NAMES begin
// with the task tag `ui_seqgrid` so `ctest -R ui_seqgrid` selects exactly these.
//
// Coverage maps 1:1 to the task-126 Acceptance criteria (each asserts a REAL effect):
//   (a) A cell edit routes THROUGH the processor <extras> handoff — after a simulated cell
//       edit the processor-visible pattern (seqPattern() / the audio-thread-adopted POD)
//       changes; the grid never touches a tree pointer into the audio thread (§9.3, ADR-008
//       C19/C20).
//   (b) The current-step highlight is driven by telemetry Snapshot.seqStep (feed a known
//       seqStep, assert the highlighted cell matches; editing the grid does NOT change the
//       highlight — display-only). Includes the CRITICAL all-ones-sentinel case: an
//       all-ones seqStep highlights NO cell (118d forward-note).
//   (c) A per-cell repaint invalidates ONLY that cell's bounds, never the whole component
//       (the captured dirty rect equals the cell bounds, not the component bounds).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/SequencerGrid.h"   // mw::ui::SequencerGrid
#include "../../ui/DesignTokens.h"            // mw::ui::DesignTokens
#include "PluginProcessor.h"                  // mw::plugin::MwAudioProcessor (plugin/ on include path)
#include "ui/Telemetry.h"                     // mw::ui::Telemetry::Snapshot (core/ on include path)
#include "state/Extras.h"                     // mw::state::Extras / kMaxSeqSteps (core/ on include path)

namespace {

constexpr int kGridW = 400;
constexpr int kGridH = 400;

// A snapshot whose seqStep is a known value (everything else default/idle).
mw::ui::Telemetry::Snapshot snapshotWithSeqStep(std::uint64_t step)
{
    mw::ui::Telemetry::Snapshot s{};
    s.seqStep = step;
    return s;
}

} // namespace

// ===========================================================================
// (a) A cell edit routes THROUGH the processor <extras> handoff.
// ===========================================================================
TEST_CASE("ui_seqgrid edits the pattern through the processor <extras> handoff, not a tree pointer",
          "[ui_seqgrid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(44'100.0, 256);

    const auto tokens = mw::ui::DesignTokens::defaultTheme();
    mw::ui::SequencerGrid grid(processor);
    grid.setTokens(tokens);
    grid.setBounds(0, 0, kGridW, kGridH);

    // The grid tiles EXACTLY the 100-step <extras> pattern.
    REQUIRE(mw::ui::SequencerGrid::cellCount() == mw::state::kMaxSeqSteps);
    REQUIRE(mw::ui::SequencerGrid::cellCount() == 100);

    constexpr int kEditedStep = 5;

    // Pre-condition: the step is NOT gated as seen through the processor accessor.
    REQUIRE_FALSE(processor.seqPattern().steps[kEditedStep].gate);
    REQUIRE_FALSE(grid.stepGateFromProcessor(kEditedStep));

    // Simulate a cell edit (the same path mouseDown -> toggleStepGate drives).
    grid.toggleStepGate(kEditedStep);

    // The PROCESSOR-VISIBLE pattern changed: the message-thread canonical copy now reflects
    // the gated step (the edit crossed the seam via setSeqPattern(), not a tree pointer).
    REQUIRE(processor.seqPattern().steps[kEditedStep].gate);
    REQUIRE(grid.stepGateFromProcessor(kEditedStep));

    // And the AUDIO THREAD adopts the edited pattern through the RT-safe handoff: drive a
    // block and read the adopted POD (proves the publish reached the audio side, not just a
    // local cache) [§9.3; ADR-008 C19/C20].
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    buffer.clear();
    processor.processBlock(buffer, midi);

    const mw::state::Extras adopted = processor.adoptedSeqPatternForTest();
    REQUIRE(adopted.steps[kEditedStep].gate);
    REQUIRE(adopted.stepCount >= kEditedStep + 1);

    // Toggling again flips it back (the edit is a real round-trip through the accessor).
    grid.toggleStepGate(kEditedStep);
    REQUIRE_FALSE(processor.seqPattern().steps[kEditedStep].gate);
}

// ===========================================================================
// (b) Current-step highlight is driven by telemetry Snapshot.seqStep (display-only).
// ===========================================================================
TEST_CASE("ui_seqgrid highlights the cell named by telemetry seqStep and is display-only",
          "[ui_seqgrid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(44'100.0, 256);

    const auto tokens = mw::ui::DesignTokens::defaultTheme();
    mw::ui::SequencerGrid grid(processor);
    grid.setTokens(tokens);
    grid.setBounds(0, 0, kGridW, kGridH);

    // No snapshot fed yet -> no cell highlighted.
    REQUIRE(grid.highlightedStepForTest() == -1);

    // Feed a known seqStep -> exactly that cell is highlighted.
    grid.setSnapshot(snapshotWithSeqStep(7));
    REQUIRE(grid.highlightedStepForTest() == 7);

    // A different seqStep moves the highlight.
    grid.setSnapshot(snapshotWithSeqStep(42));
    REQUIRE(grid.highlightedStepForTest() == 42);

    // DISPLAY-ONLY: editing the grid must NOT change the highlight (seqStep is a pure read;
    // an edit never writes it). Toggle a DIFFERENT cell's gate and confirm the highlight is
    // untouched.
    grid.toggleStepGate(3);
    REQUIRE(grid.highlightedStepForTest() == 42);   // unchanged by the edit
    REQUIRE(processor.seqPattern().steps[3].gate);   // the edit DID land on the pattern
}

// ===========================================================================
// (b-critical) An all-ones seqStep (the idle -1 sentinel widened to unsigned) highlights
// NO cell — NOT step 0 and NOT an out-of-bounds index (118d QA forward-note).
// ===========================================================================
TEST_CASE("ui_seqgrid treats an all-ones seqStep sentinel as NO highlight, never step 0",
          "[ui_seqgrid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(44'100.0, 256);

    const auto tokens = mw::ui::DesignTokens::defaultTheme();
    mw::ui::SequencerGrid grid(processor);
    grid.setTokens(tokens);
    grid.setBounds(0, 0, kGridW, kGridH);

    // First establish a real highlight so we can prove the sentinel CLEARS it (not merely
    // that the default state happens to be -1).
    grid.setSnapshot(snapshotWithSeqStep(4));
    REQUIRE(grid.highlightedStepForTest() == 4);

    // The all-ones sentinel: the engine publishes -1 when NOT playing; as an UNSIGNED
    // std::uint64_t that is 0xFFFFFFFFFFFFFFFF.
    const std::uint64_t allOnes = std::numeric_limits<std::uint64_t>::max();
    grid.setSnapshot(snapshotWithSeqStep(allOnes));
    REQUIRE(grid.highlightedStepForTest() == -1);   // NO active cell — not step 0

    // Belt-and-suspenders: a plainly out-of-range index (== step count, == 100) also
    // clears the highlight rather than reading as an out-of-bounds cell.
    grid.setSnapshot(snapshotWithSeqStep(4));
    REQUIRE(grid.highlightedStepForTest() == 4);
    grid.setSnapshot(snapshotWithSeqStep(static_cast<std::uint64_t>(mw::state::kMaxSeqSteps)));
    REQUIRE(grid.highlightedStepForTest() == -1);
}

// ===========================================================================
// (c) Per-cell repaint invalidates ONLY that cell's bounds, never the whole component.
// ===========================================================================
TEST_CASE("ui_seqgrid edit invalidates ONLY the edited cell's bounds, never the whole component",
          "[ui_seqgrid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(44'100.0, 256);

    const auto tokens = mw::ui::DesignTokens::defaultTheme();
    mw::ui::SequencerGrid grid(processor);
    grid.setTokens(tokens);
    grid.setBounds(0, 0, kGridW, kGridH);

    constexpr int kEditedStep = 23;
    const juce::Rectangle<int> cell = grid.cellBounds(kEditedStep);
    REQUIRE_FALSE(cell.isEmpty());

    // The cell is a strict sub-rectangle of the component (so "cell == component" can never
    // accidentally pass the assertion below).
    REQUIRE(grid.getLocalBounds().contains(cell));
    REQUIRE(cell != grid.getLocalBounds());

    // Edit the cell -> the captured dirty rect is EXACTLY that cell's bounds.
    grid.toggleStepGate(kEditedStep);
    REQUIRE(grid.hasInvalidated());
    REQUIRE(grid.lastInvalidatedRegion() == cell);
    REQUIRE(grid.lastInvalidatedRegion() != grid.getLocalBounds());   // never the whole component
}

// ===========================================================================
// (c) A telemetry step-advance also repaints only the affected cell(s), never the whole
// component.
// ===========================================================================
TEST_CASE("ui_seqgrid step-advance invalidates only the affected cell, never the whole component",
          "[ui_seqgrid]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor processor;
    processor.prepareToPlay(44'100.0, 256);

    const auto tokens = mw::ui::DesignTokens::defaultTheme();
    mw::ui::SequencerGrid grid(processor);
    grid.setTokens(tokens);
    grid.setBounds(0, 0, kGridW, kGridH);

    constexpr int kStep = 11;
    const juce::Rectangle<int> cell = grid.cellBounds(kStep);
    REQUIRE_FALSE(cell.isEmpty());

    const int before = grid.invalidationCountForTest();

    // A step-advance to kStep repaints kStep's cell (the newly-highlighted cell) only.
    grid.setSnapshot(snapshotWithSeqStep(kStep));
    REQUIRE(grid.invalidationCountForTest() > before);
    REQUIRE(grid.lastInvalidatedRegion() == cell);
    REQUIRE(grid.lastInvalidatedRegion() != grid.getLocalBounds());

    // Feeding the SAME seqStep again must NOT request any repaint (no visual change).
    const int afterFirst = grid.invalidationCountForTest();
    grid.setSnapshot(snapshotWithSeqStep(kStep));
    REQUIRE(grid.invalidationCountForTest() == afterFirst);
}
