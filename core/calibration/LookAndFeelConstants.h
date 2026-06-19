// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/LookAndFeelConstants.h — the (PI) drawing constants the custom
// MwAudioLookAndFeel (task 108) needs that are NOT already owned by the design-token
// table (ui/DesignTokens.h) or the token-default header (UiTokenConstants.h). These
// are pure *drawing geometry* — arc sweep proportions, thumb-size ratios, tick-box
// proportions — that parameterize the vector primitives drawn from DesignTokens
// [docs/design/10-ui.md §6.2; ADR-015 C11].
//
// Per the calibration discipline these (PI) literals are centralized here and never
// inlined at the draw site [AGENTS.md "(PI)"]. This is a NEW header (NOT
// core/calibration/Calibration.h) so parallel UI/DSP work does not collide on the
// orchestrator-owned aggregate.
//
// These are JUCE-free plain floats; the LookAndFeel layer applies them when lifting
// DesignTokens (which already own colour/stroke/radius) into juce::Graphics calls.
// They describe SHAPE proportions only — colour and stroke weight are read solely
// from the injected DesignTokens, never from this header [§6.2 acceptance].

#pragma once

namespace mw::cal::laf {

// ---------------------------------------------------------------------------
// Rotary slider drawing geometry (proportions, design-unit ratios) (PI).
// The arc is drawn inside the bounds at a radius derived from the smaller half
// extent; the value arc sweeps from the slider's start angle to its current
// proportional position.
// ---------------------------------------------------------------------------
namespace rotary {
    // Fraction of the (half-min-extent) used as the drawn arc radius, leaving a
    // margin for the stroke and the pointer (PI).
    inline constexpr float kArcRadiusFraction = 0.80f;

    // Length of the value pointer as a fraction of the arc radius (PI).
    inline constexpr float kPointerLengthFraction = 0.90f;

    // Pointer line thickness as a multiple of the token controlStroke (PI).
    inline constexpr float kPointerThicknessFactor = 1.0f;
}

// ---------------------------------------------------------------------------
// Linear slider drawing geometry (PI).
// ---------------------------------------------------------------------------
namespace linear {
    // Thumb radius as a fraction of the track's cross-axis half extent (PI).
    inline constexpr float kThumbRadiusFraction = 0.45f;

    // Track thickness as a multiple of the token controlStroke (PI).
    inline constexpr float kTrackThicknessFactor = 1.0f;
}

// ---------------------------------------------------------------------------
// Toggle button drawing geometry (PI).
// ---------------------------------------------------------------------------
namespace toggle {
    // Tick-box side length as a fraction of the button height (PI).
    inline constexpr float kBoxSizeFraction = 0.70f;

    // Inset of the filled tick from the box edge as a fraction of the box size (PI).
    inline constexpr float kTickInsetFraction = 0.22f;

    // Gap between the tick box and the text label, in design units (PI).
    inline constexpr float kTextGap = 6.0f;
}

// ---------------------------------------------------------------------------
// Combo box drawing geometry (PI).
// ---------------------------------------------------------------------------
namespace combo {
    // Arrow glyph half-width as a fraction of the arrow cell width (PI).
    inline constexpr float kArrowHalfWidthFraction = 0.30f;

    // Arrow glyph height as a fraction of the box height (PI).
    inline constexpr float kArrowHeightFraction = 0.20f;
}

} // namespace mw::cal::laf
