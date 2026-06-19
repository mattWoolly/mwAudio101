// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/controls/LinearSlider.cpp — implementation of the thin linear-slider
// subclass declared in ui/controls/LinearSlider.h [docs/design/10-ui.md §6.3, §7.3].
// Lives under plugin/ for the auto-glob (see RotarySlider.cpp for the wiring note).

#include "../../../ui/controls/LinearSlider.h"

#include "../../../core/calibration/ControlSubclassConstants.h"

namespace mw::ui {

LinearSlider::LinearSlider()
    : juce::Slider(juce::Slider::LinearVertical, juce::Slider::NoTextBox)
{
    // The SH-101 idiom is a column of faders; orientation is re-settable by the owning
    // module without touching this seam [§6.3].
}

void LinearSlider::attachParameterForDisplay(juce::RangedAudioParameter& param) noexcept
{
    displayParam_ = &param;
}

juce::String LinearSlider::getTextFromValue(double value)
{
    // Read-out text derived from the bound parameter's display string [§6.3; C4].
    if (displayParam_ != nullptr)
    {
        const float normalised = displayParam_->convertTo0to1(static_cast<float>(value));
        return displayParam_->getText(normalised, 0);
    }
    return juce::String(value, mw::cal::control::readout::kFallbackDecimalPlaces);
}

void LinearSlider::valueChanged()
{
    juce::Slider::valueChanged();
    invalidateOwnBounds();   // per-control dirty-rect: never the whole editor [§7.3]
}

void LinearSlider::invalidateOwnBounds()
{
    lastDirty_   = getLocalBounds();
    invalidated_ = true;
    repaint(lastDirty_);     // invalidate ONLY our own bounds [ADR-015 C7]
}

} // namespace mw::ui
