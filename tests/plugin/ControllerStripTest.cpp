// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ControllerStripTest.cpp — JUCE-linked acceptance tests for the CONTROLLER
// strip module ui/modules/ControllerStrip (task 124) [docs/design/10-ui.md §5.3, §8.1;
// ADR-015 C3/C4; ADR-008 §7/C5/C6/C15].
//
// Test-case display names begin with the task tag `ui_controller` so
// `ctest -R ui_controller` selects exactly these cases (silent-pass rule, AGENTS.md).
//
// They run headless on the message thread against a REAL APVTS built from the shipping
// §4 layout (buildParameterLayout) — the same construction the production processor
// uses — and assert BEHAVIOUR, never pixel equality:
//
//   [A] Every control binds via an APVTS attachment using a schema ParamId — a control
//       move writes the bound APVTS parameter, and a host-side parameter move drives the
//       control (§8.1, §8.2; ADR-015 C3/C4). No direct DSP call (the only write surface
//       exercised is the APVTS).
//   [B] The two choice controls (Glide Mode, Bend Dest) mirror the schema's
//       canonical/extension split; both are wholly hardware-canonical, so NO index is
//       fenced as a software extension (sound_ext) [ADR-008 C5/C6].
//   [C] layoutDesignUnits positions children inside the supplied design rectangle, in
//       design units only — bounds scale with the rectangle and never escape it (§5.3).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/ModuleBase.h"
#include "../../ui/modules/ControllerStrip.h"
#include "../../ui/DesignTokens.h"

#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamIDs.h"         // mw::params::ids::*
#include "params/ParamDefs.h"        // mw::params::kParamDefs (schema fence source)

using mw::ui::ControllerStrip;
using mw::ui::DesignTokens;
namespace ids = mw::params::ids;

namespace {

// A minimal headless AudioProcessor hosting an APVTS built from the real plugin layout
// — the same construction the shipping MwAudioProcessor uses — so the strip's
// attachments bind against genuine juce::AudioProcessorParameter atomics.
class HostProcessor final : public juce::AudioProcessor
{
public:
    HostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override         { return "ControllerStripHost"; }
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

// Flush pending async updates (the JUCE parameter-attachment host->control path posts to
// a private AsyncUpdater) without a modal loop — JUCE_MODAL_LOOPS_PERMITTED is 0 in this
// headless target, so runDispatchLoopUntil() is unavailable. Post a stopDispatchLoop()
// AFTER the attachment's already-queued update message, then run the (non-modal) dispatch
// loop once: it processes the queued update, then the stop, and returns. This is the
// supported headless flush (mirrors tests/plugin/ModulatorModuleTest.cpp).
void flushMessageQueue()
{
    auto* mm = juce::MessageManager::getInstance();
    mm->callAsync([mm] { mm->stopDispatchLoop(); });
    mm->runDispatchLoop();
}

} // namespace

// ---------------------------------------------------------------------------
// ControllerStrip IS a ModuleBase: it shares the same APVTS reference and reports the
// CONTROLLER title (the strip's single write/read surface — §5.3, §8.1).
// ---------------------------------------------------------------------------
TEST_CASE("ui_controller ModuleBase shares the same APVTS reference and a strip title", "[ui_controller]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ControllerStrip strip(host.apvts);

    mw::ui::ModuleBase& base = strip;
    REQUIRE(&base.valueTreeState() == &host.apvts);
    REQUIRE(base.title() == juce::String("CONTROLLER"));
}

// ---------------------------------------------------------------------------
// [A] Every bound parameter exists in the production layout (the strip never hard-codes
// a raw "mw101.*" literal — it uses the ids:: constants).
// ---------------------------------------------------------------------------
TEST_CASE("ui_controller binds every control to a schema parameter that exists", "[ui_controller]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ControllerStrip strip(host.apvts);

    REQUIRE(host.apvts.getParameter(ids::kGlideTime)       != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kGlideMode)       != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kModBendRangeVco) != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kModBendRangeVcf) != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kModBendDest)     != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kModLfoModWheel)  != nullptr);
}

// ---------------------------------------------------------------------------
// [A] Control -> APVTS write path: moving each control writes the bound parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controller each control writes its bound APVTS parameter on a user move", "[ui_controller]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ControllerStrip strip(host.apvts);

    auto* glideTime = host.apvts.getParameter(ids::kGlideTime);
    auto* glideMode = host.apvts.getParameter(ids::kGlideMode);
    auto* bendVco   = host.apvts.getParameter(ids::kModBendRangeVco);
    auto* bendVcf   = host.apvts.getParameter(ids::kModBendRangeVcf);
    auto* bendDest  = host.apvts.getParameter(ids::kModBendDest);
    auto* modWheel  = host.apvts.getParameter(ids::kModLfoModWheel);
    REQUIRE(glideTime != nullptr);
    REQUIRE(glideMode != nullptr);
    REQUIRE(bendVco   != nullptr);
    REQUIRE(bendVcf   != nullptr);
    REQUIRE(bendDest  != nullptr);
    REQUIRE(modWheel  != nullptr);

    // Continuous controls: drive each slider to its range maximum and assert the bound
    // parameter reads (normalized) 1.0. The slider's value is in modeled units; the
    // attachment maps it to the parameter's normalized atomic.
    const auto* glideDef = findDef(ids::kGlideTime);
    const auto* vcoDef   = findDef(ids::kModBendRangeVco);
    const auto* vcfDef   = findDef(ids::kModBendRangeVcf);
    const auto* mwDef     = findDef(ids::kModLfoModWheel);
    REQUIRE(glideDef != nullptr);
    REQUIRE(vcoDef   != nullptr);
    REQUIRE(vcfDef   != nullptr);
    REQUIRE(mwDef     != nullptr);

    strip.glideTimeSlider().setValue(glideDef->maxValue, juce::sendNotificationSync);
    REQUIRE(glideTime->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    strip.bendRangeVcoSlider().setValue(vcoDef->maxValue, juce::sendNotificationSync);
    REQUIRE(bendVco->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    strip.bendRangeVcfSlider().setValue(vcfDef->maxValue, juce::sendNotificationSync);
    REQUIRE(bendVcf->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    strip.modWheelSlider().setValue(mwDef->maxValue, juce::sendNotificationSync);
    REQUIRE(modWheel->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    // Choice controls: selecting item id N (1-based) selects choice index N-1; assert the
    // bound choice parameter lands on that index.
    strip.glideModeSelector().setSelectedId(3, juce::sendNotificationSync);  // id 3 == index 2 (On)
    auto* glideModeChoice = dynamic_cast<juce::AudioParameterChoice*>(glideMode);
    REQUIRE(glideModeChoice != nullptr);
    REQUIRE(glideModeChoice->getIndex() == 2);

    strip.bendDestSelector().setSelectedId(3, juce::sendNotificationSync);   // id 3 == index 2 (Both)
    auto* bendDestChoice = dynamic_cast<juce::AudioParameterChoice*>(bendDest);
    REQUIRE(bendDestChoice != nullptr);
    REQUIRE(bendDestChoice->getIndex() == 2);
}

// ---------------------------------------------------------------------------
// [A] Host -> control read path: a host-side parameter move drives the control.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controller a host-side parameter move drives the bound control", "[ui_controller]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ControllerStrip strip(host.apvts);

    auto* glideTime = host.apvts.getParameter(ids::kGlideTime);
    auto* modWheel  = host.apvts.getParameter(ids::kModLfoModWheel);
    auto* bendDest  = host.apvts.getParameter(ids::kModBendDest);
    REQUIRE(glideTime != nullptr);
    REQUIRE(modWheel  != nullptr);
    REQUIRE(bendDest  != nullptr);

    // Drive the glide-time parameter from the "host" (normalized 1.0) and assert the
    // slider moved to its range maximum. Attachments post async, so flush first.
    const auto* glideDef = findDef(ids::kGlideTime);
    const auto* mwDef    = findDef(ids::kModLfoModWheel);
    REQUIRE(glideDef != nullptr);
    REQUIRE(mwDef    != nullptr);

    glideTime->setValueNotifyingHost(1.0f);
    flushMessageQueue();
    REQUIRE(strip.glideTimeSlider().getValue() == Catch::Approx(glideDef->maxValue).margin(1.0e-3));

    modWheel->setValueNotifyingHost(1.0f);
    flushMessageQueue();
    REQUIRE(strip.modWheelSlider().getValue() == Catch::Approx(mwDef->maxValue).margin(1.0e-3));

    // Choice: drive the bend-dest parameter from the host to its LAST index and assert the
    // selector followed (1-based item id == index + 1).
    auto* bendDestChoice = dynamic_cast<juce::AudioParameterChoice*>(bendDest);
    REQUIRE(bendDestChoice != nullptr);
    const int lastIndex = bendDestChoice->choices.size() - 1;
    bendDest->setValueNotifyingHost(bendDestChoice->convertTo0to1(static_cast<float>(lastIndex)));
    flushMessageQueue();
    REQUIRE(strip.bendDestSelector().getSelectedId() == lastIndex + 1);
}

// ---------------------------------------------------------------------------
// [B] The choice controls mirror the schema's canonical/extension split; both are
// wholly hardware-canonical, so NO index is fenced as a software extension.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controller choice controls mirror the schema and fence no software extension", "[ui_controller]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ControllerStrip strip(host.apvts);
    strip.setTokens(DesignTokens::defaultTheme());

    // Glide Mode {Off, Auto, On} — choiceCount == canonicalChoiceCount, nothing fenced.
    const auto* glideModeDef = findDef(ids::kGlideMode);
    REQUIRE(glideModeDef != nullptr);
    auto& glideMode = strip.glideModeSelector();
    REQUIRE(glideMode.getNumItems()   == static_cast<int>(glideModeDef->choiceCount));
    REQUIRE(glideMode.canonicalCount() == static_cast<int>(glideModeDef->canonicalChoiceCount));
    REQUIRE_FALSE(glideModeDef->isSoftwareExt);
    for (int i = 0; i < glideMode.getNumItems(); ++i)
        REQUIRE_FALSE(glideMode.isExtensionIndex(i));

    // Bend Dest {VCO, VCF, Both} — choiceCount == canonicalChoiceCount, nothing fenced.
    const auto* bendDestDef = findDef(ids::kModBendDest);
    REQUIRE(bendDestDef != nullptr);
    auto& bendDest = strip.bendDestSelector();
    REQUIRE(bendDest.getNumItems()   == static_cast<int>(bendDestDef->choiceCount));
    REQUIRE(bendDest.canonicalCount() == static_cast<int>(bendDestDef->canonicalChoiceCount));
    REQUIRE_FALSE(bendDestDef->isSoftwareExt);
    for (int i = 0; i < bendDest.getNumItems(); ++i)
        REQUIRE_FALSE(bendDest.isExtensionIndex(i));
}

// ---------------------------------------------------------------------------
// [C] layoutDesignUnits positions children inside the supplied design rectangle, in
// design units only (proportional — bounds scale with the rectangle, never escape it).
// ---------------------------------------------------------------------------
TEST_CASE("ui_controller layoutDesignUnits keeps children inside the design rectangle and scales with it", "[ui_controller]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ControllerStrip strip(host.apvts);

    auto& glideTime = strip.glideTimeSlider();
    auto& glideMode = strip.glideModeSelector();
    auto& bendVco   = strip.bendRangeVcoSlider();
    auto& bendVcf   = strip.bendRangeVcfSlider();
    auto& bendDest  = strip.bendDestSelector();
    auto& modWheel  = strip.modWheelSlider();

    // A horizontal strip-shaped design rectangle (wide, short) per §5.3. Children must
    // each receive non-empty bounds that sit WITHIN that rectangle.
    const juce::Rectangle<int> design{ 0, 0, 900, 90 };
    strip.setBounds(design);   // resized() -> layoutDesignUnits(getLocalBounds())

    for (auto* c : { static_cast<juce::Component*>(&glideTime),
                     static_cast<juce::Component*>(&glideMode),
                     static_cast<juce::Component*>(&bendVco),
                     static_cast<juce::Component*>(&bendVcf),
                     static_cast<juce::Component*>(&bendDest),
                     static_cast<juce::Component*>(&modWheel) })
    {
        REQUIRE_FALSE(c->getBounds().isEmpty());
        REQUIRE(design.contains(c->getBounds()));
    }

    // Controls are laid left-to-right in design order; their x-origins strictly increase.
    REQUIRE(glideTime.getX() < glideMode.getX());
    REQUIRE(glideMode.getX() < bendVco.getX());
    REQUIRE(bendVco.getX()   < bendVcf.getX());
    REQUIRE(bendVcf.getX()   < bendDest.getX());
    REQUIRE(bendDest.getX()  < modWheel.getX());

    // Design-unit proportionality: doubling the design rectangle doubles a control's
    // width (within rounding). This proves the layout is fractions of the supplied
    // rectangle, not absolute pixel math (§5.3).
    const int wSmall = glideTime.getWidth();
    strip.setBounds({ 0, 0, 1800, 180 });
    const int wLarge = glideTime.getWidth();
    REQUIRE(wLarge > wSmall);
    REQUIRE(static_cast<double>(wLarge) == Catch::Approx(2.0 * wSmall).margin(2.0));
}
