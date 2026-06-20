// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/UiConstants.h — the (PI) editor-geometry constants the editor root
// (MwAudioEditor, task 114) needs: the logical design coordinate space extent, the
// frozen aspect ratio, the resize min/max logical scale, and the scale-snap presets
// [docs/design/10-ui.md §4.1-§4.4; ADR-015 C1, C2].
//
// Per the calibration discipline these (PI) literals are centralized here and never
// inlined at the layout site [AGENTS.md "(PI)"]. This is a NEW header (NOT
// core/calibration/Calibration.h, and distinct from UiTokenConstants.h which owns the
// design-token VALUES) so parallel UI/DSP work does not collide on the
// orchestrator-owned aggregate.
//
// These are JUCE-free plain floats / ints. The editor (JUCE-built) reads them when it
// builds its single design->pixels juce::AffineTransform and configures its
// juce::ComponentBoundsConstrainer; nothing here references juce::* [ADR-001 C1].
//
// The exact extent / aspect / scale numbers are a (PI) pragmatic invention pending the
// visual-design pass; centralizing them here lets that pass retune the canvas without
// touching layout code [docs/design/10-ui.md §4.1].

#pragma once

#include <array>

namespace mw::cal::editor {

// ---------------------------------------------------------------------------
// Logical design coordinate space [docs/design/10-ui.md §4.1; ADR-015 C1].
//
// The editor lays out entirely over this fixed-aspect design space; physical pixels
// are reached only via the single design->pixels AffineTransform. There are ZERO
// hard-coded pixel coordinates in the layout.
// ---------------------------------------------------------------------------
inline constexpr float kDesignWidth  = 1000.0f;  // (PI) design units
inline constexpr float kDesignHeight = 640.0f;   // (PI) design units

// The frozen aspect ratio the constrainer enforces (width / height) (PI-derived).
inline constexpr float kAspectRatio = kDesignWidth / kDesignHeight;

// ---------------------------------------------------------------------------
// Resize limits + scale presets [docs/design/10-ui.md §4.3, §4.4; ADR-015 C2].
//
// Logical scale 1.0 == the design extent in pixels (the default). The window is
// resizable between the min and max logical scale; a scale preset snaps it to one of
// the listed factors. These are (PI) until the visual-design pass.
// ---------------------------------------------------------------------------
inline constexpr float kMinScale     = 0.75f;  // (PI) smallest snap preset (75%)
inline constexpr float kMaxScale     = 2.00f;  // (PI) largest  snap preset (200%)
inline constexpr float kDefaultScale = 1.00f;  // (PI) initial size == design extent

// The scale-menu snap presets, as logical-scale ratios (75/100/150/200%) [ADR-015 C2].
inline constexpr int kNumScalePresets = 4;
inline constexpr std::array<float, kNumScalePresets> kScalePresets{
    0.75f, 1.00f, 1.50f, 2.00f
};

} // namespace mw::cal::editor
