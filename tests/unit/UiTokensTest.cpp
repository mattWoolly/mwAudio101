// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the single design-token table (task 106). Case names begin
// with "ui_tokens" so `ctest -R ui_tokens` selects them [AGENTS.md silent-pass rule;
// docs/design/10-ui.md §6.1].
//
// These assert the OBJECTIVELY-testable acceptance criteria of task 106:
//   1. the struct exposes every §6.1 field (palette + stroke + geometry + typography
//      + extensionTag);
//   2. defaultTheme() is provably never Roland grey/red/blue [ADR-015 C10];
//   3. highContrast() is a distinct, higher-contrast variant;
//   4. both factories construct and differ.
//
// JUCE-free by construction: the test binary links mwcore ONLY [tests/CMakeLists.txt;
// ADR-001 C1]. DesignTokens is therefore the JUCE-free POD form (see its header).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "../../ui/DesignTokens.h"

using mw::ui::Colour;
using mw::ui::DesignTokens;
using mw::ui::FontSpec;

namespace {

// Relative luminance (sRGB-ish, perceptual weights) of a packed colour, 0..255.
double luminance(const Colour& c) {
    return 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
}

// Largest channel minus smallest — a cheap chroma/saturation proxy, 0..255.
int chroma(const Colour& c) {
    const int r = c.red(), g = c.green(), b = c.blue();
    const int hi = std::max({r, g, b});
    const int lo = std::min({r, g, b});
    return hi - lo;
}

// "Roland grey": a near-neutral mid grey (the SH-101 panel grey). A colour is in the
// forbidden grey band if it is near-neutral (low chroma) AND mid-luminance — i.e. an
// actual panel grey, not pure black/white which are legitimate accessibility anchors.
// Thresholds are (PI) discrimination bands for the never-Roland constraint [ADR-015 C10].
bool isRolandGrey(const Colour& c) {
    const double lum = luminance(c);
    return chroma(c) <= 24 && lum >= 64.0 && lum <= 200.0;
}

// "Roland red": a warm, saturated red — red clearly dominant over green and blue.
bool isRolandRed(const Colour& c) {
    const int r = c.red(), g = c.green(), b = c.blue();
    return r >= 140 && r - g >= 70 && r - b >= 70;
}

// "Roland blue": a saturated blue — blue clearly dominant over red and green.
bool isRolandBlue(const Colour& c) {
    const int r = c.red(), g = c.green(), b = c.blue();
    return b >= 120 && b - r >= 60 && b - g >= 40;
}

bool isRolandColour(const Colour& c) {
    return isRolandGrey(c) || isRolandRed(c) || isRolandBlue(c);
}

// All ten palette slots of a token table, in §6.1 order.
constexpr std::array<Colour DesignTokens::*, 10> kPaletteSlots = {
    &DesignTokens::background,
    &DesignTokens::panel,
    &DesignTokens::moduleOutline,
    &DesignTokens::patchLine,
    &DesignTokens::controlTrack,
    &DesignTokens::controlFill,
    &DesignTokens::controlThumb,
    &DesignTokens::textPrimary,
    &DesignTokens::textSecondary,
    &DesignTokens::extensionTag,
};

} // namespace

TEST_CASE("ui_tokens: struct exposes every field listed in design 10-ui 6.1", "[ui_tokens][ui]") {
    // Palette: ten juce::Colour-shaped fields (here the JUCE-free Colour POD).
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::background),    Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::panel),         Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::moduleOutline), Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::patchLine),     Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::controlTrack),  Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::controlFill),   Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::controlThumb),  Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::textPrimary),   Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::textSecondary), Colour>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::extensionTag),  Colour>);

    // Stroke weights (design units).
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::outlineStroke),   float>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::patchLineStroke), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::controlStroke),   float>);

    // Geometry (design units).
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::cornerRadius), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::knobRadius),   float>);

    // Typography.
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::labelFont), FontSpec>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::valueFont), FontSpec>);
    STATIC_REQUIRE(std::is_same_v<decltype(DesignTokens::titleFont), FontSpec>);
}

TEST_CASE("ui_tokens: both factories construct and are usable", "[ui_tokens][ui]") {
    constexpr DesignTokens def = DesignTokens::defaultTheme();
    constexpr DesignTokens hc  = DesignTokens::highContrast();

    // Positive stroke/geometry/typography defaults — non-degenerate tables.
    REQUIRE(def.outlineStroke   > 0.0f);
    REQUIRE(def.patchLineStroke > 0.0f);
    REQUIRE(def.controlStroke   > 0.0f);
    REQUIRE(def.cornerRadius    > 0.0f);
    REQUIRE(def.knobRadius      > 0.0f);
    REQUIRE(def.labelFont.height > 0.0f);
    REQUIRE(def.valueFont.height > 0.0f);
    REQUIRE(def.titleFont.height > 0.0f);

    REQUIRE(hc.outlineStroke   > 0.0f);
    REQUIRE(hc.patchLineStroke > 0.0f);
    REQUIRE(hc.controlStroke   > 0.0f);
    REQUIRE(hc.cornerRadius    > 0.0f);
    REQUIRE(hc.knobRadius      > 0.0f);

    // All palette slots are opaque (alpha 0xFF) — a token table must paint solidly.
    for (auto slot : kPaletteSlots) {
        REQUIRE((def.*slot).alpha() == 0xFFu);
        REQUIRE((hc.*slot).alpha()  == 0xFFu);
    }

    // Theme tags are present and name their lineage.
    REQUIRE(std::string_view{def.themeTag} == "default");
    REQUIRE(std::string_view{hc.themeTag}  == "highContrast");
}

TEST_CASE("ui_tokens: defaultTheme palette is never Roland grey/red/blue", "[ui_tokens][ui]") {
    constexpr DesignTokens def = DesignTokens::defaultTheme();

    // Sanity: the forbidden-region predicates actually fire on canonical Roland-ish
    // reference colours, so a passing palette test is meaningful (not vacuous).
    REQUIRE(isRolandGrey(Colour{0xFF8C8C8Cu}));  // SH-101 panel grey
    REQUIRE(isRolandRed (Colour{0xFFC81E1Eu}));  // Roland warning red
    REQUIRE(isRolandBlue(Colour{0xFF1E50C8u}));  // Roland blue

    // The actual constraint: no shipped palette slot lands in any forbidden region
    // [ADR-015 C10; docs/design/10-ui.md §6.1; research/12 §7.2].
    for (auto slot : kPaletteSlots) {
        const Colour col = def.*slot;
        INFO("palette slot argb=0x" << std::hex << col.argb);
        CHECK_FALSE(isRolandGrey(col));
        CHECK_FALSE(isRolandRed(col));
        CHECK_FALSE(isRolandBlue(col));
    }
}

TEST_CASE("ui_tokens: highContrast is a distinct, higher-contrast variant", "[ui_tokens][ui]") {
    constexpr DesignTokens def = DesignTokens::defaultTheme();
    constexpr DesignTokens hc  = DesignTokens::highContrast();

    // Distinct: at least one palette slot differs (in practice, all do).
    bool anyPaletteDiff = false;
    for (auto slot : kPaletteSlots) {
        if (!((def.*slot) == (hc.*slot))) anyPaletteDiff = true;
    }
    REQUIRE(anyPaletteDiff);

    // The two tables are not equal as whole structs either.
    REQUIRE(std::string_view{def.themeTag} != std::string_view{hc.themeTag});

    // Higher contrast: background<->textPrimary luminance separation strictly
    // exceeds the default theme's separation.
    const double defSep = std::abs(luminance(def.textPrimary) - luminance(def.background));
    const double hcSep  = std::abs(luminance(hc.textPrimary)  - luminance(hc.background));
    INFO("default bg/text luminance separation = " << defSep);
    INFO("highContrast bg/text luminance separation = " << hcSep);
    REQUIRE(hcSep > defSep);

    // High contrast pushes to the extremes: pure-black background, pure-white text.
    REQUIRE(hc.background.red()  == 0);
    REQUIRE(hc.background.green()== 0);
    REQUIRE(hc.background.blue() == 0);
    REQUIRE(hc.textPrimary.red()  == 255);
    REQUIRE(hc.textPrimary.green()== 255);
    REQUIRE(hc.textPrimary.blue() == 255);

    // Accessibility variant thickens strokes for visibility.
    REQUIRE(hc.outlineStroke   >= def.outlineStroke);
    REQUIRE(hc.patchLineStroke >= def.patchLineStroke);
    REQUIRE(hc.controlStroke   >= def.controlStroke);
}
