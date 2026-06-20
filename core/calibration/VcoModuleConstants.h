// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/VcoModuleConstants.h — the (PI) DESIGN-UNIT layout constants for
// ui/modules/VcoModule (task 120). The module lays its children out in design units
// only (no pixel math) [docs/design/10-ui.md §5.3]; the parent passes a design-space
// rectangle to layoutDesignUnits() and the module sub-divides it with the proportions
// named here.
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these layout literals is
// inlined at the layout call site; they centralize here so the visual-design pass can
// retune the module's internal proportions in one place. This is a NEW header (NOT
// core/calibration/Calibration.h) so parallel UI/DSP work does not collide on the
// orchestrator-owned aggregate (mirrors ModulatorModuleConstants.h, task 117).
//
// JUCE-FREE: these are plain float proportions; the module applies them through JUCE
// juce::Rectangle math at the layout seam.

#pragma once

namespace mw::cal::ui::vco {

// The module reserves a title strip across the top, then lays the controls out as a
// single row of equal-width cells beneath it. All values are FRACTIONS of the
// design-space rectangle the parent supplies (0..1), so the layout is resolution- and
// pixel-independent [docs/design/10-ui.md §5.3].

// Fraction of the module height reserved for the title strip (top).
inline constexpr float kTitleHeightFraction = 0.18f;

// Inset (as a fraction of the SMALLER design-space dimension) applied uniformly
// around the control row so children never touch the module outline.
inline constexpr float kContentInsetFraction = 0.04f;

// Horizontal gap between adjacent control cells, as a fraction of a single cell width.
inline constexpr float kCellGapFraction = 0.12f;

// Fraction of each cell's height reserved for the control's caption label (bottom of
// the cell); the remainder is the control's interactive area (top of the cell).
inline constexpr float kCaptionHeightFraction = 0.22f;

// The number of control cells the module lays out in its single row, in design order:
// VCO Range (choice), Tune (rotary), Fine (rotary), Pulse Width (rotary),
// PWM Depth (rotary), Sub Mode (choice), Noise Level (rotary) — the §5.3 VCO row
// (source levels are SourceMixerModule, out of scope here).
inline constexpr int kControlCellCount = 7;

} // namespace mw::cal::ui::vco
