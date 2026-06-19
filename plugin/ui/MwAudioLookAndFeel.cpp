// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/MwAudioLookAndFeel.cpp — implementation of the custom LookAndFeel_V4
// declared in ui/MwAudioLookAndFeel.h. Every control is drawn from juce::Path /
// juce::Graphics primitives only; all colours and stroke weights come from the
// injected DesignTokens; pure shape proportions come from the (PI) constants in
// core/calibration/LookAndFeelConstants.h [docs/design/10-ui.md §6.2; ADR-015 C11].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS; see plugin/CMakeLists.txt and tests/CMakeLists.txt). The
// design-faithful header stays at ui/MwAudioLookAndFeel.h and is reached by relative
// include — no shared CMakeLists edit, no new top-level source root.

#include "../../ui/MwAudioLookAndFeel.h"

#include "../../core/calibration/LookAndFeelConstants.h"

#include <cmath>

namespace mw::ui {

namespace {
// Map the JUCE-free FontSpec style flag (0 plain / 1 bold / 2 italic; mirrors
// juce::Font::FontStyleFlags) to a JUCE FontOptions style string. Using FontOptions
// avoids the deprecated juce::Font(name, height, styleFlags) constructors (JUCE 8).
juce::String styleFlagToName(int styleFlag) noexcept
{
    switch (styleFlag)
    {
        case 1:  return "Bold";
        case 2:  return "Italic";
        default: return "Regular";
    }
}
} // namespace

juce::Font MwAudioLookAndFeel::toFont(const FontSpec& f)
{
    return juce::Font(juce::FontOptions{}
                          .withName(juce::String(f.family))
                          .withStyle(styleFlagToName(f.style))
                          .withHeight(f.height));
}

MwAudioLookAndFeel::MwAudioLookAndFeel(DesignTokens initialTokens)
    : tokens_(initialTokens)
{
    applyTokensToColourIds();
}

void MwAudioLookAndFeel::setTokens(DesignTokens newTokens)
{
    tokens_ = newTokens;
    applyTokensToColourIds();
}

void MwAudioLookAndFeel::applyTokensToColourIds()
{
    // Seed the LookAndFeel_V4 colour scheme + the per-component colour IDs from the
    // active tokens. Our overrides draw explicitly from tokens_, but seeding the IDs
    // keeps any non-overridden JUCE default path on-palette so a token swap restyles
    // EVERY control (the single-reskin contract) [§6.1; ADR-015 C10].
    const auto bg     = toColour(tokens_.background);
    const auto panel  = toColour(tokens_.panel);
    const auto track  = toColour(tokens_.controlTrack);
    const auto fill   = toColour(tokens_.controlFill);
    const auto thumb  = toColour(tokens_.controlThumb);
    const auto text   = toColour(tokens_.textPrimary);
    const auto text2  = toColour(tokens_.textSecondary);
    const auto outline = toColour(tokens_.moduleOutline);

    auto scheme = getCurrentColourScheme();
    using CS = juce::LookAndFeel_V4::ColourScheme;
    scheme.setUIColour(CS::windowBackground, bg);
    scheme.setUIColour(CS::widgetBackground, panel);
    scheme.setUIColour(CS::menuBackground,   panel);
    scheme.setUIColour(CS::outline,          outline);
    scheme.setUIColour(CS::defaultText,      text);
    scheme.setUIColour(CS::defaultFill,      fill);
    scheme.setUIColour(CS::highlightedText,  text);
    scheme.setUIColour(CS::highlightedFill,  fill);
    scheme.setUIColour(CS::menuText,         text);
    setColourScheme(scheme);

    setColour(juce::Slider::rotarySliderFillColourId,    fill);
    setColour(juce::Slider::rotarySliderOutlineColourId, track);
    setColour(juce::Slider::thumbColourId,               thumb);
    setColour(juce::Slider::trackColourId,               fill);
    setColour(juce::Slider::backgroundColourId,          track);
    setColour(juce::Slider::textBoxTextColourId,         text);

    setColour(juce::ToggleButton::textColourId,          text);
    setColour(juce::ToggleButton::tickColourId,          fill);
    setColour(juce::ToggleButton::tickDisabledColourId,  track);

    setColour(juce::ComboBox::backgroundColourId,        panel);
    setColour(juce::ComboBox::textColourId,              text);
    setColour(juce::ComboBox::outlineColourId,           outline);
    setColour(juce::ComboBox::arrowColourId,             fill);

    setColour(juce::Label::textColourId,                 text);
    setColour(juce::Label::textWhenEditingColourId,      text);

    // textSecondary feeds the slider value read-out so a token swap also restyles
    // the secondary text the design assigns to it [§6.1].
    setColour(juce::Slider::textBoxOutlineColourId,      text2);
}

// ---------------------------------------------------------------------------
// Rotary slider: token-coloured track arc + value arc + pointer line. All vector.
// ---------------------------------------------------------------------------
void MwAudioLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                          int height, float sliderPosProportional,
                                          float rotaryStartAngle, float rotaryEndAngle,
                                          juce::Slider&)
{
    namespace pi = mw::cal::laf::rotary;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f
                             * pi::kArcRadiusFraction;

    const float toAngle = rotaryStartAngle
                              + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const float trackStroke = tokens_.controlStroke;
    const float valueStroke = tokens_.controlStroke;

    // Background track arc (full sweep), token controlTrack colour.
    {
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(toColour(tokens_.controlTrack));
        g.strokePath(track, juce::PathStrokeType(trackStroke,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Value arc (start -> current), token controlFill accent colour.
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                           rotaryStartAngle, toAngle, true);
        g.setColour(toColour(tokens_.controlFill));
        g.strokePath(value, juce::PathStrokeType(valueStroke,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Pointer line from centre toward the current angle, token controlThumb colour.
    {
        const float pointerLength = radius * pi::kPointerLengthFraction;
        const float pointerThickness = tokens_.controlStroke * pi::kPointerThicknessFactor;
        juce::Path pointer;
        pointer.startNewSubPath(centre.x, centre.y);
        pointer.lineTo(centre.x + pointerLength * std::sin(toAngle),
                       centre.y - pointerLength * std::cos(toAngle));
        g.setColour(toColour(tokens_.controlThumb));
        g.strokePath(pointer, juce::PathStrokeType(pointerThickness,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
}

// ---------------------------------------------------------------------------
// Linear slider: token-coloured track line + round thumb. All vector.
// ---------------------------------------------------------------------------
void MwAudioLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width,
                                          int height, float sliderPos,
                                          float /*minSliderPos*/, float /*maxSliderPos*/,
                                          juce::Slider::SliderStyle style, juce::Slider&)
{
    namespace pi = mw::cal::laf::linear;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const bool isVertical = (style == juce::Slider::LinearVertical
                             || style == juce::Slider::LinearBarVertical);

    const float trackStroke = tokens_.controlStroke * pi::kTrackThicknessFactor;

    juce::Point<float> startPoint, endPoint, thumbPoint;
    float crossHalfExtent = 0.0f;

    if (isVertical)
    {
        const float cx = bounds.getCentreX();
        startPoint = { cx, bounds.getBottom() };
        endPoint   = { cx, bounds.getY() };
        thumbPoint = { cx, sliderPos };
        crossHalfExtent = bounds.getWidth() * 0.5f;
    }
    else
    {
        const float cy = bounds.getCentreY();
        startPoint = { bounds.getX(), cy };
        endPoint   = { bounds.getRight(), cy };
        thumbPoint = { sliderPos, cy };
        crossHalfExtent = bounds.getHeight() * 0.5f;
    }

    // Full track (controlTrack) then filled portion start->thumb (controlFill).
    {
        juce::Path track;
        track.startNewSubPath(startPoint);
        track.lineTo(endPoint);
        g.setColour(toColour(tokens_.controlTrack));
        g.strokePath(track, juce::PathStrokeType(trackStroke,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        juce::Path filled;
        filled.startNewSubPath(startPoint);
        filled.lineTo(thumbPoint);
        g.setColour(toColour(tokens_.controlFill));
        g.strokePath(filled, juce::PathStrokeType(trackStroke,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // Round thumb (controlThumb).
    {
        const float thumbRadius = crossHalfExtent * pi::kThumbRadiusFraction;
        g.setColour(toColour(tokens_.controlThumb));
        g.fillEllipse(thumbPoint.x - thumbRadius, thumbPoint.y - thumbRadius,
                      thumbRadius * 2.0f, thumbRadius * 2.0f);
    }
}

// ---------------------------------------------------------------------------
// Toggle button: token-coloured rounded tick box, filled when on, plus label text.
// All vector.
// ---------------------------------------------------------------------------
void MwAudioLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                          bool /*shouldDrawButtonAsHighlighted*/,
                                          bool /*shouldDrawButtonAsDown*/)
{
    namespace pi = mw::cal::laf::toggle;

    const auto bounds = button.getLocalBounds().toFloat();
    const float boxSize = bounds.getHeight() * pi::kBoxSizeFraction;
    const float boxY = bounds.getY() + (bounds.getHeight() - boxSize) * 0.5f;
    const juce::Rectangle<float> box(bounds.getX(), boxY, boxSize, boxSize);

    const float corner = tokens_.cornerRadius;
    const float outlineStroke = tokens_.controlStroke;

    // Box outline (controlTrack) always; fill (controlFill) when toggled on.
    g.setColour(toColour(tokens_.controlTrack));
    g.drawRoundedRectangle(box, corner, outlineStroke);

    if (button.getToggleState())
    {
        const float inset = boxSize * pi::kTickInsetFraction;
        const auto tick = box.reduced(inset);
        g.setColour(toColour(tokens_.controlFill));
        g.fillRoundedRectangle(tick, juce::jmax(0.0f, corner - inset));
    }

    // Label text (textPrimary) using the token label font.
    const auto textArea = bounds.withTrimmedLeft(boxSize + pi::kTextGap);
    g.setColour(toColour(tokens_.textPrimary));
    g.setFont(toFont(tokens_.labelFont));
    g.drawText(button.getButtonText(), textArea, juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------
// Combo box: token-coloured rounded body + outline + down-arrow glyph. All vector.
// ---------------------------------------------------------------------------
void MwAudioLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
                                      bool /*isButtonDown*/, int buttonX, int buttonY,
                                      int buttonW, int buttonH, juce::ComboBox&)
{
    namespace pi = mw::cal::laf::combo;

    const juce::Rectangle<float> body(0.0f, 0.0f, (float) width, (float) height);
    const float corner = tokens_.cornerRadius;
    const float outlineStroke = tokens_.outlineStroke;

    // Body (panel) + outline (moduleOutline).
    g.setColour(toColour(tokens_.panel));
    g.fillRoundedRectangle(body, corner);

    g.setColour(toColour(tokens_.moduleOutline));
    g.drawRoundedRectangle(body.reduced(outlineStroke * 0.5f), corner, outlineStroke);

    // Down-arrow glyph (controlFill) in the button cell.
    const juce::Rectangle<float> arrowCell((float) buttonX, (float) buttonY,
                                           (float) buttonW, (float) buttonH);
    const float halfWidth = arrowCell.getWidth() * pi::kArrowHalfWidthFraction;
    const float halfHeight = (float) height * pi::kArrowHeightFraction * 0.5f;
    const auto cc = arrowCell.getCentre();

    juce::Path arrow;
    arrow.startNewSubPath(cc.x - halfWidth, cc.y - halfHeight);
    arrow.lineTo(cc.x, cc.y + halfHeight);
    arrow.lineTo(cc.x + halfWidth, cc.y - halfHeight);
    g.setColour(toColour(tokens_.controlFill));
    g.strokePath(arrow, juce::PathStrokeType(tokens_.controlStroke,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------------------
// Label font: read solely from the token label-font spec.
// ---------------------------------------------------------------------------
juce::Font MwAudioLookAndFeel::getLabelFont(juce::Label&)
{
    return toFont(tokens_.labelFont);
}

} // namespace mw::ui
