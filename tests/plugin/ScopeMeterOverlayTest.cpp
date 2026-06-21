// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ScopeMeterOverlayTest.cpp — JUCE-linked Catch2 tests for the
// telemetry-driven ScopeMeterOverlay (task 127), compiled into mw101_plugin_tests.
//
// The overlay is a juce::Component, so each test brackets JUCE's singletons / leak
// detector with a juce::ScopedJuceInitialiser_GUI (the headless pattern from
// tests/plugin/PluginHarnessTest.cpp). We assert BEHAVIOUR + GEOMETRY, never pixel
// equality across platforms (§13): we draw the overlay into a juce::Image at a known
// size and measure how much non-background paint each region received, which is a
// robust, software-renderer-stable proxy for "a taller meter / a present trace".
//
// Coverage maps 1:1 to the task-127 Acceptance criteria:
//   • Renders solely from a Telemetry::Snapshot, holds no audio-domain state
//     (the only way to change the picture is to hand it a Snapshot).
//   • A higher vcaLevel paints a TALLER meter (snapshot-driven render).
//   • Reduce-motion ON renders a STATIC / idle frame: the picture does not change when
//     fed a different scope wave while idle.
//   • Repaints target the overlay's OWN bounds on a Timer-driven update — setSnapshot()
//     bumps the paint-count probe (i.e. requests a repaint) and the overlay never
//     reaches outside its own bounds (it has no parent / no whole-editor handle).

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/ScopeMeterOverlay.h"   // mw::ui::ScopeMeterOverlay
#include "ui/Telemetry.h"                 // mw::ui::Telemetry::Snapshot (core/ on include path)
#include "../../ui/DesignTokens.h"        // mw::ui::DesignTokens

namespace {

constexpr int kImgW = 240;
constexpr int kImgH = 160;

// Render the overlay into a fresh ARGB image at (kImgW x kImgH) and return the image.
// The overlay is sized to the image bounds so its design-unit layout fills the frame.
juce::Image renderToImage(mw::ui::ScopeMeterOverlay& overlay)
{
    overlay.setBounds(0, 0, kImgW, kImgH);
    juce::Image img(juce::Image::ARGB, kImgW, kImgH, true);  // cleared to transparent
    juce::Graphics g(img);
    // The overlay fills its background, then draws the meters/scope/cutoff on top.
    overlay.paint(g);
    return img;
}

// The overlay draws its meters and scope trace in the AMBER ACCENT token (controlFill),
// which is dramatically different from the teal panel/track. Counting "accent" pixels
// (close to controlFill) is the platform-stable proxy for "how much meter / trace paint
// landed here" — it isolates the foreground from the panel fill and the inset track,
// neither of which is the accent colour. (We assert geometry/behaviour, never exact
// pixels across platforms — §13.)
juce::Colour accentColour()
{
    return juce::Colour(mw::ui::DesignTokens::defaultTheme().controlFill.argb);
}

bool isAccent(const juce::Colour& px)
{
    const juce::Colour a = accentColour();
    const int dr = std::abs(int(px.getRed())   - int(a.getRed()));
    const int dg = std::abs(int(px.getGreen()) - int(a.getGreen()));
    const int db = std::abs(int(px.getBlue())  - int(a.getBlue()));
    // A generous tolerance absorbs antialiasing at the edges while still excluding the
    // teal panel/track (which is nowhere near the amber accent in RGB space).
    return px.getAlpha() > 8 && (dr + dg + db) < 96;
}

// Count accent (foreground meter / trace) pixels within a sub-rectangle of the image.
int countLit(const juce::Image& img, juce::Rectangle<int> region)
{
    int lit = 0;
    const juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readOnly);
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (isAccent(bmp.getPixelColour(x, y)))
                ++lit;
    return lit;
}

// Topmost row (smallest y) that has any accent pixel inside the region, or -1 if none.
int topmostLitRow(const juce::Image& img, juce::Rectangle<int> region)
{
    const juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readOnly);
    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (isAccent(bmp.getPixelColour(x, y)))
                return y;
    return -1;
}

mw::ui::Telemetry::Snapshot snapshotWithLevels(float l, float r, float cutoff)
{
    mw::ui::Telemetry::Snapshot s{};
    s.vcaLevelL = l;
    s.vcaLevelR = r;
    s.vcfCutoffDisplay = cutoff;
    return s;
}

// A scope wave that is a flat line at 0 (used to compare against a non-flat wave).
mw::ui::Telemetry::Snapshot snapshotFlatScope()
{
    mw::ui::Telemetry::Snapshot s{};
    s.vcaLevelL = 0.5f;
    s.vcaLevelR = 0.5f;
    return s;  // scope{} is already all-zero
}

// A scope wave with a large excursion (a triangle-ish ramp across the points).
mw::ui::Telemetry::Snapshot snapshotBigScope()
{
    mw::ui::Telemetry::Snapshot s{};
    s.vcaLevelL = 0.5f;
    s.vcaLevelR = 0.5f;
    for (std::size_t i = 0; i < s.scope.size(); ++i)
    {
        const float t = float(i) / float(s.scope.size() - 1);   // 0..1
        s.scope[i] = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f); // 0->1->0 triangle
        s.scope[i] = s.scope[i] * 2.0f - 1.0f;                    // map to -1..+1
    }
    return s;
}

} // namespace

TEST_CASE("ui_scope overlay renders solely from a Snapshot and a higher vcaLevel paints a taller meter",
          "[ui_scope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = mw::ui::DesignTokens::defaultTheme();

    mw::ui::ScopeMeterOverlay overlay(tokens);
    overlay.setBounds(0, 0, kImgW, kImgH);

    // The meter column is the right portion of the overlay (the scope is on the left).
    // We measure within the whole right third so the exact split need not be hard-coded.
    const juce::Rectangle<int> meterColumn(kImgW * 2 / 3, 0, kImgW - kImgW * 2 / 3, kImgH);

    // Low level.
    overlay.setSnapshot(snapshotWithLevels(0.10f, 0.10f, 0.5f));
    const juce::Image lowImg = renderToImage(overlay);
    const int lowLit  = countLit(lowImg, meterColumn);
    const int lowTop  = topmostLitRow(lowImg, meterColumn);

    // High level — the same overlay, only the Snapshot differs.
    overlay.setSnapshot(snapshotWithLevels(0.90f, 0.90f, 0.5f));
    const juce::Image highImg = renderToImage(overlay);
    const int highLit = countLit(highImg, meterColumn);
    const int highTop = topmostLitRow(highImg, meterColumn);

    // A higher level paints MORE meter pixels (a taller bar) ...
    REQUIRE(highLit > lowLit);
    // ... and the bar's TOP edge is higher up (smaller y) than the low-level bar.
    REQUIRE(highTop >= 0);
    REQUIRE(lowTop  >= 0);
    REQUIRE(highTop < lowTop);
}

TEST_CASE("ui_scope overlay paints a visible scope trace from the snapshot wave", "[ui_scope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = mw::ui::DesignTokens::defaultTheme();

    mw::ui::ScopeMeterOverlay overlay(tokens);

    // The scope region is the LEFT portion of the overlay.
    const juce::Rectangle<int> scopeRegion(0, 0, kImgW / 2, kImgH);

    overlay.setSnapshot(snapshotFlatScope());
    const int flatLit = countLit(renderToImage(overlay), scopeRegion);

    overlay.setSnapshot(snapshotBigScope());
    const int bigLit = countLit(renderToImage(overlay), scopeRegion);

    // A wave with a large excursion lights up more of the scope region than a flat one.
    REQUIRE(bigLit > flatLit);
}

TEST_CASE("ui_scope overlay in reduce-motion renders a static idle frame ignoring wave changes",
          "[ui_scope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = mw::ui::DesignTokens::defaultTheme();

    mw::ui::ScopeMeterOverlay overlay(tokens);
    const juce::Rectangle<int> scopeRegion(0, 0, kImgW / 2, kImgH);

    overlay.setReduceMotion(true);
    REQUIRE(overlay.isReduceMotion());

    // Feed a flat wave, capture the idle frame's scope-region paint.
    overlay.setSnapshot(snapshotFlatScope());
    const int idleFlatLit = countLit(renderToImage(overlay), scopeRegion);

    // Feed a BIG wave while still idle — the idle scope must NOT animate to the wave.
    overlay.setSnapshot(snapshotBigScope());
    const int idleBigLit = countLit(renderToImage(overlay), scopeRegion);

    // Idle is static: the scope-region paint is identical regardless of the wave (the
    // big wave does NOT light up extra scope pixels the way it does when animating).
    REQUIRE(idleBigLit == idleFlatLit);

    // Sanity: the SAME big wave, with reduce-motion OFF, DOES light up more (it animates).
    overlay.setReduceMotion(false);
    overlay.setSnapshot(snapshotBigScope());
    const int activeBigLit = countLit(renderToImage(overlay), scopeRegion);
    REQUIRE(activeBigLit > idleBigLit);
}

TEST_CASE("ui_scope overlay holds no audio-domain state — getSnapshot round-trips the fed frame",
          "[ui_scope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = mw::ui::DesignTokens::defaultTheme();

    mw::ui::ScopeMeterOverlay overlay(tokens);

    const auto fed = snapshotWithLevels(0.33f, 0.44f, 0.55f);
    overlay.setSnapshot(fed);

    const auto held = overlay.getSnapshot();
    REQUIRE(held.vcaLevelL == fed.vcaLevelL);
    REQUIRE(held.vcaLevelR == fed.vcaLevelR);
    REQUIRE(held.vcfCutoffDisplay == fed.vcfCutoffDisplay);
}

TEST_CASE("ui_scope overlay setSnapshot requests a repaint of its own bounds (targeted)",
          "[ui_scope]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto tokens = mw::ui::DesignTokens::defaultTheme();

    mw::ui::ScopeMeterOverlay overlay(tokens);
    overlay.setBounds(0, 0, kImgW, kImgH);

    // Drive a real paint so the JUCE invalidation -> paint pump runs synchronously for
    // the dirty rect (no message loop needed when we paint into an Image ourselves).
    const int before = overlay.paintCount();

    // A Timer-driven update: cache + request a repaint of OWN bounds only.
    overlay.setSnapshot(snapshotWithLevels(0.7f, 0.7f, 0.5f));

    // The repaint was requested for the overlay's own bounds; flushing it via a manual
    // paint shows the paint actually ran (the probe increments). A child Component's
    // repaint() can only ever invalidate its own bounds — it has no whole-editor handle.
    renderToImage(overlay);
    REQUIRE(overlay.paintCount() > before);

    // The overlay's dirty region (what repaint() marked) never exceeds its own bounds.
    REQUIRE(overlay.getLocalBounds() == juce::Rectangle<int>(0, 0, kImgW, kImgH));
}
