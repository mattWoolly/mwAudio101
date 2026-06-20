// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/preset/PresetFormat.h — the .mw101preset JSON <-> canonical ValueTree
// projection + the §6.4 schema validator (task 025).
//
// Realizes docs/design/06 §6.1 (format/location), §6.2 (JSON schema), §6.3 (projection
// interface), §6.4 (validation rules), §6.5 (category enum) and ADR-008 C13-C18,
// ADR-025 (no per-step accent), ADR-021 L11 (nullopt on malformed/invalid -> recover).
//
//   loadPresetJson(file, outMeta): parse the .mw101preset JSON, run the §6.4 validation
//     rules, and project the validated params/seq/arp into the canonical MW101_STATE
//     ValueTree (the same tree shape StateSerializer owns). Returns nullopt on malformed
//     JSON OR any validation failure (-> the caller's §8 L11 recovery).
//   writePresetJson(canonical, meta): project a canonical ValueTree (+ PresetMeta) to
//     §6.2-shaped JSON text for authoring/export. Per-step objects carry note/gate/tie/
//     rest ONLY — never accent [ADR-025].
//
// WHY plugin/ AND NOT core/: this TU references juce::ValueTree / juce::JSON / juce::var
// / juce::File, so it CANNOT live in mwcore (the no-JUCE-in-core guard fails the build
// on any JUCE token under core/) [ADR-001 C1; ADR-014 C11]. The task file's frontmatter
// says core/ and its Verification block names the `default` preset; that is STALE — the
// projection fundamentally requires the JUCE canonical tree (task 023 lives plugin-side
// for the same reason), so this module lives plugin-side and builds under
// MW_BUILD_PLUGIN=ON. Behavior is exactly the task's §6.4 scope; only the location is
// corrected (mirrors what QA accepted for task 023).
//
// MESSAGE-THREAD ONLY: parse/validate/project run on the message thread; there is no
// audio-thread work here [docs/design/06 §6.3, §12; ADR-008 C19].

#pragma once

#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>

namespace mw::plugin::preset {

// §6.3 — the on-disk preset meta block (the JSON `meta` object).
struct PresetMeta {
    juce::String      name;
    juce::String      author;
    juce::String      category;       // exactly one of the §6.5 enum
    juce::String      description;
    juce::StringArray tags;
    juce::String      inspiredBy;      // empty => JSON null (inspired-by/disputed only)
    bool              soundExt = false; // true iff a software-only feature is used
};

// JSON file -> validated canonical MW101_STATE ValueTree (§6.3). Returns nullopt on
// malformed JSON OR a §6.4 validation failure; on success outMeta is populated from the
// `meta` block. The caller runs the §7 migration chain / §8 recovery [docs/design/06
// §6.3, §6.4; ADR-021 L11].
std::optional<juce::ValueTree> loadPresetJson(const juce::File& file, PresetMeta& outMeta);

// Canonical ValueTree (+ meta) -> §6.2-shaped JSON text (authoring/export). Emits
// schemaVersion, meta, params (every live registry ID + value), seq (stepCount + steps
// with note/gate/tie/rest), and arp (latch). No per-step accent is ever emitted
// [docs/design/06 §6.2; ADR-025].
juce::String writePresetJson(const juce::ValueTree& canonical, const PresetMeta& meta);

} // namespace mw::plugin::preset
