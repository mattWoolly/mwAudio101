// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/VcaModule.cpp — implementation of the VCA panel module declared in
// ui/modules/VcaModule.h [docs/design/10-ui.md §5.3, §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached
// by a relative include — no shared CMakeLists edit (mirrors
// plugin/ui/modules/ModulatorModule.cpp).

#include <string_view>

#include "../../../ui/modules/VcaModule.h"

#include "../../../core/calibration/VcaModuleConstants.h"
#include "../../../core/params/ParamIDs.h"    // mw::params::ids — schema-owned ParamId constants
#include "../../../core/params/ParamDefs.h"   // mw::params::kParamDefs — the choice-fence source of truth

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::vca;

// Find the JUCE-free schema entry for a parameter ID. The mode control's label list AND
// its canonical/extension split are read from the schema registry — the module never
// re-mints the fence; it mirrors choiceCount / canonicalChoiceCount [ADR-008 C5/C6].
const mw::params::ParamDef* findDef(const char* id) noexcept
{
    const std::string_view want{ id };
    for (const auto& d : mw::params::kParamDefs)
        if (std::string_view{ d.id } == want)
            return &d;
    return nullptr;
}

// Configure a level/time rotary as a thin numeric control bound for display; the
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

VcaModule::VcaModule(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "VCA")
{
    // --- controls ----------------------------------------------------------------
    initRotary(level_);
    initRotary(attack_);
    initRotary(decay_);
    initRotary(sustain_);
    initRotary(release_);

    addAndMakeVisible(mode_);
    addAndMakeVisible(level_);
    addAndMakeVisible(attack_);
    addAndMakeVisible(decay_);
    addAndMakeVisible(sustain_);
    addAndMakeVisible(release_);

    initCaption(modeLabel_,    "Mode",    *this);
    initCaption(levelLabel_,   "Level",   *this);
    initCaption(attackLabel_,  "Attack",  *this);
    initCaption(decayLabel_,   "Decay",   *this);
    initCaption(sustainLabel_, "Sustain", *this);
    initCaption(releaseLabel_, "Release", *this);

    // --- the VCA-mode choice list (schema-driven, CANONICAL: no software extension) --
    // Populate the selector from the bound parameter's value strings and mirror the
    // schema's canonicalChoiceCount. For kVcaMode {ENV, GATE} canonicalChoiceCount ==
    // choiceCount == 2, so NOTHING is fenced — env/gate select is documented hardware
    // behaviour [docs/design/06 §3.0; ADR-008 C5/C6]. The selector must hold the items
    // BEFORE the ComboBoxAttachment is created so the index<->item mapping is 1:1 with
    // the parameter.
    if (const auto* modeDef = findDef(ids::kVcaMode))
    {
        juce::StringArray labels;
        for (std::uint8_t i = 0; i < modeDef->choiceCount; ++i)
            labels.add(juce::String::fromUTF8(modeDef->choices[i]));

        mode_.setChoices(labels, static_cast<int>(modeDef->canonicalChoiceCount));
    }

    // --- APVTS attachments — the SOLE write path for every control [§8.1; C3] -----
    // Created in construction (message thread); torn down in destruction. The paramID
    // is a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    modeAttach_    = std::make_unique<ComboBoxAttachment>(apvts, ids::kVcaMode,     mode_);
    levelAttach_   = std::make_unique<SliderAttachment>(apvts, ids::kVcaLevel,      level_);
    attackAttach_  = std::make_unique<SliderAttachment>(apvts, ids::kEnvAttack,     attack_);
    decayAttach_   = std::make_unique<SliderAttachment>(apvts, ids::kEnvDecay,      decay_);
    sustainAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kEnvSustain,    sustain_);
    releaseAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kEnvRelease,    release_);

    // Wire each rotary's value read-out to its bound parameter's display string so the
    // text is parameter-derived, never hard-coded [§6.3; ADR-008 C4].
    if (auto* p = apvts.getParameter(ids::kVcaLevel))   level_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kEnvAttack))  attack_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kEnvDecay))   decay_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kEnvSustain)) sustain_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kEnvRelease)) release_.attachParameterForDisplay(*p);
}

// Out-of-line dtor: the unique_ptr<…Attachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
VcaModule::~VcaModule() = default;

void VcaModule::setTokens(const DesignTokens& tokens)
{
    // Forward the single token table to the choice selector so a reskin re-tints with
    // no code change [§6.1; ADR-015 C10]. (VCA Mode has no fenced extension, but the
    // seam is kept uniform with the other modules.)
    mode_.setTokens(tokens);
}

void VcaModule::layoutDesignUnits(juce::Rectangle<float> designBounds)
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

    placeCell(0, mode_,    modeLabel_);
    placeCell(1, level_,   levelLabel_);
    placeCell(2, attack_,  attackLabel_);
    placeCell(3, decay_,   decayLabel_);
    placeCell(4, sustain_, sustainLabel_);
    placeCell(5, release_, releaseLabel_);
}

void VcaModule::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
