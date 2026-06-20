// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/PresetBrowser.cpp — implementation of the THIN preset-browser view declared
// in ui/PresetBrowser.h [docs/design/10-ui.md §9.1, §9.2, §9.3; ADR-015 C6; ADR-008 C14,
// C19].
//
// BUILD WIRING: this .cpp lives under plugin/ (not ui/) because the build only auto-globs
// plugin/**/*.cpp into the plugin target + mw101_plugin_tests (CONFIGURE_DEPENDS). The
// design-faithful header stays at ui/PresetBrowser.h and is reached by a relative include
// — no shared CMakeLists edit (mirrors plugin/ui/MwAudioLookAndFeel.cpp).

#include "../../ui/PresetBrowser.h"

#include "../../core/calibration/PresetBrowserConstants.h"

#include "preset/PresetManager.h"   // mw::plugin::preset::PresetManager (the bank, task 119)

namespace mw::ui {

namespace {
namespace cal = mw::cal::ui::presetBrowser;

// The 1-based ComboBox item ID of the "All categories" (no-filter) sentinel. Real §6.5
// categories follow at IDs 2..(kCategoryCount+1), in canonical order.
constexpr int kAllCategoriesItemId = 1;

juce::Colour toColour(const Colour& c) noexcept { return juce::Colour(c.argb); }

// Map the JUCE-free FontSpec style flag (0 plain / 1 bold / 2 italic) to a JUCE
// FontOptions style string, then build the font. Mirrors MwAudioLookAndFeel::toFont but
// kept local so this view needs no LookAndFeel dependency.
juce::Font toFont(const FontSpec& f)
{
    const char* style = (f.style == 1) ? "Bold" : (f.style == 2) ? "Italic" : "Regular";
    return juce::Font(juce::FontOptions{}
                          .withName(juce::String(f.family))
                          .withStyle(style)
                          .withHeight(f.height));
}
} // namespace

PresetBrowser::PresetBrowser(mw::plugin::preset::PresetManager& manager,
                             juce::ChangeBroadcaster* sourceBroadcaster)
    : manager_(manager), source_(sourceBroadcaster)
{
    // --- Category filter: §6.5 taxonomy, NOT re-minted (§9.3; ADR-008 C14) ----------
    // Item 1 is the "All" sentinel (no filter); items 2.. are the six §6.5 categories in
    // canonical order, read from the centralized taxonomy table (the single source).
    categoryFilter_.addItem(cal::kAllCategoriesLabel, kAllCategoriesItemId);
    for (int i = 0; i < cal::kCategoryCount; ++i)
        categoryFilter_.addItem(juce::String::fromUTF8(cal::kCategories[i]),
                                kAllCategoriesItemId + 1 + i);
    categoryFilter_.setSelectedId(kAllCategoriesItemId, juce::dontSendNotification);
    categoryFilter_.onChange = [this] { refreshList(); };
    addAndMakeVisible(categoryFilter_);

    // --- The preset ListBox (names + category; this view is its model) --------------
    presetList_.setModel(this);
    presetList_.setRowHeight(juce::roundToInt(cal::kListRowHeight));
    addAndMakeVisible(presetList_);

    // --- Prev / Next / Load buttons -------------------------------------------------
    prevButton_.setButtonText("Prev");
    nextButton_.setButtonText("Next");
    loadButton_.setButtonText("Load");
    prevButton_.onClick = [this] { selectRelative(-1); };
    nextButton_.onClick = [this] { selectRelative(+1); };
    loadButton_.onClick = [this] { loadSelected(); };
    addAndMakeVisible(prevButton_);
    addAndMakeVisible(nextButton_);
    addAndMakeVisible(loadButton_);

    // --- Refresh on the manager's broadcaster, NOT by polling (§9.1) ----------------
    if (source_ != nullptr)
        source_->addChangeListener(this);

    // Seed the initial list from the current bank state.
    refreshList();
}

PresetBrowser::~PresetBrowser()
{
    if (source_ != nullptr)
        source_->removeChangeListener(this);

    // Detach the model before destruction so the ListBox never calls back into a
    // half-destroyed view.
    presetList_.setModel(nullptr);
}

void PresetBrowser::setTokens(const DesignTokens& tokens)
{
    tokens_ = tokens;
    // The ListBox background reads from the token panel colour so a reskin restyles the
    // list with no code change [§6.1; ADR-015 C10].
    presetList_.setColour(juce::ListBox::backgroundColourId, toColour(tokens_.panel));
    presetList_.setColour(juce::ListBox::outlineColourId, toColour(tokens_.moduleOutline));
    repaint();
}

// ---------------------------------------------------------------------------
// Filter / selection introspection
// ---------------------------------------------------------------------------
juce::String PresetBrowser::selectedCategory() const
{
    const int id = categoryFilter_.getSelectedId();
    if (id <= kAllCategoriesItemId)
        return {};   // "All" => no filter

    const int catIndex = id - (kAllCategoriesItemId + 1);
    if (catIndex < 0 || catIndex >= cal::kCategoryCount)
        return {};
    return juce::String::fromUTF8(cal::kCategories[catIndex]);
}

int PresetBrowser::numVisibleRows() const noexcept
{
    return visibleIndices_.size();
}

int PresetBrowser::absoluteIndexForRow(int row) const noexcept
{
    if (row < 0 || row >= visibleIndices_.size())
        return -1;
    return visibleIndices_.getUnchecked(row);
}

void PresetBrowser::setSelectedRow(int row)
{
    if (visibleIndices_.isEmpty())
        return;
    const int clamped = juce::jlimit(0, visibleIndices_.size() - 1, row);
    presetList_.selectRow(clamped);
}

int PresetBrowser::selectedRow() const noexcept
{
    return presetList_.getSelectedRow();
}

// ---------------------------------------------------------------------------
// Refresh: re-read the bank for the active filter (broadcaster callback or direct call)
// ---------------------------------------------------------------------------
void PresetBrowser::rebuildVisibleIndices()
{
    visibleIndices_.clearQuick();

    const juce::String category = selectedCategory();
    if (category.isEmpty())
    {
        // No filter: every bank slot is visible, in bank order.
        const int n = manager_.getNumPresets();
        for (int i = 0; i < n; ++i)
            visibleIndices_.add(i);
    }
    else
    {
        // Narrow to the §6.5 category via the manager — the view does NOT re-derive the
        // grouping, it asks the bank [§9.1].
        visibleIndices_ = manager_.indicesForCategory(category);
    }
}

void PresetBrowser::refreshList()
{
    const int previousAbsolute = absoluteIndexForRow(presetList_.getSelectedRow());

    rebuildVisibleIndices();
    presetList_.updateContent();

    // Preserve the selection across a refresh where possible: if the previously-selected
    // preset is still visible, reselect its (possibly moved) row; else clear.
    if (previousAbsolute >= 0)
    {
        const int newRow = visibleIndices_.indexOf(previousAbsolute);
        if (newRow >= 0)
            presetList_.selectRow(newRow, false, true);
        else
            presetList_.deselectAllRows();
    }

    presetList_.repaint();
}

// ---------------------------------------------------------------------------
// ChangeListener: refresh on the manager's broadcaster, never on a poll (§9.1)
// ---------------------------------------------------------------------------
void PresetBrowser::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshList();
}

// ---------------------------------------------------------------------------
// ListBoxModel: rows are the FILTERED bank view
// ---------------------------------------------------------------------------
int PresetBrowser::getNumRows()
{
    return visibleIndices_.size();
}

void PresetBrowser::paintListBoxItem(int rowNumber, juce::Graphics& g, int width,
                                     int height, bool rowIsSelected)
{
    const int absolute = absoluteIndexForRow(rowNumber);
    if (absolute < 0)
        return;

    const auto bounds = juce::Rectangle<int>(0, 0, width, height);

    if (rowIsSelected)
    {
        g.setColour(toColour(tokens_.controlFill));
        g.fillRect(bounds);
        g.setColour(toColour(tokens_.background));
    }
    else
    {
        g.setColour(toColour(tokens_.textPrimary));
    }

    g.setFont(toFont(tokens_.labelFont));

    // Name on the left, category on the right — the two fields the row shows [§9.1].
    const auto name = manager_.getName(absolute);
    const auto category = manager_.getCategory(absolute);

    const int pad = height / 4;
    auto text = bounds.reduced(pad, 0);
    auto right = text.removeFromRight(text.getWidth() / 3);
    g.drawText(name, text, juce::Justification::centredLeft, true);

    if (! rowIsSelected)
        g.setColour(toColour(tokens_.textSecondary));
    g.drawText(category, right, juce::Justification::centredRight, true);
}

void PresetBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    presetList_.selectRow(row);
    loadSelected();
}

// ---------------------------------------------------------------------------
// Prev / Next / Load
// ---------------------------------------------------------------------------
void PresetBrowser::selectRelative(int delta)
{
    if (visibleIndices_.isEmpty())
        return;

    const int current = presetList_.getSelectedRow();
    // No current selection => Next starts at row 0, Prev starts at the last row.
    const int base = (current >= 0) ? current : (delta > 0 ? -1 : visibleIndices_.size());
    const int target = juce::jlimit(0, visibleIndices_.size() - 1, base + delta);
    presetList_.selectRow(target);
}

void PresetBrowser::loadSelected()
{
    // Pure VIEW: invoke the message-thread load sink with the chosen ABSOLUTE bank index.
    // The actual apply (PresetManager::loadPreset, owned by the processor) runs on the
    // message thread and hands the audio thread only parameter VALUES via APVTS atomics —
    // no tree pointer crosses through this view [§9.3; ADR-015 C6; ADR-008 C19].
    const int absolute = absoluteIndexForRow(presetList_.getSelectedRow());
    if (absolute < 0 || ! onLoadRequested)
        return;
    onLoadRequested(absolute);
}

// ---------------------------------------------------------------------------
// Layout: design units only, no pixel math (§9.1)
// ---------------------------------------------------------------------------
void PresetBrowser::layoutDesignUnits(juce::Rectangle<float> designBounds)
{
    auto area = designBounds;

    // Uniform inset (fraction of the smaller dimension) so children never touch the edge.
    const float inset = juce::jmin(designBounds.getWidth(), designBounds.getHeight())
                        * cal::kContentInsetFraction;
    area.reduce(inset, inset);

    const float gap = designBounds.getHeight() * cal::kRowGapFraction;

    // Top: the category-filter row.
    auto filterRow = area.removeFromTop(designBounds.getHeight() * cal::kFilterRowHeightFraction);
    categoryFilter_.setBounds(filterRow.toNearestInt());
    area.removeFromTop(gap);

    // Bottom: the prev/next/load button row.
    auto buttonRow = area.removeFromBottom(designBounds.getHeight() * cal::kButtonRowHeightFraction);
    area.removeFromBottom(gap);

    // Middle: the preset list fills the remainder.
    presetList_.setBounds(area.toNearestInt());

    // Lay the three buttons across the bottom row with a proportional gap.
    constexpr int n = cal::kButtonCount;
    const float bgap   = (buttonRow.getWidth() / static_cast<float>(n)) * cal::kButtonGapFraction;
    const float btnW   = (buttonRow.getWidth() - bgap * static_cast<float>(n - 1))
                             / static_cast<float>(n);

    auto placeButton = [&](int index, juce::Component& button)
    {
        const float x = buttonRow.getX() + static_cast<float>(index) * (btnW + bgap);
        button.setBounds(juce::Rectangle<float>{ x, buttonRow.getY(), btnW,
                                                 buttonRow.getHeight() }.toNearestInt());
    };
    placeButton(0, prevButton_);
    placeButton(1, nextButton_);
    placeButton(2, loadButton_);
}

void PresetBrowser::resized()
{
    layoutDesignUnits(getLocalBounds().toFloat());
}

} // namespace mw::ui
