// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/controls/RotarySlider.h — a thin juce::Slider subclass for the rotary controls
// of the signal-flow editor [docs/design/10-ui.md §6.3].
//
// Responsibilities owned here (and ONLY these — §6.3 is a deliberately thin seam):
//   • Rotary style with a predictable vertical/horizontal drag (not a circular drag)
//     so the slider-per-parameter SH-101 ergonomics are retained while the styling
//     (drawn by the LookAndFeel, task 108) carries the distinctiveness
//     [research/12 §7.2; ADR-015 Decision].
//   • Its value read-out TEXT is derived from the bound parameter's display string
//     (the schema-owned NormalisableRange formatting), never hard-coded here
//     [docs/design/10-ui.md §6.3; ADR-008 C4]. Attaching a parameter is a narrow
//     seam (attachParameterForDisplay) so this control never reaches into the APVTS
//     layout itself — the actual SliderAttachment wiring is done in the modules
//     (task scope: OUT of this task; §6.3 "APVTS attachment wiring done in modules").
//   • A value change invalidates ONLY this control's own bounds, never the whole
//     editor [docs/design/10-ui.md §7.3; ADR-015 C7].
//
// OUT OF SCOPE here: the APVTS SliderAttachment creation (modules), and the actual
// vector drawing (the LookAndFeel, task 108). This header is JUCE-built and lives at
// the design-faithful path ui/controls/; its .cpp lives under plugin/ui/controls/ so
// the plugin glob compiles it (see plugin/ui/controls/RotarySlider.cpp).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mw::ui {

class RotarySlider final : public juce::Slider
{
public:
    RotarySlider();
    ~RotarySlider() override = default;

    // Narrow display seam: remember the bound parameter so the value read-out text is
    // derived from its display string (NormalisableRange formatting), NOT hard-coded
    // [§6.3; ADR-008 C4]. This does NOT create an attachment (that is the module's
    // job); it only gives the control the parameter to format against.
    void attachParameterForDisplay(juce::RangedAudioParameter& param) noexcept;

    // The value read-out text, derived from the bound parameter's display string when
    // one is attached; a plain numeric fallback otherwise [§6.3].
    juce::String getTextFromValue(double value) override;

    // --- per-control dirty-rect discipline (§7.3; ADR-015 C7) ---------------------
    // Routed self-invalidation: every repaint this control asks for covers only its
    // own local bounds. The last such rectangle is recorded for tests so the
    // dirty-rect scope is an objectively-testable property.
    const juce::Rectangle<int>& lastInvalidatedRegion() const noexcept { return lastDirty_; }
    bool hasInvalidated() const noexcept { return invalidated_; }

protected:
    // juce::Slider fires valueChanged() on every value move (drag, automation,
    // attachment recall). We invalidate only our own bounds here [§7.3].
    void valueChanged() override;

private:
    void invalidateOwnBounds();

    juce::RangedAudioParameter* displayParam_ = nullptr;  // not owned; for value text
    juce::Rectangle<int> lastDirty_{};
    bool invalidated_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotarySlider)
};

} // namespace mw::ui
