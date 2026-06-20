// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/MwAudioEditorTest.cpp — JUCE-linked acceptance tests for the editor
// root (MwAudioEditor, task 114). Test-case display names begin with the task tag
// `ui_editor` so `ctest -R ui_editor` selects exactly these cases (silent-pass rule).
//
// The GUI is NOT pixel-identical across platforms, so these tests assert GEOMETRY and
// BEHAVIOUR, never exact pixel layout [docs/design/10-ui.md §13; ADR-015 Consequences].
//
// Acceptance criteria covered (task 114 / §4 / ADR-015 C1, C2):
//   [1] Layout is in design units; resize changes only the single AffineTransform —
//       getDesignToPixels() maps known design points to the expected pixel points,
//       within tolerance, at every scale preset (§4.1, §4.2; C1).
//   [2] The constrainer holds the FROZEN aspect ratio across resizes (§4.3; C1).
//   [3] Scale presets (75/100/150/200%) snap the window and the size round-trips via
//       the processor's <extras>-UI accessor pair (§4.4; C2).
//   [4] getScaleFactor() reports the live fit factor; getStoredEditorSize() round-trips.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"          // mw::plugin::MwAudioProcessor
#include "../../ui/MwAudioEditor.h"   // mw::ui::MwAudioEditor

using mw::plugin::MwAudioProcessor;
using mw::ui::MwAudioEditor;

namespace {

// Absolute tolerance for pixel-point geometry assertions (sub-pixel; the transform is
// pure float arithmetic + integer window rounding).
constexpr float kPixelTol = 0.75f;

bool approx(float a, float b, float tol = kPixelTol) noexcept
{
    return std::abs(a - b) <= tol;
}

// Apply a juce::AffineTransform to a design-space point, returning the pixel point.
// juce::AffineTransform::transformPoint mutates its args in place, so wrap it.
juce::Point<float> mapPoint(const juce::AffineTransform& t, float x, float y) noexcept
{
    t.transformPoint(x, y);
    return { x, y };
}

} // namespace

TEST_CASE("ui_editor maps design origin and corner to expected pixels at each scale preset",
          "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    const float dW = MwAudioEditor::designWidth();
    const float dH = MwAudioEditor::designHeight();

    for (int p = 0; p < MwAudioEditor::getNumScalePresets(); ++p)
    {
        editor->applyScalePreset(p);

        const float scale = MwAudioEditor::scalePresetAt(p);

        // The window snapped to scale*design extent (aspect locked).
        REQUIRE(editor->getWidth()  == (int) std::lround(dW * scale));
        REQUIRE(editor->getHeight() == (int) std::lround(dH * scale));

        const auto t = editor->getDesignToPixels();

        // Design origin -> pixel origin (no letterboxing offset when aspect matches).
        const auto origin = mapPoint(t, 0.0f, 0.0f);
        REQUIRE(approx(origin.x, 0.0f));
        REQUIRE(approx(origin.y, 0.0f));

        // Design bottom-right corner (dW, dH) -> the window's pixel extent.
        const auto corner = mapPoint(t, dW, dH);
        REQUIRE(approx(corner.x, (float) editor->getWidth(),  1.0f));
        REQUIRE(approx(corner.y, (float) editor->getHeight(), 1.0f));

        // The midpoint maps to the window centre — proving uniform, undistorted scale.
        const auto mid = mapPoint(t, dW * 0.5f, dH * 0.5f);
        REQUIRE(approx(mid.x, editor->getWidth()  * 0.5f, 1.0f));
        REQUIRE(approx(mid.y, editor->getHeight() * 0.5f, 1.0f));

        // getScaleFactor() reports the live fit factor implied by the transform.
        REQUIRE(approx(editor->getScaleFactor(), scale, 0.01f));
    }
}

TEST_CASE("ui_editor only the AffineTransform changes across a resize (design-unit layout)",
          "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    const float dW = MwAudioEditor::designWidth();
    const float dH = MwAudioEditor::designHeight();

    // A fixed design-space probe point (centre). Its DESIGN-unit coordinates never
    // change; only the transform that maps it to pixels does — the C1 invariant.
    const float probeX = dW * 0.5f;
    const float probeY = dH * 0.5f;

    editor->applyScalePreset(1);  // 100%
    const float scale100 = editor->getScaleFactor();
    const auto probe100  = mapPoint(editor->getDesignToPixels(), probeX, probeY);

    editor->applyScalePreset(3);  // 200%
    const float scale200 = editor->getScaleFactor();
    const auto probe200  = mapPoint(editor->getDesignToPixels(), probeX, probeY);

    // The scale doubled; the same design point now lands at ~2x the pixel coordinate.
    REQUIRE(approx(scale200 / scale100, 2.0f, 0.01f));
    REQUIRE(approx(probe200.x / probe100.x, 2.0f, 0.02f));
    REQUIRE(approx(probe200.y / probe100.y, 2.0f, 0.02f));
}

TEST_CASE("ui_editor constrainer holds the fixed aspect ratio across resizes", "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    const double expectedAspect = (double) MwAudioEditor::aspectRatio();
    const auto& c = editor->constrainerForTest();

    // The constrainer is configured with the frozen design aspect ratio.
    REQUIRE(c.getFixedAspectRatio() == Catch::Approx(expectedAspect).margin(1.0e-6));

    // Drive the constrainer with an OFF-aspect requested rectangle: it must rewrite the
    // bounds back to the locked aspect (§4.3; ADR-015 C1).
    juce::Rectangle<int> bounds(0, 0,
                                (int) std::lround(MwAudioEditor::designWidth()  * 1.50f),
                                (int) std::lround(MwAudioEditor::designHeight() * 1.50f));
    const juce::Rectangle<int> previous = bounds;
    const juce::Rectangle<int> limits(0, 0, 100000, 100000);

    // Request a deliberately distorted (too-tall) size.
    juce::Rectangle<int> requested(0, 0, bounds.getWidth(), bounds.getHeight() * 2);
    auto* mutableConstrainer =
        const_cast<juce::ComponentBoundsConstrainer*>(&c);
    mutableConstrainer->checkBounds(requested, previous, limits,
                                    /*isStretchingTop*/ false, /*isStretchingLeft*/ false,
                                    /*isStretchingBottom*/ true, /*isStretchingRight*/ true);

    const double resultAspect = (double) requested.getWidth() / (double) requested.getHeight();
    REQUIRE(resultAspect == Catch::Approx(expectedAspect).margin(0.02));
}

TEST_CASE("ui_editor scale-preset size round-trips via the processor stored-size accessor",
          "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;

    // The accessor pair itself round-trips a value (the narrow <extras>-UI seam).
    processor.setStoredEditorSize({ 1234, 789 });
    REQUIRE(processor.getStoredEditorSize() == juce::Point<int>(1234, 789));

    // An editor writes its size on resize/preset; a freshly-constructed editor then
    // restores that persisted size on session reload (§4.4; ADR-015 C2).
    {
        auto editor = std::make_unique<MwAudioEditor>(processor);
        editor->applyScalePreset(2);  // 150%
        const int w = editor->getWidth();
        const int h = editor->getHeight();
        REQUIRE(processor.getStoredEditorSize() == juce::Point<int>(w, h));
    }

    const juce::Point<int> persisted = processor.getStoredEditorSize();
    {
        auto reopened = std::make_unique<MwAudioEditor>(processor);
        // The reopened editor adopts the persisted size, not the default scale.
        REQUIRE(reopened->getWidth()  == persisted.x);
        REQUIRE(reopened->getHeight() == persisted.y);
    }
}

TEST_CASE("ui_editor stored editor size persists through getState/setState across instances",
          "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A non-default size, stored on a SOURCE processor, then serialized to an opaque host
    // blob via getStateInformation — the real persistence path (§4.4; ADR-015 C2). Use a
    // size the editor would never default to, so a pass cannot come from the default.
    constexpr int kPersistW = 1500;
    constexpr int kPersistH = 960;

    juce::MemoryBlock blob;
    {
        MwAudioProcessor source;
        source.setStoredEditorSize({ kPersistW, kPersistH });
        source.getStateInformation(blob);
        REQUIRE(blob.getSize() > 0);
    }

    // A FRESH processor (as if the session reopened) starts with no stored size, then
    // adopts the persisted size after setStateInformation reads it back out of <extras>.
    MwAudioProcessor reopened;
    REQUIRE(reopened.getStoredEditorSize() == juce::Point<int>(0, 0));

    reopened.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(reopened.getStoredEditorSize() == juce::Point<int>(kPersistW, kPersistH));

    // And a freshly-created editor on the reopened processor opens at the persisted size,
    // not the default scale — the end-to-end §4.4 reload contract.
    auto editor = std::make_unique<MwAudioEditor>(reopened);
    REQUIRE(editor->getWidth()  == kPersistW);
    REQUIRE(editor->getHeight() == kPersistH);
}

TEST_CASE("ui_editor a state blob without a stored UI size loads and leaves the default",
          "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A processor that NEVER set an editor size writes a blob with NO uiWidth/uiHeight
    // (the keys are omitted for a {0,0} size). It must still load cleanly, and the
    // reopened processor must keep "no stored size" so the editor uses the default scale
    // — backward compatibility with pre-persistence blobs (ADR-021 fallback).
    juce::MemoryBlock blob;
    {
        MwAudioProcessor source;   // no setStoredEditorSize call
        source.getStateInformation(blob);
        REQUIRE(blob.getSize() > 0);
    }

    MwAudioProcessor reopened;
    reopened.setStoredEditorSize({ 1234, 789 });  // a stale value that must be cleared
    reopened.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(reopened.getStoredEditorSize() == juce::Point<int>(0, 0));
}

TEST_CASE("ui_editor out-of-range scale presets are a safe no-op", "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    editor->applyScalePreset(1);  // 100%
    const int w = editor->getWidth();
    const int h = editor->getHeight();

    editor->applyScalePreset(-1);
    editor->applyScalePreset(MwAudioEditor::getNumScalePresets());  // one past the end
    editor->applyScalePreset(9999);

    REQUIRE(editor->getWidth()  == w);
    REQUIRE(editor->getHeight() == h);
}

TEST_CASE("ui_editor createEditor on the processor returns a working editor root", "[ui_editor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    REQUIRE(processor.hasEditor());

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);

    // It is the real editor root, laid out over the design space at a non-zero size.
    auto* root = dynamic_cast<MwAudioEditor*>(editor.get());
    REQUIRE(root != nullptr);
    REQUIRE(root->getWidth()  > 0);
    REQUIRE(root->getHeight() > 0);
    REQUIRE(root->getScaleFactor() > 0.0f);
}
