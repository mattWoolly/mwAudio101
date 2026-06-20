// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/VcoModuleTest.cpp — JUCE-linked acceptance tests for the VCO panel
// module (task 120) [docs/design/10-ui.md §5.3, §8.1; ADR-015 C3; ADR-008 §7/C6/C15].
//
// Test-case display names begin with the task tag `ui_vco` so `ctest -R ui_vco`
// selects exactly these cases (silent-pass rule, AGENTS.md).
//
// They run headless on the message thread against a REAL APVTS built from the shipping
// §4 layout (buildParameterLayout) — the same construction the production processor
// uses — and assert BEHAVIOUR, never pixel equality:
//
//   [A] Every control binds via an APVTS attachment using a schema ParamId — a control
//       move writes the bound APVTS parameter, and a host-side parameter move drives
//       the control (§8.1, ADR-015 C3). No direct DSP call.
//   [B] The 32' / 64' VCO registers are visually fenced as software extensions (§5.3,
//       ADR-008 §7/C6/C15) and mirror the schema's canonical/extension split; the fully
//       canonical Sub Mode list carries NO fence.
//   [C] layoutDesignUnits positions children inside the supplied design rectangle, in
//       design units only — bounds scale with the rectangle and never escape it (§5.3).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/ModuleBase.h"
#include "../../ui/modules/VcoModule.h"
#include "../../ui/DesignTokens.h"

#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamIDs.h"         // mw::params::ids::*
#include "params/ParamDefs.h"        // mw::params::kParamDefs (schema fence source)

using mw::ui::VcoModule;
using mw::ui::DesignTokens;
namespace ids = mw::params::ids;

namespace {

// A minimal headless AudioProcessor hosting an APVTS built from the real plugin layout
// — the same construction the shipping MwAudioProcessor uses — so the module's
// attachments bind against genuine juce::AudioProcessorParameter atomics.
class HostProcessor final : public juce::AudioProcessor
{
public:
    HostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override         { return "VcoHost"; }
    void prepareToPlay(double, int) override            {}
    void releaseResources() override                    {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram(int) override                {}
    const juce::String getProgramName(int) override     { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

const mw::params::ParamDef* findDef(const char* id) noexcept
{
    const std::string_view want{ id };
    for (const auto& d : mw::params::kParamDefs)
        if (std::string_view{ d.id } == want)
            return &d;
    return nullptr;
}

// Flush pending async updates (the JUCE parameter-attachment host->control path posts
// to a private AsyncUpdater) without a modal loop — JUCE_MODAL_LOOPS_PERMITTED is 0 in
// this headless target, so runDispatchLoopUntil() is unavailable. Instead we post a
// stopDispatchLoop() AFTER the attachment's already-queued update message, then run the
// (non-modal) dispatch loop once: it processes the queued update, then the stop, and
// returns. This is the supported headless flush (mirrors ModulatorModuleTest.cpp).
void flushMessageQueue()
{
    auto* mm = juce::MessageManager::getInstance();
    mm->callAsync([mm] { mm->stopDispatchLoop(); });
    mm->runDispatchLoop();
}

} // namespace

// ---------------------------------------------------------------------------
// ModuleBase exposes the shared APVTS reference + the title; VcoModule IS a ModuleBase
// and reports its title.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco ModuleBase shares the same APVTS reference and a module title", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);

    // The base holds the SAME juce::AudioProcessorValueTreeState instance, not a copy —
    // the editor's single write/read surface (§5.3, §8.1).
    mw::ui::ModuleBase& base = vco;
    REQUIRE(&base.valueTreeState() == &host.apvts);
    REQUIRE(base.title() == juce::String("VCO"));
}

// ---------------------------------------------------------------------------
// [A] Every control binds to its correct schema ParamId — getParameter(id) != nullptr
// for each bound ID, and each control is wired to a live parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco every control binds to a live schema parameter id", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);

    // The full §5.3 VCO row: range, tune, fine, PW, PWM depth, sub mode, noise.
    for (const char* id : { ids::kVcoRange, ids::kVcoTune, ids::kVcoFine, ids::kVcoPw,
                            ids::kVcoPwmDepth, ids::kSubMode, ids::kNoiseLevel })
        REQUIRE(host.apvts.getParameter(id) != nullptr);
}

// ---------------------------------------------------------------------------
// [A] Control -> APVTS write path: moving each control writes the bound parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco each control writes its bound APVTS parameter on a user move", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);

    auto* range    = host.apvts.getParameter(ids::kVcoRange);
    auto* tune     = host.apvts.getParameter(ids::kVcoTune);
    auto* fine     = host.apvts.getParameter(ids::kVcoFine);
    auto* pw       = host.apvts.getParameter(ids::kVcoPw);
    auto* pwmDepth = host.apvts.getParameter(ids::kVcoPwmDepth);
    auto* subMode  = host.apvts.getParameter(ids::kSubMode);
    auto* noise    = host.apvts.getParameter(ids::kNoiseLevel);
    REQUIRE(range    != nullptr);
    REQUIRE(tune     != nullptr);
    REQUIRE(fine     != nullptr);
    REQUIRE(pw       != nullptr);
    REQUIRE(pwmDepth != nullptr);
    REQUIRE(subMode  != nullptr);
    REQUIRE(noise    != nullptr);

    // Continuous controls: drive each slider to its range maximum and assert the bound
    // parameter reads (normalized) 1.0. The slider's value is in modeled units; the
    // attachment maps it to the parameter's normalized atomic.
    const auto* tuneDef = findDef(ids::kVcoTune);
    REQUIRE(tuneDef != nullptr);
    vco.tuneSlider().setValue(tuneDef->maxValue, juce::sendNotificationSync);
    REQUIRE(tune->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    const auto* fineDef = findDef(ids::kVcoFine);
    REQUIRE(fineDef != nullptr);
    vco.fineSlider().setValue(fineDef->maxValue, juce::sendNotificationSync);
    REQUIRE(fine->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    vco.pulseWidthSlider().setValue(1.0, juce::sendNotificationSync);
    REQUIRE(pw->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    vco.pwmDepthSlider().setValue(1.0, juce::sendNotificationSync);
    REQUIRE(pwmDepth->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    vco.noiseSlider().setValue(1.0, juce::sendNotificationSync);
    REQUIRE(noise->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    // Choice control (range): selecting item id N (1-based) selects choice index N-1;
    // assert the bound choice parameter lands on that index. Pick index 2 ("4'").
    vco.rangeSelector().setSelectedId(3, juce::sendNotificationSync);   // id 3 == index 2
    auto* rangeChoice = dynamic_cast<juce::AudioParameterChoice*>(range);
    REQUIRE(rangeChoice != nullptr);
    REQUIRE(rangeChoice->getIndex() == 2);

    // Choice control (sub mode): pick index 1 ("-2 Oct Sq").
    vco.subModeSelector().setSelectedId(2, juce::sendNotificationSync);  // id 2 == index 1
    auto* subChoice = dynamic_cast<juce::AudioParameterChoice*>(subMode);
    REQUIRE(subChoice != nullptr);
    REQUIRE(subChoice->getIndex() == 1);
}

// ---------------------------------------------------------------------------
// [A] Host -> control read path: a host-side parameter move drives the control.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco a host-side parameter move drives the bound control", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);

    auto* tune  = host.apvts.getParameter(ids::kVcoTune);
    auto* range = host.apvts.getParameter(ids::kVcoRange);
    REQUIRE(tune  != nullptr);
    REQUIRE(range != nullptr);

    // Drive the tune parameter from the "host" (normalized 0..1) and assert the slider
    // moved to the matching modeled value. Attachments post async, so flush the message
    // queue before reading the control.
    tune->setValueNotifyingHost(0.0f);
    flushMessageQueue();
    const auto* tuneDef = findDef(ids::kVcoTune);
    REQUIRE(tuneDef != nullptr);
    REQUIRE(vco.tuneSlider().getValue() == Catch::Approx(tuneDef->minValue).margin(1.0e-3));

    // Drive the range choice from the host to the LAST index (64' — a fenced extension)
    // and assert the selector followed (1-based item id == index + 1).
    auto* rangeChoice = dynamic_cast<juce::AudioParameterChoice*>(range);
    REQUIRE(rangeChoice != nullptr);
    const int lastIndex = rangeChoice->choices.size() - 1;
    range->setValueNotifyingHost(rangeChoice->convertTo0to1(static_cast<float>(lastIndex)));
    flushMessageQueue();
    REQUIRE(vco.rangeSelector().getSelectedId() == lastIndex + 1);
}

// ---------------------------------------------------------------------------
// [B] The 32' / 64' VCO registers are fenced as software extensions and mirror the
// schema; Sub Mode (fully canonical) carries no fence.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco the 32 and 64 foot VCO registers are fenced as software extensions", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);
    vco.setTokens(DesignTokens::defaultTheme());

    auto& selector = vco.rangeSelector();

    // The selector mirrors the schema's choiceCount / canonicalChoiceCount split.
    const auto* rangeDef = findDef(ids::kVcoRange);
    REQUIRE(rangeDef != nullptr);
    REQUIRE(selector.getNumItems() == static_cast<int>(rangeDef->choiceCount));
    REQUIRE(selector.canonicalCount() == static_cast<int>(rangeDef->canonicalChoiceCount));

    // Every canonical (hardware) register (16'/8'/4'/2') is NOT fenced; the trailing
    // 32' / 64' registers ARE.
    const int canonical = static_cast<int>(rangeDef->canonicalChoiceCount);
    for (int i = 0; i < canonical; ++i)
        REQUIRE_FALSE(selector.isExtensionIndex(i));

    for (int i = canonical; i < static_cast<int>(rangeDef->choiceCount); ++i)
        REQUIRE(selector.isExtensionIndex(i));   // 32' and 64' are fenced

    // The schema itself flags the range param as a software-extension carrier, and the
    // appended labels are the two extended registers.
    REQUIRE(rangeDef->isSoftwareExt);
    REQUIRE(juce::String::fromUTF8(rangeDef->choices[4]) == juce::String("32'"));
    REQUIRE(juce::String::fromUTF8(rangeDef->choices[5]) == juce::String("64'"));

    // Fence: the extension rows are tinted with the design-token extensionTag colour,
    // and a token swap re-tints them with no code change (ADR-015 C10).
    const auto def = DesignTokens::defaultTheme();
    REQUIRE(selector.extensionTagColour() == juce::Colour(def.extensionTag.argb));

    auto* root = selector.getRootMenu();
    REQUIRE(root != nullptr);
    int extTinted = 0;
    for (juce::PopupMenu::MenuItemIterator it(*root); it.next();)
    {
        const auto& item = it.getItem();
        if (item.itemID - 1 >= canonical)
        {
            REQUIRE(item.colour == juce::Colour(def.extensionTag.argb));
            ++extTinted;
        }
    }
    REQUIRE(extTinted == static_cast<int>(rangeDef->choiceCount) - canonical);
}

// ---------------------------------------------------------------------------
// [B] Sub Mode is fully hardware-canonical — no entry is fenced.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco the Sub Mode selector is fully canonical with no extension fence", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);

    auto& selector = vco.subModeSelector();

    const auto* subDef = findDef(ids::kSubMode);
    REQUIRE(subDef != nullptr);
    REQUIRE(selector.getNumItems() == static_cast<int>(subDef->choiceCount));
    REQUIRE(selector.canonicalCount() == static_cast<int>(subDef->canonicalChoiceCount));

    // choiceCount == canonicalChoiceCount, so no index is an extension.
    for (int i = 0; i < static_cast<int>(subDef->choiceCount); ++i)
        REQUIRE_FALSE(selector.isExtensionIndex(i));

    REQUIRE_FALSE(subDef->isSoftwareExt);
}

// ---------------------------------------------------------------------------
// [C] layoutDesignUnits positions children inside the supplied design rectangle, in
// design units only (proportional — bounds scale with the rectangle, never escape it).
// ---------------------------------------------------------------------------
TEST_CASE("ui_vco layoutDesignUnits keeps children inside the design rectangle and scales with it", "[ui_vco]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcoModule vco(host.apvts);

    auto& range    = vco.rangeSelector();
    auto& tune     = vco.tuneSlider();
    auto& fine     = vco.fineSlider();
    auto& pw       = vco.pulseWidthSlider();
    auto& pwmDepth = vco.pwmDepthSlider();
    auto& subMode  = vco.subModeSelector();
    auto& noise    = vco.noiseSlider();

    // Lay out into a design rectangle. Children must each receive non-empty bounds that
    // sit WITHIN that rectangle (no escape, no pixel literals leaking out).
    const juce::Rectangle<int> design{ 0, 0, 700, 200 };
    vco.setBounds(design);   // resized() -> layoutDesignUnits(getLocalBounds())

    for (auto* c : { static_cast<juce::Component*>(&range),
                     static_cast<juce::Component*>(&tune),
                     static_cast<juce::Component*>(&fine),
                     static_cast<juce::Component*>(&pw),
                     static_cast<juce::Component*>(&pwmDepth),
                     static_cast<juce::Component*>(&subMode),
                     static_cast<juce::Component*>(&noise) })
    {
        REQUIRE_FALSE(c->getBounds().isEmpty());
        REQUIRE(design.contains(c->getBounds()));
    }

    // Controls are laid left-to-right in design order: range, tune, fine, pw, pwmDepth,
    // subMode, noise. Their x-origins are strictly increasing (a single proportional row).
    REQUIRE(range.getX()    < tune.getX());
    REQUIRE(tune.getX()     < fine.getX());
    REQUIRE(fine.getX()     < pw.getX());
    REQUIRE(pw.getX()       < pwmDepth.getX());
    REQUIRE(pwmDepth.getX() < subMode.getX());
    REQUIRE(subMode.getX()  < noise.getX());

    // Design-unit proportionality: doubling the design rectangle doubles a control's
    // width (within rounding). This proves the layout is fractions of the supplied
    // rectangle, not absolute pixel math (§5.3).
    const int wSmall = tune.getWidth();
    vco.setBounds({ 0, 0, 1400, 400 });
    const int wLarge = tune.getWidth();
    REQUIRE(wLarge > wSmall);
    REQUIRE(static_cast<double>(wLarge) == Catch::Approx(2.0 * wSmall).margin(2.0));
}
