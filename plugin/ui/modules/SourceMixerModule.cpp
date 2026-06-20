// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/SourceMixerModule.cpp — implementation of the SOURCE MIXER panel
// module declared in ui/modules/SourceMixerModule.h [docs/design/10-ui.md §5.3, §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached by
// a relative include — no shared CMakeLists edit (mirrors plugin/ui/modules/
// ModulatorModule.cpp).

#include "../../../ui/modules/SourceMixerModule.h"

#include "../../../core/calibration/SourceMixerModuleConstants.h"
#include "../../../core/params/ParamIDs.h"   // mw::params::ids — schema-owned ParamId constants

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::source_mixer;

// Configure a level fader as a vertical fader with no inline text box (the value text is
// derived from the bound parameter; the caption is owned by the module's Label).
void initFader(LinearSlider& s)
{
    s.setSliderStyle(juce::Slider::LinearVertical);
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

SourceMixerModule::SourceMixerModule(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "SOURCE MIXER")
{
    // --- controls ----------------------------------------------------------------
    initFader(sawLevel_);
    initFader(pulseLevel_);
    initFader(subLevel_);
    initFader(noiseLevel_);

    addAndMakeVisible(sawLevel_);
    addAndMakeVisible(pulseLevel_);
    addAndMakeVisible(subLevel_);
    addAndMakeVisible(noiseLevel_);

    initCaption(sawLevelLabel_,   "Saw",   *this);
    initCaption(pulseLevelLabel_, "Pulse", *this);
    initCaption(subLevelLabel_,   "Sub",   *this);
    initCaption(noiseLevelLabel_, "Noise", *this);

    // --- APVTS attachments — the SOLE write path for every control [§8.1; C3] -----
    // Created in construction (message thread); torn down in destruction. The paramID is
    // a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    sawLevelAttach_   = std::make_unique<SliderAttachment>(apvts, ids::kSawLevel,   sawLevel_);
    pulseLevelAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kPulseLevel, pulseLevel_);
    subLevelAttach_   = std::make_unique<SliderAttachment>(apvts, ids::kSubLevel,   subLevel_);
    noiseLevelAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kNoiseLevel, noiseLevel_);

    // Wire each fader's value read-out to its bound parameter's display string so the
    // text is parameter-derived, never hard-coded [§6.3; ADR-008 C4].
    if (auto* p = apvts.getParameter(ids::kSawLevel))   sawLevel_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kPulseLevel)) pulseLevel_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kSubLevel))   subLevel_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kNoiseLevel)) noiseLevel_.attachParameterForDisplay(*p);
}

// Out-of-line dtor: the unique_ptr<SliderAttachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
SourceMixerModule::~SourceMixerModule() = default;

void SourceMixerModule::layoutDesignUnits(juce::Rectangle<float> designBounds)
{
    // All math below is in DESIGN units (fractions of the supplied rectangle) — no pixel
    // literals [docs/design/10-ui.md §5.3]. The proportions are (PI) constants.
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

    // Place each fader into the top of its cell and its caption into the bottom.
    auto placeCell = [&](int index, juce::Component& control, juce::Component& caption)
    {
        const float x = area.getX() + static_cast<float>(index) * (cellW + gap);
        juce::Rectangle<float> cell{ x, area.getY(), cellW, area.getHeight() };

        auto captionArea = cell.removeFromBottom(cell.getHeight() * cal::kCaptionHeightFraction);
        control.setBounds(cell.toNearestInt());
        caption.setBounds(captionArea.toNearestInt());
    };

    placeCell(0, sawLevel_,   sawLevelLabel_);
    placeCell(1, pulseLevel_, pulseLevelLabel_);
    placeCell(2, subLevel_,   subLevelLabel_);
    placeCell(3, noiseLevel_, noiseLevelLabel_);
}

void SourceMixerModule::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
