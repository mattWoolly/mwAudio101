// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/VcaModuleTest.cpp — JUCE-linked acceptance tests for the VCA panel
// module (task 123) [docs/design/10-ui.md §5.3, §8.1; ADR-015 C3; ADR-008 §3.0/C5/C6].
//
// Test-case display names begin with the task tag `ui_vca` so `ctest -R ui_vca`
// selects exactly these cases (silent-pass rule, AGENTS.md).
//
// They run headless on the message thread against a REAL APVTS built from the shipping
// §4 layout (buildParameterLayout) — the same construction the production processor
// uses — and assert BEHAVIOUR, never pixel equality:
//
//   [A] Every control binds via an APVTS attachment using a schema ParamId — the bound
//       parameter exists, a control move writes the bound APVTS parameter, and a
//       host-side parameter move drives the control (§8.1, ADR-015 C3). No direct DSP.
//   [B] The VCA Mode choice {ENV, GATE} is a CANONICAL choice with NO software extension
//       — it mirrors the schema's choiceCount == canonicalChoiceCount split and fences
//       nothing (§5.3; docs/design/06 §3.0; ADR-008 C5/C6).
//   [C] layoutDesignUnits positions children inside the supplied design rectangle, in
//       design units only — bounds scale with the rectangle and never escape it (§5.3).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/ModuleBase.h"
#include "../../ui/modules/VcaModule.h"
#include "../../ui/DesignTokens.h"

#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamIDs.h"         // mw::params::ids::*
#include "params/ParamDefs.h"        // mw::params::kParamDefs (schema fence source)

using mw::ui::VcaModule;
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

    const juce::String getName() const override         { return "VcaHost"; }
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
// ModuleBase exposes the shared APVTS reference + the title; VcaModule IS a ModuleBase
// and reports its title.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vca ModuleBase shares the same APVTS reference and a module title", "[ui_vca]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcaModule mod(host.apvts);

    // The base holds the SAME juce::AudioProcessorValueTreeState instance, not a copy —
    // the editor's single write/read surface (§5.3, §8.1).
    mw::ui::ModuleBase& base = mod;
    REQUIRE(&base.valueTreeState() == &host.apvts);
    REQUIRE(base.title() == juce::String("VCA"));
}

// ---------------------------------------------------------------------------
// [A] Each control binds to its schema ParamId — the bound parameter exists in the
// real layout (so the attachment is wired to a genuine atomic, not a stray string).
// ---------------------------------------------------------------------------
TEST_CASE("ui_vca every control binds to an existing schema parameter", "[ui_vca]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcaModule mod(host.apvts);

    for (const char* id : { ids::kVcaMode, ids::kVcaLevel, ids::kEnvAttack,
                            ids::kEnvDecay, ids::kEnvSustain, ids::kEnvRelease })
        REQUIRE(host.apvts.getParameter(id) != nullptr);
}

// ---------------------------------------------------------------------------
// [A] Control -> APVTS write path: moving each control writes the bound parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vca each control writes its bound APVTS parameter on a user move", "[ui_vca]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcaModule mod(host.apvts);

    auto* level   = host.apvts.getParameter(ids::kVcaLevel);
    auto* attack  = host.apvts.getParameter(ids::kEnvAttack);
    auto* decay   = host.apvts.getParameter(ids::kEnvDecay);
    auto* sustain = host.apvts.getParameter(ids::kEnvSustain);
    auto* release = host.apvts.getParameter(ids::kEnvRelease);
    auto* mode    = host.apvts.getParameter(ids::kVcaMode);
    REQUIRE(level   != nullptr);
    REQUIRE(attack  != nullptr);
    REQUIRE(decay   != nullptr);
    REQUIRE(sustain != nullptr);
    REQUIRE(release != nullptr);
    REQUIRE(mode    != nullptr);

    // Continuous controls: drive each rotary to its range maximum and assert the bound
    // parameter reads (normalized) 1.0. The slider's value is in modeled units; the
    // attachment maps it to the parameter's normalized atomic.
    auto driveToMax = [&](const char* id, mw::ui::RotarySlider& s, juce::AudioProcessorParameter* p)
    {
        const auto* def = findDef(id);
        REQUIRE(def != nullptr);
        s.setValue(def->maxValue, juce::sendNotificationSync);
        REQUIRE(p->getValue() == Catch::Approx(1.0f).margin(1.0e-4));
    };

    driveToMax(ids::kVcaLevel,   mod.levelSlider(),   level);
    driveToMax(ids::kEnvAttack,  mod.attackSlider(),  attack);
    driveToMax(ids::kEnvDecay,   mod.decaySlider(),   decay);
    driveToMax(ids::kEnvSustain, mod.sustainSlider(), sustain);
    driveToMax(ids::kEnvRelease, mod.releaseSlider(), release);

    // Choice control: selecting item id 2 (1-based) selects choice index 1 ("GATE");
    // assert the bound choice parameter lands on that index.
    mod.modeSelector().setSelectedId(2, juce::sendNotificationSync);   // id 2 == index 1
    auto* modeChoice = dynamic_cast<juce::AudioParameterChoice*>(mode);
    REQUIRE(modeChoice != nullptr);
    REQUIRE(modeChoice->getIndex() == 1);
}

// ---------------------------------------------------------------------------
// [A] Host -> control read path: a host-side parameter move drives the control.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vca a host-side parameter move drives the bound control", "[ui_vca]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcaModule mod(host.apvts);

    auto* level = host.apvts.getParameter(ids::kVcaLevel);
    auto* mode  = host.apvts.getParameter(ids::kVcaMode);
    REQUIRE(level != nullptr);
    REQUIRE(mode  != nullptr);

    // Drive the level parameter from the "host" (normalized 0..1) and assert the slider
    // moved to the matching modeled value. Attachments post async, so flush the message
    // queue before reading the control.
    level->setValueNotifyingHost(0.0f);
    flushMessageQueue();
    const auto* levelDef = findDef(ids::kVcaLevel);
    REQUIRE(levelDef != nullptr);
    REQUIRE(mod.levelSlider().getValue() == Catch::Approx(levelDef->minValue).margin(1.0e-3));

    // Drive the VCA-mode choice from the host to the LAST index (GATE) and assert the
    // selector followed (1-based item id == index + 1).
    auto* modeChoice = dynamic_cast<juce::AudioParameterChoice*>(mode);
    REQUIRE(modeChoice != nullptr);
    const int lastIndex = modeChoice->choices.size() - 1;
    mode->setValueNotifyingHost(modeChoice->convertTo0to1(static_cast<float>(lastIndex)));
    flushMessageQueue();
    REQUIRE(mod.modeSelector().getSelectedId() == lastIndex + 1);
}

// ---------------------------------------------------------------------------
// [B] The VCA Mode {ENV, GATE} choice is CANONICAL — no software extension fenced.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vca the VCA Mode choice is canonical with no fenced software extension", "[ui_vca]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcaModule mod(host.apvts);
    mod.setTokens(DesignTokens::defaultTheme());

    auto& selector = mod.modeSelector();

    // The selector mirrors the schema's choiceCount / canonicalChoiceCount split.
    const auto* modeDef = findDef(ids::kVcaMode);
    REQUIRE(modeDef != nullptr);
    REQUIRE(selector.getNumItems() == static_cast<int>(modeDef->choiceCount));
    REQUIRE(selector.canonicalCount() == static_cast<int>(modeDef->canonicalChoiceCount));

    // {ENV, GATE} is a 2-entry CANONICAL choice: choiceCount == canonicalChoiceCount,
    // the schema does NOT flag it as a software-ext carrier, and NO index is fenced
    // (env/gate select is documented hardware behaviour) [docs/design/06 §3.0; C5/C6].
    REQUIRE(modeDef->choiceCount == modeDef->canonicalChoiceCount);
    REQUIRE_FALSE(modeDef->isSoftwareExt);
    for (int i = 0; i < selector.getNumItems(); ++i)
        REQUIRE_FALSE(selector.isExtensionIndex(i));

    // The labels are the schema's verbatim {ENV, GATE}.
    REQUIRE(selector.getNumItems() == 2);
    REQUIRE(juce::String::fromUTF8(modeDef->choices[0]) == juce::String("ENV"));
    REQUIRE(juce::String::fromUTF8(modeDef->choices[1]) == juce::String("GATE"));
}

// ---------------------------------------------------------------------------
// [C] layoutDesignUnits positions children inside the supplied design rectangle, in
// design units only (proportional — bounds scale with the rectangle, never escape it).
// ---------------------------------------------------------------------------
TEST_CASE("ui_vca layoutDesignUnits keeps children inside the design rectangle and scales with it", "[ui_vca]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcaModule mod(host.apvts);

    auto& mode    = mod.modeSelector();
    auto& level   = mod.levelSlider();
    auto& attack  = mod.attackSlider();
    auto& decay   = mod.decaySlider();
    auto& sustain = mod.sustainSlider();
    auto& release = mod.releaseSlider();

    // Lay out into a design rectangle. Children must each receive non-empty bounds that
    // sit WITHIN that rectangle (no escape, no pixel literals leaking out).
    const juce::Rectangle<int> design{ 0, 0, 720, 200 };
    mod.setBounds(design);   // resized() -> layoutDesignUnits(getLocalBounds())

    for (auto* c : { static_cast<juce::Component*>(&mode),
                     static_cast<juce::Component*>(&level),
                     static_cast<juce::Component*>(&attack),
                     static_cast<juce::Component*>(&decay),
                     static_cast<juce::Component*>(&sustain),
                     static_cast<juce::Component*>(&release) })
    {
        REQUIRE_FALSE(c->getBounds().isEmpty());
        REQUIRE(design.contains(c->getBounds()));
    }

    // Controls are laid left-to-right in design order: mode, level, then A/D/S/R.
    // Their x-origins are strictly increasing (a single proportional row).
    REQUIRE(mode.getX()    < level.getX());
    REQUIRE(level.getX()   < attack.getX());
    REQUIRE(attack.getX()  < decay.getX());
    REQUIRE(decay.getX()   < sustain.getX());
    REQUIRE(sustain.getX() < release.getX());

    // Design-unit proportionality: doubling the design rectangle doubles a control's
    // width (within rounding). This proves the layout is fractions of the supplied
    // rectangle, not absolute pixel math (§5.3).
    const int wSmall = level.getWidth();
    mod.setBounds({ 0, 0, 1440, 400 });
    const int wLarge = level.getWidth();
    REQUIRE(wLarge > wSmall);
    REQUIRE(static_cast<double>(wLarge) == Catch::Approx(2.0 * wSmall).margin(2.0));
}
