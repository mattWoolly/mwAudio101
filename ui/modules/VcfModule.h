// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/VcfModule.h — the VCF panel module (cutoff, resonance, env amount, kbd
// track, mod) [docs/design/10-ui.md §5.3 table row "VcfModule"].
//
// Owns and binds the filter-section controls, each a RotarySlider bound via an APVTS
// SliderAttachment to a schema-owned ParamId constant from core/params/ParamIDs.h:
//   • Cutoff      — ids::kVcfCutoff
//   • Resonance   — ids::kVcfResonance
//   • Env Amount  — ids::kVcfEnvMod      (the cutoff envelope-modulation depth)
//   • Kbd Track   — ids::kVcfKbdTrack    (keyboard tracking amount)
//   • Mod         — ids::kVcfLfoMod      (the VCF LFO-modulation depth)
//
// All five VCF parameters are continuous (detail::cont in ParamDefs.h), so the module
// holds only rotary sliders — there is NO stepped choice in this section and therefore
// no software-extension (sound_ext) fence to apply here [ADR-008 §7]. (The fenced
// extensions live in modules whose section carries a choice, e.g. the LFO shape /
// VCO range.)
//
// Every control binds through a JUCE APVTS attachment — the module NEVER hard-codes a
// raw "mw101.*" string literal and NEVER calls processor DSP directly
// [docs/design/10-ui.md §8.1; ADR-015 C3, C4; ADR-008 C1]. Attachments are created in
// the constructor (message thread) and torn down in destruction; they are never created
// from a render/Timer hot path (RT invariant §3.6).
//
// layoutDesignUnits() positions the children in DESIGN units only (no pixel math); the
// proportions are the (PI) constants in core/calibration/VcfModuleConstants.h
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
#include "../controls/RotarySlider.h"

namespace mw::ui {

class VcfModule final : public ModuleBase
{
public:
    explicit VcfModule(juce::AudioProcessorValueTreeState& state);
    ~VcfModule() override;

    // Lay out the five controls in a single design-unit row beneath the title strip.
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Control access (for parents and tests) -----------------------------------
    RotarySlider& cutoffSlider()    noexcept { return cutoff_; }
    RotarySlider& resonanceSlider() noexcept { return resonance_; }
    RotarySlider& envModSlider()    noexcept { return envMod_; }
    RotarySlider& kbdTrackSlider()  noexcept { return kbdTrack_; }
    RotarySlider& lfoModSlider()    noexcept { return lfoMod_; }

private:
    // The controls (owned, in design order: cutoff, resonance, env, kbd, mod).
    RotarySlider cutoff_;
    RotarySlider resonance_;
    RotarySlider envMod_;
    RotarySlider kbdTrack_;
    RotarySlider lfoMod_;

    // Caption labels (one per control); positioned in the bottom of each cell.
    juce::Label cutoffLabel_;
    juce::Label resonanceLabel_;
    juce::Label envModLabel_;
    juce::Label kbdTrackLabel_;
    juce::Label lfoModLabel_;

    // The APVTS attachments — the SOLE write path for every control [§8.1; ADR-015 C3].
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> cutoffAttach_;
    std::unique_ptr<SliderAttachment> resonanceAttach_;
    std::unique_ptr<SliderAttachment> envModAttach_;
    std::unique_ptr<SliderAttachment> kbdTrackAttach_;
    std::unique_ptr<SliderAttachment> lfoModAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VcfModule)
};

} // namespace mw::ui
