// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/SourceMixerModule.h — the SOURCE MIXER panel module (saw / pulse / sub /
// noise levels) [docs/design/10-ui.md §5.3 table row "SourceMixerModule"].
//
// Owns and binds the source-mixer level controls — the mix balance between the four
// post-VCO sources. Following the SH-101 mixer idiom (a column/row of level faders) and
// §6.3 ("the slider-per-parameter SH-101 ergonomics are retained"), each level is a
// LinearSlider (vertical fader) bound via an APVTS SliderAttachment:
//   • Saw level   — LinearSlider bound to ids::kSawLevel.
//   • Pulse level — LinearSlider bound to ids::kPulseLevel.
//   • Sub level   — LinearSlider bound to ids::kSubLevel.
//   • Noise level — LinearSlider bound to ids::kNoiseLevel.
//
// All four are continuous 0..1 level parameters [core/params/ParamDefs.h, ParamGroup::
// Mixer]; none carries a software-extension choice, so this module has no sound_ext
// fence (unlike ModulatorModule's Sine LFO entry) [ADR-008 §7].
//
// Every control binds through a JUCE APVTS attachment using a schema-owned ParamId
// constant from core/params/ParamIDs.h — the module NEVER hard-codes a raw "mw101.*"
// string literal and NEVER calls processor DSP directly [docs/design/10-ui.md §8.1;
// ADR-015 C3, C4; ADR-008 C1]. Attachments are created in the constructor (message
// thread) and torn down in destruction; they are never created from a render/Timer hot
// path (RT invariant §3.6).
//
// layoutDesignUnits() positions the children in DESIGN units only (no pixel math); the
// proportions are the (PI) constants in core/calibration/SourceMixerModuleConstants.h
// [docs/design/10-ui.md §5.3].
//
// This header is JUCE-built and lives at the design-faithful path ui/modules/; its .cpp
// lives under plugin/ui/modules/ so the plugin glob compiles it (mirrors
// plugin/ui/modules/ModulatorModule.cpp).

#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ModuleBase.h"
#include "../controls/LinearSlider.h"

namespace mw::ui {

class SourceMixerModule final : public ModuleBase
{
public:
    explicit SourceMixerModule(juce::AudioProcessorValueTreeState& state);
    ~SourceMixerModule() override;

    // Lay out the four level faders in a single design-unit row beneath the title strip.
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Control access (for parents and tests) -----------------------------------
    LinearSlider& sawLevelSlider()   noexcept { return sawLevel_; }
    LinearSlider& pulseLevelSlider() noexcept { return pulseLevel_; }
    LinearSlider& subLevelSlider()   noexcept { return subLevel_; }
    LinearSlider& noiseLevelSlider() noexcept { return noiseLevel_; }

private:
    // The level faders (owned, in design order: saw, pulse, sub, noise).
    LinearSlider sawLevel_;
    LinearSlider pulseLevel_;
    LinearSlider subLevel_;
    LinearSlider noiseLevel_;

    // Caption labels (one per fader); positioned in the bottom of each cell.
    juce::Label  sawLevelLabel_;
    juce::Label  pulseLevelLabel_;
    juce::Label  subLevelLabel_;
    juce::Label  noiseLevelLabel_;

    // The APVTS attachments — the SOLE write path for every control [§8.1; ADR-015 C3].
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> sawLevelAttach_;
    std::unique_ptr<SliderAttachment> pulseLevelAttach_;
    std::unique_ptr<SliderAttachment> subLevelAttach_;
    std::unique_ptr<SliderAttachment> noiseLevelAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceMixerModule)
};

} // namespace mw::ui
