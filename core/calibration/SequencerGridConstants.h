// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/SequencerGridConstants.h — the (PI) DESIGN-UNIT layout constants for
// ui/modules/SequencerGrid (task 126). The 100-step grid lays its cells out in design
// units only (no pixel math) [docs/design/10-ui.md §5.3]; the parent passes a
// design-space rectangle to layoutDesignUnits() and the module sub-divides it into a
// fixed kRows x kColumns grid using the proportions named here.
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these layout literals is
// inlined at the layout / draw call site; they centralize here so the visual-design pass
// can retune the module's internal proportions in one place. This is a NEW dedicated
// header (NOT core/calibration/Calibration.h) so parallel UI/DSP work does not collide on
// the orchestrator-owned aggregate (mirrors VcoModuleConstants.h / ScopeMeterConstants.h).
//
// JUCE-FREE: these are plain int counts / float proportions; the module applies them
// through juce::Rectangle math at the layout seam. mwcore stays JUCE-free [ADR-001 C1].

#pragma once

namespace mw::cal::ui::seqgrid {

// The grid holds EXACTLY mw::state::kMaxSeqSteps (== 100) cells, arranged as a fixed
// kRows x kColumns matrix. 10 x 10 evenly tiles the 100-step <extras> pattern with no
// orphan cell [docs/design/06 §5.4; ADR-008 C20]. (kRows * kColumns MUST equal the
// pattern capacity; the module asserts that against mw::state::kMaxSeqSteps.)
inline constexpr int kRows    = 10;
inline constexpr int kColumns = 10;

// Fraction of the module height reserved for the title strip across the top (matches the
// VcoModule title-strip convention so the modules read as a family).
inline constexpr float kTitleHeightFraction = 0.18f;

// Inset (as a fraction of the SMALLER design-space dimension) applied uniformly around
// the cell matrix so cells never touch the module outline.
inline constexpr float kContentInsetFraction = 0.04f;

// Gap between adjacent cells (both axes), as a fraction of a single cell's extent on that
// axis. A small gap keeps the cells visually distinct without bespoke pixel math.
inline constexpr float kCellGapFraction = 0.18f;

} // namespace mw::cal::ui::seqgrid
