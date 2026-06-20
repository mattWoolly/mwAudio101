// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/StateSerializer.cpp — the ONE canonical state (de)serializer (task 023).
// Realizes docs/design/06 §5.1, §5.2, §5.4, §5.5; ADR-008 C8/C9; ADR-025 (no per-step
// accent); ADR-021 L1 (nullopt on structural parse failure).
//
// All three entry points run on the message thread; there is no audio-thread work here
// [docs/design/06 §5.2; ADR-008 C19].

#include "state/StateSerializer.h"

#include "state/StateTree.h"   // mwcore (JUCE-free): mw::state canonical key constants

namespace mw::plugin::state {

juce::ValueTree captureState(const juce::AudioProcessorValueTreeState& apvts,
                             const mw::state::Extras& extras,
                             int schemaVersion,
                             juce::String pluginVersion,
                             juce::String engineVersion,
                             int renderVersion)
{
    // --- Root with the four versioning attributes [docs/design/06 §5.1] -----------
    juce::ValueTree root{ juce::Identifier{ mw::state::kRootId } };
    root.setProperty(mw::state::kAttrSchemaVersion, schemaVersion, nullptr);
    root.setProperty(mw::state::kAttrPluginVersion, pluginVersion, nullptr);
    root.setProperty(mw::state::kAttrEngineVersion, engineVersion, nullptr);
    root.setProperty(mw::state::kAttrRenderVersion, renderVersion, nullptr);

    // --- <PARAMS>: a deep copy of the live APVTS state subtree --------------------
    // apvts.state is the canonical parameter-value ValueTree (every ID's current value);
    // createCopy() detaches a deep copy on the message thread (copyState() is non-const
    // because it locks/flushes, but the captureState contract is const, so we read the
    // already-backed state member). We re-tag the copy under a node whose type is the
    // canonical PARAMS id so the read side finds it by name regardless of the host
    // APVTS's own valueTreeType string [docs/design/06 §5.1].
    juce::ValueTree canonicalParams{ juce::Identifier{ mw::state::kParamsId } };
    canonicalParams.copyPropertiesAndChildrenFrom(apvts.state.createCopy(), nullptr);
    root.appendChild(canonicalParams, nullptr);

    // --- <extras>: the non-parameter state [docs/design/06 §5.4; ADR-008 C8] ------
    juce::ValueTree extrasNode{ juce::Identifier{ mw::state::kExtrasId } };
    extrasNode.setProperty(mw::state::kExtrasArpLatch,   extras.arpLatch, nullptr);
    extrasNode.setProperty(mw::state::kExtrasSeedLocked, extras.seedLocked, nullptr);
    // int64 carried as a juce::var int64 so the full seed survives [docs/design/06 §5.4].
    extrasNode.setProperty(mw::state::kExtrasDriftSeed,
                           juce::var{ static_cast<juce::int64>(extras.driftSeed) }, nullptr);

    // --- <seq>: stepCount + one <step> per ACTIVE step (note/gate/tie/rest; NO
    //     accent) [docs/design/06 §5.5; ADR-025] ---------------------------------
    juce::ValueTree seq{ juce::Identifier{ mw::state::kSeqId } };
    const int activeSteps = juce::jlimit(0, mw::state::kMaxSeqSteps, extras.stepCount);
    seq.setProperty(kSeqAttrStepCount, activeSteps, nullptr);
    for (int i = 0; i < activeSteps; ++i)
    {
        const auto& s = extras.steps[static_cast<std::size_t>(i)];
        juce::ValueTree step{ juce::Identifier{ mw::state::kStepId } };
        step.setProperty(kSeqStepAttrNote, static_cast<int>(s.noteSemitone), nullptr);
        step.setProperty(kSeqStepAttrGate, s.gate, nullptr);
        step.setProperty(kSeqStepAttrTie,  s.tie,  nullptr);
        step.setProperty(kSeqStepAttrRest, s.rest, nullptr);
        // NO accent attribute — the v1 sequencer has no per-step accent [ADR-025].
        seq.appendChild(step, nullptr);
    }
    extrasNode.appendChild(seq, nullptr);

    root.appendChild(extrasNode, nullptr);
    return root;
}

void writeToBlob(const juce::ValueTree& canonical, juce::MemoryBlock& dest)
{
    // Serialize to the host's opaque blob as JUCE binary [ADR-008 C9]. The
    // MemoryOutputStream is told NOT to append so a reused MemoryBlock is overwritten,
    // not extended.
    dest.reset();
    juce::MemoryOutputStream stream{ dest, /*appendToExistingBlockContents*/ false };
    canonical.writeToStream(stream);
    stream.flush();
}

std::optional<juce::ValueTree> readFromBlob(const void* data, int sizeBytes)
{
    if (data == nullptr || sizeBytes <= 0)
        return std::nullopt;

    // ValueTree::readFromData returns an invalid tree on unreadable/truncated bytes;
    // it never throws. A valid tree whose root is not MW101_STATE is structurally
    // invalid as a canonical state blob [docs/design/06 §5.2; ADR-021 L1].
    const juce::ValueTree tree =
        juce::ValueTree::readFromData(data, static_cast<size_t>(sizeBytes));

    if (! tree.isValid())
        return std::nullopt;
    if (! tree.hasType(juce::Identifier{ mw::state::kRootId }))
        return std::nullopt;

    return tree;
}

} // namespace mw::plugin::state
