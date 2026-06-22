// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/UiAssemblyTest.cpp — JUCE-linked acceptance tests for the UI INTEGRATION
// that makes the panel real (task 114c, ui/MwAudioEditor.h). The editor (114) is a SHELL
// that owns the design->pixels AffineTransform but instantiated NO modules; this task
// instantiates all 12 panel modules / components as editor members, addAndMakeVisible's
// them in Z-order, and positions each in DESIGN units via the EditorLayoutConstants
// placement map [docs/design/10-ui.md §4, §5.1, §5.3; ADR-015 C1].
//
// Test-case display names begin with the task tag `ui_assembly` so
// `ctest -R ui_assembly --no-tests=error` selects exactly these cases (silent-pass rule).
//
// Acceptance (task 114c):
//   (a) The editor, built against a REAL MwAudioProcessor under ScopedJuceInitialiser_GUI,
//       has ALL 12 expected children present (by component ID), each WITHIN the editor
//       bounds (no out-of-bounds) and each non-zero-size.
//   (b) NO two FUNCTIONAL modules overlap in their laid-out rects. The BackgroundLayer
//       underlay + the ScopeMeterOverlay overlay are the intended exceptions and are
//       excluded from the disjointness check (§5.1).
//   (c) Resizing the editor (to a scale preset) re-lays-out with every child STILL within
//       bounds and the disjointness preserved.
//
// NON-VACUITY: each assertion checks a concrete, falsifiable property. A note in the PR
// body records the mutations that make this test FAIL (omit one addAndMakeVisible -> the
// findChildWithID for that ID returns null -> (a) fails; collide two placement rects in
// EditorLayoutConstants.h -> (b)/(c) fail).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"          // mw::plugin::MwAudioProcessor
#include "../../ui/MwAudioEditor.h"   // mw::ui::MwAudioEditor (unit under test)

using mw::plugin::MwAudioProcessor;
using mw::ui::MwAudioEditor;

namespace {

// The 12 panel modules / components, by the stable component ID the editor assigns in its
// constructor. The two render-layer exceptions (background underlay, scope overlay) are
// flagged so the disjointness check can exclude them per §5.1.
struct ExpectedChild
{
    const char* id;
    bool        functional;   // true => participates in the no-overlap contract
};

constexpr std::array<ExpectedChild, 12> kExpected{ {
    { "BackgroundLayer",   false }, // bottom cached chrome layer (underlay)
    { "ModulatorModule",   true  },
    { "VcoModule",         true  },
    { "SourceMixerModule", true  },
    { "VcfModule",         true  },
    { "VcaModule",         true  },
    { "ControllerStrip",   true  },
    { "TransportModeBar",  true  },
    { "SequencerGrid",     true  },
    { "PresetBrowser",     true  },
    { "StatusBanner",      true  },
    { "ScopeMeterOverlay", false }, // top telemetry overlay
} };

// Find a direct child of `editor` by component ID (the editor assigns these in its ctor).
juce::Component* childById(const MwAudioEditor& editor, const char* id)
{
    return editor.findChildWithID(juce::String(id));
}

// Assert every expected child is present, non-empty, and inside the editor bounds.
void requireAllChildrenPresentAndInBounds(const MwAudioEditor& editor)
{
    const auto editorBounds = editor.getLocalBounds();
    REQUIRE_FALSE(editorBounds.isEmpty());

    for (const auto& exp : kExpected)
    {
        auto* child = childById(editor, exp.id);
        INFO("expected child id: " << exp.id);
        REQUIRE(child != nullptr);

        const auto b = child->getBounds();
        REQUIRE_FALSE(b.isEmpty());          // non-zero size
        REQUIRE(b.getWidth()  > 0);
        REQUIRE(b.getHeight() > 0);
        REQUIRE(editorBounds.contains(b));   // entirely within the editor (no out-of-bounds)
    }
}

// Assert no two FUNCTIONAL modules overlap (the underlay + overlay are excluded).
void requireFunctionalModulesDisjoint(const MwAudioEditor& editor)
{
    std::vector<std::pair<const char*, juce::Rectangle<int>>> rects;
    for (const auto& exp : kExpected)
    {
        if (! exp.functional)
            continue;
        auto* child = childById(editor, exp.id);
        REQUIRE(child != nullptr);
        rects.emplace_back(exp.id, child->getBounds());
    }

    for (std::size_t i = 0; i < rects.size(); ++i)
        for (std::size_t j = i + 1; j < rects.size(); ++j)
        {
            const auto inter = rects[i].second.getIntersection(rects[j].second);
            INFO("overlap between " << rects[i].first << " and " << rects[j].first);
            REQUIRE(inter.isEmpty());
        }
}

} // namespace

TEST_CASE("ui_assembly instantiates all twelve panel children within bounds and non-empty",
          "[ui_assembly]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    auto editor = std::make_unique<MwAudioEditor>(processor);

    // (a) every expected child is present, sized, and within the editor.
    requireAllChildrenPresentAndInBounds(*editor);

    // Z-order sanity: the BackgroundLayer is the BOTTOM child and the ScopeMeterOverlay is
    // the TOP child, per §5.1 (underlay below the functional modules; overlay above them).
    auto* background = childById(*editor, "BackgroundLayer");
    auto* scope      = childById(*editor, "ScopeMeterOverlay");
    REQUIRE(background != nullptr);
    REQUIRE(scope != nullptr);
    REQUIRE(editor->getIndexOfChildComponent(background) >= 0);
    REQUIRE(editor->getIndexOfChildComponent(scope) >= 0);
    REQUIRE(editor->getIndexOfChildComponent(background)
                < editor->getIndexOfChildComponent(scope));
    // The background underlay is the very bottom of the Z-order.
    REQUIRE(editor->getIndexOfChildComponent(background) == 0);
}

TEST_CASE("ui_assembly functional module placement rects are disjoint",
          "[ui_assembly]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    auto editor = std::make_unique<MwAudioEditor>(processor);

    // (b) no two functional modules overlap (background underlay + scope overlay excluded).
    requireFunctionalModulesDisjoint(*editor);
}

TEST_CASE("ui_assembly re-lays-out within bounds and disjoint across every scale preset",
          "[ui_assembly]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    auto editor = std::make_unique<MwAudioEditor>(processor);

    // (c) snap through every scale preset (75/100/150/200%) and re-verify the full contract
    // after each relayout: still all-present, in-bounds, and disjoint (no overlap appears or
    // out-of-bounds creep as the single AffineTransform rescales).
    const int n = MwAudioEditor::getNumScalePresets();
    REQUIRE(n > 1);   // there is a real range to exercise (not a single fixed size)

    for (int i = 0; i < n; ++i)
    {
        editor->applyScalePreset(i);

        requireAllChildrenPresentAndInBounds(*editor);
        requireFunctionalModulesDisjoint(*editor);
    }

    // An explicit asymmetric resize (a raw setSize, not a snap) also re-lays-out cleanly:
    // the disjointness + in-bounds invariants hold for any constrainer-allowed size, not
    // only the snap presets.
    editor->setSize(juce::roundToInt(MwAudioEditor::designWidth() * 1.25f),
                    juce::roundToInt(MwAudioEditor::designHeight() * 1.25f));
    requireAllChildrenPresentAndInBounds(*editor);
    requireFunctionalModulesDisjoint(*editor);
}
