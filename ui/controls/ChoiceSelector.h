// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// ui/controls/ChoiceSelector.h — a thin juce::ComboBox subclass for the stepped /
// choice controls of the signal-flow editor [docs/design/10-ui.md §6.3].
//
// The distinguishing responsibility (§6.3; ADR-008 §7, C6, C15): a ChoiceSelector
// VISUALLY FENCES software-only ("sound_ext") choice entries — the Sine LFO shape,
// the 32'/64' VCO registers — from the hardware-canonical options so they can never
// masquerade as documented 1982 hardware behaviour [research/12 §3.1]. Two,
// independent fences are applied to every extension entry:
//   1. its popup-menu item is tinted with the DesignTokens.extensionTag token, and
//   2. an ASCII "[ext]" suffix (from the calibration table) is appended to its label,
//      so the fence survives even a LookAndFeel that ignores per-item colour.
// The colour is supplied by the single design-token table, so a reskin re-tints the
// fence with no code change [ADR-015 C10].
//
// A choice change invalidates ONLY this control's own bounds, never the whole editor
// [§7.3; ADR-015 C7].
//
// The canonical-vs-extension split MIRRORS the schema registry (ParamDefs:
// choiceCount / canonicalChoiceCount, isSoftwareExt) — the editor never re-mints the
// fence; the module passes which indices are extensions when it populates the
// selector [ADR-008 C5/C6]. This control does not own the APVTS ComboBoxAttachment
// (that is the module's job, OUT OF SCOPE) nor the vector drawing (LookAndFeel,
// task 108). The .cpp lives under plugin/ui/controls/ so the plugin glob compiles it.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../DesignTokens.h"   // mw::ui::DesignTokens (extensionTag token)

namespace mw::ui {

class ChoiceSelector final : public juce::ComboBox
{
public:
    ChoiceSelector();
    ~ChoiceSelector() override = default;

    // Inject the design tokens so the extension fence is tinted with the
    // extensionTag colour (a reskin re-tints with no code change) [§6.1; ADR-015 C10].
    void setTokens(const DesignTokens& tokens);

    // Populate the option list, marking the entries at the given (0-based) indices as
    // software-only extensions. canonicalCount entries (indices 0..canonicalCount-1)
    // are hardware-canonical; entries at index >= canonicalCount are the appended
    // software extensions, mirroring the schema's choiceCount/canonicalChoiceCount
    // split [ADR-008 C5/C6]. Each extension entry is tinted with extensionTag and gets
    // the "[ext]" label suffix.
    void setChoices(const juce::StringArray& labels, int canonicalCount);

    // True iff the choice at this 0-based index is a fenced software extension.
    bool isExtensionIndex(int zeroBasedIndex) const noexcept;

    // The number of canonical (hardware) entries; entries at/above this are fenced.
    int canonicalCount() const noexcept { return canonicalCount_; }

    // The extensionTag colour the fence is drawn with (read from the injected tokens).
    juce::Colour extensionTagColour() const noexcept;

    // --- per-control dirty-rect discipline (§7.3; ADR-015 C7) ---------------------
    const juce::Rectangle<int>& lastInvalidatedRegion() const noexcept { return lastDirty_; }
    bool hasInvalidated() const noexcept { return invalidated_; }

private:
    void rebuildMenu();
    void invalidateOwnBounds();

    juce::StringArray baseLabels_;   // labels WITHOUT the extension suffix
    int canonicalCount_ = 0;
    DesignTokens tokens_ = DesignTokens::defaultTheme();

    juce::Rectangle<int> lastDirty_{};
    bool invalidated_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceSelector)
};

} // namespace mw::ui
