// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/ModulatorModuleTest.cpp — JUCE-linked acceptance tests for the
// MODULATOR panel module and the shared ModuleBase (task 117) [docs/design/10-ui.md
// §5.3, §8.1; ADR-015 C3; ADR-008 §7/C6/C15].
//
// Test-case display names begin with the task tag `ui_modulator` so
// `ctest -R ui_modulator` selects exactly these cases (silent-pass rule, AGENTS.md).
//
// They run headless on the message thread against a REAL APVTS built from the shipping
// §4 layout (buildParameterLayout) — the same construction the production processor
// uses — and assert BEHAVIOUR, never pixel equality:
//
//   [A] Every control binds via an APVTS attachment using a schema ParamId — a control
//       move writes the bound APVTS parameter, and a host-side parameter move drives
//       the control (§8.1, ADR-015 C3). No direct DSP call.
//   [B] The Sine LFO shape is visually fenced as a software extension (§5.3, ADR-008
//       §7/C6/C15) and mirrors the schema's canonical/extension split.
//   [C] layoutDesignUnits positions children inside the supplied design rectangle, in
//       design units only — bounds scale with the rectangle and never escape it (§5.3).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/ModuleBase.h"
#include "../../ui/modules/ModulatorModule.h"
#include "../../ui/DesignTokens.h"

#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamIDs.h"         // mw::params::ids::*
#include "params/ParamDefs.h"        // mw::params::kParamDefs (schema fence source)

using mw::ui::ModulatorModule;
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

    const juce::String getName() const override         { return "ModulatorHost"; }
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
// returns. This is the supported headless flush.
void flushMessageQueue()
{
    auto* mm = juce::MessageManager::getInstance();
    mm->callAsync([mm] { mm->stopDispatchLoop(); });
    mm->runDispatchLoop();
}

} // namespace

// ---------------------------------------------------------------------------
// ModuleBase exposes the shared APVTS reference + the title; ModulatorModule IS a
// ModuleBase and reports its title.
// ---------------------------------------------------------------------------
TEST_CASE("ui_modulator ModuleBase shares the same APVTS reference and a module title", "[ui_modulator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ModulatorModule mod(host.apvts);

    // The base holds the SAME juce::AudioProcessorValueTreeState instance, not a copy —
    // the editor's single write/read surface (§5.3, §8.1).
    mw::ui::ModuleBase& base = mod;
    REQUIRE(&base.valueTreeState() == &host.apvts);
    REQUIRE(base.title() == juce::String("MODULATOR"));
}

// ---------------------------------------------------------------------------
// [A] Control -> APVTS write path: moving each control writes the bound parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_modulator each control writes its bound APVTS parameter on a user move", "[ui_modulator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ModulatorModule mod(host.apvts);

    auto* rate   = host.apvts.getParameter(ids::kLfoRate);
    auto* shape  = host.apvts.getParameter(ids::kLfoShape);
    auto* pitch  = host.apvts.getParameter(ids::kLfoDepthPitch);
    auto* pwm    = host.apvts.getParameter(ids::kLfoDepthPwm);
    auto* cutoff = host.apvts.getParameter(ids::kLfoDepthCutoff);
    REQUIRE(rate   != nullptr);
    REQUIRE(shape  != nullptr);
    REQUIRE(pitch  != nullptr);
    REQUIRE(pwm    != nullptr);
    REQUIRE(cutoff != nullptr);

    // Continuous controls: drive each slider to its range maximum and assert the bound
    // parameter reads (normalized) 1.0. The slider's value is in modeled units; the
    // attachment maps it to the parameter's normalized atomic.
    const auto* rateDef = findDef(ids::kLfoRate);
    REQUIRE(rateDef != nullptr);
    mod.lfoRateSlider().setValue(rateDef->maxValue, juce::sendNotificationSync);
    REQUIRE(rate->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    mod.pitchDepthSlider().setValue(1.0, juce::sendNotificationSync);
    REQUIRE(pitch->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    mod.pwmDepthSlider().setValue(1.0, juce::sendNotificationSync);
    REQUIRE(pwm->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    mod.cutoffDepthSlider().setValue(1.0, juce::sendNotificationSync);
    REQUIRE(cutoff->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

    // Choice control: selecting item id N (1-based) selects choice index N-1; assert the
    // bound choice parameter lands on that index. Pick index 2 ("Random" — the S&H shape).
    mod.lfoShapeSelector().setSelectedId(3, juce::sendNotificationSync);   // id 3 == index 2
    auto* shapeChoice = dynamic_cast<juce::AudioParameterChoice*>(shape);
    REQUIRE(shapeChoice != nullptr);
    REQUIRE(shapeChoice->getIndex() == 2);
}

// ---------------------------------------------------------------------------
// [A] Host -> control read path: a host-side parameter move drives the control.
// ---------------------------------------------------------------------------
TEST_CASE("ui_modulator a host-side parameter move drives the bound control", "[ui_modulator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ModulatorModule mod(host.apvts);

    auto* rate  = host.apvts.getParameter(ids::kLfoRate);
    auto* shape = host.apvts.getParameter(ids::kLfoShape);
    REQUIRE(rate  != nullptr);
    REQUIRE(shape != nullptr);

    // Drive the rate parameter from the "host" (normalized 0..1) and assert the slider
    // moved to the matching modeled value. Attachments post async, so flush the message
    // queue before reading the control.
    rate->setValueNotifyingHost(0.0f);
    flushMessageQueue();
    const auto* rateDef = findDef(ids::kLfoRate);
    REQUIRE(rateDef != nullptr);
    REQUIRE(mod.lfoRateSlider().getValue() == Catch::Approx(rateDef->minValue).margin(1.0e-3));

    // Drive the shape choice from the host to the LAST index (Sine) and assert the
    // selector followed (1-based item id == index + 1).
    auto* shapeChoice = dynamic_cast<juce::AudioParameterChoice*>(shape);
    REQUIRE(shapeChoice != nullptr);
    const int lastIndex = shapeChoice->choices.size() - 1;
    shape->setValueNotifyingHost(shapeChoice->convertTo0to1(static_cast<float>(lastIndex)));
    flushMessageQueue();
    REQUIRE(mod.lfoShapeSelector().getSelectedId() == lastIndex + 1);
}

// ---------------------------------------------------------------------------
// [B] The Sine LFO shape is fenced as a software extension and mirrors the schema.
// ---------------------------------------------------------------------------
TEST_CASE("ui_modulator the Sine LFO shape is fenced as a software extension", "[ui_modulator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ModulatorModule mod(host.apvts);
    mod.setTokens(DesignTokens::defaultTheme());

    auto& selector = mod.lfoShapeSelector();

    // The selector mirrors the schema's choiceCount / canonicalChoiceCount split.
    const auto* shapeDef = findDef(ids::kLfoShape);
    REQUIRE(shapeDef != nullptr);
    REQUIRE(selector.getNumItems() == static_cast<int>(shapeDef->choiceCount));
    REQUIRE(selector.canonicalCount() == static_cast<int>(shapeDef->canonicalChoiceCount));

    // Every canonical (hardware) entry is NOT fenced; the trailing Sine entry IS.
    const int canonical = static_cast<int>(shapeDef->canonicalChoiceCount);
    for (int i = 0; i < canonical; ++i)
        REQUIRE_FALSE(selector.isExtensionIndex(i));

    const int sineIndex = static_cast<int>(shapeDef->choiceCount) - 1;
    REQUIRE(selector.isExtensionIndex(sineIndex));   // Sine is the fenced extension

    // The schema itself flags the shape param as a software-extension carrier, and the
    // last label is "Sine".
    REQUIRE(shapeDef->isSoftwareExt);
    REQUIRE(juce::String::fromUTF8(shapeDef->choices[sineIndex]) == juce::String("Sine"));

    // Fence #1: the Sine row is tinted with the design-token extensionTag colour, and a
    // token swap re-tints it with no code change (ADR-015 C10).
    const auto def = DesignTokens::defaultTheme();
    REQUIRE(selector.extensionTagColour() == juce::Colour(def.extensionTag.argb));

    auto* root = selector.getRootMenu();
    REQUIRE(root != nullptr);
    int sineTinted = 0;
    for (juce::PopupMenu::MenuItemIterator it(*root); it.next();)
    {
        const auto& item = it.getItem();
        if (item.itemID - 1 == sineIndex)
        {
            REQUIRE(item.colour == juce::Colour(def.extensionTag.argb));
            ++sineTinted;
        }
    }
    REQUIRE(sineTinted == 1);
}

// ---------------------------------------------------------------------------
// [C] layoutDesignUnits positions children inside the supplied design rectangle, in
// design units only (proportional — bounds scale with the rectangle, never escape it).
// ---------------------------------------------------------------------------
TEST_CASE("ui_modulator layoutDesignUnits keeps children inside the design rectangle and scales with it", "[ui_modulator]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    ModulatorModule mod(host.apvts);

    auto& rate   = mod.lfoRateSlider();
    auto& shape  = mod.lfoShapeSelector();
    auto& pitch  = mod.pitchDepthSlider();
    auto& pwm    = mod.pwmDepthSlider();
    auto& cutoff = mod.cutoffDepthSlider();

    // Lay out into a design rectangle. Children must each receive non-empty bounds that
    // sit WITHIN that rectangle (no escape, no pixel literals leaking out).
    const juce::Rectangle<int> design{ 0, 0, 600, 200 };
    mod.setBounds(design);   // resized() -> layoutDesignUnits(getLocalBounds())

    for (auto* c : { static_cast<juce::Component*>(&rate),
                     static_cast<juce::Component*>(&shape),
                     static_cast<juce::Component*>(&pitch),
                     static_cast<juce::Component*>(&pwm),
                     static_cast<juce::Component*>(&cutoff) })
    {
        REQUIRE_FALSE(c->getBounds().isEmpty());
        REQUIRE(design.contains(c->getBounds()));
    }

    // Controls are laid left-to-right in design order: rate, shape, then the three
    // depths. Their x-origins are strictly increasing (a single proportional row).
    REQUIRE(rate.getX()  < shape.getX());
    REQUIRE(shape.getX() < pitch.getX());
    REQUIRE(pitch.getX() < pwm.getX());
    REQUIRE(pwm.getX()   < cutoff.getX());

    // Design-unit proportionality: doubling the design rectangle doubles a control's
    // width (within rounding). This proves the layout is fractions of the supplied
    // rectangle, not absolute pixel math (§5.3).
    const int wSmall = rate.getWidth();
    mod.setBounds({ 0, 0, 1200, 400 });
    const int wLarge = rate.getWidth();
    REQUIRE(wLarge > wSmall);
    REQUIRE(static_cast<double>(wLarge) == Catch::Approx(2.0 * wSmall).margin(2.0));
}
