// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/VcaModule.h — the VCA panel module (VCA + amp envelope)
// [docs/design/10-ui.md §5.3 table row "VcaModule": "env gate/select, level, env
// A/D/S/R"].
//
// Owns and binds the VCA / amp-envelope controls:
//   • VCA Mode  — a ChoiceSelector bound via APVTS ComboBoxAttachment to ids::kVcaMode.
//                 The choice list is the schema's {ENV, GATE}: a CANONICAL two-entry
//                 choice with NO software-only extension (canonicalChoiceCount ==
//                 choiceCount), so the selector fences NOTHING — the env/gate select is
//                 documented 1982 hardware behaviour, not a sound_ext artifact
//                 [docs/design/06 §3.0; ADR-008 C5/C6].
//   • VCA Level — a RotarySlider bound via APVTS SliderAttachment to ids::kVcaLevel.
//   • Env A/D/S/R — four RotarySliders for the shared envelope, bound to
//                 ids::kEnvAttack / kEnvDecay / kEnvSustain / kEnvRelease. The envelope
//                 is the single shared ADSR (the same params the VCF module reads for
//                 its env amount); this module surfaces its A/D/S/R controls under the
//                 VCA per §5.3.
//
// Every control binds through a JUCE APVTS attachment using a schema-owned ParamId
// constant from core/params/ParamIDs.h — the module NEVER hard-codes a raw "mw101.*"
// string literal and NEVER calls processor DSP directly [docs/design/10-ui.md §8.1;
// ADR-015 C3, C4; ADR-008 C1]. Attachments are created in the constructor (message
// thread) and torn down in destruction; they are never created from a render/Timer hot
// path (RT invariant §3.6).
//
// layoutDesignUnits() positions the children in DESIGN units only (no pixel math); the
// proportions are the (PI) constants in core/calibration/VcaModuleConstants.h
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

class VcaModule final : public ModuleBase
{
public:
    explicit VcaModule(juce::AudioProcessorValueTreeState& state);
    ~VcaModule() override;

    // Inject the design tokens so the ChoiceSelector carries the single token table
    // (a reskin re-tints with no code change) [§6.1; ADR-015 C10]. The VCA Mode choice
    // is canonical (no fenced extension), but the seam is kept uniform with the other
    // modules so a parent can blanket-set tokens.
    void setTokens(const DesignTokens& tokens);

    // Lay out the six controls in a single design-unit row beneath the title strip.
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Control access (for parents and tests) -----------------------------------
    ChoiceSelector& modeSelector()    noexcept { return mode_; }
    RotarySlider&   levelSlider()     noexcept { return level_; }
    RotarySlider&   attackSlider()    noexcept { return attack_; }
    RotarySlider&   decaySlider()     noexcept { return decay_; }
    RotarySlider&   sustainSlider()   noexcept { return sustain_; }
    RotarySlider&   releaseSlider()   noexcept { return release_; }

private:
    // The controls (owned, in design order: mode, level, then the shared A/D/S/R).
    ChoiceSelector mode_;
    RotarySlider   level_;
    RotarySlider   attack_;
    RotarySlider   decay_;
    RotarySlider   sustain_;
    RotarySlider   release_;

    // Caption labels (one per control); positioned in the bottom of each cell.
    juce::Label    modeLabel_;
    juce::Label    levelLabel_;
    juce::Label    attackLabel_;
    juce::Label    decayLabel_;
    juce::Label    sustainLabel_;
    juce::Label    releaseLabel_;

    // The APVTS attachments — the SOLE write path for every control [§8.1; ADR-015 C3].
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<ComboBoxAttachment> modeAttach_;
    std::unique_ptr<SliderAttachment>   levelAttach_;
    std::unique_ptr<SliderAttachment>   attackAttach_;
    std::unique_ptr<SliderAttachment>   decayAttach_;
    std::unique_ptr<SliderAttachment>   sustainAttach_;
    std::unique_ptr<SliderAttachment>   releaseAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VcaModule)
};

} // namespace mw::ui
