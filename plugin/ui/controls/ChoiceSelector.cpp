// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/controls/ChoiceSelector.cpp — implementation of the thin choice-selector
// subclass declared in ui/controls/ChoiceSelector.h. Visually fences software-only
// ("sound_ext") entries with the extensionTag token + a "[ext]" label suffix
// [docs/design/10-ui.md §6.3; ADR-008 §7, C6, C15]. Lives under plugin/ for the
// auto-glob (see RotarySlider.cpp for the wiring note).

#include "../../../ui/controls/ChoiceSelector.h"

#include "../../../core/calibration/ControlSubclassConstants.h"

namespace mw::ui {

ChoiceSelector::ChoiceSelector()
{
    // Pre-seed the JUCE per-item text colour so the LookAndFeel default popup path
    // tints the fenced extensions even if it is not overridden. The token-driven
    // colour is re-applied per item in rebuildMenu().
}

void ChoiceSelector::setTokens(const DesignTokens& tokens)
{
    tokens_ = tokens;
    if (! baseLabels_.isEmpty())
        rebuildMenu();   // re-tint the fence with the (possibly swapped) token table
}

juce::Colour ChoiceSelector::extensionTagColour() const noexcept
{
    return juce::Colour(tokens_.extensionTag.argb);
}

void ChoiceSelector::setChoices(const juce::StringArray& labels, int canonicalCount)
{
    baseLabels_    = labels;
    canonicalCount_ = juce::jlimit(0, labels.size(), canonicalCount);
    rebuildMenu();
}

bool ChoiceSelector::isExtensionIndex(int zeroBasedIndex) const noexcept
{
    return zeroBasedIndex >= canonicalCount_ && zeroBasedIndex < baseLabels_.size();
}

void ChoiceSelector::rebuildMenu()
{
    const int previousSelectedId = getSelectedId();

    clear(juce::dontSendNotification);

    auto* root = getRootMenu();
    if (root == nullptr)
        return;
    root->clear();

    const auto extColour = extensionTagColour();
    const juce::String suffix(mw::cal::control::extension::kLabelSuffix);

    for (int i = 0; i < baseLabels_.size(); ++i)
    {
        const int itemId = i + 1;   // ComboBox item IDs are 1-based
        const bool isExt = isExtensionIndex(i);

        // Fence #2: the "[ext]" suffix is part of the displayed label, so the fence
        // survives a LookAndFeel that ignores per-item colour [ADR-008 C6/C15].
        juce::String text = baseLabels_[i];
        if (isExt)
            text << suffix;

        juce::PopupMenu::Item item(text);
        item.itemID = itemId;
        // Fence #1: tint the extension entry with the extensionTag token [§6.3; C10].
        if (isExt)
            item.colour = extColour;

        root->addItem(item);
    }

    // Preserve the selection across a rebuild where possible.
    if (previousSelectedId > 0 && previousSelectedId <= baseLabels_.size())
        setSelectedId(previousSelectedId, juce::dontSendNotification);

    invalidateOwnBounds();
}

void ChoiceSelector::invalidateOwnBounds()
{
    lastDirty_   = getLocalBounds();
    invalidated_ = true;
    repaint(lastDirty_);   // invalidate ONLY our own bounds [§7.3; ADR-015 C7]
}

} // namespace mw::ui
