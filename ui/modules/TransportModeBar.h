// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/modules/TransportModeBar.h — the TRANSPORT / MODE bar (arp + seq mode,
// tempo-sync subdivisions, run/hold, plus the scale-preset selector and the
// reduce-motion toggle surfaced to the editor) [docs/design/10-ui.md §5.3 table row
// "TransportModeBar"; §4.4; §10].
//
// Two distinct classes of affordance live here:
//
//   (1) APVTS-bound controls — the SOLE write path is a JUCE APVTS attachment using a
//       schema-owned ParamId constant from core/params/ParamIDs.h; the bar NEVER
//       hard-codes a raw "mw101.*" literal and NEVER calls processor DSP directly
//       [docs/design/10-ui.md §8.1; ADR-015 C3, C4]. These are:
//         • Arp Mode      — ChoiceSelector  -> ids::kArpMode      (ComboBoxAttachment)
//         • Arp Range     — ChoiceSelector  -> ids::kArpRange     (ComboBoxAttachment)
//         • Arp Tempo Sync— ToggleSwitch    -> ids::kArpTempoSync (ButtonAttachment)
//         • Arp Sync Div  — ChoiceSelector  -> ids::kArpSyncDiv   (ComboBoxAttachment)
//         • Arp Latch     — ToggleSwitch    -> ids::kArpLatch     (ButtonAttachment)
//         • Seq Mode      — ChoiceSelector  -> ids::kSeqMode      (ComboBoxAttachment)
//         • Seq Tempo Sync— ToggleSwitch    -> ids::kSeqTempoSync (ButtonAttachment)
//         • Seq Sync Div  — ChoiceSelector  -> ids::kSeqSyncDiv   (ComboBoxAttachment)
//       The choice lists + canonical/extension split MIRROR the schema registry
//       (ParamDefs: choiceCount / canonicalChoiceCount) so any future sound_ext index
//       is fenced by the ChoiceSelector with NO re-minting here [ADR-008 C5/C6].
//
//   (2) NON-APVTS UI affordances — these are NOT host parameters; they are surfaced to
//       the editor through narrow callback seams and carry NO DSP [§4.4; §10]:
//         • Run / Hold transport control — a UI affordance whose state is reported to
//           the editor (it drives transport state owned elsewhere, NOT this UI). It is
//           deliberately not an APVTS param [§5.3 "run/hold"].
//         • Scale-preset selector (75/100/150/200%) — signals the editor to snap the
//           window; the editor owns the snap/persist (§4.4, OUT OF SCOPE here)
//           [ADR-015 C2]. The percentages are the (PI) constants in
//           core/calibration/TransportModeBarConstants.h.
//         • Reduce-motion / low-CPU toggle — surfaced to the editor's Timer logic; the
//           actual Timer drain/gating is OUT OF SCOPE here (ui-6). Enabling it affects
//           NO control binding [§10; ADR-015 C8].
//
// layoutDesignUnits() positions the children as a single horizontal row (it is a BAR)
// in DESIGN units only (no pixel math); the proportions are the (PI) constants in
// core/calibration/TransportModeBarConstants.h [docs/design/10-ui.md §5.3].
//
// This header is JUCE-built and lives at the design-faithful path ui/modules/; its
// .cpp lives under plugin/ui/modules/ so the plugin glob compiles it (mirrors
// plugin/ui/modules/ModulatorModule.cpp).

#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ModuleBase.h"
#include "../DesignTokens.h"
#include "../controls/ChoiceSelector.h"
#include "../controls/ToggleSwitch.h"

namespace mw::ui {

class TransportModeBar final : public ModuleBase
{
public:
    explicit TransportModeBar(juce::AudioProcessorValueTreeState& state);
    ~TransportModeBar() override;

    // Inject the design tokens so any ChoiceSelector's software-extension fence is
    // tinted with the extensionTag colour (a reskin re-tints with no code change)
    // [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // Lay out the controls in a single design-unit horizontal row beneath the title
    // strip (no pixel math) [§5.3].
    void layoutDesignUnits(juce::Rectangle<float> designBounds) override;

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // --- Non-APVTS seams to the editor (§4.4, §10) --------------------------------
    // The scale-preset selector signals the editor to snap the window. The editor sets
    // this callback; the bar invokes it with the chosen 0-based preset index. The bar
    // owns NO snap/persist logic (that is the editor's job, OUT OF SCOPE) [ADR-015 C2].
    std::function<void(int presetIndex)> onScalePresetSelected;

    // The reduce-motion / low-CPU toggle is surfaced to the editor's Timer logic. The
    // editor sets this callback; the bar invokes it with the new boolean state. No
    // control binding is affected by this toggle [§10; ADR-015 C8].
    std::function<void(bool reduceMotion)> onReduceMotionChanged;

    // The run/hold transport control is a UI affordance (NOT an APVTS param). The
    // editor sets this callback; the bar invokes it with the new run state [§5.3].
    std::function<void(bool running)> onRunStateChanged;

    // The number of scale presets the selector offers (75/100/150/200% => 4); the
    // logical factor for an index is scalePresetFactor(). Static so the editor can map
    // an index to a factor without holding a bar instance.
    [[nodiscard]] static int numScalePresets() noexcept;
    [[nodiscard]] static float scalePresetFactor(int presetIndex) noexcept;

    // --- State accessors for the non-APVTS affordances (for the editor / tests) ----
    [[nodiscard]] int  selectedScalePresetIndex() const noexcept;
    [[nodiscard]] bool reduceMotionEnabled() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    // --- Control access (for parents and tests) -----------------------------------
    ChoiceSelector& arpModeSelector()      noexcept { return arpMode_; }
    ChoiceSelector& arpRangeSelector()     noexcept { return arpRange_; }
    ToggleSwitch&   arpTempoSyncToggle()   noexcept { return arpTempoSync_; }
    ChoiceSelector& arpSyncDivSelector()   noexcept { return arpSyncDiv_; }
    ToggleSwitch&   arpLatchToggle()       noexcept { return arpLatch_; }
    ChoiceSelector& seqModeSelector()      noexcept { return seqMode_; }
    ToggleSwitch&   seqTempoSyncToggle()   noexcept { return seqTempoSync_; }
    ChoiceSelector& seqSyncDivSelector()   noexcept { return seqSyncDiv_; }
    ChoiceSelector& scalePresetSelector()  noexcept { return scalePreset_; }
    ToggleSwitch&   reduceMotionToggle()   noexcept { return reduceMotion_; }
    ToggleSwitch&   runHoldToggle()        noexcept { return runHold_; }

private:
    // (1) APVTS-bound controls (owned).
    ChoiceSelector arpMode_;
    ChoiceSelector arpRange_;
    ToggleSwitch   arpTempoSync_;
    ChoiceSelector arpSyncDiv_;
    ToggleSwitch   arpLatch_;
    ChoiceSelector seqMode_;
    ToggleSwitch   seqTempoSync_;
    ChoiceSelector seqSyncDiv_;

    // (2) Non-APVTS UI affordances (owned).
    ToggleSwitch   runHold_;
    ChoiceSelector scalePreset_;
    ToggleSwitch   reduceMotion_;

    // Caption labels (one per cell); positioned in the bottom of each cell.
    juce::Label    arpModeLabel_;
    juce::Label    arpRangeLabel_;
    juce::Label    arpSyncLabel_;
    juce::Label    arpDivLabel_;
    juce::Label    arpLatchLabel_;
    juce::Label    seqModeLabel_;
    juce::Label    seqSyncLabel_;
    juce::Label    seqDivLabel_;
    juce::Label    runHoldLabel_;
    juce::Label    scaleLabel_;
    juce::Label    reduceMotionLabel_;

    // The APVTS attachments — the SOLE write path for every bound control
    // [§8.1; ADR-015 C3]. Created in the ctor (message thread), torn down in dtor.
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboBoxAttachment> arpModeAttach_;
    std::unique_ptr<ComboBoxAttachment> arpRangeAttach_;
    std::unique_ptr<ButtonAttachment>   arpTempoSyncAttach_;
    std::unique_ptr<ComboBoxAttachment> arpSyncDivAttach_;
    std::unique_ptr<ButtonAttachment>   arpLatchAttach_;
    std::unique_ptr<ComboBoxAttachment> seqModeAttach_;
    std::unique_ptr<ButtonAttachment>   seqTempoSyncAttach_;
    std::unique_ptr<ComboBoxAttachment> seqSyncDivAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportModeBar)
};

} // namespace mw::ui
