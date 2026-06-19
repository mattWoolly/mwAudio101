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

// Build the canonical MW101_STATE tree from the live APVTS + <extras> (message thread).
// Root carries schemaVersion/pluginVersion/engineVersion/renderVersion; <PARAMS> is the
// APVTS state subtree; <extras> carries <seq> (stepCount + one <step> per active step,
// note/gate/tie/rest), arpLatch, driftSeed, seedLocked [docs/design/06 §5.1, §5.4, §5.5].
juce::ValueTree captureState(const juce::AudioProcessorValueTreeState& apvts,
                             const mw::state::Extras& extras,
                             int schemaVersion,
                             juce::String pluginVersion,
                             juce::String engineVersion,
                             int renderVersion);

// Serialize the canonical tree -> host opaque blob (JUCE binary) [ADR-008 C9].
void writeToBlob(const juce::ValueTree& canonical, juce::MemoryBlock& dest);

// Parse a host blob -> canonical tree; returns nullopt on STRUCTURAL parse failure
// (unreadable bytes, or a tree whose root is not MW101_STATE) [docs/design/06 §5.2;
// ADR-021 L1].
std::optional<juce::ValueTree> readFromBlob(const void* data, int sizeBytes);

} // namespace mw::plugin::state
