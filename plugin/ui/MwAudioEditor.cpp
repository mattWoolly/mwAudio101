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
#include "../../core/calibration/EditorLayoutConstants.h" // (PI) §5.1 design-unit placement map (114c)

#include "../PluginProcessor.h"   // mw::plugin::MwAudioProcessor (apvts + stored editor size)

namespace mw::ui {

namespace cal  = mw::cal::editor;
namespace tcal = mw::cal::timer;
namespace lay  = mw::cal::editor::layout;

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
    // --- The 12 assembled panel members (task 114c) -------------------------------
    // Each is constructed with exactly the accessor it needs: apvts() for the APVTS-
    // attached signal-flow modules; the processor itself for the SequencerGrid (its
    // <extras> seq-pattern handoff); the PresetManager for the browser; the design
    // tokens for the token-painted overlay / banner. Declaration order == init order;
    // it mirrors the §5.1 Z-order so addAndMakeVisible below stacks them correctly.
    , background_()
    , modulator_(apvts_)
    , vco_(apvts_)
    , mixer_(apvts_)
    , vcf_(apvts_)
    , vca_(apvts_)
    , controller_(apvts_)
    , transport_(apvts_)
    , sequencer_(processor)
    , presetBrowser_(processor.presetManager())
    , banner_(tokens_)
    , scope_(tokens_)
{
    setLookAndFeel(&lookAndFeel_);

    // --- Assemble the panel: addAndMakeVisible in Z-ORDER (§5.1) ------------------
    // BackgroundLayer is the BOTTOM cached layer; the functional modules sit above it;
    // PresetBrowser + StatusBanner per §9; ScopeMeterOverlay is the TOP overlay. JUCE
    // stacks children in addAndMakeVisible order (first == back), so this sequence is the
    // documented Z-order. Each child carries a stable component ID so the editor / tests
    // can find it by name (findChildWithID).
    auto add = [this](juce::Component& c, const char* id)
    {
        c.setComponentID(id);
        addAndMakeVisible(c);
    };

    add(background_,   "BackgroundLayer");   // bottom underlay
    add(modulator_,    "ModulatorModule");
    add(vco_,          "VcoModule");
    add(mixer_,        "SourceMixerModule");
    add(vcf_,          "VcfModule");
    add(vca_,          "VcaModule");
    add(controller_,   "ControllerStrip");
    add(transport_,    "TransportModeBar");
    add(sequencer_,    "SequencerGrid");
    add(presetBrowser_,"PresetBrowser");
    add(banner_,       "StatusBanner");
    add(scope_,        "ScopeMeterOverlay"); // top overlay

    // Broadcast the single design-token table to every token-painted child so a future
    // reskin (a token-table swap) restyles the whole panel with no layout/binding change
    // [§6.1; ADR-015 C10]. (BackgroundLayer takes its tokens at regenerate() time.)
    modulator_.setTokens(tokens_);
    vco_.setTokens(tokens_);
    vca_.setTokens(tokens_);
    controller_.setTokens(tokens_);
    transport_.setTokens(tokens_);
    sequencer_.setTokens(tokens_);
    presetBrowser_.setTokens(tokens_);
    scope_.setTokens(tokens_);
    banner_.setTokens(tokens_);
    // The overlay must reflect the persisted reduce-motion preference (restored below).

    // --- Wire the TransportModeBar NON-APVTS seams to the editor (§4.4, §5.3, §10) -
    // These are the bar's narrow callback seams (NOT host parameters). The editor owns the
    // behavior each drives; the bar only reports the affordance state.
    //   onScalePresetSelected -> the editor's scale-preset snap (114, applyScalePreset).
    //   onReduceMotionChanged -> the telemetry Timer suppression (115, setReduceMotion).
    //   onRunStateChanged     -> the transport/arp run state. The processor now exposes the
    //       transient Run/Hold transport seam (task 182; ADR-030 part 2), so this forwards the
    //       reported run state to processor_.setTransportRunning — a message-thread RELEASE
    //       store the audio thread loads each block into BlockContext::transport.runHeld, where
    //       the engine free-runs the INTERNAL clock at RATE while RUN is held (ADR-022 Free-run
    //       rung; closes ADR-030 break Q4, the 114c dead end). The local lastTransportRunning_
    //       mirror is kept for the transportRunningForTest() introspection accessor. NOTE: this
    //       seam is the TRANSPORT (run/hold), NOT the persisted seq.mode play/record state —
    //       those stay APVTS-driven through the engine's seq.mode dispatch (task 181); the two
    //       must not fight (ADR-030 RECONCILIATION).
    transport_.onScalePresetSelected = [this](int presetIndex) { applyScalePreset(presetIndex); };
    transport_.onReduceMotionChanged = [this](bool reduce)     { setReduceMotion(reduce); };
    transport_.onRunStateChanged     = [this](bool running)
    {
        lastTransportRunning_ = running;            // local mirror for transportRunningForTest()
        processor_.setTransportRunning(running);    // the real transport seam (task 182)
    };

    // --- Wire the PresetBrowser load sink to the message-thread recall path (§9.3) -
    // The browser invokes this with the chosen ABSOLUTE bank index; setCurrentProgram routes
    // to PresetManager::loadPreset on the message thread (no tree pointer crosses to audio)
    // [§9.3; ADR-015 C6]. This is the same recall path Program Change uses.
    presetBrowser_.onLoadRequested = [this](int absoluteIndex)
    {
        processor_.setCurrentProgram(absoluteIndex);
    };

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
    scope_.setReduceMotion(reduceMotion_);   // the overlay idles in lock-step with the editor (§10)
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

    // Regenerate the cached static chrome at the new pixel size, drawn in design space and
    // mapped through the single design->pixels transform (regenerate runs ONLY on resize —
    // never on the telemetry Timer) [§7.1; ADR-015 C7]. The BackgroundLayer fills the whole
    // editor and the layout below positions the functional modules over it.
    background_.regenerate(getLocalBounds(), designToPixels_, tokens_);

    // Position every assembled module / component in DESIGN units per the §5.1 map.
    layoutChildren();

    persistSize();
}

// ---------------------------------------------------------------------------
// Place every assembled module / component. Each placement RECT is a named design-unit
// rectangle from core/calibration/EditorLayoutConstants.h (NO inlined magic numbers); it
// is mapped through the single design->pixels AffineTransform to the pixel bounds the child
// occupies. The modules that expose layoutDesignUnits() sub-divide their OWN local
// rectangle (their resized() forwards getLocalBounds()), so the design-unit contract holds
// end to end. The BackgroundLayer fills the whole editor (it draws chrome in design space
// internally) and the ScopeMeterOverlay overlays the top-right scope region [§4, §5.1].
// ---------------------------------------------------------------------------
void MwAudioEditor::layoutChildren()
{
    // Map a design-unit rectangle to integer pixel bounds via the single transform [§4.2].
    const auto toPixels = [this](float x, float y, float w, float h)
    {
        return juce::Rectangle<float>{ x, y, w, h }
            .transformedBy(designToPixels_)
            .getSmallestIntegerContainer();
    };

    // The cached chrome underlay fills the whole editor; it rasterizes its design-space art
    // through the transform itself, so it is positioned in pixels, not design units.
    background_.setBounds(getLocalBounds());

    // The five signal-flow modules: one uniform row of cells aligned 1:1 with the chrome the
    // BackgroundLayer strokes (same row origin / gap / width as the background constants).
    modulator_.setBounds(toPixels(lay::moduleCellX(lay::kIdxModulator),   lay::kRowTop, lay::kModuleWidth, lay::kRowHeight));
    vco_.setBounds      (toPixels(lay::moduleCellX(lay::kIdxVco),         lay::kRowTop, lay::kModuleWidth, lay::kRowHeight));
    mixer_.setBounds    (toPixels(lay::moduleCellX(lay::kIdxSourceMixer), lay::kRowTop, lay::kModuleWidth, lay::kRowHeight));
    vcf_.setBounds      (toPixels(lay::moduleCellX(lay::kIdxVcf),         lay::kRowTop, lay::kModuleWidth, lay::kRowHeight));
    vca_.setBounds      (toPixels(lay::moduleCellX(lay::kIdxVca),         lay::kRowTop, lay::kModuleWidth, lay::kRowHeight));

    // The controller strip / transport bar / sequencer grid / preset browser, in the band
    // below the row (disjoint horizontal bands; grid + browser side by side).
    controller_.setBounds   (toPixels(lay::kControllerX, lay::kControllerY, lay::kControllerW, lay::kControllerH));
    transport_.setBounds    (toPixels(lay::kTransportX,  lay::kTransportY,  lay::kTransportW,  lay::kTransportH));
    sequencer_.setBounds    (toPixels(lay::kSeqGridX,    lay::kSeqGridY,    lay::kSeqGridW,    lay::kSeqGridH));
    presetBrowser_.setBounds(toPixels(lay::kPresetX,     lay::kPresetY,     lay::kPresetW,     lay::kPresetH));

    // The status banner: a slim strip across the top-left [§9.4].
    banner_.setBounds(toPixels(lay::kBannerX, lay::kBannerY, lay::kBannerW, lay::kBannerH));

    // The scope/meter overlay: the TOP layer over the top-right scope region (the same
    // design-unit region the Timer targets) — an INTENTIONAL overlay [§5.1, §8.4].
    scope_.setBounds(toPixels(lay::kScopeX, lay::kScopeY, lay::kScopeW, lay::kScopeH));

    // Make the design-unit sub-division contract EXPLICIT for the modules that expose it:
    // forward each module's OWN local bounds into its layoutDesignUnits() so children are
    // placed proportionally in design units (setBounds already triggered resized() which
    // does the same; calling it here documents the seam and is idempotent) [§5.3].
    modulator_.layoutDesignUnits(modulator_.getLocalBounds().toFloat());
    vco_.layoutDesignUnits(vco_.getLocalBounds().toFloat());
    mixer_.layoutDesignUnits(mixer_.getLocalBounds().toFloat());
    vcf_.layoutDesignUnits(vcf_.getLocalBounds().toFloat());
    vca_.layoutDesignUnits(vca_.getLocalBounds().toFloat());
    controller_.layoutDesignUnits(controller_.getLocalBounds().toFloat());
    transport_.layoutDesignUnits(transport_.getLocalBounds().toFloat());
    sequencer_.layoutDesignUnits(sequencer_.getLocalBounds().toFloat());
    presetBrowser_.layoutDesignUnits(presetBrowser_.getLocalBounds().toFloat());
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

    // Feed the newest frame to the telemetry-driven children (DISPLAY-ONLY): the scope/
    // meter overlay renders it, and the sequencer grid takes the current-step highlight
    // from it. Each child requests a TARGETED repaint of its OWN bounds only — never the
    // whole editor [§8.3/§8.4; ADR-015 C7]. While reduce-motion idles the scope the overlay
    // caches the levels but does not animate (§10).
    scope_.setSnapshot(frame);
    sequencer_.setSnapshot(frame);

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
    scope_.setReduceMotion(reduceMotion_);   // the overlay idles/animates in lock-step (§10)

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
