// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/TransportModeBar.cpp — implementation of the TRANSPORT / MODE bar
// declared in ui/modules/TransportModeBar.h [docs/design/10-ui.md §5.3, §4.4, §10,
// §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached
// by a relative include — no shared CMakeLists edit (mirrors
// plugin/ui/modules/ModulatorModule.cpp).

#include "../../../ui/modules/TransportModeBar.h"

#include <array>
#include <string_view>

#include "../../../core/calibration/TransportModeBarConstants.h"
#include "../../../core/params/ParamIDs.h"    // mw::params::ids — schema-owned ParamId constants
#include "../../../core/params/ParamDefs.h"   // mw::params::kParamDefs — the choice-fence source of truth

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::transport;

// Find the JUCE-free schema entry for a parameter ID. A choice control's label list
// AND its canonical/extension split are read from the schema registry — the bar never
// re-mints the fence; it mirrors choiceCount / canonicalChoiceCount [ADR-008 C5/C6].
const mw::params::ParamDef* findDef(const char* id) noexcept
{
    const std::string_view want{ id };
    for (const auto& d : mw::params::kParamDefs)
        if (std::string_view{ d.id } == want)
            return &d;
    return nullptr;
}

// Populate a ChoiceSelector from the bound parameter's schema label list, mirroring
// the schema's canonicalChoiceCount so any trailing software-extension index is fenced
// [ADR-008 C5/C6]. The selector must hold the items BEFORE the ComboBoxAttachment is
// created so the attachment's index<->item mapping is 1:1 with the parameter.
void populateFromSchema(ChoiceSelector& selector, const char* paramId)
{
    if (const auto* def = findDef(paramId))
    {
        juce::StringArray labels;
        for (std::uint8_t i = 0; i < def->choiceCount; ++i)
            labels.add(juce::String::fromUTF8(def->choices[i]));

        selector.setChoices(labels, static_cast<int>(def->canonicalChoiceCount));
    }
}

void initCaption(juce::Label& l, const char* text, juce::Component& owner)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setInterceptsMouseClicks(false, false);
    owner.addAndMakeVisible(l);
}

} // namespace

TransportModeBar::TransportModeBar(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "TRANSPORT")
{
    // --- (1) APVTS-bound controls -------------------------------------------------
    addAndMakeVisible(arpMode_);
    addAndMakeVisible(arpRange_);
    addAndMakeVisible(arpTempoSync_);
    addAndMakeVisible(arpSyncDiv_);
    addAndMakeVisible(arpLatch_);
    addAndMakeVisible(seqMode_);
    addAndMakeVisible(seqTempoSync_);
    addAndMakeVisible(seqSyncDiv_);

    // --- (2) Non-APVTS UI affordances ---------------------------------------------
    addAndMakeVisible(runHold_);
    addAndMakeVisible(scalePreset_);
    addAndMakeVisible(reduceMotion_);

    // Toggle captions (the switch carries its own on/off text, §6.3).
    arpTempoSync_.setStateLabels("Free", "Sync");
    seqTempoSync_.setStateLabels("Free", "Sync");
    arpLatch_.setStateLabels("Latch Off", "Latch On");
    runHold_.setStateLabels("Hold", "Run");
    reduceMotion_.setStateLabels("Motion", "Reduced");

    // --- Captions -----------------------------------------------------------------
    initCaption(runHoldLabel_,      "Run",     *this);
    initCaption(arpModeLabel_,      "Arp",     *this);
    initCaption(arpRangeLabel_,     "Range",   *this);
    initCaption(arpSyncLabel_,      "Arp Sync",*this);
    initCaption(arpDivLabel_,       "Arp Div", *this);
    initCaption(arpLatchLabel_,     "Latch",   *this);
    initCaption(seqModeLabel_,      "Seq",     *this);
    initCaption(seqSyncLabel_,      "Seq Sync",*this);
    initCaption(seqDivLabel_,       "Seq Div", *this);
    initCaption(scaleLabel_,        "Scale",   *this);
    initCaption(reduceMotionLabel_, "Reduce",  *this);

    // --- Choice lists + software-extension fences (schema-driven) -----------------
    populateFromSchema(arpMode_,   ids::kArpMode);
    populateFromSchema(arpRange_,  ids::kArpRange);
    populateFromSchema(arpSyncDiv_, ids::kArpSyncDiv);
    populateFromSchema(seqMode_,   ids::kSeqMode);
    populateFromSchema(seqSyncDiv_, ids::kSeqSyncDiv);

    // The scale-preset selector's options are the (PI) percentage labels; it is NOT an
    // APVTS param (it signals the editor to snap, §4.4). All entries are canonical (no
    // software-extension fence).
    {
        juce::StringArray scaleLabels;
        for (int i = 0; i < cal::kScalePresetCount; ++i)
            scaleLabels.add(juce::String::fromUTF8(cal::kScalePresetLabels[i]));
        scalePreset_.setChoices(scaleLabels, cal::kScalePresetCount);
        // 1-based item IDs; default to 100%.
        scalePreset_.setSelectedId(cal::kDefaultScalePresetIndex + 1, juce::dontSendNotification);
    }

    // --- APVTS attachments — the SOLE write path for every bound control [§8.1; C3]-
    // Created in construction (message thread); torn down in destruction. The paramID
    // is a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    arpModeAttach_      = std::make_unique<ComboBoxAttachment>(apvts, ids::kArpMode,      arpMode_);
    arpRangeAttach_     = std::make_unique<ComboBoxAttachment>(apvts, ids::kArpRange,     arpRange_);
    arpTempoSyncAttach_ = std::make_unique<ButtonAttachment>  (apvts, ids::kArpTempoSync, arpTempoSync_);
    arpSyncDivAttach_   = std::make_unique<ComboBoxAttachment>(apvts, ids::kArpSyncDiv,   arpSyncDiv_);
    arpLatchAttach_     = std::make_unique<ButtonAttachment>  (apvts, ids::kArpLatch,     arpLatch_);
    seqModeAttach_      = std::make_unique<ComboBoxAttachment>(apvts, ids::kSeqMode,      seqMode_);
    seqTempoSyncAttach_ = std::make_unique<ButtonAttachment>  (apvts, ids::kSeqTempoSync, seqTempoSync_);
    seqSyncDivAttach_   = std::make_unique<ComboBoxAttachment>(apvts, ids::kSeqSyncDiv,   seqSyncDiv_);

    // --- Non-APVTS seams to the editor (§4.4, §10, §5.3) --------------------------
    // The scale-preset selector reports the chosen 0-based index; the bar owns NO
    // snap/persist logic (editor's job, OUT OF SCOPE) [ADR-015 C2].
    scalePreset_.onChange = [this]
    {
        if (onScalePresetSelected)
            onScalePresetSelected(selectedScalePresetIndex());
    };

    // The reduce-motion toggle is surfaced to the editor's Timer logic; it affects NO
    // control binding [§10; ADR-015 C8].
    reduceMotion_.onClick = [this]
    {
        if (onReduceMotionChanged)
            onReduceMotionChanged(reduceMotion_.getToggleState());
    };

    // The run/hold transport control is a UI affordance reported to the editor [§5.3].
    runHold_.onClick = [this]
    {
        if (onRunStateChanged)
            onRunStateChanged(runHold_.getToggleState());
    };
}

// Out-of-line dtor: the unique_ptr<…Attachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
TransportModeBar::~TransportModeBar() = default;

void TransportModeBar::setTokens(const DesignTokens& tokens)
{
    // Forward the token table so each ChoiceSelector re-tints any software-extension
    // fence on a reskin with no code change [§6.1; ADR-015 C10].
    arpMode_.setTokens(tokens);
    arpRange_.setTokens(tokens);
    arpSyncDiv_.setTokens(tokens);
    seqMode_.setTokens(tokens);
    seqSyncDiv_.setTokens(tokens);
    scalePreset_.setTokens(tokens);
}

int TransportModeBar::numScalePresets() noexcept
{
    return cal::kScalePresetCount;
}

float TransportModeBar::scalePresetFactor(int presetIndex) noexcept
{
    if (presetIndex < 0 || presetIndex >= cal::kScalePresetCount)
        return cal::kScalePresetFactors[cal::kDefaultScalePresetIndex];
    return cal::kScalePresetFactors[presetIndex];
}

int TransportModeBar::selectedScalePresetIndex() const noexcept
{
    // ComboBox item IDs are 1-based; convert back to the 0-based preset index.
    const int id = scalePreset_.getSelectedId();
    return id > 0 ? id - 1 : cal::kDefaultScalePresetIndex;
}

bool TransportModeBar::reduceMotionEnabled() const noexcept
{
    return reduceMotion_.getToggleState();
}

bool TransportModeBar::isRunning() const noexcept
{
    return runHold_.getToggleState();
}

void TransportModeBar::layoutDesignUnits(juce::Rectangle<float> designBounds)
{
    // All math below is in DESIGN units (fractions of the supplied rectangle) — no
    // pixel literals [docs/design/10-ui.md §5.3]. The proportions are (PI) constants.
    auto area = designBounds;

    // Reserve the title strip across the top.
    area.removeFromTop(area.getHeight() * cal::kTitleHeightFraction);

    // Uniform inset around the control row (fraction of the smaller dimension).
    const float inset = juce::jmin(designBounds.getWidth(), designBounds.getHeight())
                        * cal::kContentInsetFraction;
    area.reduce(inset, inset);

    // A single horizontal row of equal-width cells, separated by a proportional gap
    // (this module is a BAR — controls run left to right).
    constexpr int n = cal::kControlCellCount;
    const float gap   = (area.getWidth() / static_cast<float>(n)) * cal::kCellGapFraction;
    const float cellW = (area.getWidth() - gap * static_cast<float>(n - 1)) / static_cast<float>(n);

    // Place each control into the top of its cell and its caption into the bottom.
    auto placeCell = [&](int index, juce::Component& control, juce::Component& caption)
    {
        const float x = area.getX() + static_cast<float>(index) * (cellW + gap);
        juce::Rectangle<float> cell{ x, area.getY(), cellW, area.getHeight() };

        auto captionArea = cell.removeFromBottom(cell.getHeight() * cal::kCaptionHeightFraction);
        control.setBounds(cell.toNearestInt());
        caption.setBounds(captionArea.toNearestInt());
    };

    // Design order (left to right): transport, arp section, seq section, then the two
    // editor-facing affordances (scale, reduce-motion).
    placeCell(0,  runHold_,      runHoldLabel_);
    placeCell(1,  arpMode_,      arpModeLabel_);
    placeCell(2,  arpRange_,     arpRangeLabel_);
    placeCell(3,  arpTempoSync_, arpSyncLabel_);
    placeCell(4,  arpSyncDiv_,   arpDivLabel_);
    placeCell(5,  arpLatch_,     arpLatchLabel_);
    placeCell(6,  seqMode_,      seqModeLabel_);
    placeCell(7,  seqTempoSync_, seqSyncLabel_);
    placeCell(8,  seqSyncDiv_,   seqDivLabel_);
    placeCell(9,  scalePreset_,  scaleLabel_);
    placeCell(10, reduceMotion_, reduceMotionLabel_);
}

void TransportModeBar::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
