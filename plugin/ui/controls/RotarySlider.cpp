// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/controls/RotarySlider.cpp — implementation of the thin rotary-slider
// subclass declared in ui/controls/RotarySlider.h [docs/design/10-ui.md §6.3, §7.3].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS; see plugin/CMakeLists.txt and tests/CMakeLists.txt). The
// design-faithful header stays at ui/controls/RotarySlider.h and is reached by a
// relative include — no shared CMakeLists edit, no new top-level source root.

#include "../../../ui/controls/RotarySlider.h"

#include "../../../core/calibration/ControlSubclassConstants.h"

namespace mw::ui {

RotarySlider::RotarySlider()
    : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag,
                   juce::Slider::NoTextBox)
{
    // Predictable vertical/horizontal drag span (PI), not the default circular drag,
    // so the slider-per-parameter ergonomics stay legible [§6.3].
    setMouseDragSensitivity(mw::cal::control::rotary::kDragPixelSpan);
}

void RotarySlider::attachParameterForDisplay(juce::RangedAudioParameter& param) noexcept
{
    displayParam_ = &param;
}

juce::String RotarySlider::getTextFromValue(double value)
{
    // Derive the read-out text from the bound parameter's display string (the
    // schema-owned NormalisableRange formatting), NOT a hard-coded format [§6.3;
    // ADR-008 C4]. The parameter formats a NORMALISED 0..1 value, so map the slider's
    // modeled value through the parameter's own range first.
    if (displayParam_ != nullptr)
    {
        const float normalised = displayParam_->convertTo0to1(static_cast<float>(value));
        return displayParam_->getText(normalised, 0);
    }

    // Fallback (no parameter attached yet): a plain numeric read-out.
    return juce::String(value, mw::cal::control::readout::kFallbackDecimalPlaces);
}

void RotarySlider::valueChanged()
{
    juce::Slider::valueChanged();
    invalidateOwnBounds();   // per-control dirty-rect: never the whole editor [§7.3]
}

void RotarySlider::invalidateOwnBounds()
{
    lastDirty_   = getLocalBounds();
    invalidated_ = true;
    repaint(lastDirty_);     // invalidate ONLY our own bounds [ADR-015 C7]
}

} // namespace mw::ui
