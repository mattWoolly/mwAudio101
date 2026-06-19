// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/controls/LinearSlider.h — a thin juce::Slider subclass for the linear (fader)
// controls of the signal-flow editor [docs/design/10-ui.md §6.3]. It is the linear
// sibling of RotarySlider and carries the same two contracts:
//   • value read-out TEXT derived from the bound parameter's display string, never
//     hard-coded [§6.3; ADR-008 C4]; and
//   • a value change invalidates ONLY this control's own bounds [§7.3; ADR-015 C7].
//
// Style defaults to a vertical fader (the SH-101 idiom is a column of faders); the
// orientation can be re-set by the owning module without touching this seam.
//
// OUT OF SCOPE: APVTS attachment wiring (modules) and the vector drawing (LookAndFeel,
// task 108). The .cpp lives under plugin/ui/controls/ so the plugin glob compiles it.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mw::ui {

class LinearSlider final : public juce::Slider
{
public:
    LinearSlider();
    ~LinearSlider() override = default;

    // Narrow display seam (see RotarySlider): remember the bound parameter so the
    // value read-out text is derived from its display string [§6.3; ADR-008 C4].
    void attachParameterForDisplay(juce::RangedAudioParameter& param) noexcept;

    juce::String getTextFromValue(double value) override;

    // --- per-control dirty-rect discipline (§7.3; ADR-015 C7) ---------------------
    const juce::Rectangle<int>& lastInvalidatedRegion() const noexcept { return lastDirty_; }
    bool hasInvalidated() const noexcept { return invalidated_; }

protected:
    void valueChanged() override;

private:
    void invalidateOwnBounds();

    juce::RangedAudioParameter* displayParam_ = nullptr;  // not owned; for value text
    juce::Rectangle<int> lastDirty_{};
    bool invalidated_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinearSlider)
};

} // namespace mw::ui
