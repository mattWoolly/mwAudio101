// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/ModuleBase.h — the shared base every signal-flow panel module derives
// from [docs/design/10-ui.md §5.3].
//
// The panel mirrors the documented signal chain so the UI teaches the architecture
// [ADR-015 Decision; research/12 §7.3]. Each module is a juce::Component that owns its
// controls and their APVTS attachments. ModuleBase contributes exactly two shared
// seams, taken verbatim from §5.3:
//
//   • a reference to the single juce::AudioProcessorValueTreeState (the ONLY write/read
//     surface for parameters — the editor holds zero audio-domain state and never calls
//     processor DSP) [docs/design/10-ui.md §8.1; ADR-015 C3, C4]; and
//   • a pure-virtual layoutDesignUnits(designBounds) hook the parent invokes from its
//     resized(), passing a DESIGN-space rectangle. Concrete modules sub-divide that
//     rectangle in design units only — never pixel math [docs/design/10-ui.md §5.3].
//
// Header-only base; no drawing, no attachments, no layout math live here (those are
// the concrete module's job). This header is JUCE-built and lives at the design-faithful
// path ui/modules/; concrete modules' .cpp files live under plugin/ui/modules/ so the
// plugin glob compiles them.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mw::ui {

class ModuleBase : public juce::Component
{
public:
    ModuleBase(juce::AudioProcessorValueTreeState& state, juce::StringRef title)
        : apvts(state), moduleTitle(title) {}

    ~ModuleBase() override = default;

    // Lay out children in design units; the parent passes a design-space rectangle and
    // the concrete module sub-divides it with proportional (design-unit) math only —
    // no pixel literals [docs/design/10-ui.md §5.3].
    virtual void layoutDesignUnits(juce::Rectangle<float> designBounds) = 0;

    // Read-only accessors so a parent / a test can introspect the shared seam without
    // reaching into the protected members.
    [[nodiscard]] juce::AudioProcessorValueTreeState& valueTreeState() const noexcept { return apvts; }
    [[nodiscard]] const juce::String& title() const noexcept { return moduleTitle; }

protected:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String moduleTitle;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleBase)
};

} // namespace mw::ui
