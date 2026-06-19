// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/controls/ToggleSwitch.h — a thin juce::Button (ToggleButton) subclass for the
// boolean switches of the signal-flow editor [docs/design/10-ui.md §6.3].
//
// Responsibilities owned here (thin seam, §6.3):
//   • a two-state toggle whose on/off label text is its own, carried on the control
//     rather than re-derived elsewhere; and
//   • a state change invalidates ONLY this control's own bounds, never the whole
//     editor [§7.3; ADR-015 C7].
//
// OUT OF SCOPE: the APVTS ButtonAttachment (modules) and the vector drawing
// (LookAndFeel, task 108). The .cpp lives under plugin/ui/controls/.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mw::ui {

class ToggleSwitch final : public juce::ToggleButton
{
public:
    ToggleSwitch();
    explicit ToggleSwitch(const juce::String& buttonText);
    ~ToggleSwitch() override = default;

    // The label this switch shows for the on / off captions. Carried here so the
    // control formats its own text (§6.3). Default is the button text for both.
    void setStateLabels(juce::String offLabel, juce::String onLabel);

    // The caption for the current state (on/off), derived from the labels above.
    juce::String currentStateLabel() const;

    // --- per-control dirty-rect discipline (§7.3; ADR-015 C7) ---------------------
    const juce::Rectangle<int>& lastInvalidatedRegion() const noexcept { return lastDirty_; }
    bool hasInvalidated() const noexcept { return invalidated_; }

protected:
    // ToggleButton flips its state via clicked(); we invalidate only our own bounds
    // on any toggle-state change [§7.3].
    void buttonStateChanged() override;
    void clicked() override;

private:
    void invalidateOwnBounds();

    juce::String offLabel_;
    juce::String onLabel_;
    bool lastToggleState_ = false;

    juce::Rectangle<int> lastDirty_{};
    bool invalidated_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleSwitch)
};

} // namespace mw::ui
