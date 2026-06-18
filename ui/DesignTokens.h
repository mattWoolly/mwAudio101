// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/DesignTokens.h — THE single design-token table: the one place palette, stroke
// weights, corner radii, and typography live, so one struct swap reskins the whole
// plugin without touching layout or binding code [docs/design/10-ui.md §6.1; §2.4;
// ADR-015 C10].
//
// Header-only. No LookAndFeel, no drawing, no layout math (those are tasks 108 / the
// layout tasks). This file owns only the token *shape* and the two factory
// defaults; the concrete (PI) values come from core/calibration/UiTokenConstants.h.
//
// ── Representation deviation (deliberate, recorded here) ────────────────────────
// docs/design/10-ui.md §6.1 sketches the fields as juce::Colour / juce::Font. This
// build (and the headless test suite that gates task 106) is JUCE-free by
// construction: MW_BUILD_PLUGIN is OFF and the test binary links mwcore ONLY [ADR-001
// C1; ADR-014 C11; tests/CMakeLists.txt]. So the token table is expressed in
// JUCE-free POD shapes:
//   • Colour    — packed 0xAARRGGBB, byte-identical to juce::Colour::getARGB()
//   • FontSpec  — family / height / style, the inputs to a juce::Font ctor
// The §6.1 field NAMES and SET are preserved verbatim (palette + stroke + geometry +
// typography + extensionTag). The LookAndFeel layer (task 108, JUCE-built) lifts a
// Colour to juce::Colour(t.background.argb) and a FontSpec to juce::Font(...) at the
// seam — a one-liner each. This keeps the single-reskin-knob contract (C10) intact
// while keeping the UI/core seam JUCE-clean.

#pragma once

#include <cstdint>

#include "../core/calibration/UiTokenConstants.h"

namespace mw::ui {

// ── JUCE-free POD colour (packed 0xAARRGGBB, matching juce::Colour::getARGB()) ──
struct Colour
{
    std::uint32_t argb = 0xFF000000u;  // default opaque black

    constexpr Colour() = default;
    constexpr explicit Colour(std::uint32_t packedArgb) noexcept : argb(packedArgb) {}

    constexpr std::uint8_t alpha() const noexcept { return static_cast<std::uint8_t>((argb >> 24) & 0xFFu); }
    constexpr std::uint8_t red()   const noexcept { return static_cast<std::uint8_t>((argb >> 16) & 0xFFu); }
    constexpr std::uint8_t green() const noexcept { return static_cast<std::uint8_t>((argb >> 8)  & 0xFFu); }
    constexpr std::uint8_t blue()  const noexcept { return static_cast<std::uint8_t>(argb         & 0xFFu); }

    constexpr bool operator==(const Colour&) const noexcept = default;
};

// ── JUCE-free POD font spec (the inputs to a juce::Font ctor at the seam) ──
struct FontSpec
{
    const char* family = "sans-serif";  // logical family resolved by the JUCE seam
    float       height = 12.0f;          // design units
    int         style  = 0;              // juce::Font::FontStyleFlags: 0 plain, 1 bold, 2 italic

    constexpr FontSpec() = default;
    constexpr FontSpec(const char* fam, float h, int s) noexcept : family(fam), height(h), style(s) {}
};

// ── The single design-token table [docs/design/10-ui.md §6.1] ──────────────────
struct DesignTokens
{
    // Palette — deliberately NOT Roland grey/red/blue [ADR-015 C10; research/12 §7.2].
    Colour background;
    Colour panel;
    Colour moduleOutline;
    Colour patchLine;
    Colour controlTrack;
    Colour controlFill;      // accent
    Colour controlThumb;
    Colour textPrimary;
    Colour textSecondary;
    Colour extensionTag;     // marks sound_ext options [ADR-008 §7]

    // Stroke weights (design units).
    float outlineStroke   = 0.0f;
    float patchLineStroke = 0.0f;
    float controlStroke   = 0.0f;

    // Geometry (design units).
    float cornerRadius = 0.0f;
    float knobRadius   = 0.0f;

    // Typography.
    FontSpec labelFont;
    FontSpec valueFont;
    FontSpec titleFont;

    // A human-readable identifier for the active theme variant. Distinct per factory
    // so callers (and tests) can tell which table is live without an external map;
    // swapping the whole struct is still the one reskin operation [§6.1; ADR-015 C10].
    const char* themeTag = "";

    // The shipped, deliberately non-Roland palette [§6.1; ADR-015 C10].
    static constexpr DesignTokens defaultTheme() noexcept
    {
        namespace c = mw::cal::ui;
        DesignTokens t{};
        t.background    = Colour{c::defaults::kBackground};
        t.panel         = Colour{c::defaults::kPanel};
        t.moduleOutline = Colour{c::defaults::kModuleOutline};
        t.patchLine     = Colour{c::defaults::kPatchLine};
        t.controlTrack  = Colour{c::defaults::kControlTrack};
        t.controlFill   = Colour{c::defaults::kControlFill};
        t.controlThumb  = Colour{c::defaults::kControlThumb};
        t.textPrimary   = Colour{c::defaults::kTextPrimary};
        t.textSecondary = Colour{c::defaults::kTextSecondary};
        t.extensionTag  = Colour{c::defaults::kExtensionTag};

        t.outlineStroke   = c::stroke::kOutline;
        t.patchLineStroke = c::stroke::kPatchLine;
        t.controlStroke   = c::stroke::kControl;

        t.cornerRadius = c::geometry::kCornerRadius;
        t.knobRadius   = c::geometry::kKnobRadius;

        t.labelFont = FontSpec{c::typography::kFamily, c::typography::kLabelHeight, c::typography::kLabelStyle};
        t.valueFont = FontSpec{c::typography::kFamily, c::typography::kValueHeight, c::typography::kValueStyle};
        t.titleFont = FontSpec{c::typography::kFamily, c::typography::kTitleHeight, c::typography::kTitleStyle};

        t.themeTag = "default";
        return t;
    }

    // The accessibility (higher-contrast) variant [§6.1; ADR-015 C10].
    static constexpr DesignTokens highContrast() noexcept
    {
        namespace c = mw::cal::ui;
        DesignTokens t{};
        t.background    = Colour{c::highContrast::kBackground};
        t.panel         = Colour{c::highContrast::kPanel};
        t.moduleOutline = Colour{c::highContrast::kModuleOutline};
        t.patchLine     = Colour{c::highContrast::kPatchLine};
        t.controlTrack  = Colour{c::highContrast::kControlTrack};
        t.controlFill   = Colour{c::highContrast::kControlFill};
        t.controlThumb  = Colour{c::highContrast::kControlThumb};
        t.textPrimary   = Colour{c::highContrast::kTextPrimary};
        t.textSecondary = Colour{c::highContrast::kTextSecondary};
        t.extensionTag  = Colour{c::highContrast::kExtensionTag};

        t.outlineStroke   = c::stroke::kOutlineHighContrast;
        t.patchLineStroke = c::stroke::kPatchLineHighContrast;
        t.controlStroke   = c::stroke::kControlHighContrast;

        t.cornerRadius = c::geometry::kCornerRadius;
        t.knobRadius   = c::geometry::kKnobRadius;

        t.labelFont = FontSpec{c::typography::kFamily, c::typography::kLabelHeight, c::typography::kLabelStyle};
        t.valueFont = FontSpec{c::typography::kFamily, c::typography::kValueHeight, c::typography::kValueStyle};
        t.titleFont = FontSpec{c::typography::kFamily, c::typography::kTitleHeight, c::typography::kTitleStyle};

        t.themeTag = "highContrast";
        return t;
    }
};

} // namespace mw::ui
