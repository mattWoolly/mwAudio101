// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/ui/SvgAssets.h — the JUCE-free vector-asset manifest + policy classifiers
// (task 110). Header-only; mwcore has ZERO JUCE [ADR-001 C1; core/CMakeLists.txt
// no-JUCE guard], so this header carries the asset POLICY and the canonical list of
// bundled art, while the actual juce::Drawable::createFromSVG / BinaryData loading
// lives in the JUCE editor (built only under MW_BUILD_PLUGIN).
//
// Realizes docs/design/10-ui.md §12 / ADR-015 C11 (vector-first asset strategy):
//   * The ONLY bundled art is SVG — a logo plus static decorative glyphs — loaded
//     via juce::Drawable::createFromSVG through BinaryData.
//   * There are ZERO raster faceplate bitmaps and NO @2x/@3x raster matrix.
//
// This manifest is the single source of truth the editor's BinaryData wiring reads:
// the editor iterates kBundledSvgs and calls createFromSVG on each embedded blob.
// Centralizing the list here (rather than scattering filename literals across the
// editor) lets the build embed exactly these files and lets the headless test suite
// assert the vector-only / no-raster-matrix policy mechanically without JUCE.

#pragma once

#include <array>
#include <string_view>

namespace mw::ui::assets {

// ---------------------------------------------------------------------------
// Bundled SVG art (relative to ui/assets/). The logo plus genuinely-static
// decorative glyphs only — controls and chrome are coded vector paths, never
// bundled art [docs/design/10-ui.md §12; ADR-015 C11].
// ---------------------------------------------------------------------------

// The product logo (non-Roland; trademark distance is deliberate — CLAUDE.md).
inline constexpr std::string_view kLogoSvg = "logo.svg";

// Static decorative signal-flow glyph drawn into the cached background layer.
inline constexpr std::string_view kSignalFlowGlyphSvg = "signal-flow-glyph.svg";

// The complete embed set. The editor's BinaryData wiring loads exactly these via
// juce::Drawable::createFromSVG. Adding a bundled asset is one edit here.
inline constexpr std::array<std::string_view, 2> kBundledSvgs = {
    kLogoSvg,
    kSignalFlowGlyphSvg,
};

// ---------------------------------------------------------------------------
// Filename policy classifiers (case-insensitive on the extension). These encode
// the ADR-015 C11 negative constraint so it is enforceable by test and by any
// build-time asset gate.
// ---------------------------------------------------------------------------

namespace detail {

constexpr char toLowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive: does `name` end with `suffix` (suffix assumed lowercase)?
constexpr bool endsWithLower(std::string_view name, std::string_view suffix) noexcept {
    if (suffix.size() > name.size()) return false;
    const std::size_t off = name.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (toLowerAscii(name[off + i]) != suffix[i]) return false;
    }
    return true;
}

constexpr bool contains(std::string_view name, std::string_view needle) noexcept {
    return name.find(needle) != std::string_view::npos;
}

} // namespace detail

// A vector SVG asset (the only permitted bundled-art kind).
constexpr bool isVectorSvg(std::string_view name) noexcept {
    return detail::endsWithLower(name, ".svg");
}

// A raster bitmap — forbidden as a faceplate/chrome asset [ADR-015 C11]. Covers the
// formats JUCE's BinaryData/ImageCache would otherwise accept.
constexpr bool isRasterBitmap(std::string_view name) noexcept {
    return detail::endsWithLower(name, ".png")
        || detail::endsWithLower(name, ".jpg")
        || detail::endsWithLower(name, ".jpeg")
        || detail::endsWithLower(name, ".bmp")
        || detail::endsWithLower(name, ".gif")
        || detail::endsWithLower(name, ".tga")
        || detail::endsWithLower(name, ".tiff")
        || detail::endsWithLower(name, ".webp")
        || detail::endsWithLower(name, ".heic");
}

// A @2x / @3x retina raster-matrix marker — forbidden for ANY extension; vector
// paths re-rasterize at the physical-pixel transform, so a scale matrix is never
// needed [docs/design/10-ui.md §12; ADR-015 C11].
constexpr bool hasRetinaScaleMatrix(std::string_view name) noexcept {
    return detail::contains(name, "@2x")
        || detail::contains(name, "@3x")
        || detail::contains(name, "@4x");
}

// Aggregate guarantee over the manifest: every listed asset is a vector SVG with no
// raster-matrix marker. Constexpr so it is also a compile-time invariant below.
constexpr bool manifestIsVectorOnly() noexcept {
    for (const auto& n : kBundledSvgs) {
        if (!isVectorSvg(n))         return false;
        if (isRasterBitmap(n))       return false;
        if (hasRetinaScaleMatrix(n)) return false;
    }
    return true;
}

// Make the policy a hard compile-time fact: a future contributor cannot add a raster
// or @2x/@3x entry to kBundledSvgs without failing the build [ADR-015 C11].
static_assert(manifestIsVectorOnly(),
              "SvgAssets: kBundledSvgs must be vector-only — no raster bitmaps and "
              "no @2x/@3x matrix [docs/design/10-ui.md §12; ADR-015 C11].");
static_assert(!kBundledSvgs.empty(),
              "SvgAssets: at least the logo must be bundled [docs/design/10-ui.md §12].");

} // namespace mw::ui::assets
