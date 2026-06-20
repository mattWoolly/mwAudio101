// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/TransportModeBarTest.cpp — JUCE-linked tests for the TRANSPORT / MODE
// bar (task 125, ui/modules/TransportModeBar.h). Asserts every acceptance criterion of
// plan/backlog/125 against a REAL APVTS built from mw::plugin::buildParameterLayout()
// (task 020), under a juce::ScopedJuceInitialiser_GUI [docs/design/10-ui.md §5.3, §4.4,
// §10; ADR-015 C2, C3, C8]:
//
//   • Arp/seq mode, tempo-sync, run/hold bind via APVTS attachments using SCHEMA
//     ParamIds — each bound control resolves a non-null parameter, and control->APVTS
//     AND APVTS->control both move (§8.1, ADR-015 C3).
//   • The scale-preset selector exposes 75/100/150/200% and drives the editor snap seam
//     (§4.4, ADR-015 C2) — picking a preset invokes the editor callback with the index.
//   • The reduce-motion toggle is surfaced to the editor without affecting any control
//     binding (§10, ADR-015 C8).
//   • Run/hold is a UI affordance (NOT an APVTS param) reported through a seam (§5.3).
//   • Any choice index at/above the schema's canonicalChoiceCount is fenced as a
//     software extension by the ChoiceSelector (§5.3, ADR-008 C5/C6).

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/modules/TransportModeBar.h"
#include "params/ParameterLayout.h"   // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"         // mw::params::kParamDefs (JUCE-free registry)
#include "params/ParamIDs.h"          // mw::params::ids — schema-owned ParamId constants

namespace {

namespace ids = mw::params::ids;

// A minimal headless AudioProcessor hosting an APVTS built from the real 020 layout, so
// the bar binds against actual parameters exactly as it would in the shipped editor.
class HostProcessor final : public juce::AudioProcessor
{
public:
    HostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "TransportHost"; }
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

// Read a Choice parameter's current 0-based index through the typed parameter.
int readChoiceIndex(juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
    REQUIRE(p != nullptr);
    return p->getIndex();
}

// Drive a Choice parameter to a 0-based index from the host side (notifying the editor
// listeners so the attachment moves the bound control).
void setChoiceIndex(juce::AudioProcessorValueTreeState& apvts, const char* id, int index)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(index)));
}

const mw::params::ParamDef* findDef(const char* id)
{
    const std::string_view want{ id };
    for (const auto& d : mw::params::kParamDefs)
        if (std::string_view{ d.id } == want)
            return &d;
    return nullptr;
}

// Flush pending async updates (the JUCE parameter-attachment host->control path posts
// to a private AsyncUpdater) without a modal loop — JUCE_MODAL_LOOPS_PERMITTED is 0 in
// this headless target, so runDispatchLoopUntil() is unavailable. Post a
// stopDispatchLoop() AFTER the attachment's already-queued update, then run the
// (non-modal) dispatch loop once: it processes the queued update, then the stop, and
// returns. Mirrors tests/plugin/ModulatorModuleTest.cpp's headless flush idiom.
void flushMessageQueue()
{
    auto* mm = juce::MessageManager::getInstance();
    mm->callAsync([mm] { mm->stopDispatchLoop(); });
    mm->runDispatchLoop();
}

} // namespace

TEST_CASE("ui_transport binds every mode/sync/latch control to its schema ParamId",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    // Every bound parameter must resolve in the real layout (§8.1; ADR-015 C3).
    for (const char* id : { ids::kArpMode, ids::kArpRange, ids::kArpTempoSync,
                            ids::kArpSyncDiv, ids::kArpLatch, ids::kSeqMode,
                            ids::kSeqTempoSync, ids::kSeqSyncDiv })
    {
        REQUIRE(host.apvts.getParameter(id) != nullptr);
    }
}

TEST_CASE("ui_transport choice controls move control to APVTS and APVTS to control",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    // control -> APVTS: select arp mode index 2 in the selector (1-based item id 3).
    bar.arpModeSelector().setSelectedId(3 /*=index 2*/, juce::sendNotificationSync);
    CHECK(readChoiceIndex(host.apvts, ids::kArpMode) == 2);

    // APVTS -> control: drive seq mode to index 2 from the host side; the selector
    // follows via the attachment listener (§8.2). Attachments post async — flush first.
    setChoiceIndex(host.apvts, ids::kSeqMode, 2);
    flushMessageQueue();
    CHECK(bar.seqModeSelector().getSelectedId() == 3 /*=index 2*/);
}

TEST_CASE("ui_transport tempo-sync and latch toggles move both directions via APVTS",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    // control -> APVTS: flip the arp-latch toggle, the bound bool param follows.
    auto* latch = host.apvts.getParameter(ids::kArpLatch);
    REQUIRE(latch != nullptr);
    const bool before = bar.arpLatchToggle().getToggleState();
    bar.arpLatchToggle().setToggleState(! before, juce::sendNotificationSync);
    CHECK((latch->getValue() > 0.5f) == (! before));

    // APVTS -> control: drive arp tempo-sync OFF from the host; the toggle follows
    // (attachments post async — flush first).
    auto* arpSync = host.apvts.getParameter(ids::kArpTempoSync);
    REQUIRE(arpSync != nullptr);
    arpSync->setValueNotifyingHost(0.0f);
    flushMessageQueue();
    CHECK(bar.arpTempoSyncToggle().getToggleState() == false);

    arpSync->setValueNotifyingHost(1.0f);
    flushMessageQueue();
    CHECK(bar.arpTempoSyncToggle().getToggleState() == true);
}

TEST_CASE("ui_transport scale-preset selector exposes 75/100/150/200 percent options",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    // Exactly four presets, labelled as the percentages (§4.4; ADR-015 C2).
    REQUIRE(bar.scalePresetSelector().getNumItems() == 4);
    CHECK(mw::ui::TransportModeBar::numScalePresets() == 4);
    CHECK(bar.scalePresetSelector().getItemText(0) == juce::String("75%"));
    CHECK(bar.scalePresetSelector().getItemText(1) == juce::String("100%"));
    CHECK(bar.scalePresetSelector().getItemText(2) == juce::String("150%"));
    CHECK(bar.scalePresetSelector().getItemText(3) == juce::String("200%"));

    // The index->logical-scale-factor mapping is exposed for the editor snap.
    CHECK(mw::ui::TransportModeBar::scalePresetFactor(0) == 0.75f);
    CHECK(mw::ui::TransportModeBar::scalePresetFactor(1) == 1.0f);
    CHECK(mw::ui::TransportModeBar::scalePresetFactor(2) == 1.5f);
    CHECK(mw::ui::TransportModeBar::scalePresetFactor(3) == 2.0f);
}

TEST_CASE("ui_transport scale-preset selection drives the editor snap seam, not a param",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    int reported = -1;
    bar.onScalePresetSelected = [&reported](int index) { reported = index; };

    // Picking 200% (index 3, item id 4) reports that index to the editor.
    bar.scalePresetSelector().setSelectedId(4, juce::sendNotificationSync);
    CHECK(reported == 3);
    CHECK(bar.selectedScalePresetIndex() == 3);

    // The scale preset is NOT a host parameter — no APVTS entry exists for it.
    CHECK(host.apvts.getParameter("mw101.ui.scale") == nullptr);
}

TEST_CASE("ui_transport reduce-motion toggle is surfaced without touching any binding",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    // Snapshot every bound parameter's value before toggling reduce-motion.
    auto snapshot = [&]
    {
        std::vector<float> v;
        for (const char* id : { ids::kArpMode, ids::kArpRange, ids::kArpTempoSync,
                                ids::kArpSyncDiv, ids::kArpLatch, ids::kSeqMode,
                                ids::kSeqTempoSync, ids::kSeqSyncDiv })
            v.push_back(host.apvts.getParameter(id)->getValue());
        return v;
    };
    const auto before = snapshot();

    bool reported = false;
    bar.onReduceMotionChanged = [&reported](bool on) { reported = on; };

    bar.reduceMotionToggle().setToggleState(true, juce::sendNotificationSync);

    CHECK(reported == true);                  // surfaced to the editor (§10; C8)
    CHECK(bar.reduceMotionEnabled() == true);
    CHECK(snapshot() == before);              // no control binding was disturbed

    // Reduce-motion is a UI preference, NOT a host parameter.
    CHECK(host.apvts.getParameter("mw101.ui.reduce_motion") == nullptr);
}

TEST_CASE("ui_transport run/hold is a UI affordance reported through a seam, not a param",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    bool running = false;
    bar.onRunStateChanged = [&running](bool r) { running = r; };

    CHECK(bar.isRunning() == false);
    bar.runHoldToggle().setToggleState(true, juce::sendNotificationSync);
    CHECK(running == true);
    CHECK(bar.isRunning() == true);

    // Run/hold is transport state owned elsewhere — it is NOT an APVTS parameter (§5.3).
    CHECK(host.apvts.getParameter("mw101.transport.run") == nullptr);
}

TEST_CASE("ui_transport choice selectors mirror the schema canonical/extension fence",
          "[ui_transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    HostProcessor host;
    mw::ui::TransportModeBar bar(host.apvts);

    // Each schema choice control's selector holds exactly its choiceCount entries and
    // its canonicalCount matches the schema's canonicalChoiceCount, so any index >=
    // canonicalChoiceCount is fenced as a software extension by the ChoiceSelector
    // (§5.3; ADR-008 C5/C6). The arp/seq choices today carry no extensions, but the
    // fence is wired straight from the schema so a future appended index is fenced.
    struct Pair { mw::ui::ChoiceSelector* sel; const char* id; };
    const Pair pairs[] = {
        { &bar.arpModeSelector(),    ids::kArpMode    },
        { &bar.arpRangeSelector(),   ids::kArpRange   },
        { &bar.arpSyncDivSelector(), ids::kArpSyncDiv },
        { &bar.seqModeSelector(),    ids::kSeqMode    },
        { &bar.seqSyncDivSelector(), ids::kSeqSyncDiv },
    };

    for (const auto& pr : pairs)
    {
        const auto* def = findDef(pr.id);
        REQUIRE(def != nullptr);
        CHECK(pr.sel->getNumItems() == static_cast<int>(def->choiceCount));
        CHECK(pr.sel->canonicalCount() == static_cast<int>(def->canonicalChoiceCount));

        for (int i = 0; i < pr.sel->getNumItems(); ++i)
        {
            const bool schemaExt = i >= static_cast<int>(def->canonicalChoiceCount);
            CHECK(pr.sel->isExtensionIndex(i) == schemaExt);
        }
    }
}
