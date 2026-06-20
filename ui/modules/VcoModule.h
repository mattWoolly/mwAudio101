// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/VcoModule.h — the VCO panel module (VCO + sub + noise source)
// [docs/design/10-ui.md §5.3 table row "VcoModule"].
//
// Owns and binds the oscillator-section controls, in design order:
//   • VCO Range — a ChoiceSelector bound via APVTS ComboBoxAttachment to ids::kVcoRange.
//                 The range list is the schema's {16', 8', 4', 2', 32', 64'}: the
//                 trailing 32' / 64' registers are SOFTWARE-ONLY extensions that the
//                 ChoiceSelector visually fences (extensionTag tint + "[ext]" suffix)
//                 so they can never read as documented 1982 hardware behaviour
//                 [ADR-008 §7, C6, C15; research/12 §3.1].
//   • Tune      — a RotarySlider bound to ids::kVcoTune (coarse pitch, semitones).
//   • Fine      — a RotarySlider bound to ids::kVcoFine (fine pitch, semitones).
//   • PW        — a RotarySlider bound to ids::kVcoPw (pulse-width duty).
//   • PWM Depth — a RotarySlider bound to ids::kVcoPwmDepth.
//   • Sub Mode  — a ChoiceSelector bound via APVTS ComboBoxAttachment to ids::kSubMode
//                 (all three entries are hardware-canonical — no extension fence).
//   • Noise     — a RotarySlider bound to ids::kNoiseLevel.
//
// Source-mixer LEVELS (saw/pulse/sub) are SourceMixerModule's responsibility and are
// OUT OF SCOPE here [120 Out-of-scope; §5.3].
//
// Every control binds through a JUCE APVTS attachment using a schema-owned ParamId
// constant from core/params/ParamIDs.h — the module NEVER hard-codes a raw "mw101.*"
// string literal and NEVER calls processor DSP directly [docs/design/10-ui.md §8.1;
// ADR-015 C3, C4; ADR-008 C1]. Attachments are created in the constructor (message
// thread) and torn down in destruction; they are never created from a render/Timer
// hot path (RT invariant §3.6).
//
// layoutDesignUnits() positions the children in DESIGN units only (no pixel math); the
// proportions are the (PI) constants in core/calibration/VcoModuleConstants.h
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
#include "../DesignTokens.h"
#include "../controls/RotarySlider.h"
#include "../controls/ChoiceSelector.h"

namespace mw::ui {

class VcoModule final : public ModuleBase
{
public:
    explicit VcoModule(juce::AudioProcessorValueTreeState& state);
    ~VcoModule() override;

    // Inject the design tokens so the ChoiceSelectors' software-extension fences are
    // tinted with the extensionTag colour (a reskin re-tints with no code change)
    // [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // Lay out the seven controls in a single design-unit row beneath the title strip.
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Control access (for parents and tests) -----------------------------------
    ChoiceSelector& rangeSelector()    noexcept { return range_; }
    RotarySlider&   tuneSlider()       noexcept { return tune_; }
    RotarySlider&   fineSlider()       noexcept { return fine_; }
    RotarySlider&   pulseWidthSlider() noexcept { return pw_; }
    RotarySlider&   pwmDepthSlider()   noexcept { return pwmDepth_; }
    ChoiceSelector& subModeSelector()  noexcept { return subMode_; }
    RotarySlider&   noiseSlider()      noexcept { return noise_; }

private:
    // The controls (owned, in design order).
    ChoiceSelector range_;
    RotarySlider   tune_;
    RotarySlider   fine_;
    RotarySlider   pw_;
    RotarySlider   pwmDepth_;
    ChoiceSelector subMode_;
    RotarySlider   noise_;

    // Caption labels (one per control); positioned in the bottom of each cell.
    juce::Label    rangeLabel_;
    juce::Label    tuneLabel_;
    juce::Label    fineLabel_;
    juce::Label    pwLabel_;
    juce::Label    pwmDepthLabel_;
    juce::Label    subModeLabel_;
    juce::Label    noiseLabel_;

    // The APVTS attachments — the SOLE write path for every control [§8.1; ADR-015 C3].
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboBoxAttachment> rangeAttach_;
    std::unique_ptr<SliderAttachment>   tuneAttach_;
    std::unique_ptr<SliderAttachment>   fineAttach_;
    std::unique_ptr<SliderAttachment>   pwAttach_;
    std::unique_ptr<SliderAttachment>   pwmDepthAttach_;
    std::unique_ptr<ComboBoxAttachment> subModeAttach_;
    std::unique_ptr<SliderAttachment>   noiseAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VcoModule)
};

} // namespace mw::ui
