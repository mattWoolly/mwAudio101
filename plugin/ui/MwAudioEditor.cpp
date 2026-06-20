// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/MwAudioEditor.cpp — implementation of the editor root declared in
// ui/MwAudioEditor.h. The layout is expressed ENTIRELY in logical design units over
// the 1000x640 design space; resize() recomputes a SINGLE design->pixels
// juce::AffineTransform and (later, ui-8..ui-14) lays out child modules in DESIGN
// units; a juce::ComponentBoundsConstrainer enforces the FROZEN aspect ratio with
// min/max read from the (PI) calibration constants [docs/design/10-ui.md §4; ADR-015
// C1, C2]. There are ZERO hard-coded pixel coordinates here.
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/MwAudioEditor.h and is
// reached by relative include — no shared CMakeLists edit (mirrors
// plugin/ui/MwAudioLookAndFeel.cpp).

#include "../../ui/MwAudioEditor.h"

#include "../../core/calibration/UiConstants.h"  // (PI) design extent / aspect / scale presets

#include "../PluginProcessor.h"   // mw::plugin::MwAudioProcessor (apvts + stored editor size)

namespace mw::ui {

namespace cal = mw::cal::editor;

// ---------------------------------------------------------------------------
// Static design-space accessors (forward the (PI) constants; never inlined).
// ---------------------------------------------------------------------------
float MwAudioEditor::designWidth()  noexcept { return cal::kDesignWidth; }
float MwAudioEditor::designHeight() noexcept { return cal::kDesignHeight; }
float MwAudioEditor::aspectRatio()  noexcept { return cal::kAspectRatio; }

int   MwAudioEditor::getNumScalePresets() noexcept { return cal::kNumScalePresets; }

float MwAudioEditor::scalePresetAt(int presetIndex) noexcept
{
    if (presetIndex < 0 || presetIndex >= cal::kNumScalePresets)
        return cal::kDefaultScale;
    return cal::kScalePresets[static_cast<std::size_t>(presetIndex)];
}

// ---------------------------------------------------------------------------
// Construction: install the LookAndFeel, configure the aspect-locked constrainer
// with (PI) min/max pixel limits, then set the initial size from the persisted
// <extras> UI size (if present) or the default scale [§4.3, §4.4].
// ---------------------------------------------------------------------------
MwAudioEditor::MwAudioEditor(mw::plugin::MwAudioProcessor& processor)
    : juce::AudioProcessorEditor(processor)
    , processor_(processor)
    , apvts_(processor.apvts())
    , lookAndFeel_(DesignTokens::defaultTheme())
    , tokens_(DesignTokens::defaultTheme())
{
    setLookAndFeel(&lookAndFeel_);

    // Aspect-locked resizable window: the constrainer holds the FROZEN aspect ratio
    // across every resize, and clamps the logical scale to [kMinScale, kMaxScale]
    // in pixels [§4.3; ADR-015 C1, C2]. min/max come from the (PI) calibration
    // constants — never inlined.
    const int minW = juce::roundToInt(cal::kDesignWidth  * cal::kMinScale);
    const int minH = juce::roundToInt(cal::kDesignHeight * cal::kMinScale);
    const int maxW = juce::roundToInt(cal::kDesignWidth  * cal::kMaxScale);
    const int maxH = juce::roundToInt(cal::kDesignHeight * cal::kMaxScale);

    constrainer_.setFixedAspectRatio(static_cast<double>(cal::kAspectRatio));
    constrainer_.setSizeLimits(minW, minH, maxW, maxH);

    setResizable(/*useBottomRightCornerResizer*/ true, /*useConstrainer*/ true);
    setConstrainer(&constrainer_);

    // Initial size: the persisted <extras> UI size if a valid one round-tripped,
    // otherwise the default-scale design extent [§4.4].
    const juce::Point<int> stored = processor_.getStoredEditorSize();
    if (stored.x >= minW && stored.y >= minH && stored.x <= maxW && stored.y <= maxH)
    {
        setSize(stored.x, stored.y);
    }
    else
    {
        setSize(juce::roundToInt(cal::kDesignWidth  * cal::kDefaultScale),
                juce::roundToInt(cal::kDesignHeight * cal::kDefaultScale));
    }
}

MwAudioEditor::~MwAudioEditor()
{
    // Detach the LookAndFeel before it (a member) is destroyed [JUCE lifetime rule].
    setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// Paint: ONLY the cached static background fill. Module chrome / patch lines are the
// cached BackgroundLayer (task ui-7) — out of scope here; no per-frame path work [§7.1].
// ---------------------------------------------------------------------------
void MwAudioEditor::paint(juce::Graphics& g)
{
    g.fillAll(MwAudioLookAndFeel::toColour(tokens_.background));
}

// ---------------------------------------------------------------------------
// Resize: recompute the SINGLE design->pixels AffineTransform and persist the new
// pixel size. Child-module layout (in DESIGN units, via this transform) is wired by
// ui-8..ui-14 [§4.2, §4.4].
// ---------------------------------------------------------------------------
void MwAudioEditor::resized()
{
    recomputeTransform();
    persistSize();
}

void MwAudioEditor::recomputeTransform()
{
    // ONE AffineTransform maps the design space to physical pixels: a single uniform
    // scale (the fit factor) plus a centring translation so the design space is
    // letterboxed without distortion. Because the constrainer locks the aspect ratio
    // to the design aspect, the centring offsets are ~0 in practice, but they are
    // computed generally so an off-aspect transient never distorts the layout [§4.2].
    const auto bounds = getLocalBounds().toFloat();

    const float scale = juce::jmin(bounds.getWidth()  / cal::kDesignWidth,
                                   bounds.getHeight() / cal::kDesignHeight);

    const float scaledW = cal::kDesignWidth  * scale;
    const float scaledH = cal::kDesignHeight * scale;
    const float offsetX = (bounds.getWidth()  - scaledW) * 0.5f;
    const float offsetY = (bounds.getHeight() - scaledH) * 0.5f;

    designToPixels_ = juce::AffineTransform::scale(scale).translated(offsetX, offsetY);
}

void MwAudioEditor::persistSize()
{
    // Round-trip the new pixel size through the processor's narrow <extras>-UI
    // accessor (message thread) so it restores on session reload [§4.4; ADR-015 C2].
    processor_.setStoredEditorSize({ getWidth(), getHeight() });
}

// ---------------------------------------------------------------------------
// Scale presets: snap the window to scale*kDesignWidth x scale*kDesignHeight; the
// constrainer keeps the aspect exact and clamps to the min/max limits [§4.4].
// ---------------------------------------------------------------------------
void MwAudioEditor::applyScalePreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= cal::kNumScalePresets)
        return;

    const float scale = cal::kScalePresets[static_cast<std::size_t>(presetIndex)];
    setSize(juce::roundToInt(cal::kDesignWidth  * scale),
            juce::roundToInt(cal::kDesignHeight * scale));
}

float MwAudioEditor::getScaleFactor() const noexcept
{
    // The live fit factor implied by the current design->pixels transform: the design
    // origin maps to the centred pixel origin, and (kDesignWidth, 0) maps to a point
    // whose x-distance from the origin is scale*kDesignWidth — so dividing recovers the
    // uniform scale [§4.2].
    float ox = 0.0f, oy = 0.0f;
    designToPixels_.transformPoint(ox, oy);
    float rx = cal::kDesignWidth, ry = 0.0f;
    designToPixels_.transformPoint(rx, ry);
    return (rx - ox) / cal::kDesignWidth;
}

} // namespace mw::ui
