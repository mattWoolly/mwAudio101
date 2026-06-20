// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/ModulatorModule.cpp — implementation of the MODULATOR panel module
// declared in ui/modules/ModulatorModule.h [docs/design/10-ui.md §5.3, §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached
// by a relative include — no shared CMakeLists edit (mirrors plugin/ui/controls/*.cpp).

#include "../../../ui/modules/ModulatorModule.h"

#include "../../../core/calibration/ModulatorModuleConstants.h"
#include "../../../core/params/ParamIDs.h"    // mw::params::ids — schema-owned ParamId constants
#include "../../../core/params/ParamDefs.h"   // mw::params::kParamDefs — the choice-fence source of truth

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::modulator;

// Find the JUCE-free schema entry for a parameter ID. The shape control's label list
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

// Configure a depth/rate rotary as a thin numeric control bound for display; the
// caption is owned by the module's Label (set in the ctor).
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

} // namespace

ModulatorModule::ModulatorModule(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "MODULATOR")
{
    // --- controls ----------------------------------------------------------------
    initRotary(lfoRate_);
    initRotary(depthPitch_);
    initRotary(depthPwm_);
    initRotary(depthCutoff_);

    addAndMakeVisible(lfoRate_);
    addAndMakeVisible(lfoShape_);
    addAndMakeVisible(depthPitch_);
    addAndMakeVisible(depthPwm_);
    addAndMakeVisible(depthCutoff_);

    initCaption(lfoRateLabel_,    "Rate",   *this);
    initCaption(lfoShapeLabel_,   "Shape",  *this);
    initCaption(depthPitchLabel_, "Pitch",  *this);
    initCaption(depthPwmLabel_,   "PWM",    *this);
    initCaption(depthCutoffLabel_,"Cutoff", *this);

    // --- the LFO-shape choice list + software-extension fence (schema-driven) -----
    // Populate the selector from the bound parameter's value strings and mirror the
    // schema's canonicalChoiceCount so the trailing Sine entry is fenced as sound_ext
    // [ADR-008 C5/C6]. The selector must hold the items BEFORE the ComboBoxAttachment
    // is created so the attachment's index<->item mapping is 1:1 with the parameter.
    if (const auto* shapeDef = findDef(ids::kLfoShape))
    {
        juce::StringArray labels;
        for (std::uint8_t i = 0; i < shapeDef->choiceCount; ++i)
            labels.add(juce::String::fromUTF8(shapeDef->choices[i]));

        lfoShape_.setChoices(labels, static_cast<int>(shapeDef->canonicalChoiceCount));
    }

    // --- APVTS attachments — the SOLE write path for every control [§8.1; C3] -----
    // Created in construction (message thread); torn down in destruction. The paramID
    // is a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    lfoRateAttach_     = std::make_unique<SliderAttachment>(apvts, ids::kLfoRate,        lfoRate_);
    lfoShapeAttach_    = std::make_unique<ComboBoxAttachment>(apvts, ids::kLfoShape,     lfoShape_);
    depthPitchAttach_  = std::make_unique<SliderAttachment>(apvts, ids::kLfoDepthPitch,  depthPitch_);
    depthPwmAttach_    = std::make_unique<SliderAttachment>(apvts, ids::kLfoDepthPwm,    depthPwm_);
    depthCutoffAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kLfoDepthCutoff, depthCutoff_);

    // Wire each rotary's value read-out to its bound parameter's display string so the
    // text is parameter-derived, never hard-coded [§6.3; ADR-008 C4].
    if (auto* p = apvts.getParameter(ids::kLfoRate))        lfoRate_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kLfoDepthPitch))  depthPitch_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kLfoDepthPwm))    depthPwm_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kLfoDepthCutoff)) depthCutoff_.attachParameterForDisplay(*p);
}

// Out-of-line dtor: the unique_ptr<…Attachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
ModulatorModule::~ModulatorModule() = default;

void ModulatorModule::setTokens(const DesignTokens& tokens)
{
    // Only the ChoiceSelector carries a token-driven fence; forward the table so a
    // reskin re-tints the Sine extension with no code change [§6.1; ADR-015 C10].
    lfoShape_.setTokens(tokens);
}

void ModulatorModule::layoutDesignUnits(juce::Rectangle<float> designBounds)
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
    const float gap      = (area.getWidth() / static_cast<float>(n)) * cal::kCellGapFraction;
    const float cellW    = (area.getWidth() - gap * static_cast<float>(n - 1)) / static_cast<float>(n);

    // Place each control into the top of its cell and its caption into the bottom.
    auto placeCell = [&](int index, juce::Component& control, juce::Component& caption)
    {
        const float x = area.getX() + static_cast<float>(index) * (cellW + gap);
        juce::Rectangle<float> cell{ x, area.getY(), cellW, area.getHeight() };

        auto captionArea = cell.removeFromBottom(cell.getHeight() * cal::kCaptionHeightFraction);
        control.setBounds(cell.toNearestInt());
        caption.setBounds(captionArea.toNearestInt());
    };

    placeCell(0, lfoRate_,     lfoRateLabel_);
    placeCell(1, lfoShape_,    lfoShapeLabel_);
    placeCell(2, depthPitch_,  depthPitchLabel_);
    placeCell(3, depthPwm_,    depthPwmLabel_);
    placeCell(4, depthCutoff_, depthCutoffLabel_);
}

void ModulatorModule::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
