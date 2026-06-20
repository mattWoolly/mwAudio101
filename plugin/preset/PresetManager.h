// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/preset/PresetManager.h — the in-memory factory preset bank + per-slot INIT
// fallback (task 119). Realizes docs/design/06 §10.1 (class surface), §10.2 (bank
// rules), §10.3 (authoring discipline touchpoints), §8.3 L9 (per-slot fallback) and
// ADR-008 C17/C18, ADR-021 L9/L11.
//
// WHAT IT IS. PresetManager loads the embedded factory bank at construction (message
// thread), decoding each slot's .mw101preset JSON via the canonical task-025 decoder
// (loadPresetJson, plugin/preset/PresetFormat.h). It exposes name/category/index
// queries for the browser (owned by the UI doc), and applies a preset by running it
// through the SAME migration (§7) + recovery (§8) chain as session state
// (mw::plugin::state::recoverState, task 024) and binding the recovered canonical tree
// into the live APVTS + the <extras> POD via the §5.3 message-thread path.
//
//   §8.3 L9 (per-slot INIT fallback): a missing or undecodable embedded preset resolves
//   THAT slot to INIT (§11) and records a warning NAMING it; construction NEVER aborts
//   or empties the bank — every other slot still loads [§10.2; ADR-021 L9].
//
// EMPTY BANK IS VALID. At this development stage there is NO embedded preset BinaryData
// yet (authoring the 64 files + the BinaryData embedding are later tasks 131/144-150,
// explicitly OUT OF SCOPE here). The default constructor therefore produces an empty
// bank gracefully: getNumPresets()==0 is valid and never a crash/abort. A test-only
// injection constructor takes in-memory JSON sources so the bank-load, the L9 per-slot
// fallback and indicesForCategory can be exercised without real embedded files.
//
// OUT OF SCOPE (other tasks, per §10 / the task spec): authoring the 64 preset files
// (task 131/144-150); the SPSC double-buffer audio-thread swap that publishes the
// applied <extras> to the audio thread (plugin-processor, task 111); the browser UI
// (ui-skeleton). This module only assembles the message-thread bank and applies via the
// §5.3 path; it does no audio-thread work [docs/design/06 §10.2, §12; ADR-008 C19].
//
// WHY plugin/ AND NOT core/: this TU constructs/recovers the CANONICAL juce::ValueTree
// that lives in plugin/state/StateSerializer (task 023) and decodes JSON via the JUCE
// loadPresetJson, so it references juce::ValueTree / juce::AudioProcessorValueTreeState
// and CANNOT live in mwcore (the no-JUCE-in-core guard fails the build on any JUCE token
// under core/) [ADR-001 C1]. The task file's frontmatter says core/ and names the
// `default` preset; that is STALE — the projection fundamentally requires JUCE, so this
// module lives plugin-side and builds under MW_BUILD_PLUGIN=ON, mirroring tasks 023/024/
// 025. Behavior is exactly the task's §10/§8.3-L9 scope; only the location is corrected.

#pragma once

#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "state/Extras.h"        // mwcore (JUCE-free): mw::state::Extras
#include "state/LoadFailure.h"   // mw::plugin::state::RecoveryReport / RecoveryOutcome

namespace mw::plugin::preset {

class PresetManager {
public:
    // A test-only in-memory preset source: a display name + the raw .mw101preset JSON
    // text for that slot. This is the injection seam the task spec calls for — the bank
    // is exercised by injecting JSON fixtures rather than depending on real embedded
    // files (none exist yet; tasks 131/144-150). The embedded-BinaryData constructor is
    // a later task; nothing in this seam is on the audio thread.
    struct SlotSource {
        juce::String name;  // the slot's display name (also the L9 warning subject)
        juce::String json;  // the raw .mw101preset JSON text to decode (task-025)
    };

    // Default constructor: loads the embedded factory bank (message thread). At this
    // stage there is no embedded BinaryData, so the bank is empty — getNumPresets()==0
    // is valid and construction never aborts/crashes [§10.1; §10.2; §8.3 L9].
    PresetManager();

    // Test-only injection constructor: build the bank from in-memory JSON sources. Each
    // slot decodes via loadPresetJson (task-025); an undecodable slot resolves to INIT
    // and warns naming it WITHOUT emptying the bank (§8.3 L9). Used by the tests and by
    // any future embedded-bank constructor that gathers BinaryData into SlotSources.
    explicit PresetManager(const std::vector<SlotSource>& sources);

    // --- §10.1 query surface --------------------------------------------------------
    [[nodiscard]] int          getNumPresets() const noexcept;
    [[nodiscard]] juce::String getName(int index) const;       // empty if out of range
    [[nodiscard]] juce::String getCategory(int index) const;   // empty if out of range

    // Apply a preset's canonical tree to APVTS + extras via the §5.3 message-thread
    // assembly. Runs the SAME migration (§7) + recovery (§8) chain as session state
    // (recoverState), then binds parameters into APVTS and the <extras> into the POD.
    // An out-of-range index is a safe no-op (no crash) and leaves APVTS untouched. The
    // audio-thread SPSC publish of `extras` is owned by the processor (task 111), OUT
    // OF SCOPE here [§10.1; §10.2; §5.3].
    void loadPreset(int index, juce::AudioProcessorValueTreeState& apvts,
                    mw::state::Extras& extras,
                    mw::plugin::state::RecoveryReport& outReport);

    // category -> the bank indices in that §6.5 category, for the browser (UI doc).
    // An unknown / empty category returns an empty array (never a crash) [§10.1; §6.5].
    [[nodiscard]] juce::Array<int> indicesForCategory(juce::StringRef category) const;

    // The coalesced construction report: the deviation notes raised while building the
    // bank (the per-slot L9 INIT-fallback warnings, naming each offending slot). Empty
    // when every slot decoded cleanly. Surfaced as ONE non-modal warning by the owner
    // (the L12 coalesce rule) [§8.3 L9; L12].
    [[nodiscard]] const mw::plugin::state::RecoveryReport& constructionReport() const noexcept
    {
        return constructionReport_;
    }

private:
    // One decoded slot in the in-memory bank. `canonicalBlob` is the slot's canonical
    // MW101_STATE tree serialized to the wire form recoverState consumes at loadPreset
    // time, so applying a preset runs the SAME §8 recovery path as session state. For an
    // undecodable slot this carries the INIT canonical (§8.3 L9).
    struct Slot {
        juce::String      name;
        juce::String      category;        // a §6.5 category, or empty for an INIT slot
        juce::MemoryBlock canonicalBlob;   // wire form of the slot's canonical tree
    };

    // Decode one SlotSource into a Slot: parse/validate the JSON via loadPresetJson; on
    // success serialize the decoded canonical tree to the slot blob; on failure resolve
    // the slot to INIT and append an L9 warning naming it [§8.3 L9].
    void decodeSlot(const SlotSource& source);

    std::vector<Slot>                       slots_;
    mw::plugin::state::RecoveryReport       constructionReport_;
};

} // namespace mw::plugin::preset
