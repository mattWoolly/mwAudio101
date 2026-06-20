// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/BackgroundLayerTest.cpp — JUCE-linked acceptance tests for the cached
// static-chrome layer ui/BackgroundLayer (task 116). Test-case display names begin
// with the task tag `ui_bg` so `ctest -R ui_bg` selects exactly these cases
// (silent-pass rule).
//
// The GUI is NOT pixel-identical across platforms, so these tests assert BEHAVIOUR
// (cache lifecycle, regenerate-only-on-resize) and RENDERED-COLOUR PRESENCE/CHANGE
// (tokens drive the chrome), never exact pixel layout [docs/design/10-ui.md §13;
// ADR-015 Consequences].
//
// Acceptance criteria covered (task 116 / §7.1 / §7.2 / ADR-015 C7):
//   [1] paint() blits the cached image with NO juce::Path stroking per frame — proven
//       by a probe: paint() many times and assert the regeneration count never moves.
//   [2] Patch lines + chrome regenerate ONLY on resize (a regenerate() call), never on
//       paint — the same probe distinguishes regenerate() from paint().
//   [3] All colours / strokes read from DesignTokens — proven by asserting the active
//       theme's colours are present in the raster and that a token swap (default ->
//       highContrast) changes the raster.
//   [4] regenerate() at a known size produces a non-empty image with ink.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/BackgroundLayer.h"

using mw::ui::BackgroundLayer;
using mw::ui::DesignTokens;

namespace {

// A simple uniform-scale design->pixels transform (no centring offset needed for the
// test: the image is sized exactly to the design space at this scale).
juce::AffineTransform makeTransform(float scale) noexcept
{
    return juce::AffineTransform::scale(scale);
}

// True if ANY pixel in the image is non-transparent (something was rasterized).
bool imageHasInk(const juce::Image& img)
{
    if (! img.isValid())
        return false;
    const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (data.getPixelColour(x, y).getAlpha() != 0)
                return true;
    return false;
}

// Count pixels whose ARGB exactly equals the target (opaque token colours are filled
// solidly, so an exact match is reliable for the large panel/background fills).
int countExactColour(const juce::Image& img, juce::Colour target)
{
    int count = 0;
    const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (data.getPixelColour(x, y).getARGB() == target.getARGB())
                ++count;
    return count;
}

// Hash the raster's pixels so two regenerations with different tokens can be compared
// for "the image changed" without asserting an exact layout.
std::uint64_t imageHash(const juce::Image& img)
{
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
    const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
        {
            h ^= data.getPixelColour(x, y).getARGB();
            h *= 1099511628211ull;  // FNV prime
        }
    return h;
}

constexpr int kW = 500;  // a known pixel size for the cache (half of the 1000-unit
constexpr int kH = 320;  // design width -> a 0.5 scale transform)

} // namespace

TEST_CASE("ui_bg regenerate caches a non-empty image with ink at a known size", "[ui_bg]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BackgroundLayer layer;
    REQUIRE_FALSE(layer.hasCachedImage());          // nothing cached before regen
    REQUIRE(layer.regenerationCount() == 0);

    layer.regenerate({ 0, 0, kW, kH }, makeTransform(0.5f), DesignTokens::defaultTheme());

    REQUIRE(layer.hasCachedImage());
    REQUIRE(layer.regenerationCount() == 1);
    REQUIRE(layer.cachedImage().getWidth() == kW);
    REQUIRE(layer.cachedImage().getHeight() == kH);
    REQUIRE(imageHasInk(layer.cachedImage()));      // chrome actually drawn
}

TEST_CASE("ui_bg paint blits the cache without re-rasterizing (no per-frame path work)", "[ui_bg]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BackgroundLayer layer;
    layer.setBounds(0, 0, kW, kH);
    layer.regenerate({ 0, 0, kW, kH }, makeTransform(0.5f), DesignTokens::defaultTheme());
    REQUIRE(layer.regenerationCount() == 1);

    // Capture the cached raster so we can prove paint() does not change it.
    const auto hashBefore = imageHash(layer.cachedImage());

    // Paint many frames into a fresh target. NOT ONE of them may re-rasterize: the
    // regeneration count must be frozen, and the cache must be byte-identical
    // [§7.1; ADR-015 C7].
    for (int frame = 0; frame < 64; ++frame)
    {
        juce::Image target(juce::Image::ARGB, kW, kH, true);
        juce::Graphics g(target);
        layer.paint(g);
    }

    REQUIRE(layer.paintCount() == 64);
    REQUIRE(layer.regenerationCount() == 1);                 // never bumped by paint
    REQUIRE(imageHash(layer.cachedImage()) == hashBefore);   // cache untouched

    // And the blit actually transfers the cached art (the target receives ink).
    juce::Image target(juce::Image::ARGB, kW, kH, true);
    juce::Graphics g(target);
    layer.paint(g);
    REQUIRE(imageHasInk(target));
}

TEST_CASE("ui_bg regeneration is gated on resize, only regenerate bumps the cache", "[ui_bg]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BackgroundLayer layer;
    const auto t = makeTransform(0.5f);

    layer.regenerate({ 0, 0, kW, kH }, t, DesignTokens::defaultTheme());
    REQUIRE(layer.regenerationCount() == 1);

    // Several paints (simulating Timer-driven frames) — the count stays put.
    for (int i = 0; i < 10; ++i)
    {
        juce::Image target(juce::Image::ARGB, kW, kH, true);
        juce::Graphics g(target);
        layer.paint(g);
    }
    REQUIRE(layer.regenerationCount() == 1);

    // A genuine resize (new pixel bounds + transform) regenerates exactly once more.
    layer.regenerate({ 0, 0, kW * 2, kH * 2 }, makeTransform(1.0f), DesignTokens::defaultTheme());
    REQUIRE(layer.regenerationCount() == 2);
    REQUIRE(layer.cachedImage().getWidth() == kW * 2);
    REQUIRE(layer.cachedImage().getHeight() == kH * 2);
}

TEST_CASE("ui_bg chrome colours are read from the DesignTokens table", "[ui_bg]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BackgroundLayer layer;
    const auto t = makeTransform(0.5f);

    // Default theme: the background fill colour must be present in the raster (the
    // canvas is filled with tokens.background) — proving the chrome reads the tokens.
    const auto defTokens = DesignTokens::defaultTheme();
    layer.regenerate({ 0, 0, kW, kH }, t, defTokens);
    const auto defBg = juce::Colour(defTokens.background.argb);
    REQUIRE(countExactColour(layer.cachedImage(), defBg) > 0);
    const auto defHash = imageHash(layer.cachedImage());

    // Swap to the high-contrast theme: the raster must change, and the new background
    // colour must now be present (the single-reskin contract drives the chrome too).
    const auto hcTokens = DesignTokens::highContrast();
    layer.regenerate({ 0, 0, kW, kH }, t, hcTokens);
    const auto hcBg = juce::Colour(hcTokens.background.argb);
    REQUIRE(countExactColour(layer.cachedImage(), hcBg) > 0);
    REQUIRE(imageHash(layer.cachedImage()) != defHash);
}

TEST_CASE("ui_bg empty pixel bounds clears the cache and paint is a safe no-op", "[ui_bg]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    BackgroundLayer layer;
    layer.regenerate({ 0, 0, kW, kH }, makeTransform(0.5f), DesignTokens::defaultTheme());
    REQUIRE(layer.hasCachedImage());

    // A zero-size resize clears the cache (no raster to blit).
    layer.regenerate({ 0, 0, 0, 0 }, makeTransform(0.5f), DesignTokens::defaultTheme());
    REQUIRE_FALSE(layer.hasCachedImage());
    REQUIRE(layer.regenerationCount() == 2);

    // paint() with no cache must not throw / draw — exercise it.
    juce::Image target(juce::Image::ARGB, kW, kH, true);
    juce::Graphics g(target);
    layer.paint(g);
    REQUIRE(layer.paintCount() == 1);
    REQUIRE_FALSE(imageHasInk(target));   // nothing blitted
}
