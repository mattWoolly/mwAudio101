// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/SourceMixerModuleTest.cpp — JUCE-linked acceptance tests for the SOURCE
// MIXER panel module (task 121) [docs/design/10-ui.md §5.3, §8.1; ADR-015 C3; ADR-008
// §8.1].
//
// Test-case display names begin with the task tag `ui_mixer` so `ctest -R ui_mixer`
// selects exactly these cases (silent-pass rule, AGENTS.md).
//
// They run headless on the message thread against a REAL APVTS built from the shipping
// §4 layout (buildParameterLayout) — the same construction the production processor uses
// — and assert BEHAVIOUR, never pixel equality:
//
//   [A] Every level control binds via an APVTS attachment using a schema ParamId — the
//       bound parameter exists, a control move writes it, and a host-side parameter move
//       drives the control (§8.1, ADR-015 C3). No direct DSP call.
//   [B] layoutDesignUnits positions children inside the supplied design rectangle, in
//       design units only — bounds scale with the rectangle and never escape it (§5.3).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/ModuleBase.h"
#include "../../ui/modules/SourceMixerModule.h"

#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamIDs.h"         // mw::params::ids::*
#include "params/ParamDefs.h"        // mw::params::kParamDefs (schema source of truth)

using mw::ui::SourceMixerModule;
namespace ids = mw::params::ids;

namespace {

// A minimal headless AudioProcessor hosting an APVTS built from the real plugin layout —
// the same construction the shipping MwAudioProcessor uses — so the module's attachments
// bind against genuine juce::AudioProcessorParameter atomics.
class HostProcessor final : public juce::AudioProcessor
{
public:
    HostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override         { return "SourceMixerHost"; }
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
// headless target, so runDispatchLoopUntil() is unavailable. Instead we post a
// stopDispatchLoop() AFTER the attachment's already-queued update message, then run the
// (non-modal) dispatch loop once: it processes the queued update, then the stop, and
// returns. This is the supported headless flush (mirrors ModulatorModuleTest).
void flushMessageQueue()
{
    auto* mm = juce::MessageManager::getInstance();
    mm->callAsync([mm] { mm->stopDispatchLoop(); });
    mm->runDispatchLoop();
}

} // namespace

// ---------------------------------------------------------------------------
// ModuleBase exposes the shared APVTS reference + the title; SourceMixerModule IS a
// ModuleBase and reports its title (§5.3, §8.1).
// ---------------------------------------------------------------------------
TEST_CASE("ui_mixer ModuleBase shares the same APVTS reference and a module title", "[ui_mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    SourceMixerModule mixer(host.apvts);

    // The base holds the SAME juce::AudioProcessorValueTreeState instance, not a copy —
    // the editor's single write/read surface (§5.3, §8.1).
    mw::ui::ModuleBase& base = mixer;
    REQUIRE(&base.valueTreeState() == &host.apvts);
    REQUIRE(base.title() == juce::String("SOURCE MIXER"));
}

// ---------------------------------------------------------------------------
// [A] Every level control binds to the correct schema ParamId — the bound parameter
// exists in the real layout (getParameter(id) != nullptr) (§8.1, ADR-015 C3).
// ---------------------------------------------------------------------------
TEST_CASE("ui_mixer every level control binds to a live schema ParamId", "[ui_mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    SourceMixerModule mixer(host.apvts);

    // Each of the four mixer ids resolves to a live parameter in the shipping layout.
    for (const char* id : { ids::kSawLevel, ids::kPulseLevel, ids::kSubLevel, ids::kNoiseLevel })
        REQUIRE(host.apvts.getParameter(id) != nullptr);

    // And each is a continuous (float) level in ParamGroup::Mixer per the schema — these
    // are level faders, not choices, so there is no software-extension fence here.
    for (const char* id : { ids::kSawLevel, ids::kPulseLevel, ids::kSubLevel, ids::kNoiseLevel })
    {
        const auto* def = findDef(id);
        REQUIRE(def != nullptr);
        REQUIRE(def->group == mw::params::ParamGroup::Mixer);
    }
}

// ---------------------------------------------------------------------------
// [A] Control -> APVTS write path: moving each fader writes the bound parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_mixer each level fader writes its bound APVTS parameter on a user move", "[ui_mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    SourceMixerModule mixer(host.apvts);

    auto* saw   = host.apvts.getParameter(ids::kSawLevel);
    auto* pulse = host.apvts.getParameter(ids::kPulseLevel);
    auto* sub   = host.apvts.getParameter(ids::kSubLevel);
    auto* noise = host.apvts.getParameter(ids::kNoiseLevel);
    REQUIRE(saw   != nullptr);
    REQUIRE(pulse != nullptr);
    REQUIRE(sub   != nullptr);
    REQUIRE(noise != nullptr);

    // All four are continuous 0..1 levels: drive each fader to its range maximum (1.0) and
    // assert the bound parameter reads (normalized) 1.0; then to the minimum (0.0).
    auto driveAndCheck = [](mw::ui::LinearSlider& fader, juce::RangedAudioParameter* param)
    {
        fader.setValue(1.0, juce::sendNotificationSync);
        REQUIRE(param->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

        fader.setValue(0.0, juce::sendNotificationSync);
        REQUIRE(param->getValue() == Catch::Approx(0.0f).margin(1.0e-4));
    };

    driveAndCheck(mixer.sawLevelSlider(),   saw);
    driveAndCheck(mixer.pulseLevelSlider(), pulse);
    driveAndCheck(mixer.subLevelSlider(),   sub);
    driveAndCheck(mixer.noiseLevelSlider(), noise);
}

// ---------------------------------------------------------------------------
// [A] Host -> control read path: a host-side parameter move drives the bound fader.
// ---------------------------------------------------------------------------
TEST_CASE("ui_mixer a host-side parameter move drives the bound fader", "[ui_mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    SourceMixerModule mixer(host.apvts);

    struct Binding { const char* id; mw::ui::LinearSlider* fader; };
    const Binding bindings[] = {
        { ids::kSawLevel,   &mixer.sawLevelSlider()   },
        { ids::kPulseLevel, &mixer.pulseLevelSlider() },
        { ids::kSubLevel,   &mixer.subLevelSlider()   },
        { ids::kNoiseLevel, &mixer.noiseLevelSlider() },
    };

    for (const auto& b : bindings)
    {
        auto* param = host.apvts.getParameter(b.id);
        REQUIRE(param != nullptr);
        const auto* def = findDef(b.id);
        REQUIRE(def != nullptr);

        // Drive the parameter from the "host" (normalized 0..1) and assert the fader moved
        // to the matching modeled value. Attachments post async, so flush before reading.
        param->setValueNotifyingHost(1.0f);
        flushMessageQueue();
        REQUIRE(b.fader->getValue() == Catch::Approx(def->maxValue).margin(1.0e-3));

        param->setValueNotifyingHost(0.0f);
        flushMessageQueue();
        REQUIRE(b.fader->getValue() == Catch::Approx(def->minValue).margin(1.0e-3));
    }
}

// ---------------------------------------------------------------------------
// [B] layoutDesignUnits positions children inside the supplied design rectangle, in
// design units only (proportional — bounds scale with the rectangle, never escape it).
// ---------------------------------------------------------------------------
TEST_CASE("ui_mixer layoutDesignUnits keeps children inside the design rectangle and scales with it", "[ui_mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    SourceMixerModule mixer(host.apvts);

    auto& saw   = mixer.sawLevelSlider();
    auto& pulse = mixer.pulseLevelSlider();
    auto& sub   = mixer.subLevelSlider();
    auto& noise = mixer.noiseLevelSlider();

    // Lay out into a design rectangle. Children must each receive non-empty bounds that
    // sit WITHIN that rectangle (no escape, no pixel literals leaking out).
    const juce::Rectangle<int> design{ 0, 0, 600, 200 };
    mixer.setBounds(design);   // resized() -> layoutDesignUnits(getLocalBounds())

    for (auto* c : { static_cast<juce::Component*>(&saw),
                     static_cast<juce::Component*>(&pulse),
                     static_cast<juce::Component*>(&sub),
                     static_cast<juce::Component*>(&noise) })
    {
        REQUIRE_FALSE(c->getBounds().isEmpty());
        REQUIRE(design.contains(c->getBounds()));
    }

    // Faders are laid left-to-right in design order: saw, pulse, sub, noise. Their
    // x-origins are strictly increasing (a single proportional row).
    REQUIRE(saw.getX()   < pulse.getX());
    REQUIRE(pulse.getX() < sub.getX());
    REQUIRE(sub.getX()   < noise.getX());

    // Design-unit proportionality: doubling the design rectangle doubles a control's width
    // (within rounding). This proves the layout is fractions of the supplied rectangle,
    // not absolute pixel math (§5.3).
    const int wSmall = saw.getWidth();
    mixer.setBounds({ 0, 0, 1200, 400 });
    const int wLarge = saw.getWidth();
    REQUIRE(wLarge > wSmall);
    REQUIRE(static_cast<double>(wLarge) == Catch::Approx(2.0 * wSmall).margin(2.0));
}
