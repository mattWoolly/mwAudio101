// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/MwAudioEditorTimerTest.cpp — JUCE-linked acceptance tests for the
// editor's single COALESCING telemetry Timer + the reduce-motion / low-CPU toggle
// (task 115, ui/MwAudioEditor.h). Test-case display names begin with the task tag
// `ui_timer` so `ctest -R ui_timer` selects exactly these cases (silent-pass rule).
//
// Acceptance criteria covered (task 115 / §8.4 / §10 / ADR-015 C5, C7, C8):
//   [1] One Timer drains the SPSC telemetry ring to the MOST-RECENT Snapshot at a
//       30-60 Hz default rate and triggers ONLY a TARGETED repaint of the scope /
//       indicator region — never a whole-editor repaint (§8.4; ADR-015 C5/C7).
//   [2] Reduce-motion ON suppresses/downsamples the Timer and idles the scope, while
//       every APVTS control attachment / automation stays fully functional (§10; C8).
//   [3] The reduce-motion toggle state PERSISTS in the <extras> UI subtree and restores
//       through the processor's getState/setState round-trip (§10; ADR-008 C8).
//
// The Snapshot is published through the REAL audio path: processBlock pushes one
// telemetry frame per block via the processor's SPSC Producer, exactly as the shipped
// plugin does (no test-only producer poke). The Timer is driven by calling
// timerCallback() directly (headless; no real 60 Hz wall-clock wait).

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"          // mw::plugin::MwAudioProcessor
#include "../../ui/MwAudioEditor.h"   // mw::ui::MwAudioEditor
#include "calibration/TimerConstants.h" // (PI) Timer rate band

using mw::plugin::MwAudioProcessor;
using mw::ui::MwAudioEditor;

namespace {

namespace tcal = mw::cal::timer;

// Drive one real audio block so processBlock PUBLISHES exactly one telemetry Snapshot
// through the processor's SPSC Producer (the shipped path the editor Timer drains).
void publishOneTelemetryFrame(MwAudioProcessor& p)
{
    juce::AudioBuffer<float> buffer(2, 64);
    buffer.clear();
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
}

} // namespace

TEST_CASE("ui_timer drains the SPSC to the most-recent snapshot and repaints only the scope region",
          "[ui_timer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    auto editor = std::make_unique<MwAudioEditor>(processor);

    // The editor is a juce::Timer running at the default rate (within 30-60 Hz).
    REQUIRE(editor->isTimerRunning());
    REQUIRE(editor->getTimerHzForTest() == tcal::kDefaultTimerHz);
    REQUIRE(editor->getTimerHzForTest() >= tcal::kMinTimerHz);
    REQUIRE(editor->getTimerHzForTest() <= tcal::kMaxTimerHz);

    // Nothing published yet -> a Timer tick pulls nothing and does NOT repaint.
    const int repaintsBefore = editor->scopeRepaintCountForTest();
    editor->timerCallback();
    REQUIRE(editor->scopeRepaintCountForTest() == repaintsBefore);
    REQUIRE_FALSE(editor->lastPulledFrameForTest());

    // Publish three frames; the coalescing pull must jump straight to the MOST-RECENT
    // (seqStep is monotonic, advanced once per published block).
    publishOneTelemetryFrame(processor);
    publishOneTelemetryFrame(processor);
    publishOneTelemetryFrame(processor);

    editor->timerCallback();

    // It pulled a frame (the newest), and triggered exactly one TARGETED scope repaint.
    REQUIRE(editor->lastPulledFrameForTest());
    REQUIRE(editor->scopeRepaintCountForTest() == repaintsBefore + 1);
    REQUIRE(editor->wholeEditorRepaintCountForTest() == 0);  // never whole-editor (C7)
    REQUIRE(editor->lastSnapshotForTest().seqStep == 3);     // coalesced to the newest

    // The targeted region is a real, non-empty sub-rectangle of the editor (not the
    // whole bounds) — a dirty-rect, not a full invalidation (§8.4; ADR-015 C7).
    const auto region = editor->scopeRepaintRegionForTest();
    REQUIRE_FALSE(region.isEmpty());
    REQUIRE(editor->getLocalBounds().contains(region));
    REQUIRE(region != editor->getLocalBounds());

    // A second tick with nothing newly published is a no-op (coalescing returns false).
    editor->timerCallback();
    REQUIRE(editor->scopeRepaintCountForTest() == repaintsBefore + 1);
}

TEST_CASE("ui_timer reduce-motion downsamples the timer and idles the scope without touching bindings",
          "[ui_timer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);

    auto editor = std::make_unique<MwAudioEditor>(processor);

    // Snapshot a representative APVTS parameter's value BEFORE toggling reduce-motion, so
    // we can prove the toggle leaves the binding/automation surface untouched (§10; C8).
    auto* anyParam = processor.getParameters().getFirst();
    REQUIRE(anyParam != nullptr);
    const float paramBefore = anyParam->getValue();

    REQUIRE_FALSE(editor->reduceMotionEnabled());
    REQUIRE(editor->getTimerHzForTest() == tcal::kDefaultTimerHz);

    // Reduce-motion ON: the Timer drops to the (lower) reduce-motion rate, the scope
    // goes idle, and NO control attachment / parameter value is touched.
    editor->setReduceMotion(true);
    REQUIRE(editor->reduceMotionEnabled());
    REQUIRE(editor->getTimerHzForTest() == tcal::kReduceMotionTimerHz);
    REQUIRE(editor->getTimerHzForTest() < tcal::kMinTimerHz);   // downsampled below the floor
    REQUIRE(editor->scopeIsIdleForTest());

    // The parameter surface is unchanged by the toggle (a UI preference, not a binding).
    REQUIRE(anyParam->getValue() == paramBefore);

    // Even with frames freshly published, an idle scope coalesces the snapshot but does
    // NOT animate (no scope repaint while idle) — the §10 "static/idle" contract.
    publishOneTelemetryFrame(processor);
    const int idleRepaints = editor->scopeRepaintCountForTest();
    editor->timerCallback();
    REQUIRE(editor->scopeRepaintCountForTest() == idleRepaints);  // idle: no animation
    REQUIRE(editor->scopeIsIdleForTest());

    // Reduce-motion OFF: the Timer restores the default rate and the scope animates again.
    editor->setReduceMotion(false);
    REQUIRE_FALSE(editor->reduceMotionEnabled());
    REQUIRE(editor->getTimerHzForTest() == tcal::kDefaultTimerHz);
    REQUIRE_FALSE(editor->scopeIsIdleForTest());

    publishOneTelemetryFrame(processor);
    const int liveRepaints = editor->scopeRepaintCountForTest();
    editor->timerCallback();
    REQUIRE(editor->scopeRepaintCountForTest() == liveRepaints + 1);

    REQUIRE(anyParam->getValue() == paramBefore);  // still untouched end-to-end
}

TEST_CASE("ui_timer reduce-motion state persists and restores via the processor accessor pair",
          "[ui_timer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;

    // The narrow <extras>-UI accessor pair round-trips the boolean directly.
    REQUIRE_FALSE(processor.getStoredReduceMotion());
    processor.setStoredReduceMotion(true);
    REQUIRE(processor.getStoredReduceMotion());

    // An editor reads the stored preference on construction (restores the toggle), and
    // writes it back through the accessor when the user toggles it.
    {
        auto editor = std::make_unique<MwAudioEditor>(processor);
        REQUIRE(editor->reduceMotionEnabled());   // adopted the persisted ON state
        editor->setReduceMotion(false);
        REQUIRE_FALSE(processor.getStoredReduceMotion());  // wrote back through the seam
    }
}

TEST_CASE("ui_timer reduce-motion persists through getState/setState across instances",
          "[ui_timer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Store reduce-motion ON on a SOURCE processor, serialize to an opaque host blob via
    // getStateInformation (the real <extras> persistence path), then reload on a FRESH
    // processor and confirm the preference round-trips (§10; ADR-008 C8).
    juce::MemoryBlock blob;
    {
        MwAudioProcessor source;
        source.setStoredReduceMotion(true);
        source.getStateInformation(blob);
        REQUIRE(blob.getSize() > 0);
    }

    MwAudioProcessor reopened;
    REQUIRE_FALSE(reopened.getStoredReduceMotion());  // fresh: default OFF
    reopened.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(reopened.getStoredReduceMotion());        // restored ON from <extras>

    // A freshly-created editor on the reopened processor adopts the restored preference.
    auto editor = std::make_unique<MwAudioEditor>(reopened);
    REQUIRE(editor->reduceMotionEnabled());
    REQUIRE(editor->getTimerHzForTest() == tcal::kReduceMotionTimerHz);
}

TEST_CASE("ui_timer a state blob without a stored reduce-motion key defaults to OFF",
          "[ui_timer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A processor that never set reduce-motion writes a blob WITHOUT the key; a reopened
    // processor must keep the default (OFF) — backward compatibility (ADR-021 fallback).
    juce::MemoryBlock blob;
    {
        MwAudioProcessor source;   // no setStoredReduceMotion call
        source.getStateInformation(blob);
        REQUIRE(blob.getSize() > 0);
    }

    MwAudioProcessor reopened;
    reopened.setStoredReduceMotion(true);  // a stale value that must be cleared on load
    reopened.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE_FALSE(reopened.getStoredReduceMotion());
}
