// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/controls/ToggleSwitch.cpp — implementation of the thin toggle-switch
// subclass declared in ui/controls/ToggleSwitch.h [docs/design/10-ui.md §6.3, §7.3].
// Lives under plugin/ for the auto-glob (see RotarySlider.cpp for the wiring note).

#include "../../../ui/controls/ToggleSwitch.h"

namespace mw::ui {

ToggleSwitch::ToggleSwitch() : ToggleSwitch(juce::String{}) {}

ToggleSwitch::ToggleSwitch(const juce::String& buttonText)
    : juce::ToggleButton(buttonText),
      offLabel_(buttonText),
      onLabel_(buttonText)
{
    lastToggleState_ = getToggleState();
}

void ToggleSwitch::setStateLabels(juce::String offLabel, juce::String onLabel)
{
    offLabel_ = std::move(offLabel);
    onLabel_  = std::move(onLabel);
}

juce::String ToggleSwitch::currentStateLabel() const
{
    return getToggleState() ? onLabel_ : offLabel_;
}

void ToggleSwitch::buttonStateChanged()
{
    juce::ToggleButton::buttonStateChanged();

    // Only repaint our own bounds when the LOGICAL toggle state actually flips
    // (buttonStateChanged also fires for hover/press transitions) [§7.3].
    if (getToggleState() != lastToggleState_)
    {
        lastToggleState_ = getToggleState();
        invalidateOwnBounds();
    }
}

void ToggleSwitch::clicked()
{
    juce::ToggleButton::clicked();
    invalidateOwnBounds();   // per-control dirty-rect: never the whole editor [§7.3]
}

void ToggleSwitch::invalidateOwnBounds()
{
    lastDirty_   = getLocalBounds();
    invalidated_ = true;
    repaint(lastDirty_);     // invalidate ONLY our own bounds [ADR-015 C7]
}

} // namespace mw::ui
