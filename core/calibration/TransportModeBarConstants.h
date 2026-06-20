// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/TransportModeBarConstants.h — the (PI) DESIGN-UNIT layout
// constants for ui/modules/TransportModeBar (task 125). The module lays its
// children out in design units only (no pixel math) [docs/design/10-ui.md §5.3];
// the parent passes a design-space rectangle to layoutDesignUnits() and the module
// sub-divides it horizontally (it is a BAR) with the proportions named here.
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these layout literals
// is inlined at the layout call site; they centralize here so the visual-design
// pass can retune the bar's internal proportions in one place. This is a NEW header
// (NOT core/calibration/Calibration.h) so parallel UI/DSP work does not collide on
// the orchestrator-owned aggregate.
//
// The scale-preset percentages (75/100/150/200%) are ALSO (PI) here — the bar
// surfaces them as a selector that signals the editor to snap the window
// [docs/design/10-ui.md §4.4; ADR-015 C2]. They are the SINGLE source of the
// percentage labels the selector shows.
//
// JUCE-FREE: these are plain float proportions / ints; the module applies them
// through JUCE juce::Rectangle math at the layout seam.

#pragma once

#include <cstddef>

namespace mw::cal::ui::transport {

// The bar reserves a short title strip across the top, then lays the controls out
// as a single horizontal row of cells beneath it. All values are FRACTIONS of the
// design-space rectangle the parent supplies (0..1), so the layout is resolution-
// and pixel-independent [docs/design/10-ui.md §5.3].

// Fraction of the bar height reserved for the title strip (top).
inline constexpr float kTitleHeightFraction = 0.30f;

// Inset (as a fraction of the SMALLER design-space dimension) applied uniformly
// around the control row so children never touch the bar outline.
inline constexpr float kContentInsetFraction = 0.06f;

// Horizontal gap between adjacent control cells, as a fraction of a single cell
// width.
inline constexpr float kCellGapFraction = 0.10f;

// Fraction of each cell's height reserved for the control's caption label (bottom
// of the cell); the remainder is the control's interactive area (top of the cell).
inline constexpr float kCaptionHeightFraction = 0.28f;

// The number of control cells the bar lays out in its single horizontal row, in
// design order:
//   Run/Hold, Arp Mode, Arp Range, Arp Sync (toggle), Arp Div, Arp Latch,
//   Seq Mode, Seq Sync (toggle), Seq Div, Scale, Reduce-Motion.
inline constexpr int kControlCellCount = 11;

// --- Scale-preset percentages (75/100/150/200%) [§4.4; ADR-015 C2] -------------
// The SINGLE source of the selector's option list AND the logical scale factors a
// chosen index maps to. Kept in lock-step: kScalePresetCount == size of both arrays.
inline constexpr int kScalePresetCount = 4;

// The logical scale factors, in selector-index order.
inline constexpr float kScalePresetFactors[kScalePresetCount] = { 0.75f, 1.0f, 1.5f, 2.0f };

// The human-readable percentage labels, in selector-index order (must mirror the
// factors above 1:1).
inline constexpr const char* kScalePresetLabels[kScalePresetCount] = { "75%", "100%", "150%", "200%" };

// The default scale-preset index the selector shows on construction (100%).
inline constexpr int kDefaultScalePresetIndex = 1;

} // namespace mw::cal::ui::transport
