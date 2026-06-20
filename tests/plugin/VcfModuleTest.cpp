// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/VcfModuleTest.cpp — JUCE-linked acceptance tests for the VCF panel
// module (task 122) [docs/design/10-ui.md §5.3, §8.1, §8.2; ADR-015 C3, C4; ADR-008 C1].
//
// Test-case display names begin with the task tag `ui_vcf` so `ctest -R ui_vcf` selects
// exactly these cases (silent-pass rule, AGENTS.md).
//
// They run headless on the message thread against a REAL APVTS built from the shipping
// §4 layout (buildParameterLayout) — the same construction the production processor uses
// — and assert BEHAVIOUR, never pixel equality:
//
//   [A] Every control binds via an APVTS attachment to the correct schema ParamId — the
//       parameter exists, a control move writes the bound APVTS parameter, and a
//       host-side parameter move drives the control (§8.1, §8.2; ADR-015 C3, C4). No
//       direct DSP call: the module only owns sliders + attachments.
//   [B] layoutDesignUnits positions children inside the supplied design rectangle, in
//       design units only — bounds scale with the rectangle and never escape it (§5.3).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/ModuleBase.h"
#include "../../ui/modules/VcfModule.h"

#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamIDs.h"         // mw::params::ids::*
#include "params/ParamDefs.h"        // mw::params::kParamDefs (schema source)

using mw::ui::VcfModule;
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

    const juce::String getName() const override         { return "VcfHost"; }
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
// returns. This is the supported headless flush.
void flushMessageQueue()
{
    auto* mm = juce::MessageManager::getInstance();
    mm->callAsync([mm] { mm->stopDispatchLoop(); });
    mm->runDispatchLoop();
}

} // namespace

// ---------------------------------------------------------------------------
// ModuleBase exposes the shared APVTS reference + the title; VcfModule IS a ModuleBase
// and reports its title.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vcf ModuleBase shares the same APVTS reference and a module title", "[ui_vcf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcfModule mod(host.apvts);

    // The base holds the SAME juce::AudioProcessorValueTreeState instance, not a copy —
    // the editor's single write/read surface (§5.3, §8.1).
    mw::ui::ModuleBase& base = mod;
    REQUIRE(&base.valueTreeState() == &host.apvts);
    REQUIRE(base.title() == juce::String("VCF"));
}

// ---------------------------------------------------------------------------
// [A] Each control is bound to the correct schema ParamId (the parameter exists in the
// real layout under the exact ids:: constant the module attaches to).
// ---------------------------------------------------------------------------
TEST_CASE("ui_vcf every control binds to its schema VCF parameter id", "[ui_vcf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcfModule mod(host.apvts);

    // The five §5.3 VCF controls map 1:1 to the five canonical mw101.vcf.* ids; each must
    // resolve to a live parameter in the shipping layout (§8.1; ADR-008 C1).
    REQUIRE(host.apvts.getParameter(ids::kVcfCutoff)    != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kVcfResonance) != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kVcfEnvMod)    != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kVcfKbdTrack)  != nullptr);
    REQUIRE(host.apvts.getParameter(ids::kVcfLfoMod)    != nullptr);
}

// ---------------------------------------------------------------------------
// [A] Control -> APVTS write path: moving each control writes its bound parameter.
// ---------------------------------------------------------------------------
TEST_CASE("ui_vcf each control writes its bound APVTS parameter on a user move", "[ui_vcf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcfModule mod(host.apvts);

    struct Binding
    {
        const char*           id;
        mw::ui::RotarySlider& slider;
    };

    const Binding bindings[] = {
        { ids::kVcfCutoff,    mod.cutoffSlider()    },
        { ids::kVcfResonance, mod.resonanceSlider() },
        { ids::kVcfEnvMod,    mod.envModSlider()    },
        { ids::kVcfKbdTrack,  mod.kbdTrackSlider()  },
        { ids::kVcfLfoMod,    mod.lfoModSlider()    },
    };

    // Every VCF control is continuous: drive each slider to its modeled range maximum and
    // assert the bound parameter reads (normalized) 1.0 — proving the attachment maps the
    // control's value to the parameter's normalized atomic (§8.1; ADR-015 C3).
    for (const auto& b : bindings)
    {
        auto* param = host.apvts.getParameter(b.id);
        REQUIRE(param != nullptr);

        const auto* def = findDef(b.id);
        REQUIRE(def != nullptr);

        b.slider.setValue(def->maxValue, juce::sendNotificationSync);
        REQUIRE(param->getValue() == Catch::Approx(1.0f).margin(1.0e-4));

        // And the range minimum maps to normalized 0.0 (a non-degenerate round trip).
        b.slider.setValue(def->minValue, juce::sendNotificationSync);
        REQUIRE(param->getValue() == Catch::Approx(0.0f).margin(1.0e-4));
    }
}

// ---------------------------------------------------------------------------
// [A] Host -> control read path: a host-side parameter move drives the bound control via
// the attachment only (no engine polling) (§8.2; ADR-015 C4).
// ---------------------------------------------------------------------------
TEST_CASE("ui_vcf a host-side parameter move drives the bound control", "[ui_vcf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcfModule mod(host.apvts);

    struct Binding
    {
        const char*           id;
        mw::ui::RotarySlider& slider;
    };

    const Binding bindings[] = {
        { ids::kVcfCutoff,    mod.cutoffSlider()    },
        { ids::kVcfResonance, mod.resonanceSlider() },
        { ids::kVcfEnvMod,    mod.envModSlider()    },
        { ids::kVcfKbdTrack,  mod.kbdTrackSlider()  },
        { ids::kVcfLfoMod,    mod.lfoModSlider()    },
    };

    for (const auto& b : bindings)
    {
        auto* param = host.apvts.getParameter(b.id);
        REQUIRE(param != nullptr);

        const auto* def = findDef(b.id);
        REQUIRE(def != nullptr);

        // Drive from the "host" to normalized maximum; the slider must follow to its
        // modeled max. Attachments post async, so flush the message queue before reading.
        param->setValueNotifyingHost(1.0f);
        flushMessageQueue();
        REQUIRE(b.slider.getValue() == Catch::Approx(def->maxValue).margin(1.0e-3));

        // ... and back to the host minimum.
        param->setValueNotifyingHost(0.0f);
        flushMessageQueue();
        REQUIRE(b.slider.getValue() == Catch::Approx(def->minValue).margin(1.0e-3));
    }
}

// ---------------------------------------------------------------------------
// [B] layoutDesignUnits positions children inside the supplied design rectangle, in
// design units only (proportional — bounds scale with the rectangle, never escape it).
// ---------------------------------------------------------------------------
TEST_CASE("ui_vcf layoutDesignUnits keeps children inside the design rectangle and scales with it", "[ui_vcf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    HostProcessor host;
    VcfModule mod(host.apvts);

    auto& cutoff    = mod.cutoffSlider();
    auto& resonance = mod.resonanceSlider();
    auto& envMod    = mod.envModSlider();
    auto& kbdTrack  = mod.kbdTrackSlider();
    auto& lfoMod    = mod.lfoModSlider();

    // Lay out into a design rectangle. Children must each receive non-empty bounds that
    // sit WITHIN that rectangle (no escape, no pixel literals leaking out).
    const juce::Rectangle<int> design{ 0, 0, 600, 200 };
    mod.setBounds(design);   // resized() -> layoutDesignUnits(getLocalBounds())

    for (auto* c : { static_cast<juce::Component*>(&cutoff),
                     static_cast<juce::Component*>(&resonance),
                     static_cast<juce::Component*>(&envMod),
                     static_cast<juce::Component*>(&kbdTrack),
                     static_cast<juce::Component*>(&lfoMod) })
    {
        REQUIRE_FALSE(c->getBounds().isEmpty());
        REQUIRE(design.contains(c->getBounds()));
    }

    // Controls are laid left-to-right in design order: cutoff, resonance, env, kbd, mod.
    // Their x-origins are strictly increasing (a single proportional row).
    REQUIRE(cutoff.getX()    < resonance.getX());
    REQUIRE(resonance.getX() < envMod.getX());
    REQUIRE(envMod.getX()    < kbdTrack.getX());
    REQUIRE(kbdTrack.getX()  < lfoMod.getX());

    // Design-unit proportionality: doubling the design rectangle doubles a control's
    // width (within rounding). This proves the layout is fractions of the supplied
    // rectangle, not absolute pixel math (§5.3).
    const int wSmall = cutoff.getWidth();
    mod.setBounds({ 0, 0, 1200, 400 });
    const int wLarge = cutoff.getWidth();
    REQUIRE(wLarge > wSmall);
    REQUIRE(static_cast<double>(wLarge) == Catch::Approx(2.0 * wSmall).margin(2.0));
}
