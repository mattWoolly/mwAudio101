// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/modules/VcfModule.cpp — implementation of the VCF panel module declared in
// ui/modules/VcfModule.h [docs/design/10-ui.md §5.3, §8.1].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only
// auto-globs plugin/**/*.cpp into the plugin target + mw101_plugin_tests
// (CONFIGURE_DEPENDS). The design-faithful header stays at ui/modules/ and is reached by
// a relative include — no shared CMakeLists edit (mirrors plugin/ui/modules/
// ModulatorModule.cpp).

#include "../../../ui/modules/VcfModule.h"

#include "../../../core/calibration/VcfModuleConstants.h"
#include "../../../core/params/ParamIDs.h"   // mw::params::ids — schema-owned ParamId constants

namespace mw::ui {

namespace {

namespace ids = mw::params::ids;
namespace cal = mw::cal::ui::vcf;

// Configure a rotary as a thin numeric control bound for display; the caption is owned
// by the module's Label (set in the ctor).
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

VcfModule::VcfModule(juce::AudioProcessorValueTreeState& state)
    : ModuleBase(state, "VCF")
{
    // --- controls ----------------------------------------------------------------
    initRotary(cutoff_);
    initRotary(resonance_);
    initRotary(envMod_);
    initRotary(kbdTrack_);
    initRotary(lfoMod_);

    addAndMakeVisible(cutoff_);
    addAndMakeVisible(resonance_);
    addAndMakeVisible(envMod_);
    addAndMakeVisible(kbdTrack_);
    addAndMakeVisible(lfoMod_);

    initCaption(cutoffLabel_,    "Cutoff",    *this);
    initCaption(resonanceLabel_, "Resonance", *this);
    initCaption(envModLabel_,    "Env",       *this);
    initCaption(kbdTrackLabel_,  "Kbd",       *this);
    initCaption(lfoModLabel_,    "Mod",       *this);

    // --- APVTS attachments — the SOLE write path for every control [§8.1; C3] -----
    // Created in construction (message thread); torn down in destruction. The paramID is
    // a schema-owned constant from ParamIDs.h — never a raw "mw101.*" literal.
    cutoffAttach_    = std::make_unique<SliderAttachment>(apvts, ids::kVcfCutoff,    cutoff_);
    resonanceAttach_ = std::make_unique<SliderAttachment>(apvts, ids::kVcfResonance, resonance_);
    envModAttach_    = std::make_unique<SliderAttachment>(apvts, ids::kVcfEnvMod,    envMod_);
    kbdTrackAttach_  = std::make_unique<SliderAttachment>(apvts, ids::kVcfKbdTrack,  kbdTrack_);
    lfoModAttach_    = std::make_unique<SliderAttachment>(apvts, ids::kVcfLfoMod,    lfoMod_);

    // Wire each rotary's value read-out to its bound parameter's display string so the
    // text is parameter-derived, never hard-coded [§6.3; ADR-008 C4].
    if (auto* p = apvts.getParameter(ids::kVcfCutoff))    cutoff_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcfResonance)) resonance_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcfEnvMod))    envMod_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcfKbdTrack))  kbdTrack_.attachParameterForDisplay(*p);
    if (auto* p = apvts.getParameter(ids::kVcfLfoMod))    lfoMod_.attachParameterForDisplay(*p);
}

// Out-of-line dtor: the unique_ptr<…Attachment> members are complete here, and the
// attachments tear down (off any hot path) before the controls they reference.
VcfModule::~VcfModule() = default;

void VcfModule::layoutDesignUnits(juce::Rectangle<float> designBounds)
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

    // Place each control into the top of its cell and its caption into the bottom.
    auto placeCell = [&](int index, juce::Component& control, juce::Component& caption)
    {
        const float x = area.getX() + static_cast<float>(index) * (cellW + gap);
        juce::Rectangle<float> cell{ x, area.getY(), cellW, area.getHeight() };

        auto captionArea = cell.removeFromBottom(cell.getHeight() * cal::kCaptionHeightFraction);
        control.setBounds(cell.toNearestInt());
        caption.setBounds(captionArea.toNearestInt());
    };

    placeCell(0, cutoff_,    cutoffLabel_);
    placeCell(1, resonance_, resonanceLabel_);
    placeCell(2, envMod_,    envModLabel_);
    placeCell(3, kbdTrack_,  kbdTrackLabel_);
    placeCell(4, lfoMod_,    lfoModLabel_);
}

void VcfModule::resized()
{
    // The parent works in design space; here the module's own integer bounds ARE the
    // design rectangle, so forward them straight into the design-unit layout.
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
