// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControllerStripConstants.h — the (PI) DESIGN-UNIT layout constants
// for ui/modules/ControllerStrip (task 124). The CONTROLLER strip is a HORIZONTAL strip
// (vs a panel module) but, like every module, lays its children out in design units
// only (no pixel math) [docs/design/10-ui.md §5.3]: the parent passes a design-space
// rectangle to layoutDesignUnits() and the strip sub-divides it with the proportions
// named here.
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these layout literals is
// inlined at the layout call site; they centralize here so the visual-design pass can
// retune the strip's internal proportions in one place. This is a NEW header (NOT
// core/calibration/Calibration.h) so parallel UI/DSP work does not collide on the
// orchestrator-owned aggregate. Mirrors core/calibration/ModulatorModuleConstants.h.
//
// JUCE-FREE: these are plain float proportions; the strip applies them through
// juce::Rectangle math at the layout seam.

#pragma once

namespace mw::cal::ui::controller {

// The strip reserves a title cell on the left, then lays the controls out as a single
// horizontal row of equal-width cells to its right. All values are FRACTIONS of the
// design-space rectangle the parent supplies (0..1), so the layout is resolution- and
// pixel-independent [docs/design/10-ui.md §5.3].

// Fraction of the strip WIDTH reserved for the title cell (left edge). A strip layout
// puts the title at the side rather than across the top (a wide, short rectangle).
inline constexpr float kTitleWidthFraction = 0.10f;

// Inset (as a fraction of the SMALLER design-space dimension) applied uniformly around
// the control row so children never touch the strip outline.
inline constexpr float kContentInsetFraction = 0.06f;

// Horizontal gap between adjacent control cells, as a fraction of a single cell width.
inline constexpr float kCellGapFraction = 0.14f;

// Fraction of each cell's height reserved for the control's caption label (bottom of
// the cell); the remainder is the control's interactive area (top of the cell).
inline constexpr float kCaptionHeightFraction = 0.24f;

// The number of control cells the strip lays out in its single row:
// Glide Time, Glide Mode, Bend Range (VCO), Bend Range (VCF), Bend Dest, Mod Wheel->LFO.
inline constexpr int kControlCellCount = 6;

} // namespace mw::cal::ui::controller
