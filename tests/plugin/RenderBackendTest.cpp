// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/RenderBackendTest.cpp — JUCE-linked acceptance tests for the OpenGL
// opt-in escape hatch on the editor root (MwAudioEditor, task 130). Test-case display
// names begin with the task tag `ui_opengl` so `ctest -R ui_opengl` selects exactly
// these cases (silent-pass rule).
//
// Acceptance criteria covered (task 130 / docs/design/10-ui.md §11; ADR-015 C9):
//   [1] No render-backend context is attached by DEFAULT — the software path is used
//       (§11; ADR-015 C9). A fresh editor reports openGlEnabled()==false and the
//       backend reports isAttached()==false.
//   [2] The context attaches ONLY on the explicit advanced setter
//       (setOpenGlEnabled(true) -> isAttached()==true) and detaches CLEANLY
//       (setOpenGlEnabled(false) -> isAttached()==false; no crash) (§11).
//   [3] The opt-in PERSISTS: it round-trips through the processor state I/O
//       (getStateInformation -> a NEW processor -> setStateInformation), exactly like
//       the 114/115 <extras> UI-preference pattern (the 115 QA verified this works).
//   [4] The default is OFF for a key-less blob — a processor that never saw a stored
//       opt-in restores false (animation/software path), so pre-130 blobs stay
//       byte-compatible.
//
// The GUI is NOT pixel-identical across platforms, so these tests assert BEHAVIOUR /
// attach state, never pixels [docs/design/10-ui.md §13; ADR-015 Consequences].

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"          // mw::plugin::MwAudioProcessor
#include "../../ui/MwAudioEditor.h"   // mw::ui::MwAudioEditor + the render-backend hatch

using mw::plugin::MwAudioProcessor;
using mw::ui::MwAudioEditor;

TEST_CASE("ui_opengl no render-backend context is attached by default (software path)",
          "[ui_opengl]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    // Default-OFF: the software/CPU render path is primary; nothing is attached
    // [docs/design/10-ui.md §11; ADR-015 C9].
    REQUIRE_FALSE(editor->openGlEnabled());
    REQUIRE_FALSE(editor->renderBackendAttachedForTest());

    // The processor's stored opt-in default is OFF too (no key written yet).
    REQUIRE_FALSE(processor.getStoredOpenGl());
}

TEST_CASE("ui_opengl explicit setter attaches the context and detaches it cleanly",
          "[ui_opengl]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    REQUIRE_FALSE(editor->renderBackendAttachedForTest());

    // Opt IN -> the escape hatch attaches the context [§11].
    editor->setOpenGlEnabled(true);
    REQUIRE(editor->openGlEnabled());
    REQUIRE(editor->renderBackendAttachedForTest());

    // Idempotent: a redundant enable does not double-attach or crash.
    editor->setOpenGlEnabled(true);
    REQUIRE(editor->renderBackendAttachedForTest());

    // Opt OUT -> detach cleanly; isAttached() reflects the OFF state [§11].
    editor->setOpenGlEnabled(false);
    REQUIRE_FALSE(editor->openGlEnabled());
    REQUIRE_FALSE(editor->renderBackendAttachedForTest());

    // Idempotent OFF is also a safe no-op.
    editor->setOpenGlEnabled(false);
    REQUIRE_FALSE(editor->renderBackendAttachedForTest());

    // Re-attaching after a detach works (state machine is reusable).
    editor->setOpenGlEnabled(true);
    REQUIRE(editor->renderBackendAttachedForTest());
}

TEST_CASE("ui_opengl teardown with an attached context does not crash (clean detach)",
          "[ui_opengl]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    MwAudioProcessor processor;
    auto editor = std::make_unique<MwAudioEditor>(processor);

    editor->setOpenGlEnabled(true);
    REQUIRE(editor->renderBackendAttachedForTest());

    // Destroying the editor while the context is ATTACHED must detach it cleanly in the
    // destructor — no dangling context [§11]. (Reaching the next line is the assertion.)
    editor.reset();
    SUCCEED("editor destroyed with an attached render backend without crashing");
}

TEST_CASE("ui_opengl the opt-in persists and restores across a state round-trip",
          "[ui_opengl]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Enable the opt-in on a first editor, then snapshot processor state.
    juce::MemoryBlock blob;
    {
        MwAudioProcessor processor;
        auto editor = std::make_unique<MwAudioEditor>(processor);

        editor->setOpenGlEnabled(true);
        REQUIRE(processor.getStoredOpenGl());   // the editor wrote it through the accessor

        processor.getStateInformation(blob);
    }
    REQUIRE(blob.getSize() > 0);

    // A brand-new processor restores the persisted opt-in from the blob (the 115 pattern).
    MwAudioProcessor restored;
    REQUIRE_FALSE(restored.getStoredOpenGl());   // OFF before any state is applied
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(restored.getStoredOpenGl());         // ON after the round-trip

    // A freshly-created editor over the restored processor opens with the opt-in already
    // ON and the context attached (it read the stored flag on construction).
    auto restoredEditor = std::make_unique<MwAudioEditor>(restored);
    REQUIRE(restoredEditor->openGlEnabled());
    REQUIRE(restoredEditor->renderBackendAttachedForTest());
}

TEST_CASE("ui_opengl a key-less blob restores the default OFF (pre-130 byte-compat)",
          "[ui_opengl]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // A processor that never toggled the opt-in writes a blob WITHOUT the renderOptIn key
    // (it is only written when ON), so it stays byte-compatible with pre-130 sessions.
    juce::MemoryBlock blob;
    {
        MwAudioProcessor processor;            // default OFF; never opted in
        REQUIRE_FALSE(processor.getStoredOpenGl());
        processor.getStateInformation(blob);
    }

    // Restoring that key-less blob leaves the opt-in OFF (software path) [§11; ADR-015 C9].
    MwAudioProcessor restored;
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE_FALSE(restored.getStoredOpenGl());

    // And the editor over it stays on the software path with nothing attached.
    auto editor = std::make_unique<MwAudioEditor>(restored);
    REQUIRE_FALSE(editor->openGlEnabled());
    REQUIRE_FALSE(editor->renderBackendAttachedForTest());
}
