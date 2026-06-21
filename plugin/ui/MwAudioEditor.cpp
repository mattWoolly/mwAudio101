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

#include "../../core/calibration/UiConstants.h"   // (PI) design extent / aspect / scale presets
#include "../../core/calibration/TimerConstants.h" // (PI) Timer rate band + scope region (115)

#include "../PluginProcessor.h"   // mw::plugin::MwAudioProcessor (apvts + stored editor size)

namespace mw::ui {

namespace cal  = mw::cal::editor;
namespace tcal = mw::cal::timer;

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
    // otherwise the default-scale design extent [§4.4]. (setSize triggers resized(),
    // which computes the scope dirty-rect — so initialise the transform-dependent
    // members below FIRST is unnecessary: recomputeScopeRegion() is robust to either
    // order because it reads the live transform.)
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

    // --- Telemetry Timer + reduce-motion (task 115) -------------------------------
    // Attach the SPSC Consumer view onto the processor-owned telemetry Buffer (message
    // thread; the audio thread is the single producer) [§8.3]. Restore the reduce-motion
    // UI preference from the <extras> subtree the processor persisted, then start the
    // SINGLE coalescing Timer at the resulting rate [§8.4, §10; ADR-015 C5/C8].
    telemetry_ = processor_.telemetryConsumer();
    reduceMotion_ = processor_.getStoredReduceMotion();
    scopeIdle_    = reduceMotion_;
    startTimerAtHz(reduceMotion_ ? tcal::kReduceMotionTimerHz : tcal::kDefaultTimerHz);

    // --- OpenGL opt-in escape hatch (task 130) ------------------------------------
    // The software/CPU render path is PRIMARY and DEFAULT — the render-backend context is
    // NOT attached here. Restore the persisted advanced opt-in from the processor's narrow
    // <extras>-UI accessor; only if it was explicitly turned ON in a prior session do we
    // attach the context now (via the same setter the advanced setting drives). A default
    // (key-less / never-opted-in) session leaves the hatch OFF [docs/design/10-ui.md §11;
    // ADR-015 C9].
    if (processor_.getStoredOpenGl())
        setOpenGlEnabled(true);
}

MwAudioEditor::~MwAudioEditor()
{
    // Stop the coalescing Timer before any member it drains is destroyed [§8.4].
    stopTimer();
    // Detach the OpenGL render-backend context (if the advanced opt-in attached it) BEFORE
    // this component is torn down, so a teardown with an attached context never leaves a
    // dangling context [docs/design/10-ui.md §11; ADR-015 C9]. Idempotent: a no-op when the
    // software path (the default) was active. (RenderBackend's own destructor also detaches,
    // but doing it here keeps the detach ordered ahead of the component teardown.)
    renderBackend_.detach();
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
    recomputeScopeRegion();   // keep the Timer's dirty-rect in sync with the new transform
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

// ---------------------------------------------------------------------------
// Coalescing telemetry Timer [§8.4; ADR-015 C5/C7].
//
// One Timer tick: pull the MOST-RECENT Snapshot from the SPSC (the Consumer coalesces
// past every intermediate frame). If nothing new was published, do NOTHING — no
// repaint. Otherwise cache the frame and, unless reduce-motion has idled the scope,
// trigger a TARGETED repaint of the scope/indicator dirty-rect ONLY — never the whole
// editor. The whole-editor repaint() is deliberately NOT called here (C7).
// ---------------------------------------------------------------------------
void MwAudioEditor::timerCallback()
{
    mw::ui::Telemetry::Snapshot frame{};
    lastPulled_ = telemetry_.pull(frame);
    if (! lastPulled_)
        return;                      // nothing new published since the last tick (§8.3)

    lastSnapshot_ = frame;           // coalesced to the newest frame (display cache)

    if (scopeIdle_)
        return;                      // reduce-motion: scope paints a static/idle frame (§10)

    // Targeted, NOT whole-editor: invalidate only the scope/indicator dirty-rect [C7].
    repaint(scopeRegionPx_);
    ++scopeRepaints_;
}

// ---------------------------------------------------------------------------
// Reduce-motion / low-CPU toggle [§10; ADR-015 C8].
//
// Downsample the single Timer to the (PI) reduce-motion rate and idle the scope (a
// static frame, no animation), or restore the default rate and resume animation. This
// touches ZERO APVTS attachment — it is a pure UI preference. The state is written back
// through the processor's narrow <extras>-UI accessor so it persists on reload.
// ---------------------------------------------------------------------------
void MwAudioEditor::setReduceMotion(bool reduceMotion)
{
    if (reduceMotion_ == reduceMotion)
        return;                      // idempotent; no redundant Timer restart

    reduceMotion_ = reduceMotion;
    scopeIdle_    = reduceMotion;    // ON => the scope settles to a static/idle frame (§10)

    // Persist the preference (message thread) so it round-trips on session reload. No
    // control attachment is involved — this is a <extras> UI preference only [ADR-008 C8].
    processor_.setStoredReduceMotion(reduceMotion_);

    // Re-rate the single Timer: downsampled when reducing motion, default otherwise. The
    // Timer is never destroyed/recreated — only its interval changes (no attachment churn).
    startTimerAtHz(reduceMotion_ ? tcal::kReduceMotionTimerHz : tcal::kDefaultTimerHz);
}

// ---------------------------------------------------------------------------
// OpenGL opt-in escape hatch [docs/design/10-ui.md §11; ADR-015 C9].
//
// The software/CPU render path is PRIMARY and DEFAULT. This is the ONLY place the
// render-backend context is attached, and only when an explicit advanced user setting
// requests it. ON attaches the context to this component; OFF detaches it CLEANLY. The
// opt-in is a UI PREFERENCE, persisted in the <extras> subtree via the processor's narrow
// accessor pair so it round-trips on reload (the same 114/115 pattern). It touches ZERO
// APVTS attachment. Idempotent; message-thread only.
// ---------------------------------------------------------------------------
void MwAudioEditor::setOpenGlEnabled(bool enabled)
{
    if (openGlEnabled_ == enabled)
        return;                      // idempotent; no redundant attach/detach

    openGlEnabled_ = enabled;

    if (enabled)
        renderBackend_.attach(*this);   // attach the §11 escape-hatch context to the editor
    else
        renderBackend_.detach();        // detach cleanly — back to the software path

    // Persist the preference (message thread) so it round-trips on session reload. No
    // control attachment is involved — this is a <extras> UI preference only [§11; C9].
    processor_.setStoredOpenGl(openGlEnabled_);
}

// ---------------------------------------------------------------------------
// Restart the single Timer at `hz`, recording the active rate. Clamped defensively to a
// positive interval; the (PI) rates are validated by static_assert in TimerConstants.h.
// ---------------------------------------------------------------------------
void MwAudioEditor::startTimerAtHz(int hz)
{
    timerHz_ = hz;
    startTimerHz(hz);   // juce::Timer: (re)arm at hz; replaces any prior interval
}

// ---------------------------------------------------------------------------
// Map the (PI) DESIGN-unit scope/indicator region through the single design->pixels
// transform to the pixel dirty-rect the Timer repaints. Clamped to the local bounds so
// the targeted region is always a valid sub-rectangle of the editor [§8.4; ADR-015 C7].
// ---------------------------------------------------------------------------
void MwAudioEditor::recomputeScopeRegion()
{
    const juce::Rectangle<float> design{
        tcal::kScopeRegionX, tcal::kScopeRegionY, tcal::kScopeRegionW, tcal::kScopeRegionH };

    const juce::Rectangle<float> px = design.transformedBy(designToPixels_);
    scopeRegionPx_ = px.getSmallestIntegerContainer().getIntersection(getLocalBounds());
}

} // namespace mw::ui
