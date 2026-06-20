// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/ControllerStrip.cpp — implementation of the CONTROLLER strip
// declared in ui/modules/ControllerStrip.h [docs/design/10-ui.md §5.3, §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached
// by a relative include — no shared CMakeLists edit (mirrors plugin/ui/modules/
// ModulatorModule.cpp).

#include "../../../ui/modules/ControllerStrip.h"

#include "../../../core/calibration/ControllerStripConstants.h"
#include "../../../core/params/ParamIDs.h"    // mw::params::ids — schema-owned ParamId constants
#include "../../../core/params/ParamDefs.h"   // mw::params::kParamDefs — the choice-fence source of truth

#include <string_view>

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::controller;

// Find the JUCE-free schema entry for a parameter ID. A choice control's label list AND
// its canonical/extension split are read from the schema registry — the strip never
// re-mints the fence; it mirrors choiceCount / canonicalChoiceCount [ADR-008 C5/C6].
const mw::params::ParamDef* findDef(const char* id) noexcept
{
    const std::string_view want{ id };
    for (const auto& d : mw::params::kParamDefs)
        if (std::string_view{ d.id } == want)
            return &d;
    return nullptr;
}

// Configure a rotary as a thin numeric control bound for display; the caption is owned
// by the strip's Label (set in the ctor).
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

// Populate a ChoiceSelector from the bound parameter's schema entry, mirroring the
// schema's canonicalChoiceCount so any appended software extension would be fenced as
// sound_ext. The selector must hold its items BEFORE the ComboBoxAttachment is created
// so the attachment's index<->item mapping is 1:1 with the parameter [ADR-008 C5/C6].
void initChoiceFromSchema(ChoiceSelector& sel, const char* id)
{
    if (const auto* def = findDef(id))
    {
        juce::StringArray labels;
        for (std::uint8_t i = 0; i < def->choiceCount; ++i)
            labels.add(juce::String::fromUTF8(def->choices[i]));

        sel.setChoices(labels, static_cast<int>(def->canonicalChoiceCount));
    }
}

} // namespace

ControllerStrip::ControllerStrip(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "CONTROLLER")
{
    // --- controls ----------------------------------------------------------------
    initRotary(glideTime_);
    initRotary(bendRangeVco_);
    initRotary(bendRangeVcf_);
    initRotary(modWheel_);

    addAndMakeVisible(glideTime_);
    addAndMakeVisible(glideMode_);
    addAndMakeVisible(bendRangeVco_);
    addAndMakeVisible(bendRangeVcf_);
    addAndMakeVisible(bendDest_);
    addAndMakeVisible(modWheel_);

    initCaption(glideTimeLabel_,    "Glide",      *this);
    initCaption(glideModeLabel_,    "Glide Mode", *this);
    initCaption(bendRangeVcoLabel_, "Bend VCO",   *this);
    initCaption(bendRangeVcfLabel_, "Bend VCF",   *this);
    initCaption(bendDestLabel_,     "Bend Dest",  *this);
    initCaption(modWheelLabel_,     "Mod Wheel",  *this);

    // --- choice lists + software-extension fences (schema-driven) ----------------
    // Both choices are wholly hardware-canonical in the schema (no sound_ext entry), so
    // no item is fenced — but we still mirror canonicalChoiceCount so a future appended
    // extension would fence correctly-by-construction [ADR-008 C5/C6].
    initChoiceFromSchema(glideMode_, ids::kGlideMode);
    initChoiceFromSchema(bendDest_,  ids::kModBendDest);

    // --- APVTS attachments — the SOLE write path for every control [§8.1; C3] -----
    // Created in construction (message thread); torn down in destruction. The paramID is
    // a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    glideTimeAttach_    = std::make_unique<SliderAttachment>(apvts, ids::kGlideTime,       glideTime_);
    glideModeAttach_    = std::make_unique<ComboBoxAttachment>(apvts, ids::kGlideMode,     glideMode_);
    bendRangeVcoAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kModBendRangeVco, bendRangeVco_);
    bendRangeVcfAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kModBendRangeVcf, bendRangeVcf_);
    bendDestAttach_     = std::make_unique<ComboBoxAttachment>(apvts, ids::kModBendDest,    bendDest_);
    modWheelAttach_     = std::make_unique<SliderAttachment>(apvts, ids::kModLfoModWheel,   modWheel_);

    // Wire each rotary's value read-out to its bound parameter's display string so the
    // text is parameter-derived, never hard-coded [§6.3; ADR-008 C4].
    if (auto* p = apvts.getParameter(ids::kGlideTime))       glideTime_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kModBendRangeVco)) bendRangeVco_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kModBendRangeVcf)) bendRangeVcf_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kModLfoModWheel))  modWheel_.attachParameterForDisplay(*p);
}

// Out-of-line dtor: the unique_ptr<…Attachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
ControllerStrip::~ControllerStrip() = default;

void ControllerStrip::setTokens(const DesignTokens& tokens)
{
    // Forward the table so a reskin re-tints any extension fence with no code change
    // [§6.1; ADR-015 C10]. Neither choice currently carries an extension entry, but the
    // forward keeps the strip consistent with the other modules.
    glideMode_.setTokens(tokens);
    bendDest_.setTokens(tokens);
}

void ControllerStrip::layoutDesignUnits(juce::Rectangle<float> designBounds)
{
    // All math below is in DESIGN units (fractions of the supplied rectangle) — no pixel
    // literals [docs/design/10-ui.md §5.3]. The proportions are the (PI) constants in
    // core/calibration/ControllerStripConstants.h.
    auto area = designBounds;

    // A horizontal strip reserves the title cell on the LEFT (vs a panel's top strip).
    area.removeFromLeft(area.getWidth() * cal::kTitleWidthFraction);

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

    placeCell(0, glideTime_,    glideTimeLabel_);
    placeCell(1, glideMode_,    glideModeLabel_);
    placeCell(2, bendRangeVco_, bendRangeVcoLabel_);
    placeCell(3, bendRangeVcf_, bendRangeVcfLabel_);
    placeCell(4, bendDest_,     bendDestLabel_);
    placeCell(5, modWheel_,     modWheelLabel_);
}

void ControllerStrip::resized()
{
    // The parent works in design space; here the strip's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
