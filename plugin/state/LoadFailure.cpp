// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/LoadFailure.cpp — the graded load-failure recovery ladder (task 024).
// Realizes docs/design/06 §8.1, §8.2, §8.3 (verbatim L1-L8/L12), §5.3 and ADR-021.
//
// All work is on the message thread; recoverState NEVER throws — every parse/validate/
// migrate step is wrapped so a failure becomes a classified recovery [§8.1; ADR-021
// L1/L13]. The returned tree is fully assembled before the owner hands it off via the
// atomic/SPSC path (L7, owned by the processor) — this module produces the tree only.

#include "state/LoadFailure.h"

#include <algorithm>
#include <string_view>

#include "state/StateSerializer.h"          // readFromBlob (the structural parse, L1)
#include "state/ValueTreeMutableAdapter.h"  // juce::ValueTree -> IMutableTree (migration)

#include "params/ParamDefs.h"               // mwcore: kParamDefs ranges / choice counts
#include "state/InitPatch.h"                // mwcore: buildInitPatch() (INIT, §11)
#include "state/Extras.h"                   // mwcore: kMaxSeqSteps
#include "state/Migration.h"                // mwcore: migrateToCurrent
#include "state/StateTree.h"                // mwcore: canonical key constants
#include "version/EngineVersion.h"          // mwcore: kCurrent*Version, kEngine/Plugin

namespace mw::plugin::state {

namespace {

namespace ids = mw::state;

// --- Identifiers reused across the file (avoid re-hashing string literals) -------
const juce::Identifier kRootTag   { ids::kRootId };
const juce::Identifier kParamsTag { ids::kParamsId };
const juce::Identifier kExtrasTag { ids::kExtrasId };
const juce::Identifier kSeqTag    { ids::kSeqId };
const juce::Identifier kStepTag   { ids::kStepId };
const juce::Identifier kParamElem { "PARAM" };
const juce::Identifier kIdKey     { "id" };
const juce::Identifier kValueKey  { "value" };

// Look up a live registry entry by ID; nullptr if the ID is unknown (future param).
const mw::params::ParamDef* defFor(const juce::String& id) noexcept
{
    for (const auto& d : mw::params::kParamDefs)
        if (id == juce::String(juce::CharPointer_ASCII{ d.id }))
            return &d;
    return nullptr;
}

// --- INIT canonical tree (§11; ADR-021 L1 last resort) ---------------------------
// Project the JUCE-free INIT patch (id -> modeled value + empty <extras>) onto the
// canonical §5.1 ValueTree shape: <MW101_STATE><PARAMS>(<PARAM id value>*)</PARAMS>
// <extras>...<seq stepCount="0"/></extras></MW101_STATE>.
juce::ValueTree buildInitCanonical()
{
    const auto patch = mw::state::buildInitPatch();

    juce::ValueTree root{ kRootTag };
    root.setProperty(ids::kAttrSchemaVersion, patch.schemaVersion, nullptr);
    root.setProperty(ids::kAttrPluginVersion,
                     juce::String(mw101::version::kPluginVersion), nullptr);
    root.setProperty(ids::kAttrEngineVersion,
                     juce::String(mw101::version::kEngineVersion), nullptr);
    root.setProperty(ids::kAttrRenderVersion, patch.renderVersion, nullptr);

    juce::ValueTree params{ kParamsTag };
    for (const auto& pv : patch.params)
    {
        juce::ValueTree child{ kParamElem };
        child.setProperty(kIdKey,
                          juce::String(juce::CharPointer_UTF8{ pv.id.data() }, pv.id.size()),
                          nullptr);
        child.setProperty(kValueKey, static_cast<double>(pv.value), nullptr);
        params.appendChild(child, nullptr);
    }
    root.appendChild(params, nullptr);

    // Empty <extras> with a stepCount=0 <seq> (no active steps) [§5.4; §11].
    juce::ValueTree extras{ kExtrasTag };
    extras.setProperty(ids::kExtrasArpLatch, patch.extras.arpLatch, nullptr);
    extras.setProperty(ids::kExtrasSeedLocked, patch.extras.seedLocked, nullptr);
    extras.setProperty(ids::kExtrasDriftSeed,
                       juce::var{ static_cast<juce::int64>(patch.extras.driftSeed) }, nullptr);
    juce::ValueTree seq{ kSeqTag };
    seq.setProperty(kSeqAttrStepCount, 0, nullptr);
    extras.appendChild(seq, nullptr);
    root.appendChild(extras, nullptr);

    return root;
}

// --- L2/L3 bind: every live registry ID present + in range -----------------------
// For each kParamDefs entry: keep a present in-range value; default a missing one; and
// (L4) clamp an out-of-range continuous value / reset an invalid choice index. Returns
// flags describing what had to be done so the caller can pick the outcome + coalesce
// notes. Unknown (future) param children are LEFT in place for round-trip [§5.3 step 4;
// ADR-008 C11].
struct BindResult {
    int  interpretableKnown = 0;  // count of known IDs that were present (interpretable)
    bool defaultedMissing   = false;
    bool clampedOrReset     = false;
};

BindResult bindAndValidate(juce::ValueTree& root)
{
    BindResult r;

    auto params = root.getChildWithName(kParamsTag);
    if (! params.isValid())
    {
        params = juce::ValueTree{ kParamsTag };
        root.addChild(params, 0, nullptr);
    }

    // Index the present children by ID for an O(1)-ish lookup as we walk the registry.
    // (Linear build; message-thread setup-time only — no audio-thread work.)
    for (const auto& d : mw::params::kParamDefs)
    {
        const juce::String id{ juce::CharPointer_ASCII{ d.id } };

        juce::ValueTree child;
        for (int i = 0; i < params.getNumChildren(); ++i)
        {
            const auto c = params.getChild(i);
            if (c.getProperty(kIdKey).toString() == id) { child = c; break; }
        }

        if (! child.isValid())
        {
            // Missing param -> registry default (L2/L11) [ADR-008 C11; ADR-021 L2].
            child = juce::ValueTree{ kParamElem };
            child.setProperty(kIdKey, id, nullptr);
            child.setProperty(kValueKey, static_cast<double>(d.defaultValue), nullptr);
            params.appendChild(child, nullptr);
            r.defaultedMissing = true;
            continue;
        }

        ++r.interpretableKnown;

        // L4: clamp / reset by registry range.
        if (d.type == mw::params::ParamType::Continuous)
        {
            const double v   = static_cast<double>(child.getProperty(kValueKey));
            const double lo  = static_cast<double>(d.minValue);
            const double hi  = static_cast<double>(d.maxValue);
            const double cl  = std::clamp(v, lo, hi);
            if (cl != v)
            {
                child.setProperty(kValueKey, cl, nullptr);
                r.clampedOrReset = true;
            }
        }
        else // Choice / Bool: index must be in [0, choiceCount).
        {
            const int idx = static_cast<int>(child.getProperty(kValueKey));
            if (idx < 0 || idx >= static_cast<int>(d.choiceCount))
            {
                child.setProperty(kValueKey,
                                  static_cast<double>(static_cast<int>(d.defaultValue)),
                                  nullptr);
                r.clampedOrReset = true;
            }
        }
    }

    return r;
}

// --- L8 sequence pad/clamp into the fixed 100-step capacity ----------------------
// Ensures <extras><seq> exists; clamps stepCount into [0, kMaxSeqSteps]; makes the
// number of <step> children match the clamped count (drop excess, pad missing with the
// §5.5 defaults gate=true/tie=false/rest=false/note=0). Returns true if anything was
// adjusted (a deviation note). No crash, no over-allocation beyond the fixed capacity
// [§5.4; §5.5; ADR-008 C20; ADR-021 L8].
bool normalizeSequence(juce::ValueTree& root)
{
    auto extras = root.getChildWithName(kExtrasTag);
    if (! extras.isValid())
    {
        extras = juce::ValueTree{ kExtrasTag };
        root.appendChild(extras, nullptr);
    }

    auto seq = extras.getChildWithName(kSeqTag);
    if (! seq.isValid())
    {
        seq = juce::ValueTree{ kSeqTag };
        seq.setProperty(kSeqAttrStepCount, 0, nullptr);
        extras.appendChild(seq, nullptr);
        return false; // a freshly-defaulted empty seq is not a "deviation" by itself
    }

    bool adjusted = false;

    const int declared = static_cast<int>(seq.getProperty(kSeqAttrStepCount));
    const int clamped  = std::clamp(declared, 0, mw::state::kMaxSeqSteps);
    if (clamped != declared)
    {
        seq.setProperty(kSeqAttrStepCount, clamped, nullptr);
        adjusted = true;
    }

    // Make the <step> child count match the clamped active-step count.
    while (seq.getNumChildren() > clamped)
    {
        seq.removeChild(seq.getNumChildren() - 1, nullptr);
        adjusted = true;
    }
    while (seq.getNumChildren() < clamped)
    {
        juce::ValueTree step{ kStepTag };
        step.setProperty(kSeqStepAttrNote, 0, nullptr);
        step.setProperty(kSeqStepAttrGate, true, nullptr);   // §5.5 defaults
        step.setProperty(kSeqStepAttrTie,  false, nullptr);
        step.setProperty(kSeqStepAttrRest, false, nullptr);
        seq.appendChild(step, nullptr);
        adjusted = true;
    }

    // Per-step: default any missing attribute; clamp note into the int8 range the POD
    // carries (so the SPSC handoff never narrows out of range) [§5.4 SeqStep].
    for (int i = 0; i < seq.getNumChildren(); ++i)
    {
        auto step = seq.getChild(i);
        if (! step.hasProperty(juce::Identifier{ kSeqStepAttrGate }))
        { step.setProperty(kSeqStepAttrGate, true, nullptr);  adjusted = true; }
        if (! step.hasProperty(juce::Identifier{ kSeqStepAttrTie }))
        { step.setProperty(kSeqStepAttrTie, false, nullptr);  adjusted = true; }
        if (! step.hasProperty(juce::Identifier{ kSeqStepAttrRest }))
        { step.setProperty(kSeqStepAttrRest, false, nullptr); adjusted = true; }
        if (! step.hasProperty(juce::Identifier{ kSeqStepAttrNote }))
        { step.setProperty(kSeqStepAttrNote, 0, nullptr);     adjusted = true; }
        else
        {
            const int note = static_cast<int>(step.getProperty(kSeqStepAttrNote));
            const int cl    = std::clamp(note, -128, 127);
            if (cl != note) { step.setProperty(kSeqStepAttrNote, cl, nullptr); adjusted = true; }
        }
    }

    return adjusted;
}

} // namespace

juce::ValueTree recoverState(const void* blob, int sizeBytes, RecoveryReport& outReport)
{
    outReport = RecoveryReport{};
    juce::StringArray deviations;   // collected, then coalesced into ONE note (L12).

    // --- L1: structural parse. nullopt -> truncated/garbage/wrong-root -> INIT. ----
    const auto parsed = readFromBlob(blob, sizeBytes);
    if (! parsed.has_value())
    {
        outReport.outcome = RecoveryOutcome::InitFallback;
        outReport.notes.add("Saved state could not be read; loaded the INIT patch.");
        return buildInitCanonical();
    }

    juce::ValueTree root = parsed->createCopy();   // detached, mutable working copy

    // --- Read schemaVersion (absent == v1 baseline, handled by migrateToCurrent). --
    const bool hasSchema = root.hasProperty(juce::Identifier{ ids::kAttrSchemaVersion });
    const int  schema    = hasSchema
                             ? static_cast<int>(root.getProperty(ids::kAttrSchemaVersion))
                             : mw101::version::kCurrentSchemaVersion;
    const bool isNewer   = schema > mw101::version::kCurrentSchemaVersion;

    // --- L3 raw retention (L6): capture the ORIGINAL bytes BEFORE we mutate. -------
    juce::MemoryBlock rawNewer;
    if (isNewer && blob != nullptr && sizeBytes > 0)
        rawNewer.append(blob, static_cast<std::size_t>(sizeBytes));

    // --- L2/L3 migrate: run the JUCE-free chain through the ValueTree adapter. -----
    // migrateToCurrent is tolerant of schema > CURRENT (a no-op down-bind that PRESERVES
    // the newer stamp so the raw round-trip stays possible) [§7.1; §8.3 L3].
    {
        ValueTreeMutableAdapter adapter{ root };
        mw::state::migrateToCurrent(adapter);
    }

    // --- L2/L3/L4 bind + validate every registry ID. ------------------------------
    const BindResult bind = bindAndValidate(root);

    // --- L8 sequence pad/clamp. ---------------------------------------------------
    const bool seqAdjusted = normalizeSequence(root);

    // --- L5: prefer partial recovery over INIT. INIT only when NOTHING survives. ---
    // No interpretable known param present (e.g. a parsed-but-empty/foreign <PARAMS>):
    // fall back to INIT rather than ship an all-defaults tree masquerading as recovery.
    if (bind.interpretableKnown == 0)
    {
        outReport.outcome = RecoveryOutcome::InitFallback;
        outReport.notes.add("Saved state had no recognizable parameters; loaded the INIT patch.");
        return buildInitCanonical();
    }

    // --- L6: attach the retained raw blob on the newer-down-interpreted rung. ------
    if (isNewer && rawNewer.getSize() > 0)
    {
        auto extras = root.getChildWithName(kExtrasTag);
        if (! extras.isValid())
        {
            extras = juce::ValueTree{ kExtrasTag };
            root.appendChild(extras, nullptr);
        }
        extras.setProperty(ids::kExtrasRawNewerBlob, juce::var{ rawNewer }, nullptr);
    }

    // --- Outcome ranking + coalesced note (L12). ----------------------------------
    if (isNewer)
    {
        outReport.outcome = RecoveryOutcome::NewerDownInterpreted;
        deviations.add("Loaded a session saved by a newer version: known settings were "
                       "kept and the newer data was preserved for re-saving.");
    }
    else if (bind.clampedOrReset)
    {
        outReport.outcome = RecoveryOutcome::ClampedValues;
        deviations.add("Some settings were out of range and were corrected to valid values.");
    }
    else if (bind.defaultedMissing || seqAdjusted)
    {
        outReport.outcome = RecoveryOutcome::MigratedAndBound;
    }
    else
    {
        outReport.outcome = RecoveryOutcome::CleanLoad;
    }

    // Sub-notes that ride along ANY non-clean outcome, all coalesced into one warning.
    if (bind.defaultedMissing)
        deviations.add("Missing settings were filled in with their defaults.");
    if (seqAdjusted)
        deviations.add("The stored sequence length was adjusted to fit the 100-step limit.");
    // On a non-newer ClampedValues outcome the clamp note is the headline; on the newer
    // rung any clamp is folded under the single newer-version note (already added).
    if (! isNewer && bind.clampedOrReset && deviations.size() > 1
        && outReport.outcome != RecoveryOutcome::ClampedValues)
        deviations.add("Some settings were out of range and were corrected.");

    // L12: COALESCE every sub-deviation into exactly ONE note (never a storm). The UI
    // surfaces this single string as one non-modal warning [§8.3 L12; ADR-021 L12].
    if (! deviations.isEmpty())
        outReport.notes.add(deviations.joinIntoString(" "));

    return root;
}

} // namespace mw::plugin::state
