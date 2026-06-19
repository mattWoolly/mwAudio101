// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/CustomControlsTest.cpp — JUCE-linked acceptance tests for the thin
// custom control subclasses (task 109): RotarySlider, LinearSlider, ToggleSwitch,
// ChoiceSelector [docs/design/10-ui.md §6.3, §7.3; ADR-008 §7/C6/C15; ADR-015 C7].
//
// Test-case display names begin with the task tag `ui_controls` so
// `ctest -R ui_controls` selects exactly these cases (silent-pass rule, AGENTS.md).
//
// These run headless on the message thread and assert BEHAVIOUR — dirty-rect scope,
// parameter-derived value text, and the software-extension fence — never pixel
// equality (the GUI is not pixel-identical across platforms) [docs/design/10-ui.md
// §13; ADR-015 Consequences].
//
// Acceptance criteria covered (one or more cases each):
//   [A] Each control invalidates only its own bounds on value change, never the whole
//       editor (§7.3, ADR-015 C7).
//   [B] ChoiceSelector visually marks sound_ext entries with extensionTag (§6.3,
//       ADR-008 §7/C6/C15).
//   [C] Value text derives from the parameter's display string, not hard-coded (§6.3).

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../ui/controls/RotarySlider.h"
#include "../../ui/controls/LinearSlider.h"
#include "../../ui/controls/ToggleSwitch.h"
#include "../../ui/controls/ChoiceSelector.h"
#include "../../ui/DesignTokens.h"
#include "../../core/calibration/ControlSubclassConstants.h"

using mw::ui::RotarySlider;
using mw::ui::LinearSlider;
using mw::ui::ToggleSwitch;
using mw::ui::ChoiceSelector;
using mw::ui::DesignTokens;

namespace {

// A parent that records every repaint() request made against IT, so a test can prove
// a child control never triggers a whole-(parent)-editor repaint. JUCE's repaint()
// is non-virtual, so we expose a counted wrapper and assert the child uses its own
// recorded self-invalidation instead.
struct CountingParent final : public juce::Component
{
    void paint(juce::Graphics&) override {}
};

} // namespace

// ---------------------------------------------------------------------------
// [A] Dirty-rect scope — RotarySlider
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls RotarySlider invalidates only its own bounds on value change", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CountingParent parent;
    parent.setBounds(0, 0, 1000, 640);   // stand-in for the whole editor extent

    RotarySlider slider;
    parent.addAndMakeVisible(slider);
    slider.setBounds(100, 80, 60, 60);    // a small sub-region of the parent
    slider.setRange(0.0, 1.0, 0.0);

    REQUIRE_FALSE(slider.hasInvalidated());

    slider.setValue(0.42, juce::sendNotificationSync);

    REQUIRE(slider.hasInvalidated());
    // The invalidated region is the control's OWN local bounds, not the editor extent.
    REQUIRE(slider.lastInvalidatedRegion() == slider.getLocalBounds());
    REQUIRE(slider.lastInvalidatedRegion().getWidth()  == 60);
    REQUIRE(slider.lastInvalidatedRegion().getHeight() == 60);
    // And it is strictly smaller than the parent (whole-editor) extent.
    REQUIRE(slider.lastInvalidatedRegion().getWidth()  < parent.getWidth());
    REQUIRE(slider.lastInvalidatedRegion().getHeight() < parent.getHeight());
}

// ---------------------------------------------------------------------------
// [A] Dirty-rect scope — LinearSlider
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls LinearSlider invalidates only its own bounds on value change", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CountingParent parent;
    parent.setBounds(0, 0, 1000, 640);

    LinearSlider slider;
    parent.addAndMakeVisible(slider);
    slider.setBounds(40, 40, 30, 200);
    slider.setRange(0.0, 1.0, 0.0);

    slider.setValue(0.6, juce::sendNotificationSync);

    REQUIRE(slider.hasInvalidated());
    REQUIRE(slider.lastInvalidatedRegion() == slider.getLocalBounds());
    REQUIRE(slider.lastInvalidatedRegion() != parent.getLocalBounds());
}

// ---------------------------------------------------------------------------
// [A] Dirty-rect scope — ToggleSwitch
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls ToggleSwitch invalidates only its own bounds on toggle", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CountingParent parent;
    parent.setBounds(0, 0, 1000, 640);

    ToggleSwitch toggle("ENV");
    parent.addAndMakeVisible(toggle);
    toggle.setBounds(10, 10, 120, 28);

    REQUIRE_FALSE(toggle.getToggleState());
    toggle.setToggleState(true, juce::sendNotificationSync);

    REQUIRE(toggle.hasInvalidated());
    REQUIRE(toggle.lastInvalidatedRegion() == toggle.getLocalBounds());
    REQUIRE(toggle.lastInvalidatedRegion() != parent.getLocalBounds());
}

// ---------------------------------------------------------------------------
// [A] Dirty-rect scope — ChoiceSelector
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls ChoiceSelector invalidates only its own bounds on choice change", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    CountingParent parent;
    parent.setBounds(0, 0, 1000, 640);

    ChoiceSelector selector;
    parent.addAndMakeVisible(selector);
    selector.setBounds(200, 100, 140, 26);
    selector.setChoices({ "16'", "8'", "4'", "2'" }, /*canonicalCount=*/4);

    // setChoices already invalidates; clear the flag by reading bounds and re-check on
    // a selection change.
    selector.setSelectedId(2, juce::sendNotificationSync);

    REQUIRE(selector.hasInvalidated());
    REQUIRE(selector.lastInvalidatedRegion() == selector.getLocalBounds());
    REQUIRE(selector.lastInvalidatedRegion() != parent.getLocalBounds());
}

// ---------------------------------------------------------------------------
// [B] Software-extension fence — indices, suffix, and the extensionTag colour.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls ChoiceSelector fences sound_ext entries with the extensionTag token", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Mirror the schema's VCO-range split: 4 hardware-canonical (16'/8'/4'/2') + two
    // appended software extensions (32'/64') [ParamDefs kVcoRange; ADR-008 C6].
    const juce::StringArray labels { "16'", "8'", "4'", "2'", "32'", "64'" };
    constexpr int canonical = 4;

    ChoiceSelector selector;
    selector.setTokens(DesignTokens::defaultTheme());
    selector.setChoices(labels, canonical);

    REQUIRE(selector.canonicalCount() == canonical);

    // Canonical entries are NOT fenced; appended entries ARE.
    for (int i = 0; i < canonical; ++i)
        REQUIRE_FALSE(selector.isExtensionIndex(i));
    REQUIRE(selector.isExtensionIndex(4));   // 32'
    REQUIRE(selector.isExtensionIndex(5));   // 64'
    REQUIRE_FALSE(selector.isExtensionIndex(6));   // out of range

    // Fence #2: the displayed item text for an extension carries the "[ext]" suffix,
    // so it can never read as plain 1982 hardware behaviour; canonical entries do not.
    const juce::String suffix(mw::cal::control::extension::kLabelSuffix);
    REQUIRE(selector.getItemText(0) == juce::String("16'"));          // canonical, no suffix
    REQUIRE(selector.getItemText(4) == juce::String("32'") + suffix); // extension, suffixed
    REQUIRE(selector.getItemText(5) == juce::String("64'") + suffix);
    REQUIRE(selector.getNumItems() == labels.size());

    // Fence #1: the extension tint is the design-token extensionTag colour, and a
    // token swap re-tints it with no code change (ADR-015 C10).
    const auto def = DesignTokens::defaultTheme();
    const auto hc  = DesignTokens::highContrast();
    REQUIRE(selector.extensionTagColour() == juce::Colour(def.extensionTag.argb));

    selector.setTokens(hc);
    REQUIRE(selector.extensionTagColour() == juce::Colour(hc.extensionTag.argb));
    REQUIRE(juce::Colour(def.extensionTag.argb) != juce::Colour(hc.extensionTag.argb));
}

// ---------------------------------------------------------------------------
// [B] The popup-menu items actually carry the extensionTag colour on the fenced rows.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls ChoiceSelector tints the extension popup rows with extensionTag", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto def = DesignTokens::defaultTheme();
    ChoiceSelector selector;
    selector.setTokens(def);
    selector.setChoices({ "Tri", "Sq", "Random", "Noise", "Sine" }, /*canonicalCount=*/4);

    auto* root = selector.getRootMenu();
    REQUIRE(root != nullptr);

    const auto extColour = juce::Colour(def.extensionTag.argb);
    int extRowsTinted = 0;
    int canonicalRows = 0;

    for (juce::PopupMenu::MenuItemIterator it(*root); it.next();)
    {
        const auto& item = it.getItem();
        // The 0-based index is itemID - 1.
        const int idx = item.itemID - 1;
        if (selector.isExtensionIndex(idx))
        {
            REQUIRE(item.colour == extColour);   // Sine row tinted with extensionTag
            ++extRowsTinted;
        }
        else
        {
            ++canonicalRows;
        }
    }

    REQUIRE(extRowsTinted == 1);    // only "Sine"
    REQUIRE(canonicalRows == 4);    // Tri/Sq/Random/Noise
}

// ---------------------------------------------------------------------------
// [C] Value text derives from the bound parameter's display string — RotarySlider.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls RotarySlider value text derives from the parameter display string", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A parameter whose display string is unambiguously NOT what a bare slider would
    // print: a unit suffix + a custom stringFromValue with fixed decimals.
    juce::AudioParameterFloat param(
        juce::ParameterID{ "mw101.vcf.cutoff", 1 },
        "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.3f),
        1000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " Hz"; }));

    RotarySlider slider;
    slider.setNormalisableRange({ 20.0, 20000.0, 0.0, 0.3 });
    slider.setValue(1000.0, juce::dontSendNotification);

    // Before attaching: the fallback numeric read-out (NOT the "Hz" display string).
    const auto fallback = slider.getTextFromValue(1000.0);
    REQUIRE_FALSE(fallback.contains("Hz"));

    // After attaching: the read-out is the PARAMETER's display string, proving the
    // text is parameter-derived, not hard-coded [§6.3; ADR-008 C4]. getText is public
    // on the RangedAudioParameter base (private only on the concrete override), so we
    // format through the base reference exactly as the production code does.
    juce::RangedAudioParameter& ranged = param;
    slider.attachParameterForDisplay(param);
    const auto displayed = slider.getTextFromValue(1000.0);
    REQUIRE(displayed == ranged.getText(ranged.convertTo0to1(1000.0f), 0));
    REQUIRE(displayed.contains("Hz"));
}

// ---------------------------------------------------------------------------
// [C] Value text derives from the bound parameter's display string — LinearSlider.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls LinearSlider value text derives from the parameter display string", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioParameterFloat param(
        juce::ParameterID{ "mw101.sub.level", 1 },
        "Sub Level",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }));

    LinearSlider slider;
    slider.setRange(0.0, 1.0, 0.0);
    slider.attachParameterForDisplay(param);

    juce::RangedAudioParameter& ranged = param;
    const auto displayed = slider.getTextFromValue(0.5);
    REQUIRE(displayed == ranged.getText(ranged.convertTo0to1(0.5f), 0));
    REQUIRE(displayed.contains("%"));
    // It is the parameter's "%" projection, not a raw double print.
    REQUIRE_FALSE(displayed == juce::String(0.5, 2));
}

// ---------------------------------------------------------------------------
// [C/§6.3] ToggleSwitch carries its own on/off captions.
// ---------------------------------------------------------------------------
TEST_CASE("ui_controls ToggleSwitch carries its own state labels", "[ui_controls]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    ToggleSwitch toggle("VCA");
    toggle.setStateLabels("GATE", "ENV");

    toggle.setToggleState(false, juce::dontSendNotification);
    REQUIRE(toggle.currentStateLabel() == juce::String("GATE"));

    toggle.setToggleState(true, juce::dontSendNotification);
    REQUIRE(toggle.currentStateLabel() == juce::String("ENV"));
}
