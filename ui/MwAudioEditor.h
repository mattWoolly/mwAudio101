// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/MwAudioEditor.h — the editor ROOT: the single format-agnostic
// juce::AudioProcessorEditor that lays out over the 1000x640 LOGICAL design space,
// scales design-units->pixels via ONE juce::AffineTransform recomputed only in
// resized(), and enforces a FIXED-aspect resizable window with scale-preset snapping
// and a persisted size [docs/design/10-ui.md §4, §5.2; ADR-015 C1, C2].
//
// SCOPE (task 114 — the editor root only):
//   • Coordinate space + the single design->pixels AffineTransform (§4.1, §4.2).
//   • juce::ComponentBoundsConstrainer holding the frozen aspect ratio with min/max
//     from the (PI) calibration constants (§4.3) — NEVER inlined magic numbers.
//   • Scale presets (75/100/150/200%) that snap the window (§4.4).
//   • Editor size persisted through the processor's getStoredEditorSize() /
//     setStoredEditorSize() accessor pair (the narrow <extras>-UI seam, §4.4) so it
//     round-trips on session reload.
//   • getDesignToPixels() / getScaleFactor() test hooks (§4.2, §13).
//
// TELEMETRY TIMER + REDUCE-MOTION (task 115 — ui-6): the root is also the SINGLE
// coalescing juce::Timer that drains the processor's audio->GUI SPSC telemetry
// Consumer to the MOST-RECENT Snapshot at a 30-60 Hz default rate (floor 30, from the
// (PI) calibration band) and triggers a TARGETED repaint of the scope / indicator
// region ONLY — never a whole-editor repaint [docs/design/10-ui.md §8.4; ADR-015
// C5/C7]. A reduce-motion / low-CPU toggle downsamples that Timer and idles the scope
// WITHOUT affecting any APVTS attachment, and persists in the <extras> UI subtree via
// the processor's narrow accessor pair [docs/design/10-ui.md §10; ADR-015 C8].
//
// OUT OF SCOPE (other tasks; deliberately NOT owned here): the module internals
// (ui-8..ui-14), the cached background regen internals (ui-7), and the actual scope
// PAINTING / Snapshot+Consumer types (ui-15 / ui-4). The root paints only the
// (currently empty) static background fill and exposes the geometry seam the modules
// will later lay out into in DESIGN units via the AffineTransform [docs/design/10-ui.md
// §5.1].
//
// BUILD WIRING: this header lives at the design-faithful path ui/; its implementation
// lives under plugin/ui/MwAudioEditor.cpp so the plugin glob compiles it into the
// plugin target + mw101_plugin_tests (CONFIGURE_DEPENDS) — no shared CMakeLists edit
// (mirrors plugin/ui/MwAudioLookAndFeel.cpp).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DesignTokens.h"        // sibling ui/: mw::ui::DesignTokens (JUCE-free POD)
#include "MwAudioLookAndFeel.h"  // sibling ui/: the custom vector LookAndFeel (task 108)
#include "RenderBackend.h"       // sibling ui/: the §11 OpenGL opt-in escape hatch (task 130)

#include "ui/Telemetry.h"        // mwcore (JUCE-free): mw::ui::Telemetry::Consumer/Snapshot (107)

namespace mw::plugin { class MwAudioProcessor; }

namespace mw::ui {

// The root derives juce::Timer so it owns the ONE coalescing telemetry Timer (§8.4).
class MwAudioEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit MwAudioEditor(mw::plugin::MwAudioProcessor& processor);
    ~MwAudioEditor() override;

    // Paints ONLY the cached static background fill (no per-frame path work). The
    // module chrome / patch-line regen is task ui-7 [§7.1].
    void paint(juce::Graphics&) override;

    // Recompute the SINGLE design->pixels AffineTransform and lay child modules out in
    // DESIGN units; persist the new size via the processor accessor (§4.2, §4.4).
    void resized() override;

    // --- Scale presets (75/100/150/200%) [§4.4; ADR-015 C2] -----------------------
    // Snap the window to a logical-scale preset by index into the (PI) preset list.
    // Out-of-range indices are a safe no-op. Sets bounds to scale*kDesignWidth x
    // scale*kDesignHeight; the constrainer keeps the aspect exact.
    void applyScalePreset(int presetIndex);
    [[nodiscard]] static int getNumScalePresets() noexcept;
    [[nodiscard]] static float scalePresetAt(int presetIndex) noexcept;

    // --- Reduce-motion / low-CPU toggle [§10; ADR-015 C8] -------------------------
    // The non-APVTS seam the TransportModeBar's onReduceMotionChanged callback drives
    // (task 125), or that task 114c wires when it composes the bar into this root. ON
    // downsamples the single telemetry Timer (to the (PI) reduce-motion rate) and idles
    // the scope; OFF restores the default rate. It NEVER touches an APVTS attachment —
    // it is a UI preference, persisted in the <extras> subtree via the processor's narrow
    // accessor pair so it round-trips on reload [§10; ADR-008 §4/§5 C8].
    void setReduceMotion(bool reduceMotion);
    [[nodiscard]] bool reduceMotionEnabled() const noexcept { return reduceMotion_; }

    // --- OpenGL opt-in escape hatch [§11; ADR-015 C9] -----------------------------
    // The software/CPU render path is PRIMARY and DEFAULT: the render-backend context is
    // NOT attached in the constructor. This setter is the ONLY way the context is
    // attached, and only from an explicit ADVANCED user setting — ON attaches it, OFF
    // detaches it CLEANLY (the destructor also detaches, so a teardown with an attached
    // context never dangles). The opt-in is a UI PREFERENCE, not a host parameter, so it
    // persists in the canonical <extras> subtree via the processor's narrow accessor pair
    // (the same 114/115 pattern) and round-trips on session reload. The Linux x64 hard
    // gate never requires OpenGL — it runs this software default [§11; ADR-015 C9;
    // ADR-011 platform tiers; ADR-008 §4/§5]. Idempotent; message-thread only.
    void setOpenGlEnabled(bool enabled);
    [[nodiscard]] bool openGlEnabled() const noexcept { return openGlEnabled_; }

    // --- Test / inspection hooks (no audio-domain state) [§4.2, §13] --------------
    [[nodiscard]] juce::AffineTransform getDesignToPixels() const noexcept { return designToPixels_; }
    [[nodiscard]] float getScaleFactor() const noexcept;

    // --- Timer / telemetry test hooks (task 115; no audio-thread state) -----------
    // The current Timer rate (Hz): the default within the 30-60 band, or the reduce-
    // motion downsample rate when the toggle is on. timerCallback() is also exposed so a
    // headless test ticks the coalescing drain without a real wall-clock wait.
    [[nodiscard]] bool isTimerRunning()    const noexcept { return juce::Timer::isTimerRunning(); }
    [[nodiscard]] int  getTimerHzForTest() const noexcept { return timerHz_; }
    [[nodiscard]] bool scopeIsIdleForTest() const noexcept { return scopeIdle_; }
    [[nodiscard]] bool lastPulledFrameForTest() const noexcept { return lastPulled_; }
    [[nodiscard]] int  scopeRepaintCountForTest() const noexcept { return scopeRepaints_; }
    [[nodiscard]] int  wholeEditorRepaintCountForTest() const noexcept { return wholeRepaints_; }
    [[nodiscard]] mw::ui::Telemetry::Snapshot lastSnapshotForTest() const noexcept { return lastSnapshot_; }
    [[nodiscard]] juce::Rectangle<int> scopeRepaintRegionForTest() const noexcept { return scopeRegionPx_; }

    // True iff the §11 render-backend (OpenGL) context is currently attached. Lets a test
    // prove the escape hatch is OFF (software path) by default and that the explicit
    // setter attaches/detaches it (task 130). No audio-domain state.
    [[nodiscard]] bool renderBackendAttachedForTest() const noexcept { return renderBackend_.isAttached(); }

    // The constrainer is exposed for geometry tests (aspect-ratio enforcement, §4.3).
    [[nodiscard]] const juce::ComponentBoundsConstrainer& constrainerForTest() const noexcept
    {
        return constrainer_;
    }

    // The frozen design-space extent (design units) the layout is expressed over.
    [[nodiscard]] static float designWidth()  noexcept;
    [[nodiscard]] static float designHeight() noexcept;
    [[nodiscard]] static float aspectRatio()  noexcept;

    // The single coalescing telemetry drain: pull the MOST-RECENT Snapshot and, unless
    // reduce-motion idles the scope, trigger a TARGETED repaint of the scope/indicator
    // region only — never the whole editor [§8.4; ADR-015 C5/C7]. Public so a headless
    // test can tick it directly (juce::Timer's callback is otherwise driven by the
    // message loop). Const-correctness aside, it mutates only display/test state.
    void timerCallback() override;

private:
    // Restart the Timer at `hz`, recording the active rate. Idempotent.
    void startTimerAtHz(int hz);

    // Recompute the pixel scope-repaint region from the current bounds (a fixed DESIGN-
    // unit sub-rectangle mapped through designToPixels_) so repaints stay targeted.
    void recomputeScopeRegion();
    // Recompute designToPixels_ from the current local bounds: a single uniform scale
    // (the fit factor) plus a centring translation, so the design space is letterboxed
    // inside the (aspect-locked) window with no distortion [§4.2].
    void recomputeTransform();

    // Persist the current pixel size through the processor's narrow <extras>-UI
    // accessor (message thread) [§4.4]. Cheap; called from resized().
    void persistSize();

    mw::plugin::MwAudioProcessor& processor_;
    juce::AudioProcessorValueTreeState& apvts_;   // reference only (lives in processor)

    MwAudioLookAndFeel lookAndFeel_;              // the custom vector LookAndFeel (108)
    DesignTokens tokens_;                         // the active design-token table
    juce::ComponentBoundsConstrainer constrainer_;
    juce::AffineTransform designToPixels_;        // the ONE design->pixels transform

    // --- Coalescing telemetry Timer + reduce-motion (task 115) --------------------
    // The single SPSC Consumer view onto the processor-owned telemetry Buffer; pull()
    // coalesces to the most-recent Snapshot [§8.3/§8.4]. Constructed on the message
    // thread from the processor accessor; never touched by the audio thread.
    mw::ui::Telemetry::Consumer telemetry_;
    mw::ui::Telemetry::Snapshot lastSnapshot_{};  // the most-recent pulled frame (display)
    bool  reduceMotion_ = false;                  // the UI-preference toggle state
    int   timerHz_      = 0;                       // the live Timer rate (Hz)

    // --- OpenGL opt-in escape hatch (task 130) ------------------------------------
    // The §11 render-backend hatch. Owned here but UNATTACHED by default (software path
    // is primary); attached ONLY via setOpenGlEnabled(true) and detached on teardown.
    // openGlEnabled_ mirrors the persisted <extras> opt-in (restored from the processor
    // accessor on construction; written back when toggled) [§11; ADR-015 C9].
    RenderBackend renderBackend_;
    bool          openGlEnabled_ = false;
    bool  scopeIdle_    = false;                   // scope paints a static/idle frame (§10)
    juce::Rectangle<int> scopeRegionPx_;          // the targeted scope/indicator dirty-rect

    // Test-only counters (message-thread; no audio-domain state).
    bool lastPulled_   = false;
    int  scopeRepaints_ = 0;
    int  wholeRepaints_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MwAudioEditor)
};

} // namespace mw::ui
