// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/ModulatorModule.h — the MODULATOR panel module (LFO / S&H + mod depth)
// [docs/design/10-ui.md §5.3 table row "ModulatorModule"].
//
// Owns and binds the modulation-section controls:
//   • LFO Rate     — a RotarySlider bound via APVTS SliderAttachment to ids::kLfoRate.
//   • LFO Shape    — a ChoiceSelector bound via APVTS ComboBoxAttachment to
//                    ids::kLfoShape. The shape list is the schema's {Tri, Sq, Random,
//                    Noise, Sine}: Random/Noise ARE the sample-&-hold ("S&H") shapes,
//                    and the trailing Sine entry is a SOFTWARE-ONLY extension that the
//                    ChoiceSelector visually fences (extensionTag tint + "[ext]" suffix)
//                    so it can never read as documented 1982 hardware behaviour
//                    [ADR-008 §7, C6, C15; research/12 §3.1].
//   • Mod-depth    — three RotarySliders for the LFO routing depths, bound to
//                    ids::kLfoDepthPitch / kLfoDepthPwm / kLfoDepthCutoff.
//
// Every control binds through a JUCE APVTS attachment using a schema-owned ParamId
// constant from core/params/ParamIDs.h — the module NEVER hard-codes a raw "mw101.*"
// string literal and NEVER calls processor DSP directly [docs/design/10-ui.md §8.1;
// ADR-015 C3, C4; ADR-008 C1]. Attachments are created in the constructor (message
// thread) and torn down in destruction; they are never created from a render/Timer
// hot path (RT invariant §3.6).
//
// layoutDesignUnits() positions the children in DESIGN units only (no pixel math); the
// proportions are the (PI) constants in core/calibration/ModulatorModuleConstants.h
// [docs/design/10-ui.md §5.3].
//
// This header is JUCE-built and lives at the design-faithful path ui/modules/; its .cpp
// lives under plugin/ui/modules/ so the plugin glob compiles it (mirrors
// plugin/ui/controls/*.cpp).

#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ModuleBase.h"
#include "../DesignTokens.h"
#include "../controls/RotarySlider.h"
#include "../controls/ChoiceSelector.h"

namespace mw::ui {

class ModulatorModule final : public ModuleBase
{
public:
    explicit ModulatorModule(juce::AudioProcessorValueTreeState& state);
    ~ModulatorModule() override;

    // Inject the design tokens so the ChoiceSelector's software-extension fence is
    // tinted with the extensionTag colour (a reskin re-tints with no code change)
    // [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // Lay out the five controls in a single design-unit row beneath the title strip.
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Control access (for parents and tests) -----------------------------------
    RotarySlider&   lfoRateSlider()    noexcept { return lfoRate_; }
    ChoiceSelector& lfoShapeSelector() noexcept { return lfoShape_; }
    RotarySlider&   pitchDepthSlider() noexcept { return depthPitch_; }
    RotarySlider&   pwmDepthSlider()   noexcept { return depthPwm_; }
    RotarySlider&   cutoffDepthSlider()noexcept { return depthCutoff_; }

private:
    // The controls (owned, in design order: rate, shape, then the three depths).
    RotarySlider   lfoRate_;
    ChoiceSelector lfoShape_;
    RotarySlider   depthPitch_;
    RotarySlider   depthPwm_;
    RotarySlider   depthCutoff_;

    // Caption labels (one per control); positioned in the bottom of each cell.
    juce::Label    lfoRateLabel_;
    juce::Label    lfoShapeLabel_;
    juce::Label    depthPitchLabel_;
    juce::Label    depthPwmLabel_;
    juce::Label    depthCutoffLabel_;

    // The APVTS attachments — the SOLE write path for every control [§8.1; ADR-015 C3].
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment>   lfoRateAttach_;
    std::unique_ptr<ComboBoxAttachment> lfoShapeAttach_;
    std::unique_ptr<SliderAttachment>   depthPitchAttach_;
    std::unique_ptr<SliderAttachment>   depthPwmAttach_;
    std::unique_ptr<SliderAttachment>   depthCutoffAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorModule)
};

} // namespace mw::ui
