// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/MwAudioLookAndFeel.h — the custom juce::LookAndFeel_V4 that draws every control
// (rotary slider, linear slider, toggle button, combo box) ENTIRELY from
// juce::Path / juce::Graphics vector primitives, parameterized by the single
// DesignTokens table, with a live setTokens() reskin / theme switch
// [docs/design/10-ui.md §6.2; ADR-015 C10, C11].
//
// Contract realized here:
//   • Every draw* override renders from juce::Path/Graphics only — no raster asset,
//     no filmstrip read (§6.2; ADR-015 C11).
//   • Drawing colours and stroke weights are read SOLELY from the injected
//     DesignTokens (§6.2). The only non-token drawing inputs are pure shape
//     proportions, centralized as (PI) constants in
//     core/calibration/LookAndFeelConstants.h (no inlined literals).
//   • setTokens() restyles the output without touching layout or binding code
//     (§6.1; ADR-015 C10).
//
// SCOPE NOTE: this task owns only the LookAndFeel (the drawing seam). The control
// subclasses (ui/controls/*) are task ui-3, the token VALUES are task ui-1 (106),
// and the background chrome is task ui-7 — all OUT OF SCOPE here (§ task 108).
//
// DesignTokens is a JUCE-free POD (packed-ARGB mw::ui::Colour + mw::ui::FontSpec);
// this LookAndFeel is the JUCE seam that lifts a Colour to juce::Colour(argb) and a
// FontSpec to a juce::Font at the point of drawing — the one-liner lift the token
// header documents.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DesignTokens.h"   // sibling: mw::ui::DesignTokens (JUCE-free POD)

namespace mw::ui {

class MwAudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit MwAudioLookAndFeel(DesignTokens initialTokens);

    // Live reskin / theme switch: swap the whole token table; the next paint pass
    // restyles every control with no layout or binding change [§6.1; ADR-015 C10].
    void setTokens(DesignTokens newTokens);

    // Inspection hook for tests (read-only view of the active table).
    const DesignTokens& getTokens() const noexcept { return tokens_; }

    // --- juce::LookAndFeel_V4 draw overrides (all vector, all token-driven) -------
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;

    juce::Font getLabelFont(juce::Label&) override;

    // --- token -> JUCE lift helpers (the single seam; public for unit tests) ------
    static juce::Colour toColour(const Colour& c) noexcept { return juce::Colour(c.argb); }
    static juce::Font   toFont(const FontSpec& f);

private:
    // Re-seed the LookAndFeel_V4 ColourScheme + per-component colour IDs from the
    // active tokens so that any default JUCE draw path we do NOT override still
    // matches the palette (defence in depth for the single-reskin contract).
    void applyTokensToColourIds();

    DesignTokens tokens_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MwAudioLookAndFeel)
};

} // namespace mw::ui
