// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/LoadFailure.h — the graded load-failure recovery ladder (task 024).
// Realizes docs/design/06 §8.1, §8.2, §8.3, §5.3 and ADR-021 L1-L8/L12/L13.
//
// recoverState is a NEVER-throwing graded fallback: it parses a host state blob,
// runs the §7 migration chain, defaults missing params, clamps/resets out-of-range
// values, retains a newer-than-current raw blob for round-trip, pads/clamps the stored
// sequence into the fixed 100-step capacity, prefers partial recovery over INIT (INIT
// only when no interpretable params survive), and ALWAYS returns a complete valid
// canonical MW101_STATE ValueTree plus a coalesced RecoveryReport (the deviation note
// list that surfaces as ONE non-modal warning, §8.3 L12) [ADR-021 Decision].
//
// WHY plugin/ AND NOT core/: recoverState projects/recovers the CANONICAL
// juce::ValueTree that lives in plugin/state/StateSerializer (task 023), so it
// references juce::ValueTree / juce::MemoryBlock and CANNOT live in mwcore (the
// no-JUCE-in-core guard fails the build on any JUCE token under core/) [ADR-001 C1].
// It builds on the JUCE-free INIT patch (core/state/InitPatch.h), the JUCE-free
// migration chain (core/state/Migration.h, run through the plugin ValueTree adapter),
// and the JUCE-free registry (core/params/ParamDefs.h kParamDefs ranges/choice counts).
//
// MESSAGE-THREAD ONLY: all classification, parse, migrate, validate, fallback
// construction and raw retention run on the message thread; the audio thread never
// parses, allocates, locks, or observes an in-progress recovery [§8.1; ADR-021 L13].
// The actual atomic/SPSC handoff (L7) and the UI warning affordance (L12 surfacing)
// are OWNED ELSEWHERE (plugin processor / ui) — this module owns only the recovered
// tree + the RecoveryReport data.

#pragma once

#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

namespace mw::plugin::state {

// The graded fallback outcome (§8.2). Ordered least-to-most severe; recovery prefers
// the most faithful valid state reachable, INIT last [ADR-021 L5].
enum class RecoveryOutcome : std::uint8_t {
    CleanLoad,            // no deviation: parsed, current schema, every value in range
    MigratedAndBound,     // schemaVersion <= CURRENT: ran the chain, defaulted missing
    NewerDownInterpreted, // schemaVersion >  CURRENT: bound known IDs, raw retained (L6)
    ClampedValues,        // out-of-range continuous clamped / invalid choice index reset
    InitFallback          // structurally unparseable / no interpretable params -> INIT
};

// The coalesced recovery report. `notes` is the deviation list that surfaces as ONE
// non-modal warning (never a storm) [§8.3 L12]. A clean full load leaves it empty.
struct RecoveryReport {
    RecoveryOutcome   outcome = RecoveryOutcome::CleanLoad;
    juce::StringArray notes;   // coalesced; surfaced as a single warning by the UI/owner
};

// Never throws; ALWAYS returns a complete valid canonical MW101_STATE tree + report for
// ANY input [§8.1; §8.2; ADR-021 L1]. The returned tree carries the four versioning
// root attributes, a <PARAMS> subtree with every live registry ID present and in range,
// and an <extras> subtree (with a 0..100-step <seq>, and — on the NewerDownInterpreted
// rung — the retained rawNewerBlob for round-trip, L6). Runs on the message thread only.
juce::ValueTree recoverState(const void* blob, int sizeBytes, RecoveryReport& outReport);

} // namespace mw::plugin::state
