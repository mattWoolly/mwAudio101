// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/BackgroundLayer.h — the cached static-chrome layer [docs/design/10-ui.md §7.1,
// §7.2; ADR-015 C7].
//
// Panel chrome, module outlines, signal-flow patch lines, and static module labels are
// rendered ONCE into a juce::Image (the cache) by regenerate(), and regenerate() is
// invoked ONLY from MwAudioEditor::resized() — never on the telemetry Timer. paint()
// does no juce::Path / vector work at all: it blits the cached image. This keeps the
// most expensive static art off the per-frame repaint path, which is the whole point
// of the repaint-discipline regime [§7; ADR-015 C7].
//
// All colours and stroke weights are read SOLELY from the injected DesignTokens
// (§7.1); only pure layout geometry (the module-row proportions, the patch-line
// height) comes from the (PI) constants in core/calibration/BackgroundLayerConstants.h
// — no inlined literals.
//
// BUILD WIRING: this header lives at the design-faithful path ui/; its implementation
// lives under plugin/ui/BackgroundLayer.cpp so the plugin glob compiles it into the
// plugin target + mw101_plugin_tests (CONFIGURE_DEPENDS) — no shared CMakeLists edit
// (mirrors plugin/ui/MwAudioLookAndFeel.cpp).
//
// SCOPE NOTE: this task owns ONLY the static chrome layer. The interactive controls
// (ui-2/ui-3) and the telemetry-driven visuals (ui-15) are OUT OF SCOPE here (§7 task).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DesignTokens.h"  // sibling ui/: mw::ui::DesignTokens (JUCE-free POD)

namespace mw::ui {

class BackgroundLayer final : public juce::Component
{
public:
    BackgroundLayer();
    ~BackgroundLayer() override = default;

    // Rasterize the static chrome ONCE into the cached juce::Image at the given pixel
    // size, drawing into a design-space coordinate system mapped through
    // designToPixels. Called from MwAudioEditor::resized() — i.e. only on resize
    // [§7.1]. A no-op (the cache is cleared) for an empty pixel bounds. Every colour /
    // stroke is taken from `tokens`.
    void regenerate(juce::Rectangle<int> pixelBounds,
                    const juce::AffineTransform& designToPixels,
                    const DesignTokens& tokens);

    // Blit the cached image only — NO juce::Path stroking, NO vector work per frame
    // [§7.1; ADR-015 C7]. If the cache is empty (never regenerated, or zero-size),
    // paint() does nothing.
    void paint(juce::Graphics&) override;

    // --- Test / inspection hooks [§13] --------------------------------------------
    // True once a non-empty image has been cached by regenerate().
    [[nodiscard]] bool hasCachedImage() const noexcept { return cached_.isValid(); }

    // Read-only view of the cached image so a test can assert it is non-empty / has
    // ink / re-blits the same raster without re-rasterizing.
    [[nodiscard]] const juce::Image& cachedImage() const noexcept { return cached_; }

    // Number of times regenerate() actually rasterized the cache. A repaint must NEVER
    // bump this — only a regenerate() (resize) call does. The probe lets a test prove
    // regenerate-only-on-resize: paint() N times, assert the count is unchanged
    // [§7.1, §7.2 acceptance; ADR-015 C7].
    [[nodiscard]] int regenerationCount() const noexcept { return regenerationCount_; }

    // Number of times paint() has blitted the cache (a test probe; paint() never
    // rasterizes, so this counter and regenerationCount() are independent).
    [[nodiscard]] int paintCount() const noexcept { return paintCount_; }

private:
    // Draw the full static chrome (panel fill, module outlines, patch lines, labels)
    // into `g`, which is already set up in design-space coordinates via the inverse of
    // designToPixels. All colours / strokes come from `tokens`.
    static void drawChrome(juce::Graphics& g, juce::Rectangle<float> designBounds,
                           const DesignTokens& tokens);

    juce::Image cached_;            // the rasterized chrome + patch lines + labels
    int regenerationCount_ = 0;     // bumped only by regenerate() (resize)
    int paintCount_ = 0;            // bumped only by paint() (blit)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundLayer)
};

} // namespace mw::ui
