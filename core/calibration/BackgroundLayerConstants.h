// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/BackgroundLayerConstants.h — the (PI) DESIGN-UNIT layout constants
// for ui/BackgroundLayer (task 116). The BackgroundLayer rasterizes the static chrome
// (panel fill, module outlines, signal-flow patch lines, module labels) ONCE into a
// cached juce::Image, regenerated only on resize, then blits it on paint
// [docs/design/10-ui.md §7.1, §7.2; ADR-015 C7].
//
// All of these literals are FRACTIONS of, or offsets within, the 1000x640 logical
// design space (§4.1) — the BackgroundLayer draws into a design-space coordinate
// system that the editor's design->pixels AffineTransform then maps to the cached
// image. Nothing here is a pixel value.
//
// Per the calibration discipline (AGENTS.md "(PI)"), none of these layout literals is
// inlined at the draw site; they centralize here so the visual-design pass can retune
// the chrome geometry in one place. This is a NEW header (NOT
// core/calibration/Calibration.h) so parallel UI/DSP work does not collide on the
// orchestrator-owned aggregate.
//
// JUCE-FREE: these are plain float / int proportions; the BackgroundLayer applies them
// through juce::Rectangle / juce::Path math at the draw seam. Colours and stroke
// weights are NOT here — those are read SOLELY from the injected DesignTokens
// (§7.1 acceptance).

#pragma once

namespace mw::cal::ui::background {

// The signal-flow modules are laid out as a single horizontal row of equal-width
// module cells in the documented order MODULATOR -> VCO -> SOURCE MIXER -> VCF -> VCA
// [docs/design/10-ui.md §5.1, §7.2]. The BackgroundLayer draws an outline + a label
// for each cell and a patch line connecting consecutive cells.
inline constexpr int kModuleCount = 5;

// Module labels, in row order. The BackgroundLayer renders these (static art) into the
// cached image using the token title font; they are never re-stroked per frame.
inline constexpr const char* kModuleLabels[kModuleCount] = {
    "MODULATOR", "VCO", "SOURCE MIXER", "VCF", "VCA"
};

// Fraction of the design-space WIDTH reserved as a uniform margin on the left and
// right of the module row (PI).
inline constexpr float kRowMarginXFraction = 0.03f;

// Fraction of the design-space HEIGHT at which the module row begins (top edge), and
// the fraction of height the row occupies (PI). The remaining space (top strip / the
// area below the row) is left for components other tasks own.
inline constexpr float kRowTopFraction    = 0.10f;
inline constexpr float kRowHeightFraction = 0.55f;

// Horizontal gap between adjacent module cells, as a fraction of the full row width
// (PI). The patch line spans this gap from one cell to the next.
inline constexpr float kModuleGapFraction = 0.025f;

// Fraction of a module cell's HEIGHT reserved at the top for its label strip; the
// outline is drawn around the whole cell and the label is centred in this strip (PI).
inline constexpr float kLabelStripHeightFraction = 0.16f;

// The vertical position of the patch line within a module cell, as a fraction of the
// cell height from its top (PI) — the lines connect the cells at a consistent height.
inline constexpr float kPatchLineYFraction = 0.55f;

} // namespace mw::cal::ui::background
