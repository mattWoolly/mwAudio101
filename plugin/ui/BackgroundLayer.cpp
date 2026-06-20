// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/BackgroundLayer.cpp — implementation of the cached static-chrome layer
// declared in ui/BackgroundLayer.h [docs/design/10-ui.md §7.1, §7.2; ADR-015 C7].
//
// regenerate() rasterizes the panel fill, module outlines, signal-flow patch lines
// (MODULATOR -> VCO -> SOURCE MIXER -> VCF -> VCA), and the static module labels ONCE
// into the cached juce::Image, using the supplied design->pixels AffineTransform so
// the vector art rasterizes crisply at the physical-pixel scale. paint() does NO
// vector work — it blits the cached raster. Every colour / stroke comes from the
// injected DesignTokens; only pure layout geometry comes from the (PI) constants in
// core/calibration/BackgroundLayerConstants.h.
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/BackgroundLayer.h and is
// reached by relative include — no shared CMakeLists edit (mirrors
// plugin/ui/MwAudioLookAndFeel.cpp).

#include "../../ui/BackgroundLayer.h"

#include "../../core/calibration/BackgroundLayerConstants.h"

#include "../../ui/MwAudioLookAndFeel.h"  // sibling ui/: the token -> juce lift helpers

namespace mw::ui {

namespace {
namespace cal = mw::cal::ui::background;

// Lift a JUCE-free POD Colour to a juce::Colour (the single token seam, shared with
// the LookAndFeel). Drawing colours are read SOLELY from the injected tokens [§7.1].
juce::Colour toColour(const Colour& c) noexcept { return MwAudioLookAndFeel::toColour(c); }
} // namespace

BackgroundLayer::BackgroundLayer()
{
    // The static chrome never intercepts mouse input — it sits behind the interactive
    // controls and is purely decorative art [§5.1, §7.1].
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

void BackgroundLayer::regenerate(juce::Rectangle<int> pixelBounds,
                                 const juce::AffineTransform& designToPixels,
                                 const DesignTokens& tokens)
{
    // A degenerate (empty) target clears the cache: paint() then blits nothing. This is
    // a regeneration event, but it produces no raster, so callers can rely on
    // hasCachedImage() being false afterwards.
    if (pixelBounds.isEmpty())
    {
        cached_ = juce::Image();
        ++regenerationCount_;
        return;
    }

    // Rasterize ONCE into a fresh ARGB image sized to the physical pixel bounds. We
    // draw in DESIGN-space coordinates and let the supplied design->pixels transform map
    // them to pixels, so the vector art rasterizes crisply at the current scale
    // (the same hi-DPI mechanism the editor root uses) [§4.2, §7.1].
    juce::Image image(juce::Image::ARGB, pixelBounds.getWidth(), pixelBounds.getHeight(), true);
    {
        juce::Graphics g(image);
        g.addTransform(designToPixels);

        // The design-space rectangle is recovered by inverse-transforming the pixel
        // bounds; drawChrome lays the module row out inside it in design units.
        const auto designBounds = pixelBounds.toFloat()
                                      .transformedBy(designToPixels.inverted());
        drawChrome(g, designBounds, tokens);
    }

    cached_ = image;
    ++regenerationCount_;
}

void BackgroundLayer::paint(juce::Graphics& g)
{
    // BLIT ONLY — no juce::Path stroking, no fills, no vector work per frame
    // [§7.1; ADR-015 C7]. If never regenerated (or regenerated for an empty bounds),
    // there is nothing to draw.
    ++paintCount_;
    if (cached_.isValid())
        g.drawImageAt(cached_, 0, 0);
}

// ---------------------------------------------------------------------------
// Static chrome: panel fill, per-module outline + label, and the patch lines that
// connect the modules in signal-flow order. All vector, drawn ONCE into the cache.
// Every colour / stroke is read from the injected tokens; geometry from the (PI)
// constants — no inlined literals.
// ---------------------------------------------------------------------------
void BackgroundLayer::drawChrome(juce::Graphics& g, juce::Rectangle<float> designBounds,
                                 const DesignTokens& tokens)
{
    // Panel background fill across the whole design space (the canvas) [§7.1].
    g.setColour(toColour(tokens.background));
    g.fillRect(designBounds);

    // The module row: a horizontal band inset by the (PI) margins, split into
    // kModuleCount equal cells separated by the (PI) gap.
    const float marginX = designBounds.getWidth() * cal::kRowMarginXFraction;
    const float rowTop  = designBounds.getY() + designBounds.getHeight() * cal::kRowTopFraction;
    const float rowH    = designBounds.getHeight() * cal::kRowHeightFraction;

    const juce::Rectangle<float> row(designBounds.getX() + marginX, rowTop,
                                     designBounds.getWidth() - 2.0f * marginX, rowH);

    const float gap = row.getWidth() * cal::kModuleGapFraction;
    const float cellW = (row.getWidth() - gap * static_cast<float>(cal::kModuleCount - 1))
                            / static_cast<float>(cal::kModuleCount);

    const float corner = tokens.cornerRadius;
    const float outlineStroke = tokens.outlineStroke;
    const float patchStroke   = tokens.patchLineStroke;

    // Collect each cell rectangle so we can draw the connecting patch lines after the
    // outlines (lines on top of the panel edges, under nothing else).
    juce::Rectangle<float> cells[cal::kModuleCount];
    for (int i = 0; i < cal::kModuleCount; ++i)
    {
        const float x = row.getX() + static_cast<float>(i) * (cellW + gap);
        cells[i] = juce::Rectangle<float>(x, row.getY(), cellW, row.getHeight());
    }

    // Patch lines first (MODULATOR -> VCO -> SOURCE MIXER -> VCF -> VCA), drawn at a
    // consistent height across the gaps so the routing topology reads as one chain
    // [§7.2]. Token patchLine colour + patchLineStroke weight.
    {
        const float lineY = row.getY() + row.getHeight() * cal::kPatchLineYFraction;
        g.setColour(toColour(tokens.patchLine));
        juce::Path patch;
        for (int i = 0; i + 1 < cal::kModuleCount; ++i)
        {
            patch.startNewSubPath(cells[i].getRight(), lineY);
            patch.lineTo(cells[i + 1].getX(), lineY);
        }
        g.strokePath(patch, juce::PathStrokeType(patchStroke,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Module outlines (panel fill + moduleOutline edge) and their static labels.
    g.setFont(MwAudioLookAndFeel::toFont(tokens.titleFont));
    for (int i = 0; i < cal::kModuleCount; ++i)
    {
        const auto& cell = cells[i];

        // Panel fill behind each module so the patch line tucks behind the outline.
        g.setColour(toColour(tokens.panel));
        g.fillRoundedRectangle(cell, corner);

        // Outline edge.
        g.setColour(toColour(tokens.moduleOutline));
        g.drawRoundedRectangle(cell.reduced(outlineStroke * 0.5f), corner, outlineStroke);

        // Static label centred in the top label strip, token primary text colour.
        const float stripH = cell.getHeight() * cal::kLabelStripHeightFraction;
        const auto labelArea = cell.withHeight(stripH);
        g.setColour(toColour(tokens.textPrimary));
        g.drawText(juce::String(cal::kModuleLabels[i]), labelArea,
                   juce::Justification::centred, true);
    }
}

} // namespace mw::ui
