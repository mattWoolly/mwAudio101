// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/PresetBrowser.h — the THIN preset-browser editor view over the processor-owned
// PresetManager [docs/design/10-ui.md §9.1, §9.2, §9.3; ADR-015 C6; ADR-008 §6, C14,
// C19].
//
// OWNERSHIP BOUNDARY (§9.1). The PresetManager lives in the PROCESSOR, not the editor.
// PresetBrowser is a THIN VIEW: it holds a REFERENCE to that manager and only ever
//   • lists  — reads getNumPresets()/getName()/getCategory() into a juce::ListBox,
//   • filters — narrows the list to a §6.5 category via indicesForCategory(), and
//   • loads  — invokes a message-thread LOAD SINK (a std::function set by the editor)
//              with the chosen ABSOLUTE bank index.
// It NEVER duplicates bank logic, never parses a preset, never owns the on-disk format,
// migration, or the factory bank (those are the state-presets / preset-format streams,
// OUT OF SCOPE) [§9.1; §9.2].
//
// LOAD IS MESSAGE-THREAD ONLY; NO TREE POINTER CROSSES TO AUDIO (§9.3). The actual apply
// (PresetManager::loadPreset, which builds/swaps the state tree on the message thread and
// hands the audio thread parameter VALUES via APVTS atomics — never a tree pointer) is
// owned by the processor. The browser's role is purely to invoke `loadPreset(index)` on
// the message thread; it expresses that as the `onLoadRequested(absoluteIndex)` sink the
// editor wires to the processor's recall path. No tree pointer and no alloc/free crosses
// to the audio thread through this view [§9.3; ADR-015 C6; ADR-008 C19]. (The real task-
// 119 PresetManager::loadPreset takes apvts/extras/report owned by the processor, so the
// view cannot — and must not — call it directly; the sink keeps the view a pure VIEW.)
//
// REFRESH ON A BROADCASTER, NOT BY POLLING (§9.1). The view is a juce::ChangeListener; it
// re-reads the bank in refreshList() only when its source broadcaster fires (e.g. after a
// load/save changes the bank), never on a timer/poll. The broadcaster is supplied at
// construction (the processor owns it); when none is supplied the view still works (a
// host can drive refreshList() directly), so the no-broadcaster task-119 manager is fine.
//
// CATEGORY FILTER ENUMERATES THE SCHEMA TAXONOMY, NOT RE-MINTED (§9.3). The filter combo
// is populated from the §6.5 category strings centralized in
// core/calibration/PresetBrowserConstants.h (which mirror the schema's category enum /
// ADR-008 C14 1:1); the strings are not re-minted inline at the control site [§9.3].
//
// layoutDesignUnits() positions the children in DESIGN units only (no pixel math); the
// proportions are the (PI) constants in PresetBrowserConstants.h [§9.1].
//
// BUILD WIRING: this header is JUCE-built and lives at the design-faithful path
// ui/PresetBrowser.h; its .cpp lives under plugin/ui/PresetBrowser.cpp so the plugin glob
// compiles it (mirrors plugin/ui/MwAudioLookAndFeel.cpp). No shared CMakeLists edit.

#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DesignTokens.h"   // mw::ui::DesignTokens (reskin tokens, §6.1)

// The processor-owned bank this view reads. Declared by task 119; the view depends only
// on its narrow query surface (getNumPresets/getName/getCategory/indicesForCategory).
namespace mw::plugin::preset { class PresetManager; }

namespace mw::ui {

class PresetBrowser final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::ListBoxModel
{
public:
    // Construct over the processor-owned manager. `sourceBroadcaster` is the change
    // source the view listens to for refreshes (the processor owns it); pass nullptr if
    // the bank has no broadcaster yet (the view still works — refreshList() can be driven
    // directly) [§9.1].
    explicit PresetBrowser(mw::plugin::preset::PresetManager& manager,
                           juce::ChangeBroadcaster* sourceBroadcaster = nullptr);
    ~PresetBrowser() override;

    // Inject the design tokens so a reskin restyles the view with no code change
    // [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // The message-thread LOAD SINK. The editor sets this to the processor's recall path;
    // the view invokes it with the chosen ABSOLUTE bank index when Load is pressed (or a
    // row is double-clicked). The view owns NO apply logic — this keeps it a pure VIEW and
    // guarantees no tree pointer crosses to audio through it [§9.3; ADR-015 C6].
    std::function<void(int absoluteIndex)> onLoadRequested;

    // Lay the children out (filter row / list / button row) in DESIGN units only (no
    // pixel math); the proportions are the (PI) constants in PresetBrowserConstants.h
    // [§9.1].
    void layoutDesignUnits(juce::Rectangle<float> designBounds);

    // juce::Component override: forward the integer bounds into the design-unit layout.
    void resized() override;

    // Re-read names/categories from the manager, honouring the active category filter,
    // and refresh the ListBox. Runs on the message thread (broadcaster callback or a
    // direct call); never polls [§9.1].
    void refreshList();

    // --- Filter / selection introspection (for the editor and tests) ---------------
    // The currently-selected category filter string, or empty when "All" is selected
    // (no filter). Always one of the §6.5 strings or empty [§9.3].
    [[nodiscard]] juce::String selectedCategory() const;

    // The number of rows currently visible (post-filter). Equals getNumPresets() when no
    // filter is active.
    [[nodiscard]] int numVisibleRows() const noexcept;

    // Map a visible row to its ABSOLUTE bank index (-1 if out of range). The list shows a
    // FILTERED view, so a row index is not the bank index when a category is selected.
    [[nodiscard]] int absoluteIndexForRow(int row) const noexcept;

    // Select a visible row (clamped); the next loadSelected()/Prev/Next acts on it.
    void setSelectedRow(int row);
    [[nodiscard]] int selectedRow() const noexcept;

    // Invoke the message-thread load sink with the ABSOLUTE index of the selected row.
    // This is the load action the Load button and a row double-click trigger; exposed so
    // a host (or a headless test) can drive the load deterministically without a live
    // message loop. A no-op if nothing is selected or no sink is wired [§9.3].
    void loadSelected();

    // --- Control access (for parents and tests) -----------------------------------
    juce::ListBox&    presetListBox()    noexcept { return presetList_; }
    juce::ComboBox&   categoryFilterBox() noexcept { return categoryFilter_; }
    juce::TextButton& prevButtonRef()    noexcept { return prevButton_; }
    juce::TextButton& nextButtonRef()    noexcept { return nextButton_; }
    juce::TextButton& loadButtonRef()    noexcept { return loadButton_; }

private:
    // --- ChangeListener: refresh on the manager's broadcaster, not by polling (§9.1) --
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // --- ListBoxModel: the rows are the FILTERED bank view -------------------------
    int  getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    // Re-read the visible-index map from the manager for the active filter (§9.1).
    void rebuildVisibleIndices();

    // Move the selection one row earlier / later (clamped); used by Prev / Next.
    void selectRelative(int delta);

    mw::plugin::preset::PresetManager& manager_;   // processor-owned (a reference, §9.1)
    juce::ChangeBroadcaster*           source_ = nullptr;  // the refresh source (or null)

    juce::ListBox    presetList_;        // names + category, the FILTERED bank view
    juce::ComboBox   categoryFilter_;    // §6.5 taxonomy + "All" (ADR-008 C14)
    juce::TextButton prevButton_;
    juce::TextButton nextButton_;
    juce::TextButton loadButton_;

    // The absolute bank indices currently visible, in row order. Empty filter => 0..N-1;
    // a category filter => the manager's indicesForCategory() result [§9.1].
    juce::Array<int> visibleIndices_;

    DesignTokens tokens_ = DesignTokens::defaultTheme();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};

} // namespace mw::ui
