// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/ScopeMeterOverlay.cpp — implementation of the telemetry-driven overlay
// declared in ui/ScopeMeterOverlay.h [docs/design/10-ui.md §5.1, §8.4, §10;
// ADR-015 C5, C7, C8, C12].
//
// The overlay paints from a value copy of the most-recent Telemetry::Snapshot only.
// It never touches the engine, takes no lock, holds no audio-domain state, and never
// requests a whole-editor repaint — a child Component's repaint() invalidates only its
// own bounds (targeted; §7.3, §8.4; ADR-015 C7). Every colour / stroke weight is read
// from the injected DesignTokens; pure layout proportions come from the (PI) header
// core/calibration/ScopeMeterConstants.h — no literal is inlined here [§6.2; "(PI)"].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) so the plugin glob compiles it
// into the plugin target + mw101_plugin_tests (CONFIGURE_DEPENDS). The design-faithful
// header stays at ui/ScopeMeterOverlay.h, reached by relative include — no shared
// CMakeLists edit (mirrors plugin/ui/MwAudioLookAndFeel.cpp, plugin/ui/StatusBanner.cpp).

#include "../../ui/ScopeMeterOverlay.h"

#include "../../ui/MwAudioLookAndFeel.h"  // toColour() — the token -> juce::Colour lift seam
#include "../../core/calibration/ScopeMeterConstants.h"

#include <algorithm>
#include <cmath>

namespace mw::ui {

namespace cal = mw::cal::scope_meter;

ScopeMeterOverlay::ScopeMeterOverlay(const DesignTokens& tokens)
    : tokens_(&tokens)
{
    // Transparent so the cached BackgroundLayer (task ui-7) shows through where the
    // overlay paints nothing; the overlay fills only its own panel area.
    setOpaque(false);
    setInterceptsMouseClicks(false, false);  // a read-only HUD; never steals input
}

ScopeMeterOverlay::~ScopeMeterOverlay() = default;

// ---------------------------------------------------------------------------
// Telemetry display feed (driven by the editor Timer, task 115) [§8.4].
// Cache the frame by value and request a TARGETED repaint of OWN bounds. We always
// request the repaint (so the static idle frame reflects updated levels too); the
// idle/animated choice happens in paint(). A child Component's repaint() can only
// invalidate this component's bounds — never the whole editor [§7.3; ADR-015 C7].
// ---------------------------------------------------------------------------
void ScopeMeterOverlay::setSnapshot(const Telemetry::Snapshot& snapshot)
{
    snapshot_ = snapshot;          // value copy: no pointer into the SPSC ring retained
    repaint();                     // OWN bounds only (targeted) [ADR-015 C7]
}

void ScopeMeterOverlay::setReduceMotion(bool reduceMotion)
{
    if (reduceMotion_ == reduceMotion)
        return;
    reduceMotion_ = reduceMotion;
    repaint();                     // OWN bounds only: switch idle<->animated paint [§10]
}

void ScopeMeterOverlay::setTokens(const DesignTokens& tokens)
{
    tokens_ = &tokens;
    repaint();                     // OWN bounds only: restyle on the next paint [§6.1]
}

// ---------------------------------------------------------------------------
// paint: lay the overlay out in DESIGN units expressed as fractions of its own bounds
// (no absolute pixel coordinate stored; the editor's AffineTransform already scaled the
// Graphics context) [§4; ADR-015 C1]. Three regions: scope trace (left), level meters
// (right), cutoff-indicator strip (bottom).
// ---------------------------------------------------------------------------
void ScopeMeterOverlay::paint(juce::Graphics& g)
{
    ++paintCount_;                 // repaint-hygiene probe (see header)

    const DesignTokens& t = *tokens_;
    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty())
        return;

    // Panel fill so the HUD reads as a distinct surface (token-driven colour) [§6.1].
    g.setColour(MwAudioLookAndFeel::toColour(t.panel));
    g.fillRoundedRectangle(bounds, t.cornerRadius);

    // Uniform inner pad, then split off the bottom cutoff strip, then split the
    // remaining area into the scope region (left) and the meter column (right).
    const float pad = cal::kInnerPadFraction * std::min(bounds.getWidth(), bounds.getHeight());
    auto inner = bounds.reduced(pad);

    auto cutoffStrip = inner.removeFromBottom(inner.getHeight() * cal::kCutoffStripHeightFraction);

    const float gap = cal::kRegionGapFraction * inner.getWidth();
    auto scopeRegion = inner.removeFromLeft(inner.getWidth() * cal::kScopeWidthFraction);
    inner.removeFromLeft(gap);
    auto meterColumn = inner;      // the remainder is the meter column

    // Scope trace: animated from the wave, or a static baseline under reduce-motion.
    if (reduceMotion_)
        paintIdle(g, scopeRegion);
    else
        paintActive(g, scopeRegion);

    paintMeters(g, meterColumn);
    paintCutoffIndicator(g, cutoffStrip);
}

// ---------------------------------------------------------------------------
// Active scope: the decimated wave (snapshot_.scope, nominally -1..+1) mapped to a
// polyline about the region's vertical centre [§8.4].
// ---------------------------------------------------------------------------
void ScopeMeterOverlay::paintActive(juce::Graphics& g, juce::Rectangle<float> scopeRegion)
{
    const DesignTokens& t = *tokens_;
    const auto& wave = snapshot_.scope;
    const int n = static_cast<int>(wave.size());
    if (n < 2 || scopeRegion.getWidth() <= 1.0f)
    {
        paintIdle(g, scopeRegion);   // degenerate: fall back to the baseline
        return;
    }

    const float midY = scopeRegion.getCentreY();
    const float amp  = scopeRegion.getHeight() * cal::kScopeAmplitudeFraction;

    juce::Path path;
    for (int i = 0; i < n; ++i)
    {
        const float fx = static_cast<float>(i) / static_cast<float>(n - 1);
        const float x  = scopeRegion.getX() + fx * scopeRegion.getWidth();
        const float v  = std::clamp(wave[static_cast<std::size_t>(i)], -1.0f, 1.0f);
        const float y  = midY - v * amp;   // +v goes UP (smaller y)
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    g.setColour(MwAudioLookAndFeel::toColour(t.controlFill));   // accent (token-driven)
    g.strokePath(path, juce::PathStrokeType(t.controlStroke * cal::kScopeStrokeFactor));
}

// ---------------------------------------------------------------------------
// Idle / static scope (reduce-motion): a flat baseline through the region centre, with
// NO dependence on the snapshot wave so the frame is identical frame-to-frame [§10;
// ADR-015 C8].
// ---------------------------------------------------------------------------
void ScopeMeterOverlay::paintIdle(juce::Graphics& g, juce::Rectangle<float> scopeRegion)
{
    const DesignTokens& t = *tokens_;
    const float midY = scopeRegion.getCentreY();

    juce::Path baseline;
    baseline.startNewSubPath(scopeRegion.getX(), midY);
    baseline.lineTo(scopeRegion.getRight(), midY);

    g.setColour(MwAudioLookAndFeel::toColour(t.controlTrack));  // quiet, non-accent
    g.strokePath(baseline, juce::PathStrokeType(t.controlStroke * cal::kIdleBaselineStrokeFactor));
}

// ---------------------------------------------------------------------------
// Post-VCA level meters: two vertical bars (L then R) growing UP from the bottom of the
// meter column in proportion to vcaLevelL / vcaLevelR (each 0..1) [§8.3]. Drawn in both
// the animated and idle states (a level read-out, not an animation) so a reduce-motion
// frame still shows the current static levels.
// ---------------------------------------------------------------------------
void ScopeMeterOverlay::paintMeters(juce::Graphics& g, juce::Rectangle<float> meterColumn)
{
    if (meterColumn.getWidth() <= 0.0f || meterColumn.getHeight() <= 0.0f)
        return;

    const DesignTokens& t = *tokens_;

    const float gap = cal::kMeterBarGapFraction * meterColumn.getWidth();
    const float totalGap = gap * static_cast<float>(cal::kNumMeterBars - 1);
    const float barW = (meterColumn.getWidth() - totalGap) / static_cast<float>(cal::kNumMeterBars);

    const float levels[cal::kNumMeterBars] = {
        std::clamp(snapshot_.vcaLevelL, 0.0f, 1.0f),
        std::clamp(snapshot_.vcaLevelR, 0.0f, 1.0f),
    };

    for (int i = 0; i < cal::kNumMeterBars; ++i)
    {
        const float x = meterColumn.getX() + static_cast<float>(i) * (barW + gap);
        const juce::Rectangle<float> track(x, meterColumn.getY(), barW, meterColumn.getHeight());

        // Track (quiet background of the bar).
        g.setColour(MwAudioLookAndFeel::toColour(t.controlTrack));
        g.fillRect(track);

        // Filled portion grows UP from the bottom in proportion to the level.
        const float fillFrac = std::max(levels[i], cal::kMeterFloorFraction);
        const float fillH = track.getHeight() * fillFrac;
        const juce::Rectangle<float> fill(track.getX(),
                                          track.getBottom() - fillH,
                                          track.getWidth(),
                                          fillH);
        g.setColour(MwAudioLookAndFeel::toColour(t.controlFill));   // accent
        g.fillRect(fill);
    }
}

// ---------------------------------------------------------------------------
// Modulated-cutoff indicator: a horizontal fill bar in the bottom strip whose width is
// proportional to vcfCutoffDisplay (0..1) [§8.3]. Token-driven colours; drawn in both
// states (a read-out, not an animation).
// ---------------------------------------------------------------------------
void ScopeMeterOverlay::paintCutoffIndicator(juce::Graphics& g, juce::Rectangle<float> cutoffStrip)
{
    if (cutoffStrip.getWidth() <= 0.0f || cutoffStrip.getHeight() <= 0.0f)
        return;

    const DesignTokens& t = *tokens_;

    const float barH = cutoffStrip.getHeight() * cal::kCutoffBarHeightFraction;
    const juce::Rectangle<float> track(cutoffStrip.getX(),
                                       cutoffStrip.getCentreY() - barH * 0.5f,
                                       cutoffStrip.getWidth(),
                                       barH);

    g.setColour(MwAudioLookAndFeel::toColour(t.controlTrack));
    g.fillRect(track);

    const float frac = std::clamp(snapshot_.vcfCutoffDisplay, 0.0f, 1.0f);
    const juce::Rectangle<float> fill(track.getX(), track.getY(), track.getWidth() * frac, track.getHeight());
    g.setColour(MwAudioLookAndFeel::toColour(t.controlThumb));   // distinct from the meter accent
    g.fillRect(fill);
}

} // namespace mw::ui
