// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/VcoModule.cpp — implementation of the VCO panel module declared
// in ui/modules/VcoModule.h [docs/design/10-ui.md §5.3, §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached
// by a relative include — no shared CMakeLists edit (mirrors
// plugin/ui/modules/ModulatorModule.cpp).

#include "../../../ui/modules/VcoModule.h"

#include "../../../core/calibration/VcoModuleConstants.h"
#include "../../../core/params/ParamIDs.h"    // mw::params::ids — schema-owned ParamId constants
#include "../../../core/params/ParamDefs.h"   // mw::params::kParamDefs — the choice-fence source of truth

#include <array>
#include <cstdint>
#include <string_view>

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::vco;

// Find the JUCE-free schema entry for a parameter ID. A choice control's label list
// AND its canonical/extension split are read from the schema registry — the module
// never re-mints the fence; it mirrors choiceCount / canonicalChoiceCount [ADR-008
// C5/C6].
const mw::params::ParamDef* findDef(const char* id) noexcept
{
    const std::string_view want{ id };
    for (const auto& d : mw::params::kParamDefs)
        if (std::string_view{ d.id } == want)
            return &d;
    return nullptr;
}

// Configure a tune/PW/depth rotary as a thin numeric control bound for display;
// the caption is owned by the module's Label (set in the ctor).
void initRotary(RotarySlider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
}

void initCaption(juce::Label& l, const char* text, juce::Component& owner)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setInterceptsMouseClicks(false, false);
    owner.addAndMakeVisible(l);
}

// Populate a ChoiceSelector from the bound parameter's value strings, mirroring the
// schema's canonicalChoiceCount so any appended software-extension entries (the 32' /
// 64' VCO registers) are fenced as sound_ext [ADR-008 C5/C6]. The selector must hold
// the items BEFORE the ComboBoxAttachment is created so the attachment's index<->item
// mapping is 1:1 with the parameter.
void populateChoice(ChoiceSelector& sel, const mw::params::ParamDef* def)
{
    if (def == nullptr)
        return;

    juce::StringArray labels;
    for (std::uint8_t i = 0; i < def->choiceCount; ++i)
        labels.add(juce::String::fromUTF8(def->choices[i]));

    sel.setChoices(labels, static_cast<int>(def->canonicalChoiceCount));
}

} // namespace

VcoModule::VcoModule(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "VCO")
{
    // --- controls ----------------------------------------------------------------
    initRotary(tune_);
    initRotary(fine_);
    initRotary(pw_);
    initRotary(pwmDepth_);

    addAndMakeVisible(range_);
    addAndMakeVisible(tune_);
    addAndMakeVisible(fine_);
    addAndMakeVisible(pw_);
    addAndMakeVisible(pwmDepth_);
    addAndMakeVisible(subMode_);

    initCaption(rangeLabel_,    "Range", *this);
    initCaption(tuneLabel_,     "Tune",  *this);
    initCaption(fineLabel_,     "Fine",  *this);
    initCaption(pwLabel_,       "PW",    *this);
    initCaption(pwmDepthLabel_, "PWM",   *this);
    initCaption(subModeLabel_,  "Sub",   *this);

    // --- the choice lists + software-extension fence (schema-driven) --------------
    // The VCO range list's trailing 32' / 64' registers are fenced as sound_ext via the
    // ChoiceSelector extension affordance (canonicalChoiceCount = 4 of 6) [ADR-008
    // §7/C6/C15]. Sub mode is fully hardware-canonical (no extension).
    populateChoice(range_,   findDef(ids::kVcoRange));
    populateChoice(subMode_, findDef(ids::kSubMode));

    // --- APVTS attachments — the SOLE write path for every control [§8.1; C3] -----
    // Created in construction (message thread); torn down in destruction. The paramID
    // is a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    rangeAttach_    = std::make_unique<ComboBoxAttachment>(apvts, ids::kVcoRange,    range_);
    tuneAttach_     = std::make_unique<SliderAttachment>(apvts, ids::kVcoTune,       tune_);
    fineAttach_     = std::make_unique<SliderAttachment>(apvts, ids::kVcoFine,       fine_);
    pwAttach_       = std::make_unique<SliderAttachment>(apvts, ids::kVcoPw,         pw_);
    pwmDepthAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kVcoPwmDepth,   pwmDepth_);
    subModeAttach_  = std::make_unique<ComboBoxAttachment>(apvts, ids::kSubMode,     subMode_);

    // Wire each rotary's value read-out to its bound parameter's display string so the
    // text is parameter-derived, never hard-coded [§6.3; ADR-008 C4].
    if (auto* p = apvts.getParameter(ids::kVcoTune))     tune_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcoFine))     fine_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcoPw))       pw_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcoPwmDepth)) pwmDepth_.attachParameterForDisplay(*p);
}

// Out-of-line dtor: the unique_ptr<…Attachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
VcoModule::~VcoModule() = default;

void VcoModule::setTokens(const DesignTokens& tokens)
{
    // The ChoiceSelectors carry the token-driven fence; forward the table so a reskin
    // re-tints the 32' / 64' extensions with no code change [§6.1; ADR-015 C10].
    range_.setTokens(tokens);
    subMode_.setTokens(tokens);
}

void VcoModule::layoutDesignUnits(juce::Rectangle<float> designBounds)
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

    // A single row of equal-width cells, separated by a proportional gap.
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

    placeCell(0, range_,    rangeLabel_);
    placeCell(1, tune_,     tuneLabel_);
    placeCell(2, fine_,     fineLabel_);
    placeCell(3, pw_,       pwLabel_);
    placeCell(4, pwmDepth_, pwmDepthLabel_);
    placeCell(5, subMode_,  subModeLabel_);
}

void VcoModule::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
