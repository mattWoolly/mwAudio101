// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/UiTokenConstants.h — the (PI) default values for the single
// UI design-token table (task 106). These are the concrete hex colours, stroke
// weights, corner radii, and font metrics that ui/DesignTokens.h reads its
// defaultTheme()/highContrast() factories from [docs/design/10-ui.md §6.1; ADR-015
// C10].
//
// Per the calibration discipline, no UI call site inlines one of these (PI)
// literals — they centralize here so the visual-design pass can retune the look in
// one place [docs/design/10-ui.md §6.1; AGENTS.md "(PI)"]. This is a NEW header
// (NOT core/calibration/Calibration.h) so parallel UI/DSP work does not collide on
// the orchestrator-owned aggregate; Calibration.h includes it later.
//
// Representation note (deliberate deviation): mwcore is JUCE-free [ADR-001 C1;
// ADR-014 C11], so colours are packed 0xAARRGGBB constants (NOT juce::Colour) and
// fonts are name/height/style metrics (NOT juce::Font). ui/DesignTokens.h carries
// these same JUCE-free POD shapes; the LookAndFeel layer (task 108, JUCE-built)
// lifts them into juce::Colour / juce::Font at the seam. The packed-ARGB layout is
// byte-identical to juce::Colour::getARGB(), so that lift is a one-liner.
//
// The ONE hard constraint on these defaults is the negative one: the shipped
// palette is NEVER Roland grey/red/blue [ADR-015 C10; research/12 §7.2].

#pragma once

#include <cstdint>

namespace mw::cal::ui {

// Packed colour literal: 0xAARRGGBB (alpha in the high byte), matching the layout
// juce::Colour::getARGB() returns so the JUCE seam is a direct construction.
using Argb = std::uint32_t;

// ---------------------------------------------------------------------------
// defaultTheme() palette — the shipped, deliberately non-Roland look (PI).
//
// Direction (PI): a cool deep-teal/charcoal canvas with an amber accent and warm
// off-white text — a signal-flow diagram aesthetic, NOT a recoloured SH-101
// faceplate. Explicitly avoids Roland grey/red/blue [ADR-015 C10; research/12 §7.2].
// ---------------------------------------------------------------------------
namespace defaults {
    inline constexpr Argb kBackground    = 0xFF101A1Cu;  // (PI) near-black deep teal
    inline constexpr Argb kPanel         = 0xFF18272Bu;  // (PI) raised teal panel
    inline constexpr Argb kModuleOutline = 0xFF2F4A50u;  // (PI) muted teal outline
    inline constexpr Argb kPatchLine     = 0xFF3E6168u;  // (PI) cool patch-line teal
    inline constexpr Argb kControlTrack  = 0xFF24383Du;  // (PI) inset track
    inline constexpr Argb kControlFill    = 0xFFE8A33Du;  // (PI) amber accent (the brand colour)
    inline constexpr Argb kControlThumb  = 0xFFF4C77Bu;  // (PI) light amber thumb
    inline constexpr Argb kTextPrimary   = 0xFFF2EDE4u;  // (PI) warm off-white
    inline constexpr Argb kTextSecondary = 0xFF7FB8B0u;  // (PI) clearly teal-tinted secondary text
    inline constexpr Argb kExtensionTag  = 0xFF7FD1A6u;  // (PI) mint — marks sound_ext options
}

// ---------------------------------------------------------------------------
// highContrast() palette — accessibility variant (PI).
//
// Maximally separated luminance: pure-black canvas, pure-white text, and a bright
// high-chroma accent. Every channel differs from defaults so the two factories are
// provably distinct [docs/design/10-ui.md §6.1; ADR-015 C10].
// ---------------------------------------------------------------------------
namespace highContrast {
    inline constexpr Argb kBackground    = 0xFF000000u;  // (PI) pure black
    inline constexpr Argb kPanel         = 0xFF0A0A0Au;  // (PI) near-black panel
    inline constexpr Argb kModuleOutline = 0xFFFFFFFFu;  // (PI) white outline (max edge)
    inline constexpr Argb kPatchLine     = 0xFFFFFFFFu;  // (PI) white patch lines
    inline constexpr Argb kControlTrack  = 0xFF1A1A1Au;  // (PI) dark track
    inline constexpr Argb kControlFill    = 0xFFFFD400u;  // (PI) saturated yellow accent
    inline constexpr Argb kControlThumb  = 0xFFFFFFFFu;  // (PI) white thumb
    inline constexpr Argb kTextPrimary   = 0xFFFFFFFFu;  // (PI) pure white text
    inline constexpr Argb kTextSecondary = 0xFFE6E6E6u;  // (PI) near-white secondary
    inline constexpr Argb kExtensionTag  = 0xFF00FF80u;  // (PI) bright green extension tag
}

// ---------------------------------------------------------------------------
// Stroke weights, in design units (PI). Shared by both themes; high-contrast
// thickens edges for visibility.
// ---------------------------------------------------------------------------
namespace stroke {
    inline constexpr float kOutline   = 1.5f;  // (PI) module outline weight
    inline constexpr float kPatchLine = 2.0f;  // (PI) signal-flow patch-line weight
    inline constexpr float kControl   = 2.5f;  // (PI) control arc/track weight

    inline constexpr float kOutlineHighContrast   = 2.5f;  // (PI) thicker for accessibility
    inline constexpr float kPatchLineHighContrast = 3.0f;  // (PI)
    inline constexpr float kControlHighContrast   = 3.5f;  // (PI)
}

// ---------------------------------------------------------------------------
// Geometry (corner / knob radii), in design units (PI).
// ---------------------------------------------------------------------------
namespace geometry {
    inline constexpr float kCornerRadius = 6.0f;   // (PI) panel/module corner radius
    inline constexpr float kKnobRadius   = 22.0f;  // (PI) rotary-control radius
}

// ---------------------------------------------------------------------------
// Typography defaults (PI). Family name is a logical face the JUCE seam resolves
// to a concrete typeface; heights are in design units; styleFlags mirror
// juce::Font::FontStyleFlags (0=plain, 1=bold, 2=italic) so the seam is direct.
// ---------------------------------------------------------------------------
namespace typography {
    inline constexpr const char* kFamily = "sans-serif";  // (PI) logical family

    inline constexpr float kLabelHeight = 13.0f;  // (PI) control labels
    inline constexpr float kValueHeight = 12.0f;  // (PI) value read-outs
    inline constexpr float kTitleHeight = 18.0f;  // (PI) module titles

    inline constexpr int kLabelStyle = 0;  // (PI) plain
    inline constexpr int kValueStyle = 0;  // (PI) plain
    inline constexpr int kTitleStyle = 1;  // (PI) bold module titles
}

} // namespace mw::cal::ui
