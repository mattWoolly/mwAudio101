// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/preset/PresetManager.cpp — the in-memory factory preset bank + per-slot INIT
// fallback (task 119). See PresetManager.h for the contract and the WHY-plugin/
// rationale. Realizes docs/design/06 §10.1, §10.2, §10.3, §8.3 L9, §5.3; ADR-008
// C17/C18; ADR-021 L9/L11.
//
// Message-thread / setup-time only; no audio-thread work [docs/design/06 §10.2, §12;
// ADR-008 C19]. The audio-thread SPSC publish of the applied <extras> is owned by the
// processor (task 111), out of scope here.

#include "preset/PresetManager.h"

#include <algorithm>

#include "preset/PresetFormat.h"            // loadPresetJson / PresetMeta (task 025)
#include "state/StateSerializer.h"          // writeToBlob (slot wire form)
#include "state/LoadFailure.h"              // recoverState (shared §7 migrate + §8 recover)

#include "state/StateTree.h"                // mwcore (JUCE-free): canonical key constants
#include "state/Extras.h"                   // mwcore (JUCE-free): kMaxSeqSteps

namespace mw::plugin::preset {

namespace {

namespace ids = mw::state;

// --- <seq> per-step attribute keys (note/gate/tie/rest ONLY — no accent in v1) -------
// Mirror the StateSerializer on-tree shape so reading the recovered <extras> back into
// the POD matches what was written [docs/design/06 §5.5; ADR-025].
constexpr const char* kSeqAttrStepCount = "stepCount";
constexpr const char* kSeqStepAttrNote  = "note";
constexpr const char* kSeqStepAttrGate  = "gate";
constexpr const char* kSeqStepAttrTie   = "tie";
constexpr const char* kSeqStepAttrRest  = "rest";

// Apply the recovered canonical tree's <PARAMS> subtree to the live APVTS (§5.3 step 5).
// The canonical <PARAMS> carries one <PARAM id= value=> child per live ID with `value`
// in MODELED (denormalised) units — the same shape APVTS serializes its own state in
// (StateSerializer captures from apvts.state). We re-tag a deep copy with the APVTS's
// own state type so replaceState accepts it, then replace. This stores every parameter
// atomically on the message thread; the audio thread later reads the APVTS atoms
// lock-free [docs/design/06 §5.3; ADR-008 C19].
void bindParamsToApvts(const juce::ValueTree& canonical,
                       juce::AudioProcessorValueTreeState& apvts)
{
    const auto params = canonical.getChildWithName(juce::Identifier{ ids::kParamsId });
    if (! params.isValid())
        return;

    juce::ValueTree apvtsState{ apvts.state.getType() };
    apvtsState.copyPropertiesAndChildrenFrom(params, nullptr);
    apvts.replaceState(apvtsState);
}

// Read the recovered canonical tree's <extras>/<seq> back into the JUCE-free POD
// (§5.4). stepCount is clamped into the fixed 100-step capacity; per-step note is
// clamped into the int8 the POD carries; missing step attributes take the §5.5 defaults.
// The caller (processor, task 111) is responsible for the SPSC publish to the audio
// thread — this only fills the message-thread POD [docs/design/06 §5.4; ADR-008 C20].
void bindExtrasToPod(const juce::ValueTree& canonical, mw::state::Extras& extras)
{
    extras = mw::state::Extras{};   // reset to empty defaults

    const auto ex = canonical.getChildWithName(juce::Identifier{ ids::kExtrasId });
    if (! ex.isValid())
        return;

    extras.arpLatch   = static_cast<bool>(ex.getProperty(ids::kExtrasArpLatch, false));
    extras.seedLocked = static_cast<bool>(ex.getProperty(ids::kExtrasSeedLocked, false));
    extras.driftSeed  =
        static_cast<std::int64_t>(ex.getProperty(ids::kExtrasDriftSeed, juce::var{ juce::int64{ 0 } }));

    const auto seq = ex.getChildWithName(juce::Identifier{ ids::kSeqId });
    if (! seq.isValid())
        return;

    const int declared = static_cast<int>(seq.getProperty(kSeqAttrStepCount, 0));
    const int count = juce::jlimit(0, mw::state::kMaxSeqSteps,
                                   juce::jmin(declared, seq.getNumChildren()));
    extras.stepCount = count;
    for (int i = 0; i < count; ++i)
    {
        const auto step = seq.getChild(i);
        auto& s = extras.steps[static_cast<std::size_t>(i)];
        s.noteSemitone = static_cast<std::int8_t>(
            juce::jlimit(-128, 127, static_cast<int>(step.getProperty(kSeqStepAttrNote, 0))));
        s.gate = static_cast<bool>(step.getProperty(kSeqStepAttrGate, true));
        s.tie  = static_cast<bool>(step.getProperty(kSeqStepAttrTie,  false));
        s.rest = static_cast<bool>(step.getProperty(kSeqStepAttrRest, false));
    }
}

} // namespace

PresetManager::PresetManager()
{
    // At this development stage there is NO embedded preset BinaryData (the 64 files and
    // the BinaryData embedding are later tasks 131/144-150, out of scope here), so the
    // default bank is empty. getNumPresets()==0 is valid and this never aborts/crashes
    // [§10.2; §8.3 L9]. The embedded-bank wiring (gathering BinaryData into SlotSources)
    // is a later task; it reuses decodeSlot() so the L9 per-slot fallback is identical.
}

PresetManager::PresetManager(const std::vector<SlotSource>& sources)
{
    slots_.reserve(sources.size());
    for (const auto& source : sources)
        decodeSlot(source);
}

void PresetManager::decodeSlot(const SlotSource& source)
{
    // Decode the slot's JSON via the canonical task-025 loader. loadPresetJson takes a
    // juce::File, so write the in-memory JSON to a temporary file and decode it through
    // the SAME validator/projection the real embedded bank will use [§6.3; §10.2]. A
    // juce::TemporaryFile is deleted when it goes out of scope.
    PresetMeta meta;
    std::optional<juce::ValueTree> canonical;

    if (source.json.isNotEmpty())
    {
        const juce::TemporaryFile temp{ ".mw101preset" };
        if (temp.getFile().replaceWithText(source.json))
            canonical = loadPresetJson(temp.getFile(), meta);
    }

    Slot slot;
    slot.name = source.name;

    if (canonical.has_value())
    {
        // A clean decode: keep the decoded category and serialize the canonical tree to
        // the slot's wire form, so loadPreset later runs the SAME §8 recovery path.
        slot.category = meta.category;
        mw::plugin::state::writeToBlob(*canonical, slot.canonicalBlob);
    }
    else
    {
        // §8.3 L9: a missing/undecodable embedded preset resolves THAT slot to INIT and
        // warns NAMING it; the rest of the bank still loads (we keep adding slots), and
        // construction never aborts/empties the bank [ADR-021 L9]. We store an EMPTY
        // blob: loadPreset feeds it through recoverState, whose L1 rung yields the §11
        // INIT canonical — the single shared fallback chain, not a duplicated INIT path.
        slot.category = juce::String{};
        slot.canonicalBlob.reset();
        constructionReport_.outcome = mw::plugin::state::RecoveryOutcome::InitFallback;
        constructionReport_.notes.add(
            "Factory preset \"" + source.name
            + "\" could not be read and was replaced with the INIT patch.");
    }

    slots_.push_back(std::move(slot));
}

int PresetManager::getNumPresets() const noexcept
{
    return static_cast<int>(slots_.size());
}

juce::String PresetManager::getName(int index) const
{
    if (index < 0 || index >= getNumPresets())
        return {};
    return slots_[static_cast<std::size_t>(index)].name;
}

juce::String PresetManager::getCategory(int index) const
{
    if (index < 0 || index >= getNumPresets())
        return {};
    return slots_[static_cast<std::size_t>(index)].category;
}

void PresetManager::loadPreset(int index, juce::AudioProcessorValueTreeState& apvts,
                               mw::state::Extras& extras,
                               mw::plugin::state::RecoveryReport& outReport)
{
    outReport = mw::plugin::state::RecoveryReport{};

    // An out-of-range index is a safe no-op (no crash); APVTS/extras are left untouched
    // [§10.1; §8.3 L7 — never leave a half-applied state]. The caller may inspect the
    // (clean) report and skip the SPSC publish.
    if (index < 0 || index >= getNumPresets())
        return;

    const auto& slot = slots_[static_cast<std::size_t>(index)];

    // Run the slot's canonical blob through the SAME migration (§7) + recovery (§8)
    // chain session state uses (recoverState, task 024): it migrates to CURRENT, defaults
    // missing params, clamps/resets out-of-range values, pads/clamps the sequence, and —
    // for an INIT slot whose blob is empty — yields the §11 INIT canonical via the L1
    // rung. recoverState NEVER throws and always returns a complete valid tree [§10.2;
    // ADR-008 C17; ADR-021 L9/L11].
    const juce::ValueTree recovered = mw::plugin::state::recoverState(
        slot.canonicalBlob.getData(), static_cast<int>(slot.canonicalBlob.getSize()),
        outReport);

    // §5.3 step 5: assemble fully on the message thread, then bind. Parameters land in
    // APVTS (atomic stores via replaceState); <extras> lands in the POD the processor
    // will SPSC-publish to the audio thread (task 111). No audio-thread work here.
    bindParamsToApvts(recovered, apvts);
    bindExtrasToPod(recovered, extras);
}

juce::Array<int> PresetManager::indicesForCategory(juce::StringRef category) const
{
    juce::Array<int> out;
    for (int i = 0; i < getNumPresets(); ++i)
        if (slots_[static_cast<std::size_t>(i)].category == category)
            out.add(i);
    return out;
}

} // namespace mw::plugin::preset
