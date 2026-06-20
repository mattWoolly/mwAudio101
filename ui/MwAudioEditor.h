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
// OUT OF SCOPE (other tasks; deliberately NOT owned here): the module internals
// (ui-8..ui-14), the cached background regen internals (ui-7), and the telemetry
// Timer logic (ui-6). The root paints only the (currently empty) static background
// fill and exposes the geometry seam the modules will later lay out into in DESIGN
// units via the AffineTransform [docs/design/10-ui.md §5.1].
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

namespace mw::plugin { class MwAudioProcessor; }

namespace mw::ui {

class MwAudioEditor final : public juce::AudioProcessorEditor
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

    // --- Test / inspection hooks (no audio-domain state) [§4.2, §13] --------------
    [[nodiscard]] juce::AffineTransform getDesignToPixels() const noexcept { return designToPixels_; }
    [[nodiscard]] float getScaleFactor() const noexcept;

    // The constrainer is exposed for geometry tests (aspect-ratio enforcement, §4.3).
    [[nodiscard]] const juce::ComponentBoundsConstrainer& constrainerForTest() const noexcept
    {
        return constrainer_;
    }

    // The frozen design-space extent (design units) the layout is expressed over.
    [[nodiscard]] static float designWidth()  noexcept;
    [[nodiscard]] static float designHeight() noexcept;
    [[nodiscard]] static float aspectRatio()  noexcept;

private:
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MwAudioEditor)
};

} // namespace mw::ui
