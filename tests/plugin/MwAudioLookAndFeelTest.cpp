// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/MwAudioLookAndFeelTest.cpp — JUCE-linked acceptance tests for the
// custom MwAudioLookAndFeel (task 108). Test-case display names begin with the task
// tag `ui_laf` so `ctest -R ui_laf` selects exactly these cases (silent-pass rule).
//
// The GUI is NOT pixel-identical across platforms, so these tests assert BEHAVIOUR
// (token application) and RENDERED-COLOUR PRESENCE/CHANGE, never exact pixel layout
// [docs/design/10-ui.md §13; ADR-015 Consequences]. We render each control into an
// offscreen juce::Image and assert that:
//   • the active token's accent colour (controlFill) is present in the drawing
//     (proving the draw* override actually reads the injected tokens), and
//   • swapping the token table via setTokens() changes the rendered colours
//     (the C10 single-reskin acceptance: a token swap restyles every control).
//
// Acceptance criteria covered (task 108 / §6.2 / ADR-015 C10, C11):
//   [1] Every draw* override renders from juce::Path/Graphics only, no raster asset.
//       — exercised by drawing each override into an Image with NO BinaryData / no
//         ImageCache filmstrip; the only inputs are tokens + (PI) shape constants.
//   [2] setTokens() restyles output without touching layout or binding code.
//   [3] Drawing colours/strokes are read solely from the injected DesignTokens.
//   [4] A token swap changes rendered colours/strokes.

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/MwAudioLookAndFeel.h"

using mw::ui::DesignTokens;
using mw::ui::MwAudioLookAndFeel;

namespace {

// Count pixels in an image that match a target juce::Colour (exact ARGB, since the
// LookAndFeel sets opaque colours and we draw with antialiasing off-target areas
// untouched; we tolerate AA by counting a small neighbourhood of the target hue).
int countExactColour(const juce::Image& img, juce::Colour target)
{
    int count = 0;
    const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
    for (int yy = 0; yy < img.getHeight(); ++yy)
        for (int xx = 0; xx < img.getWidth(); ++xx)
        {
            const auto px = data.getPixelColour(xx, yy);
            if (px.getARGB() == target.getARGB())
                ++count;
        }
    return count;
}

// True if ANY pixel in the image is non-transparent (something was drawn).
bool imageHasInk(const juce::Image& img)
{
    const juce::Image::BitmapData data(img, juce::Image::BitmapData::readOnly);
    for (int yy = 0; yy < img.getHeight(); ++yy)
        for (int xx = 0; xx < img.getWidth(); ++xx)
            if (data.getPixelColour(xx, yy).getAlpha() != 0)
                return true;
    return false;
}

// Render the rotary slider override into a fresh transparent image.
juce::Image renderRotary(MwAudioLookAndFeel& laf, juce::Slider& s, int w, int h)
{
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    laf.drawRotarySlider(g, 0, 0, w, h, 0.7f,
                         juce::MathConstants<float>::pi * 1.2f,
                         juce::MathConstants<float>::pi * 2.8f, s);
    return img;
}

juce::Image renderLinear(MwAudioLookAndFeel& laf, juce::Slider& s, int w, int h)
{
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    // Horizontal slider; thumb at ~70% across.
    laf.drawLinearSlider(g, 0, 0, w, h, (float) w * 0.7f, 0.0f, (float) w,
                         juce::Slider::LinearHorizontal, s);
    return img;
}

juce::Image renderToggle(MwAudioLookAndFeel& laf, juce::ToggleButton& b, int w, int h)
{
    b.setBounds(0, 0, w, h);
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    laf.drawToggleButton(g, b, false, false);
    return img;
}

juce::Image renderCombo(MwAudioLookAndFeel& laf, juce::ComboBox& c, int w, int h)
{
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    const int buttonW = h;
    laf.drawComboBox(g, w, h, false, w - buttonW, 0, buttonW, h, c);
    return img;
}

} // namespace

TEST_CASE("ui_laf constructs and exposes the injected tokens", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioLookAndFeel laf(DesignTokens::defaultTheme());
    REQUIRE(juce::String(laf.getTokens().themeTag) == "default");

    laf.setTokens(DesignTokens::highContrast());
    REQUIRE(juce::String(laf.getTokens().themeTag) == "highContrast");
}

TEST_CASE("ui_laf getLabelFont is read solely from the token label-font spec", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioLookAndFeel laf(DesignTokens::defaultTheme());
    juce::Label label;

    const auto def = DesignTokens::defaultTheme();
    REQUIRE(laf.getLabelFont(label).getHeight() == def.labelFont.height);

    // A token table with a distinct font height must flow straight through.
    DesignTokens custom = def;
    custom.labelFont = mw::ui::FontSpec{"sans-serif", def.labelFont.height + 10.0f, 0};
    laf.setTokens(custom);
    REQUIRE(laf.getLabelFont(label).getHeight() == def.labelFont.height + 10.0f);
}

TEST_CASE("ui_laf seeds JUCE colour IDs from the active tokens and reskins on swap", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto def = DesignTokens::defaultTheme();
    const auto hc  = DesignTokens::highContrast();

    MwAudioLookAndFeel laf(def);
    REQUIRE(laf.findColour(juce::Slider::rotarySliderFillColourId)
            == MwAudioLookAndFeel::toColour(def.controlFill));
    REQUIRE(laf.findColour(juce::ToggleButton::tickColourId)
            == MwAudioLookAndFeel::toColour(def.controlFill));
    REQUIRE(laf.findColour(juce::ComboBox::backgroundColourId)
            == MwAudioLookAndFeel::toColour(def.panel));

    // A single setTokens() swap must restyle the colour IDs with no other change.
    laf.setTokens(hc);
    REQUIRE(laf.findColour(juce::Slider::rotarySliderFillColourId)
            == MwAudioLookAndFeel::toColour(hc.controlFill));
    REQUIRE(laf.findColour(juce::ComboBox::backgroundColourId)
            == MwAudioLookAndFeel::toColour(hc.panel));
}

TEST_CASE("ui_laf drawRotarySlider renders the token accent and reskins on swap", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto def = DesignTokens::defaultTheme();
    const auto hc  = DesignTokens::highContrast();
    const auto defFill = MwAudioLookAndFeel::toColour(def.controlFill);
    const auto hcFill  = MwAudioLookAndFeel::toColour(hc.controlFill);

    // Sanity: the two themes' accents differ, so a colour change is observable.
    REQUIRE(defFill.getARGB() != hcFill.getARGB());

    MwAudioLookAndFeel laf(def);
    juce::Slider s;

    const auto imgDefault = renderRotary(laf, s, 120, 120);
    REQUIRE(imageHasInk(imgDefault));
    REQUIRE(countExactColour(imgDefault, defFill) > 0);   // value arc uses controlFill

    laf.setTokens(hc);
    const auto imgHc = renderRotary(laf, s, 120, 120);
    REQUIRE(countExactColour(imgHc, hcFill) > 0);          // now the HC accent appears
    REQUIRE(countExactColour(imgHc, defFill) == 0);        // the old accent is gone
}

TEST_CASE("ui_laf drawLinearSlider renders the token accent and reskins on swap", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto def = DesignTokens::defaultTheme();
    const auto hc  = DesignTokens::highContrast();
    const auto defFill = MwAudioLookAndFeel::toColour(def.controlFill);
    const auto hcThumb = MwAudioLookAndFeel::toColour(hc.controlThumb);
    const auto defThumb = MwAudioLookAndFeel::toColour(def.controlThumb);

    MwAudioLookAndFeel laf(def);
    juce::Slider s;

    const auto imgDefault = renderLinear(laf, s, 160, 40);
    REQUIRE(imageHasInk(imgDefault));
    REQUIRE(countExactColour(imgDefault, defFill) > 0);    // filled track uses controlFill
    REQUIRE(countExactColour(imgDefault, defThumb) > 0);   // thumb uses controlThumb

    laf.setTokens(hc);
    const auto imgHc = renderLinear(laf, s, 160, 40);
    REQUIRE(countExactColour(imgHc, hcThumb) > 0);         // HC thumb now present
    REQUIRE(countExactColour(imgHc, defThumb) == 0);       // default thumb gone
}

TEST_CASE("ui_laf drawToggleButton renders the token fill only when on", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto def = DesignTokens::defaultTheme();
    const auto defFill = MwAudioLookAndFeel::toColour(def.controlFill);
    const auto defTrack = MwAudioLookAndFeel::toColour(def.controlTrack);

    MwAudioLookAndFeel laf(def);
    juce::ToggleButton button("ENV");

    // Off: outline present (controlTrack), no accent fill in the tick.
    button.setToggleState(false, juce::dontSendNotification);
    const auto imgOff = renderToggle(laf, button, 120, 40);
    REQUIRE(imageHasInk(imgOff));
    REQUIRE(countExactColour(imgOff, defTrack) > 0);
    REQUIRE(countExactColour(imgOff, defFill) == 0);

    // On: accent fill now present.
    button.setToggleState(true, juce::dontSendNotification);
    const auto imgOn = renderToggle(laf, button, 120, 40);
    REQUIRE(countExactColour(imgOn, defFill) > 0);

    // Reskin: the on-state fill follows the swapped token table.
    const auto hc = DesignTokens::highContrast();
    const auto hcFill = MwAudioLookAndFeel::toColour(hc.controlFill);
    laf.setTokens(hc);
    const auto imgOnHc = renderToggle(laf, button, 120, 40);
    REQUIRE(countExactColour(imgOnHc, hcFill) > 0);
    REQUIRE(countExactColour(imgOnHc, defFill) == 0);
}

TEST_CASE("ui_laf drawComboBox renders the token body/outline and reskins on swap", "[ui_laf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto def = DesignTokens::defaultTheme();
    const auto hc  = DesignTokens::highContrast();
    const auto defPanel = MwAudioLookAndFeel::toColour(def.panel);
    const auto hcPanel  = MwAudioLookAndFeel::toColour(hc.panel);

    MwAudioLookAndFeel laf(def);
    juce::ComboBox combo;

    const auto imgDefault = renderCombo(laf, combo, 160, 40);
    REQUIRE(imageHasInk(imgDefault));
    REQUIRE(countExactColour(imgDefault, defPanel) > 0);   // body fill uses panel token

    laf.setTokens(hc);
    const auto imgHc = renderCombo(laf, combo, 160, 40);
    REQUIRE(countExactColour(imgHc, hcPanel) > 0);
    REQUIRE(countExactColour(imgHc, defPanel) == 0);
}
