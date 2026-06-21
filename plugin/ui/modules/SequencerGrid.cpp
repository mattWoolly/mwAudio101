// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/SequencerGrid.cpp — implementation of the 100-step sequencer grid
// editor declared in ui/modules/SequencerGrid.h [docs/design/10-ui.md §5.1, §5.3, §9.3;
// ADR-015 C3, C7; ADR-008 C19/C20].
//
// The grid edits the non-APVTS <extras> 100-step pattern THROUGH the processor's SPSC
// handoff accessor pair (seqPattern() / setSeqPattern()) — it never holds a tree pointer
// into the audio thread, takes no lock, and never calls processor DSP [§9.3; docs/design/
// 00 §5.4; ADR-008 C19/C20]. The current-step highlight is DISPLAY-ONLY, fed via a
// Telemetry::Snapshot value copy; the grid never polls the engine [§8.3; ADR-015 C4]. A
// cell edit or step-advance invalidates ONLY the affected cell's bounds — never the whole
// component [§7.3; ADR-015 C7].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) so the plugin glob compiles it
// into the plugin target + mw101_plugin_tests (CONFIGURE_DEPENDS). The design-faithful
// header stays at ui/modules/SequencerGrid.h, reached by a relative include — no shared
// CMakeLists edit (mirrors plugin/ui/modules/VcoModule.cpp).

#include "../../../ui/modules/SequencerGrid.h"

#include "../../../core/calibration/SequencerGridConstants.h"
#include "../../PluginProcessor.h"   // mw::plugin::MwAudioProcessor — the <extras> handoff accessor
#include "../../../ui/MwAudioLookAndFeel.h"  // toColour() — the token -> juce::Colour lift seam

#include <algorithm>
#include <limits>

namespace mw::ui {

namespace cal = mw::cal::ui::seqgrid;

namespace {

// kRows * kColumns MUST tile the fixed pattern capacity exactly — no orphan cell.
static_assert(cal::kRows * cal::kColumns == mw::state::kMaxSeqSteps,
              "SequencerGrid: kRows * kColumns MUST equal mw::state::kMaxSeqSteps (100) "
              "so the grid tiles the <extras> pattern with no orphan cell [ADR-008 C20].");

} // namespace

SequencerGrid::SequencerGrid(mw::plugin::MwAudioProcessor& processor)
    : ModuleBase(processor.apvts(), "SEQ"),
      processor_(processor),
      tokens_(nullptr)
{
    setInterceptsMouseClicks(true, true);  // the grid is an editor: it handles cell clicks
}

SequencerGrid::~SequencerGrid() = default;

void SequencerGrid::setTokens(const DesignTokens& tokens)
{
    tokens_ = &tokens;
    repaint(getLocalBounds());   // OWN bounds only: restyle on the next paint [§6.1]
}

// ---------------------------------------------------------------------------
// cellCount / resolveHighlight
// ---------------------------------------------------------------------------
int SequencerGrid::cellCount() noexcept
{
    return mw::state::kMaxSeqSteps;
}

// Interpret a raw telemetry seqStep into a highlighted cell index. Snapshot.seqStep is an
// UNSIGNED std::uint64_t: the sequencer-idle -1 sentinel WIDENS to all-ones, and any value
// >= the step count is likewise out of range. BOTH map to NO highlight (-1), never to step
// 0 and never to an out-of-bounds index (118d QA forward-note) [§8.3].
int SequencerGrid::resolveHighlight(std::uint64_t seqStep) noexcept
{
    if (seqStep >= static_cast<std::uint64_t>(mw::state::kMaxSeqSteps))
        return -1;   // all-ones sentinel OR any out-of-range value -> no active cell
    return static_cast<int>(seqStep);
}

// ---------------------------------------------------------------------------
// setSnapshot — display-only current-step highlight (driven by the editor Timer, 115).
// Cache the resolved highlight; on a CHANGE, repaint ONLY the old + new cells (each its
// own bounds). NEVER writes the <extras> pattern, NEVER touches seqStep on the engine
// [§8.3; ADR-015 C4/C7].
// ---------------------------------------------------------------------------
void SequencerGrid::setSnapshot(const Telemetry::Snapshot& snapshot)
{
    const int next = resolveHighlight(snapshot.seqStep);
    if (next == highlightedStep_)
        return;   // no visual change -> no repaint

    const int previous = highlightedStep_;
    highlightedStep_ = next;

    // Targeted per-cell repaint of the cells whose highlight state changed — never the
    // whole component [§7.3; ADR-015 C7].
    if (previous >= 0)
        invalidateCell(previous);
    if (next >= 0)
        invalidateCell(next);
}

// ---------------------------------------------------------------------------
// toggleStepGate — the SOLE cell-edit write path. Routes THROUGH the processor's <extras>
// SPSC handoff accessor pair: read the canonical pattern by value, flip the gate, write it
// back via setSeqPattern() (which publishes the edited POD to the audio thread). NO tree
// pointer, NO lock, NO DSP call [§9.3; ADR-008 C19/C20].
// ---------------------------------------------------------------------------
void SequencerGrid::toggleStepGate(int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= mw::state::kMaxSeqSteps)
        return;   // out of range: safe no-op

    mw::state::Extras pattern = processor_.seqPattern();   // message-thread read (value copy)

    pattern.steps[static_cast<std::size_t>(stepIndex)].gate =
        !pattern.steps[static_cast<std::size_t>(stepIndex)].gate;

    // Grow the active-step count to cover the edited index so a freshly-gated step beyond
    // the current run length is actually part of the pattern (clamped to capacity).
    if (stepIndex >= pattern.stepCount)
        pattern.stepCount = std::min(stepIndex + 1, mw::state::kMaxSeqSteps);

    processor_.setSeqPattern(pattern);   // RT-safe message->audio publish [§9.3]

    invalidateCell(stepIndex);           // per-cell dirty-rect only [§7.3]
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------
bool SequencerGrid::stepGateFromProcessor(int stepIndex) const noexcept
{
    if (stepIndex < 0 || stepIndex >= mw::state::kMaxSeqSteps)
        return false;
    return processor_.seqPattern().steps[static_cast<std::size_t>(stepIndex)].gate;
}

juce::Rectangle<int> SequencerGrid::cellBounds(int stepIndex) const noexcept
{
    if (stepIndex < 0 || stepIndex >= mw::state::kMaxSeqSteps)
        return {};
    return cellBounds_[static_cast<std::size_t>(stepIndex)];
}

// ---------------------------------------------------------------------------
// invalidateCell — invalidate ONLY this cell's bounds and record it behind the test seam.
// A child Component's repaint(rect) marks only that rect dirty — never the whole editor
// [§7.3; ADR-015 C7].
// ---------------------------------------------------------------------------
void SequencerGrid::invalidateCell(int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= mw::state::kMaxSeqSteps)
        return;

    lastDirty_ = cellBounds_[static_cast<std::size_t>(stepIndex)];
    invalidated_ = true;
    ++invalidationCount_;
    repaint(lastDirty_);   // ONLY this cell's bounds [ADR-015 C7]
}

// ---------------------------------------------------------------------------
// Hit-testing
// ---------------------------------------------------------------------------
int SequencerGrid::cellIndexAt(juce::Point<int> pixel) const noexcept
{
    for (int i = 0; i < mw::state::kMaxSeqSteps; ++i)
        if (cellBounds_[static_cast<std::size_t>(i)].contains(pixel))
            return i;
    return -1;
}

void SequencerGrid::mouseDown(const juce::MouseEvent& e)
{
    const int idx = cellIndexAt(e.getPosition());
    if (idx >= 0)
        toggleStepGate(idx);
}

// ---------------------------------------------------------------------------
// layoutDesignUnits — fixed kRows x kColumns matrix beneath the title strip, in DESIGN
// units only (no pixel literals); the proportions are the (PI) constants [§5.3].
// ---------------------------------------------------------------------------
void SequencerGrid::layoutDesignUnits(juce::Rectangle<float> designBounds)
{
    auto area = designBounds;

    // Reserve the title strip across the top (matches the sibling-module convention).
    area.removeFromTop(area.getHeight() * cal::kTitleHeightFraction);

    // Uniform inset around the cell matrix (fraction of the smaller design dimension).
    const float inset = juce::jmin(designBounds.getWidth(), designBounds.getHeight())
                        * cal::kContentInsetFraction;
    area.reduce(inset, inset);

    // Equal-extent cells with a proportional inter-cell gap on both axes.
    constexpr int cols = cal::kColumns;
    constexpr int rows = cal::kRows;

    const float gapX = (area.getWidth()  / static_cast<float>(cols)) * cal::kCellGapFraction;
    const float gapY = (area.getHeight() / static_cast<float>(rows)) * cal::kCellGapFraction;
    const float cellW = (area.getWidth()  - gapX * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float cellH = (area.getHeight() - gapY * static_cast<float>(rows - 1)) / static_cast<float>(rows);

    // Steps run left-to-right, top-to-bottom: step 0 is the top-left cell.
    for (int i = 0; i < mw::state::kMaxSeqSteps; ++i)
    {
        const int row = i / cols;
        const int col = i % cols;
        const float x = area.getX() + static_cast<float>(col) * (cellW + gapX);
        const float y = area.getY() + static_cast<float>(row) * (cellH + gapY);
        cellBounds_[static_cast<std::size_t>(i)] =
            juce::Rectangle<float>{ x, y, cellW, cellH }.toNearestInt();
    }
}

void SequencerGrid::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

// ---------------------------------------------------------------------------
// paint — each cell as a token-driven rounded rect: a quiet track when off, the accent
// fill when its step is gated, and a distinct thumb-colour OUTLINE when it is the
// telemetry-highlighted current step (display-only) [§6.1, §8.3].
// ---------------------------------------------------------------------------
void SequencerGrid::paint(juce::Graphics& g)
{
    if (tokens_ == nullptr)
        return;

    const DesignTokens& t = *tokens_;
    const mw::state::Extras pattern = processor_.seqPattern();   // message-thread display read

    for (int i = 0; i < mw::state::kMaxSeqSteps; ++i)
    {
        const auto cell = cellBounds_[static_cast<std::size_t>(i)].toFloat();
        if (cell.isEmpty())
            continue;

        const bool gated = pattern.steps[static_cast<std::size_t>(i)].gate;

        // Cell body: accent fill when gated, quiet track otherwise.
        g.setColour(MwAudioLookAndFeel::toColour(gated ? t.controlFill : t.controlTrack));
        g.fillRoundedRectangle(cell, t.cornerRadius);

        // Current-step highlight: a distinct outline around the telemetry-highlighted cell
        // only. DISPLAY-ONLY — derived from setSnapshot(), never from an edit [§8.3].
        if (i == highlightedStep_)
        {
            g.setColour(MwAudioLookAndFeel::toColour(t.controlThumb));
            g.drawRoundedRectangle(cell, t.cornerRadius, t.controlStroke);
        }
    }
}

} // namespace mw::ui
