// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/CcLearnCodec.h — round-trip the user MIDI-learn (CC-learn) bindings of a
// live CcLearnMap (task 100) through the canonical MW101_STATE <extras> tree (task 023b).
// Closes the 023 QA MEDIUM (PR #106): state captured seq/arp/drift but NOT CC-learn, so a
// MIDI-learn binding was lost on reload [docs/design/06 §5.4/§5.5; ADR-012 C16; ADR-021].
//
// COMPACT, DEFAULT-RELATIVE PERSISTENCE. The default §6.2 seed map (CC1/7/11/74/71/5 ->
// doc-06 indices, CC64 -> HOLD sentinel) is the same on every fresh instance, so it does
// NOT need to be stored: a fresh CcLearnMap already IS the seed. Only the NON-DEFAULT
// bindings — the rows a user changed via MIDI-learn — are written, as <binding> children
// of a <ccLearn> node under <extras>, each carrying {cc, paramIndex, enabled}. A
// default-OFF/never-learned session writes NO <ccLearn> node at all, so a blob from before
// this task (or one with no learned bindings) stays byte-compatible and still loads.
//
// FALLBACK (ADR-021). readCcLearn never fails the whole load: an absent, empty, or garbage
// <ccLearn> node simply leaves the live map at its default seed; only the well-formed
// <binding> rows that differ from the seed are applied, each clamped/validated against the
// frozen registry so a corrupt cc/paramIndex can never poison the audio-thread lookup.
//
// "Non-default" is computed by DIFFING against a fresh default-constructed CcLearnMap (the
// canonical seed) rather than re-declaring the §6.2 table here, so this codec can never
// drift from the seed table owned by task 100. CcLearnMap internals are NOT modified.
//
// MESSAGE-THREAD ONLY: both functions parse/build a juce::ValueTree and edit the map via
// editableCopy()/publish() on the message thread (the single writer). The audio thread
// only ever reads the live pointer; it never observes an in-progress restore [§6.3;
// ADR-008 C19].

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/CcLearnMap.h"   // mw::plugin::CcLearnMap / CcBinding (the live map, task 100)

namespace mw::plugin::state {

// <ccLearn> on-tree shape. A <ccLearn> child of <extras> holds one <binding> element per
// NON-DEFAULT CC row. Declared here (not in core StateTree.h) because this codec owns the
// <binding> attribute shape, exactly as StateSerializer owns the <step> shape [§5.5].
inline constexpr const char* kCcBindingId       = "binding";  // a single <ccLearn> row element
inline constexpr const char* kCcBindingAttrCc   = "cc";       // int; 0..127 CC number
inline constexpr const char* kCcBindingAttrParam = "param";   // int; kParamDefs index (or HOLD)
inline constexpr const char* kCcBindingAttrOn   = "enabled";  // bool; row active

// Write the NON-DEFAULT bindings of `map` as a <ccLearn> child of the given <extras> node.
// Diffs every CC row against a fresh default-seeded CcLearnMap and emits a <binding> only
// for rows that differ (compact). When the map is at its default seed, NO <ccLearn> node is
// created, so the blob stays byte-compatible with sessions that never used MIDI-learn.
// `extrasNode` must be the canonical <extras> node (built by captureState). Message thread.
void writeCcLearn(juce::ValueTree& extrasNode, const CcLearnMap& map);

// Restore CC-learn bindings from a canonical MW101_STATE tree's <extras><ccLearn> node into
// the live `map`. The map starts from its default seed; each WELL-FORMED <binding> whose cc
// is in [0,127] and whose param index is valid (a real registry index, the HOLD sentinel,
// or the unmapped sentinel) is applied over the seed, then published in one atomic swap.
// An absent / empty / garbage <ccLearn> leaves the map at the default seed WITHOUT failing
// the load [ADR-021]. Returns the number of <binding> rows applied (0 when none/absent).
// Message thread (single writer): edits an inactive buffer then publish()es. No allocation.
int readCcLearn(const juce::ValueTree& canonical, CcLearnMap& map);

} // namespace mw::plugin::state
