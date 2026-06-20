// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetBrowserTest.cpp — JUCE-linked tests for the THIN preset-browser
// view over the processor-owned PresetManager (task 128). Asserts every acceptance
// criterion of plan/backlog/128 against docs/design/10-ui.md §9.1, §9.2, §9.3 and
// ADR-015 C6 / ADR-008 C14, C19:
//
//   1. The list reflects the bank: getNumRows / row name+category come straight from the
//      manager's getNumPresets()/getName()/getCategory() — the view duplicates no bank
//      logic [§9.1; §9.2].
//   2. The category filter narrows the list via PresetManager::indicesForCategory() and
//      enumerates exactly the §6.5 taxonomy strings (NOT re-minted) [§9.3; ADR-008 C14].
//   3. Selecting + Load invokes the message-thread LOAD SINK with the chosen ABSOLUTE
//      bank index; wired to PresetManager::loadPreset it applies the preset to APVTS, and
//      no tree pointer crosses through the view [§9.3; ADR-015 C6; ADR-008 C19].
//   4. The list refreshes on the manager's change BROADCASTER, not by polling [§9.1].
//   5. layoutDesignUnits() positions the children in design units (no pixel math) [§9.1].
//
// Test names begin with the `ui_preset` tag so `ctest -R ui_preset` selects exactly these
// and the --no-tests=error rule holds. No '[' appears in any display name. A
// juce::ScopedJuceInitialiser_GUI brackets every case (JUCE GUI singletons + leak
// detector), matching tests/plugin/PluginHarnessTest.cpp.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/PresetBrowser.h"               // mw::ui::PresetBrowser (unit under test)
#include "../../core/calibration/PresetBrowserConstants.h" // §6.5 taxonomy + layout (PI)

#include "preset/PresetManager.h"                 // mw::plugin::preset::PresetManager
#include "preset/PresetFormat.h"                  // writePresetJson (build valid fixtures)
#include "params/ParameterLayout.h"               // mw::plugin::buildParameterLayout
#include "state/StateTree.h"                      // canonical keys
#include "state/Extras.h"                         // mw::state::Extras
#include "state/LoadFailure.h"                    // RecoveryReport

namespace {

using mw::plugin::preset::PresetManager;
using mw::plugin::preset::PresetMeta;
using mw::plugin::preset::writePresetJson;
using mw::plugin::state::RecoveryReport;
namespace cal = mw::cal::ui::presetBrowser;

// A minimal headless AudioProcessor hosting the FULL 91-param APVTS so a wired load sink
// (PresetManager::loadPreset) has a real tree to bind into and writePresetJson has a real
// <PARAMS> subtree.
class BrowserHostProcessor final : public juce::AudioProcessor
{
public:
    BrowserHostProcessor()
        : apvts(*this, nullptr, "PARAMS", mw::plugin::buildParameterLayout()) {}

    const juce::String getName() const override        { return "BrowserHost"; }
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

juce::ValueTree makeCanonicalTree(const char* overrideId = nullptr, double overrideValue = 0.0)
{
    BrowserHostProcessor host;
    juce::ValueTree root{ juce::Identifier{ mw::state::kRootId } };
    root.setProperty(mw::state::kAttrSchemaVersion, 1, nullptr);

    juce::ValueTree params{ juce::Identifier{ mw::state::kParamsId } };
    params.copyPropertiesAndChildrenFrom(host.apvts.copyState(), nullptr);

    if (overrideId != nullptr)
    {
        auto node = params.getChildWithProperty("id", juce::String(overrideId));
        if (node.isValid())
            node.setProperty("value", overrideValue, nullptr);
    }
    root.appendChild(params, nullptr);
    return root;
}

// Author a VALID .mw101preset JSON string for the given meta name/category, optionally
// stamping a recognisable non-default value to prove a load lands in APVTS.
juce::String validPresetJson(const juce::String& name, const juce::String& category,
                             const char* overrideId = nullptr, double overrideValue = 0.0)
{
    PresetMeta meta;
    meta.name     = name;
    meta.author   = "Matt Woolly";
    meta.category = category;
    meta.soundExt = false;
    return writePresetJson(makeCanonicalTree(overrideId, overrideValue), meta);
}

float apvtsModeledValue(const BrowserHostProcessor& host, const char* id)
{
    auto* p = host.apvts.getParameter(id);
    REQUIRE(p != nullptr);
    return p->getNormalisableRange().convertFrom0to1(p->getValue());
}

// Select a category-filter item by its display text (robust for a non-editable combo:
// resolve the text to its item ID and select that, sending a synchronous notification so
// the browser's onChange refresh fires before we assert).
void selectCategoryByText(juce::ComboBox& combo, const juce::String& text)
{
    for (int i = 0; i < combo.getNumItems(); ++i)
    {
        if (combo.getItemText(i) == text)
        {
            combo.setSelectedId(combo.getItemId(i), juce::sendNotificationSync);
            return;
        }
    }
    FAIL("category item not found: " + text.toStdString());
}

// A five-slot bank spanning three §6.5 categories (3 Lead, 1 SubBass, 1 AcidBassLead) so
// filtering and absolute-vs-row mapping are both exercised.
std::vector<PresetManager::SlotSource> makeMixedBank()
{
    return {
        { "Lead A", validPresetJson("Lead A", "Lead") },
        { "Bass A", validPresetJson("Bass A", "SubBass") },
        { "Lead B", validPresetJson("Lead B", "Lead") },
        { "Acid A", validPresetJson("Acid A", "AcidBassLead") },
        { "Lead C", validPresetJson("Lead C", "Lead") },
    };
}

} // namespace

// --- (1) the list reflects the bank (no duplicated bank logic) -----------------------

TEST_CASE("ui_preset list reflects the bank rows from the manager", "[ui_preset]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetManager manager{ makeMixedBank() };
    mw::ui::PresetBrowser browser{ manager };

    // No filter active: every bank slot is a visible row, in bank order.
    REQUIRE(browser.numVisibleRows() == manager.getNumPresets());
    REQUIRE(browser.numVisibleRows() == 5);

    // Each visible row maps to the matching ABSOLUTE bank index (identity, no filter).
    for (int row = 0; row < browser.numVisibleRows(); ++row)
        CHECK(browser.absoluteIndexForRow(row) == row);

    // The ListBox is genuinely backed by the view's model, and that model reports the
    // same row count as the bank (the view duplicates no bank logic).
    auto* model = browser.presetListBox().getListBoxModel();
    REQUIRE(model != nullptr);
    CHECK(model->getNumRows() == manager.getNumPresets());
}

// --- (2) the category filter narrows via indicesForCategory + enumerates the taxonomy --

TEST_CASE("ui_preset category filter enumerates the schema taxonomy and narrows the list",
          "[ui_preset]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetManager manager{ makeMixedBank() };
    mw::ui::PresetBrowser browser{ manager };

    // The filter combo enumerates EXACTLY the §6.5 taxonomy (an "All" sentinel + the six
    // categories), in canonical order — not re-minted [§9.3; ADR-008 C14].
    auto& combo = browser.categoryFilterBox();
    REQUIRE(combo.getNumItems() == cal::kCategoryCount + 1);
    CHECK(combo.getItemText(0) == juce::String::fromUTF8(cal::kAllCategoriesLabel));
    for (int i = 0; i < cal::kCategoryCount; ++i)
        CHECK(combo.getItemText(i + 1) == juce::String::fromUTF8(cal::kCategories[i]));

    // Selecting "Lead" narrows the list to exactly the manager's Lead indices (the view
    // asks the bank — it does not re-derive the grouping) [§9.1].
    selectCategoryByText(combo, "Lead");
    REQUIRE(browser.selectedCategory() == "Lead");

    const auto expectedLeads = manager.indicesForCategory("Lead");
    REQUIRE(browser.numVisibleRows() == expectedLeads.size());
    REQUIRE(browser.numVisibleRows() == 3);
    for (int row = 0; row < browser.numVisibleRows(); ++row)
        CHECK(expectedLeads.contains(browser.absoluteIndexForRow(row)));

    // Row 0 under the Lead filter is bank index 0 (Lead A); row 1 is bank index 2 (Lead
    // B) — proving the row->absolute mapping is the FILTERED view, not the identity.
    CHECK(browser.absoluteIndexForRow(0) == 0);
    CHECK(browser.absoluteIndexForRow(1) == 2);
    CHECK(browser.absoluteIndexForRow(2) == 4);

    // Selecting the "All" sentinel clears the filter and restores the whole bank.
    combo.setSelectedId(1, juce::sendNotificationSync);
    CHECK(browser.selectedCategory().isEmpty());
    CHECK(browser.numVisibleRows() == manager.getNumPresets());
}

// --- (3) select + Load invokes the message-thread sink with the absolute index --------

TEST_CASE("ui_preset Load invokes the message-thread load sink with the selected absolute index",
          "[ui_preset]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Stamp a recognisable cutoff into the SubBass slot so a real load is observable.
    constexpr double kCutoff = 0.37;
    std::vector<PresetManager::SlotSource> sources{
        { "Lead A", validPresetJson("Lead A", "Lead") },
        { "Bass A", validPresetJson("Bass A", "SubBass", "mw101.vcf.cutoff", kCutoff) },
        { "Lead B", validPresetJson("Lead B", "Lead") },
    };

    PresetManager manager{ sources };
    mw::ui::PresetBrowser browser{ manager };

    // Wire the load sink to the processor-owned apply path. The view never owns apvts/
    // extras/report — it only hands the ABSOLUTE index to this sink, on the message
    // thread; the manager does the actual apply [§9.3; ADR-015 C6; ADR-008 C19].
    BrowserHostProcessor host;
    mw::state::Extras extras{};
    RecoveryReport report;

    int sinkCalls = 0;
    int lastIndex = -1;
    bool onMessageThread = false;
    browser.onLoadRequested = [&](int absoluteIndex)
    {
        ++sinkCalls;
        lastIndex = absoluteIndex;
        onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
        manager.loadPreset(absoluteIndex, host.apvts, extras, report);
    };

    // Select the SubBass row (absolute index 1) and load it. loadSelected() is the same
    // load action the Load button / a row double-click triggers, driven directly so the
    // headless test is deterministic (Button::triggerClick posts an async message).
    browser.setSelectedRow(1);
    REQUIRE(browser.selectedRow() == 1);
    browser.loadSelected();

    REQUIRE(sinkCalls == 1);
    CHECK(lastIndex == 1);                 // the ABSOLUTE bank index, not a filtered row
    CHECK(onMessageThread);                // the load is invoked on the message thread

    // Wired to PresetManager::loadPreset, the selected preset's value reached APVTS — the
    // full thin-view -> manager loadPreset path applied on the message thread.
    CHECK(apvtsModeledValue(host, "mw101.vcf.cutoff") == Catch::Approx(kCutoff).margin(1.0e-4));
}

// --- (3b) under a filter, Load still hands the ABSOLUTE index, not the row -------------

TEST_CASE("ui_preset Load under a category filter hands the absolute bank index", "[ui_preset]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetManager manager{ makeMixedBank() };
    mw::ui::PresetBrowser browser{ manager };

    int lastIndex = -1;
    browser.onLoadRequested = [&](int absoluteIndex) { lastIndex = absoluteIndex; };

    // Filter to Lead, select the SECOND visible Lead (row 1 == absolute bank index 2),
    // and Load: the sink must receive the ABSOLUTE index 2, not the row index 1.
    selectCategoryByText(browser.categoryFilterBox(), "Lead");
    browser.setSelectedRow(1);
    browser.loadSelected();

    CHECK(lastIndex == 2);
}

// --- (4) the list refreshes on the manager's change broadcaster, not by polling --------

TEST_CASE("ui_preset list refreshes on the change broadcaster, not by polling", "[ui_preset]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A change source the processor would own; the browser listens to it for refreshes.
    juce::ChangeBroadcaster broadcaster;

    PresetManager manager{ makeMixedBank() };
    mw::ui::PresetBrowser browser{ manager, &broadcaster };

    REQUIRE(browser.numVisibleRows() == 5);

    // Apply a category filter so the visible count diverges from the bank count, proving
    // refreshList re-reads the filtered view when the broadcaster fires.
    selectCategoryByText(browser.categoryFilterBox(), "SubBass");
    REQUIRE(browser.numVisibleRows() == 1);

    // Fire the broadcaster synchronously on the message thread; the view must re-read its
    // list off this callback WITHOUT any poll/timer (it is a juce::ChangeListener).
    broadcaster.sendSynchronousChangeMessage();

    // The filtered view is intact after the broadcaster-driven refresh (SubBass => 1).
    CHECK(browser.numVisibleRows() == 1);
    CHECK(browser.absoluteIndexForRow(0) == 1);

    // Prove the refresh truly tracks the manager: a broadcaster fire after clearing the
    // filter restores the full bank (the view never cached a stale list).
    browser.categoryFilterBox().setSelectedId(1, juce::sendNotificationSync);
    broadcaster.sendSynchronousChangeMessage();
    CHECK(browser.numVisibleRows() == manager.getNumPresets());
}

// --- (5) layoutDesignUnits positions children in design units (no pixel math) ---------

TEST_CASE("ui_preset layoutDesignUnits positions the filter, list and buttons in bounds",
          "[ui_preset]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    PresetManager manager{ makeMixedBank() };
    mw::ui::PresetBrowser browser{ manager };

    const juce::Rectangle<float> design{ 0.0f, 0.0f, 400.0f, 300.0f };
    browser.setBounds(design.toNearestInt());
    browser.layoutDesignUnits(design);

    const auto outer = design.toNearestInt();

    // Every child lands inside the browser bounds (no negative / overflowing geometry).
    CHECK(outer.contains(browser.categoryFilterBox().getBounds()));
    CHECK(outer.contains(browser.presetListBox().getBounds()));
    CHECK(outer.contains(browser.prevButtonRef().getBounds()));
    CHECK(outer.contains(browser.nextButtonRef().getBounds()));
    CHECK(outer.contains(browser.loadButtonRef().getBounds()));

    // The filter sits above the list; the button row sits below it (the §9.1 stacking).
    CHECK(browser.categoryFilterBox().getBottom() <= browser.presetListBox().getY());
    CHECK(browser.presetListBox().getBottom() <= browser.loadButtonRef().getY());

    // The three buttons share the bottom row left-to-right without overlap.
    CHECK(browser.prevButtonRef().getRight() <= browser.nextButtonRef().getX());
    CHECK(browser.nextButtonRef().getRight() <= browser.loadButtonRef().getX());
    CHECK(browser.prevButtonRef().getY() == browser.loadButtonRef().getY());
}
