// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/SequencerGrid.h — the 100-step sequencer GRID editor module
// [docs/design/10-ui.md §5.1, §5.3, §9.3 table row "Sequencer grid"; ADR-015 C3, C7;
// ADR-008 C19/C20].
//
// CONTRACT (task 126):
//   • EDITS the non-APVTS <extras> 100-step pattern THROUGH the processor's SPSC handoff
//     accessor pair (MwAudioProcessor::seqPattern() / setSeqPattern()). A cell edit reads
//     the current pattern by value, mutates the affected SeqStep, and writes it back via
//     setSeqPattern() — which publishes the edited POD to the audio thread through the
//     RT-safe double-buffer swap. The grid NEVER holds a tree pointer into the audio
//     thread, takes no lock, and never calls processor DSP [docs/design/10-ui.md §9.3;
//     docs/design/00 §5.4; ADR-008 C19/C20]. This is the SAME message->audio seam the
//     PresetBrowser / UI-node persistence already cross (114/115/128).
//   • CURRENT-STEP HIGHLIGHT is DISPLAY-ONLY, driven from a Telemetry::Snapshot handed in
//     via setSnapshot() (the editor's coalescing Timer feeds it the live slot 118d
//     publishes). The grid NEVER polls the engine [docs/design/10-ui.md §8.2/§8.3;
//     ADR-015 C4]. Editing the grid NEVER writes seqStep — seqStep is a pure read.
//       *** Snapshot.seqStep is an UNSIGNED std::uint64_t. When the sequencer is NOT
//           playing the engine publishes the -1 sentinel, which WIDENS to all-ones
//           (0xFFFFFFFFFFFFFFFF). The grid interprets all-ones — and ANY value >= the
//           step count — as NO active cell (no highlight), NEVER as step 0 and NEVER as
//           an out-of-bounds index. (118d QA forward-note.) ***
//   • PER-CELL DIRTY-RECT REPAINT: a cell edit OR a step-advance invalidates ONLY the
//     affected cell's bounds (repaint(cellBounds)), never the whole component
//     [docs/design/10-ui.md §5.3/§7.3; ADR-015 C7]. A whole-component repaint() is a
//     frame-cost regression. The last-invalidated region is captured behind a test seam.
//   • layoutDesignUnits() positions the cells in DESIGN units only (no pixel math); the
//     fixed kRows x kColumns matrix proportions are the (PI) constants in
//     core/calibration/SequencerGridConstants.h [docs/design/10-ui.md §5.3].
//
// OUT OF SCOPE (deliberately NOT owned here): the arp/seq engine SEMANTICS (the
// mod-arp-seq stream), the transport mode controls (task 125 / TransportModeBar), and
// wiring this module INTO MwAudioEditor (task 114c). This module only creates the grid +
// the message->audio edit seam + the display-only highlight.
//
// This header is JUCE-built and lives at the design-faithful path ui/modules/; its .cpp
// lives under plugin/ui/modules/ so the plugin glob compiles it (mirrors
// plugin/ui/modules/VcoModule.cpp). It derives the same ModuleBase the sibling modules
// use; ModuleBase carries the shared APVTS seam, while the <extras> pattern crosses the
// thread boundary through the processor accessor — NOT APVTS [ADR-008 §4/§5 — the
// sequencer pattern is a non-APVTS <extras> payload, not a host parameter].

#pragma once

#include <array>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ModuleBase.h"
#include "../DesignTokens.h"

#include "ui/Telemetry.h"        // mwcore (JUCE-free, core/ on include path): Telemetry::Snapshot
#include "state/Extras.h"        // mwcore (JUCE-free): mw::state::Extras / SeqStep / kMaxSeqSteps

namespace mw::plugin { class MwAudioProcessor; }

namespace mw::ui {

class SequencerGrid final : public ModuleBase
{
public:
    // The grid binds to the processor's APVTS (the ModuleBase shared seam) AND holds a
    // reference to the processor so pattern edits route through the <extras> SPSC handoff
    // accessor pair (seqPattern() / setSeqPattern()) — NOT through APVTS and NOT through a
    // tree pointer into the audio thread [docs/design/10-ui.md §9.3; ADR-008 C19/C20].
    explicit SequencerGrid(mw::plugin::MwAudioProcessor& processor);
    ~SequencerGrid() override;

    // Inject the design tokens (a reskin re-tints with no code change) [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // --- Current-step highlight feed (message thread; driven by the editor Timer, 115) ---
    // Cache the most-recent display frame and, when the highlighted cell CHANGES, request a
    // TARGETED repaint of ONLY the previously-highlighted cell and the newly-highlighted
    // cell (each its own bounds) — never the whole component [§7.3, §8.4; ADR-015 C7].
    // DISPLAY-ONLY: this NEVER writes the <extras> pattern and NEVER touches seqStep on the
    // engine. An all-ones / out-of-range seqStep clears the highlight (no active cell).
    void setSnapshot(const Telemetry::Snapshot& snapshot);

    // --- Cell editing (message thread) -> audio-thread handoff --------------------------
    // Toggle the gate of the step at `stepIndex` (0..kMaxSeqSteps-1): read the current
    // pattern via the processor accessor, flip steps[stepIndex].gate (growing stepCount to
    // cover the edited index if needed), and write it back via setSeqPattern() — the
    // RT-safe message->audio publish. Invalidates ONLY that cell's bounds. Out-of-range is
    // a safe no-op. This is the SOLE write path for a cell edit [§9.3; ADR-008 C19/C20].
    void toggleStepGate(int stepIndex);

    // --- Inspection (message thread; pure reads for tests / editor) ---------------------
    // The fixed cell count (== mw::state::kMaxSeqSteps == 100).
    [[nodiscard]] static int cellCount() noexcept;

    // The 0-based index of the cell currently highlighted by the telemetry seqStep, or -1
    // when NO cell is highlighted (sequencer idle: all-ones sentinel, or an out-of-range
    // value). Pure display state derived from the last setSnapshot() [§8.3].
    [[nodiscard]] int highlightedStepForTest() const noexcept { return highlightedStep_; }

    // The design-unit / pixel bounds of one cell within the laid-out grid (for tests and
    // hit-testing). Empty rectangle for an out-of-range index.
    [[nodiscard]] juce::Rectangle<int> cellBounds(int stepIndex) const noexcept;

    // --- Per-control dirty-rect discipline test seam (§7.3; ADR-015 C7) -----------------
    // The last region repaint() was asked to invalidate, and whether anything was. A test
    // asserts the dirty rect equals the affected CELL bounds, never the whole component.
    [[nodiscard]] const juce::Rectangle<int>& lastInvalidatedRegion() const noexcept { return lastDirty_; }
    [[nodiscard]] bool hasInvalidated() const noexcept { return invalidated_; }
    [[nodiscard]] int  invalidationCountForTest() const noexcept { return invalidationCount_; }

    // The current gate of the step at `stepIndex` AS SEEN THROUGH the processor accessor
    // (so a test reads the message-thread canonical pattern the audio thread will adopt).
    [[nodiscard]] bool stepGateFromProcessor(int stepIndex) const noexcept;

    // --- juce::Component ----------------------------------------------------------------
    void paint(juce::Graphics&) override;
    void resized() override;

    // Lay the 100 cells out as a fixed kRows x kColumns matrix beneath the title strip, in
    // DESIGN units only (no pixel math) [§5.3].
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // Mouse: a click toggles the step under the cursor (routes through toggleStepGate).
    void mouseDown(const juce::MouseEvent& e) override;

private:
    // Map a step index to its (row, col) and back to a cell rectangle. kColumns-major so
    // the top-left cell is step 0 and steps run left-to-right, top-to-bottom.
    [[nodiscard]] int cellIndexAt(juce::Point<int> pixel) const noexcept;

    // Invalidate ONLY the given cell's bounds (the per-cell dirty-rect; never the whole
    // component) and record it behind the test seam [§7.3; ADR-015 C7].
    void invalidateCell(int stepIndex);

    // Interpret a raw telemetry seqStep into a highlighted cell index, applying the
    // all-ones / out-of-range -> NO highlight rule (returns -1 for no active cell).
    [[nodiscard]] static int resolveHighlight(std::uint64_t seqStep) noexcept;

    mw::plugin::MwAudioProcessor& processor_;   // for the <extras> seqPattern handoff (§9.3)

    const DesignTokens* tokens_;                // non-owning; the editor owns the table

    // The laid-out pixel bounds of each cell (recomputed in layoutDesignUnits()). A cached
    // geometry table so a per-cell repaint() and hit-test need no re-derivation.
    std::array<juce::Rectangle<int>, mw::state::kMaxSeqSteps> cellBounds_{};

    // The current display-only highlight (cell index, or -1 for none). Driven solely by
    // setSnapshot(); NEVER written by an edit [§8.3].
    int highlightedStep_ = -1;

    // Per-cell dirty-rect test seam (see lastInvalidatedRegion()).
    juce::Rectangle<int> lastDirty_{};
    bool invalidated_ = false;
    int  invalidationCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerGrid)
};

} // namespace mw::ui
