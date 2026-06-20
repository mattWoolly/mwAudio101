// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/PresetBrowserConstants.h — the (PI) DESIGN-UNIT layout constants
// AND the §6.5 category-taxonomy strings the PresetBrowser (task 128) needs
// [docs/design/10-ui.md §9.1; docs/design/06 §6.5].
//
// Two kinds of constant live here, both centralized per the calibration discipline so
// they are NEVER inlined at the layout/populate site [AGENTS.md "(PI)"]:
//
//   (1) Layout proportions (PI) — the browser lays its children out in DESIGN units
//       only (no pixel math); the parent passes a design-space rectangle to
//       layoutDesignUnits() and the browser sub-divides it with the proportions named
//       here [docs/design/10-ui.md §9.1].
//
//   (2) The §6.5 category-taxonomy strings the category-filter ComboBox enumerates.
//       These mirror docs/design/06 §6.5 / the schema's category enum (ADR-008 C14)
//       1:1 and are the SINGLE source the browser reads — the filter is NOT re-minting
//       the taxonomy inline at the control site [docs/design/10-ui.md §9.3; ADR-008
//       C14]. The PresetManager's own decode (plugin/preset/PresetFormat) validates
//       each preset's category against the same six-string enum, so a category that
//       reaches the bank is always one of these.
//
// This is a NEW header (NOT core/calibration/Calibration.h) so parallel UI/DSP work
// does not collide on the orchestrator-owned aggregate. JUCE-FREE: plain floats / ints
// / char* strings; the browser applies them through JUCE at the layout/populate seam.

#pragma once

#include <cstddef>

namespace mw::cal::ui::presetBrowser {

// --- (1) Layout proportions (PI), fractions of the supplied design-space rectangle ---
// The browser reserves a category-filter row across the top and a button row across the
// bottom; the preset ListBox fills the remaining middle. All values are FRACTIONS of the
// design-space rectangle the parent supplies (0..1), so the layout is resolution- and
// pixel-independent [docs/design/10-ui.md §9.1].

// Fraction of the browser height reserved for the category-filter row (top).
inline constexpr float kFilterRowHeightFraction = 0.14f;

// Fraction of the browser height reserved for the prev/next/load button row (bottom).
inline constexpr float kButtonRowHeightFraction = 0.14f;

// Uniform inset (as a fraction of the SMALLER design-space dimension) applied around the
// whole browser so children never touch its outline.
inline constexpr float kContentInsetFraction = 0.03f;

// Vertical gap between the three stacked regions (filter / list / buttons), as a fraction
// of the browser height.
inline constexpr float kRowGapFraction = 0.02f;

// Horizontal gap between the three buttons in the bottom row, as a fraction of a single
// button's width.
inline constexpr float kButtonGapFraction = 0.08f;

// Number of buttons in the bottom row (Prev / Next / Load).
inline constexpr int kButtonCount = 3;

// The list row height in DESIGN units (the ListBox row pitch). A constant pitch keeps the
// list pixel-independent under the editor's single AffineTransform [§9.1].
inline constexpr float kListRowHeight = 22.0f;

// --- (2) §6.5 category-taxonomy strings (mirrors ADR-008 C14 / the schema enum) -------
// The category-filter ComboBox shows an "All" sentinel first, then exactly these six
// §6.5 categories IN ORDER. Selecting "All" clears the filter; selecting a category
// narrows the list via PresetManager::indicesForCategory(category) [§9.3; ADR-008 C14].

// The number of real §6.5 categories (excludes the "All" sentinel).
inline constexpr int kCategoryCount = 6;

// The §6.5 categories in canonical order. Mirrors docs/design/06 §6.5 / the schema's
// category enum (plugin/preset/PresetFormat kCategoryEnum) 1:1 — the SINGLE source the
// filter reads; never re-minted inline [ADR-008 C14].
inline constexpr const char* kCategories[kCategoryCount] = {
    "AcidBassLead", "SubBass", "Lead", "PWMStrings", "BlipsFX", "SeqArpRiff"
};

// The label of the "no filter" sentinel the filter shows first (selecting it lists the
// whole bank). Distinct from every §6.5 category string [§9.1].
inline constexpr const char* kAllCategoriesLabel = "All Categories";

} // namespace mw::cal::ui::presetBrowser
