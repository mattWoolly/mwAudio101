// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/ControllerStrip.h — the CONTROLLER strip (glide, pitch-bend sensitivity,
// mod-wheel routing) [docs/design/10-ui.md §5.3 table row "ControllerStrip"].
//
// The CONTROLLER section is rendered as a horizontal STRIP (a wide, short rectangle)
// rather than a panel module, but it still derives from ModuleBase and lays its
// children out in DESIGN units only via layoutDesignUnits() (no pixel math) [§5.3].
//
// Owns and binds the controller-section controls — every parameter ID is a schema-owned
// constant from core/params/ParamIDs.h (task 014b, COMPLETE); the strip NEVER hard-codes
// a raw "mw101.*" literal and NEVER calls processor DSP directly [§8.1; ADR-015 C3, C4;
// ADR-008 C1]:
//   • Glide Time         — RotarySlider  -> SliderAttachment   ids::kGlideTime
//   • Glide Mode         — ChoiceSelector-> ComboBoxAttachment ids::kGlideMode      {Off, Auto, On}
//   • Bend Range (VCO)   — RotarySlider  -> SliderAttachment   ids::kModBendRangeVco (cents; pitch-bend sensitivity)
//   • Bend Range (VCF)   — RotarySlider  -> SliderAttachment   ids::kModBendRangeVcf (cents; pitch-bend sensitivity)
//   • Bend Dest          — ChoiceSelector-> ComboBoxAttachment ids::kModBendDest     {VCO, VCF, Both} (mod routing)
//   • Mod Wheel -> LFO   — RotarySlider  -> SliderAttachment   ids::kModLfoModWheel  (mod-wheel routing depth)
//
// NOTE on "transpose": the §5.3 row names a transpose control, but the parameter schema
// (the single source of truth for IDs — ADR-008) defines NO transpose parameter in the
// Glide/Mod groups. Binding a non-existent ID would require a raw literal (forbidden),
// so the strip binds exactly the controller parameters the schema DOES expose; a future
// transpose parameter would be appended here once it is minted in the registry.
//
// The two choice controls (Glide Mode, Bend Dest) are wholly hardware-canonical in the
// schema (choiceCount == canonicalChoiceCount), so NO entry is fenced as a software
// extension here — the strip still populates the selector from the schema's
// canonicalChoiceCount so the fence is correct-by-construction if a future extension
// option is appended [ADR-008 §7, C5/C6, C15].
//
// Attachments are created in the constructor (message thread) and torn down in
// destruction; they are never created from a render/Timer hot path (RT invariant §3.6).
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

class ControllerStrip final : public ModuleBase
{
public:
    explicit ControllerStrip(juce::AudioProcessorValueTreeState& state);
    ~ControllerStrip() override;

    // Inject the design tokens so any ChoiceSelector software-extension fence is tinted
    // with the extensionTag colour (a reskin re-tints with no code change) [§6.1;
    // ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // Lay out the controls in a single design-unit row to the right of the title cell.
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Control access (for parents and tests) -----------------------------------
    RotarySlider&   glideTimeSlider()    noexcept { return glideTime_; }
    ChoiceSelector& glideModeSelector()  noexcept { return glideMode_; }
    RotarySlider&   bendRangeVcoSlider() noexcept { return bendRangeVco_; }
    RotarySlider&   bendRangeVcfSlider() noexcept { return bendRangeVcf_; }
    ChoiceSelector& bendDestSelector()   noexcept { return bendDest_; }
    RotarySlider&   modWheelSlider()     noexcept { return modWheel_; }

private:
    // The controls (owned, in design order: glide time, glide mode, bend ranges, bend
    // dest, mod-wheel routing).
    RotarySlider   glideTime_;
    ChoiceSelector glideMode_;
    RotarySlider   bendRangeVco_;
    RotarySlider   bendRangeVcf_;
    ChoiceSelector bendDest_;
    RotarySlider   modWheel_;

    // Caption labels (one per control); positioned in the bottom of each cell.
    juce::Label    glideTimeLabel_;
    juce::Label    glideModeLabel_;
    juce::Label    bendRangeVcoLabel_;
    juce::Label    bendRangeVcfLabel_;
    juce::Label    bendDestLabel_;
    juce::Label    modWheelLabel_;

    // The APVTS attachments — the SOLE write path for every control [§8.1; ADR-015 C3].
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment>   glideTimeAttach_;
    std::unique_ptr<ComboBoxAttachment> glideModeAttach_;
    std::unique_ptr<SliderAttachment>   bendRangeVcoAttach_;
    std::unique_ptr<SliderAttachment>   bendRangeVcfAttach_;
    std::unique_ptr<ComboBoxAttachment> bendDestAttach_;
    std::unique_ptr<SliderAttachment>   modWheelAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerStrip)
};

} // namespace mw::ui
