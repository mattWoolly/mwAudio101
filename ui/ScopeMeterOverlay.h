// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/ScopeMeterOverlay.h — the telemetry-driven scope / level-meter / cutoff-indicator
// overlay [docs/design/10-ui.md §5.1, §8.4, §10; ADR-015 C5, C7, C8, C12].
//
// CONTRACT (task 127):
//   • Renders SOLELY from a Telemetry::Snapshot it is handed. It holds NO audio-domain
//     state, no engine reference, no lock — only a value copy of the most-recent frame
//     for display [§8.3; ADR-015 C5/C12]. The Snapshot is the JUCE-free POD published
//     by the audio thread (post-VCA level L/R, decimated scope wave, modulated cutoff).
//   • setSnapshot() is called by the editor's coalescing Timer (task 115); the overlay
//     does NOT own a Timer. It caches the frame and requests a TARGETED repaint of its
//     OWN bounds only — a child Component's repaint() invalidates only the component's
//     bounds, never the whole editor [§7.3, §8.4; ADR-015 C7].
//   • Reduce-motion ON => setReduceMotion(true) switches paint to a STATIC / idle frame
//     (a flat baseline, no animated wave); a subsequent setSnapshot() updates the cached
//     levels but does NOT request an animation repaint while idle [§10; ADR-015 C8].
//   • Layout is in DESIGN units via the editor's single AffineTransform — the overlay
//     stores no absolute pixel coordinate; all geometry is a fraction of its own bounds
//     [§4; ADR-015 C1]. All (PI) layout proportions live in
//     core/calibration/ScopeMeterConstants.h — none is inlined here [§6.2; "(PI)"].
//   • All drawing colours / stroke weights come from the injected DesignTokens; the
//     overlay authors no concrete colour, so a token swap reskins it like every other
//     surface [§6.1, §6.2; ADR-015 C10].
//
// BUILD WIRING: this design-faithful header lives at ui/; its implementation lives at
// plugin/ui/ScopeMeterOverlay.cpp so the plugin glob compiles it into the plugin target
// + mw101_plugin_tests (CONFIGURE_DEPENDS) — no shared CMakeLists edit (mirrors
// plugin/ui/MwAudioLookAndFeel.cpp, plugin/ui/StatusBanner.cpp).
//
// OUT OF SCOPE (deliberately NOT owned here): the Timer drain (task 115 / MwAudioEditor),
// the telemetry types (task 107), and the background chrome (task ui-7). This component
// only renders whatever Snapshot it is handed and never reaches into the engine.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DesignTokens.h"   // sibling ui/: mw::ui::DesignTokens (JUCE-free POD)
#include "ui/Telemetry.h"   // mwcore (JUCE-free, core/ on include path): Telemetry::Snapshot

namespace mw::ui {

class ScopeMeterOverlay final : public juce::Component
{
public:
    // The overlay paints from the injected token table (non-owning; the editor owns the
    // table and may live-swap it via setTokens()). No audio-domain state is taken.
    explicit ScopeMeterOverlay(const DesignTokens& tokens);
    ~ScopeMeterOverlay() override;

    // --- Telemetry display feed (message thread; driven by the editor Timer, 115) -----
    // Cache the most-recent frame for display and request a TARGETED repaint of THIS
    // component's own bounds (never the whole editor) [§7.3, §8.4; ADR-015 C7]. While
    // reduce-motion is ON the cached levels still update, but no animation repaint is
    // requested — the next idle paint reflects the latest static levels [§10]. The
    // overlay copies the Snapshot by value; it retains no pointer into the SPSC ring.
    void setSnapshot(const Telemetry::Snapshot& snapshot);

    // --- Reduce-motion / idle gate (message thread; UI preference, 115/§10) -----------
    // ON => paint a STATIC / idle frame (flat scope baseline, no animation); control
    // bindings and automation are unaffected (none live here) [§10; ADR-015 C8]. Toggling
    // requests one targeted repaint of own bounds so the idle/animated state takes effect.
    void setReduceMotion(bool reduceMotion);
    [[nodiscard]] bool isReduceMotion() const noexcept { return reduceMotion_; }

    // --- Live reskin: swap the token table the overlay paints from [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // --- Inspection (message thread; pure reads for tests / editor) -------------------
    // A value copy of the most-recent cached display frame (no audio-domain state).
    [[nodiscard]] Telemetry::Snapshot getSnapshot() const noexcept { return snapshot_; }

    // Paint-count probe: how many times paint() has run since construction, so a test can
    // assert that a setSnapshot() / setReduceMotion() triggers a repaint of own bounds
    // (and only own bounds) — repaint-hygiene per §13 / ADR-015 C7.
    [[nodiscard]] int paintCount() const noexcept { return paintCount_; }

    // --- juce::Component --------------------------------------------------------------
    void paint(juce::Graphics&) override;

private:
    // Paint the animated frame (scope wave from the cached snapshot) [§8.4].
    void paintActive(juce::Graphics&, juce::Rectangle<float> scopeRegion);
    // Paint the static / idle frame (flat baseline) [§10; ADR-015 C8].
    void paintIdle(juce::Graphics&, juce::Rectangle<float> scopeRegion);
    // Paint the post-VCA L/R level meters from the cached snapshot.
    void paintMeters(juce::Graphics&, juce::Rectangle<float> meterColumn);
    // Paint the modulated-cutoff indicator readout from the cached snapshot.
    void paintCutoffIndicator(juce::Graphics&, juce::Rectangle<float> cutoffStrip);

    // The most-recent display frame, copied by value. Holds NO audio-domain state — it
    // is a presentation cache only [§8.3; ADR-015 C5/C12].
    Telemetry::Snapshot snapshot_{};

    bool reduceMotion_ = false;

    const DesignTokens* tokens_;   // non-owning; the editor owns the table

    int paintCount_ = 0;           // repaint-hygiene probe (see paintCount())

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeMeterOverlay)
};

} // namespace mw::ui
