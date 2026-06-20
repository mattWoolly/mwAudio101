// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/StateSerializer.h — the ONE canonical state (de)serializer (task 023).
// Realizes docs/design/06 §5.1, §5.2, §5.4, §5.5 and ADR-008 C8/C9, ADR-025 (no
// per-step accent), ADR-021 L1 (nullopt on structural parse failure).
//
// captureState builds the canonical MW101_STATE ValueTree from the live APVTS plus
// the JUCE-free <extras> POD; writeToBlob serializes that tree to the host's opaque
// blob (JUCE binary); readFromBlob parses a blob back to the canonical tree, returning
// nullopt on a structural parse failure. The full <extras>/<seq> round-trip (the
// 100-step note/rest/tie/gate pattern, arp latch, drift seed + lock) is captured here.
//
// WHY plugin/ AND NOT core/: this TU references juce::ValueTree /
// juce::AudioProcessorValueTreeState / juce::MemoryBlock, so it CANNOT live in mwcore
// (the no-JUCE-in-core guard, cmake/NoJuceInCore.cmake, fails the build on any JUCE
// include/token under core/) [ADR-001 C1; ADR-014 C11]. The <extras> POD it consumes
// (mw::state::Extras) and the canonical-tree key constants (mw::state::*) stay
// JUCE-free in core/state/; only this thin (de)serializer lives plugin-side, matching
// the JUCE / JUCE-free split [docs/design/00 §3.3].
//
// MESSAGE-THREAD ONLY: capture/write/read run on the message thread; there is NO
// audio-thread work here. The audio thread reads APVTS atomics and the pre-allocated
// SPSC <extras> double-buffer, neither of which this module touches [docs/design/06
// §5.2; ADR-008 C19].

#pragma once

#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/Extras.h"     // mwcore (JUCE-free): mw::state::Extras / SeqStep / kMaxSeqSteps

namespace mw::plugin::state {

// <seq> per-step attribute keys (note/gate/tie/rest ONLY — NO accent in v1)
// [docs/design/06 §5.5; ADR-025]. Declared here (not in core StateTree.h) because the
// canonical (de)serializer owns the on-tree <step> attribute shape.
inline constexpr const char* kSeqAttrStepCount = "stepCount";  // int; 0..100 active steps
inline constexpr const char* kSeqStepAttrNote  = "note";       // int; relative semitone
inline constexpr const char* kSeqStepAttrGate  = "gate";       // bool
inline constexpr const char* kSeqStepAttrTie   = "tie";        // bool
inline constexpr const char* kSeqStepAttrRest  = "rest";       // bool

// The advisory UI editor size (pixels) persisted in the <extras> node (kExtrasUiWidth /
// kExtrasUiHeight). The UI size is a message-thread <extras> PREFERENCE, NOT an
// audio-thread POD field — so it is threaded into captureState as a plain parameter
// rather than carried on the trivially-copyable mw::state::Extras SPSC payload
// [docs/design/06 §5.4, §706; docs/design/10-ui.md §4.4; ADR-008 §4/§5 C8; ADR-015 C2].
// A zero/non-positive dimension means "no size to persist" -> the key is omitted, and a
// freshly-created editor falls back to the default design scale [ADR-021 fallback].
struct UiEditorSize {
    int width  = 0;   // pixels; <= 0 means "none stored" (key omitted)
    int height = 0;   // pixels; <= 0 means "none stored" (key omitted)
};

// Build the canonical MW101_STATE tree from the live APVTS + <extras> (message thread).
// Root carries schemaVersion/pluginVersion/engineVersion/renderVersion; <PARAMS> is the
// APVTS state subtree; <extras> carries <seq> (stepCount + one <step> per active step,
// note/gate/tie/rest), arpLatch, driftSeed, seedLocked, and — when uiSize is valid (both
// dimensions > 0) — the advisory uiWidth/uiHeight UI-size keys [docs/design/06 §5.1,
// §5.4, §5.5; docs/design/10-ui.md §4.4; ADR-015 C2]. uiSize defaults to {0,0} (omit) so
// pre-existing call sites that do not persist a UI size keep their exact behaviour.
juce::ValueTree captureState(const juce::AudioProcessorValueTreeState& apvts,
                             const mw::state::Extras& extras,
                             int schemaVersion,
                             juce::String pluginVersion,
                             juce::String engineVersion,
                             int renderVersion,
                             UiEditorSize uiSize = {});

// Serialize the canonical tree -> host opaque blob (JUCE binary) [ADR-008 C9].
void writeToBlob(const juce::ValueTree& canonical, juce::MemoryBlock& dest);

// Parse a host blob -> canonical tree; returns nullopt on STRUCTURAL parse failure
// (unreadable bytes, or a tree whose root is not MW101_STATE) [docs/design/06 §5.2;
// ADR-021 L1].
std::optional<juce::ValueTree> readFromBlob(const void* data, int sizeBytes);

// Read the advisory UI editor size back out of a canonical MW101_STATE tree's <extras>
// node (the inverse of captureState's uiSize write) [docs/design/06 §706;
// docs/design/10-ui.md §4.4; ADR-015 C2]. Returns {0,0} when the keys are absent or any
// dimension is non-positive (garbage / never-persisted) so the caller falls back to the
// default design scale [ADR-021 fallback discipline]. Message-thread only.
UiEditorSize readUiEditorSize(const juce::ValueTree& canonical);

} // namespace mw::plugin::state
