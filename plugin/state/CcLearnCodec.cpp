// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/CcLearnCodec.cpp — writeCcLearn / readCcLearn (task 023b). Round-trips the
// NON-DEFAULT CcLearnMap bindings through the canonical <extras><ccLearn> tree; an absent /
// garbage node falls back to the default seed without failing the load. Message-thread only.

#include "CcLearnCodec.h"

#include <array>

#include "params/ParamDefs.h"                  // mw::params::kParamDefs (frozen registry size)
#include "calibration/CcLearnMapConstants.h"   // mw::cal::cclearn::kHoldParamIndex
#include "state/StateTree.h"                   // mw::state::kExtrasId / kCcLearnId

namespace mw::plugin::state {

namespace {

// True iff `paramIndex` is a value the live map may legitimately hold: a real registry
// index [0, kParamDefs.size()), the HOLD sentinel (CC64 default), or the unmapped sentinel
// (-1). Any other value in a restored <binding> is garbage and is rejected so it can never
// poison the audio-thread lookup [docs/design/09 §6.2/§6.3; ADR-021].
[[nodiscard]] bool paramIndexIsValid(std::int32_t paramIndex) noexcept
{
    if (paramIndex == CcLearnMap::kUnmapped)             return true;   // -1 unmapped
    if (paramIndex == mw::cal::cclearn::kHoldParamIndex) return true;   // HOLD (CC64)
    return paramIndex >= 0
        && paramIndex < static_cast<std::int32_t>(mw::params::kParamDefs.size());
}

// True iff two binding rows are byte-identical (same mapping). Used to diff a live row
// against the corresponding fresh-seed row so only CHANGED rows are persisted.
[[nodiscard]] bool sameBinding(const CcBinding& a, const CcBinding& b) noexcept
{
    return a.ccNumber == b.ccNumber
        && a.paramIndex == b.paramIndex
        && a.enabled == b.enabled;
}

} // namespace

void writeCcLearn(juce::ValueTree& extrasNode, const CcLearnMap& map)
{
    if (! extrasNode.isValid())
        return;

    // The canonical seed: a fresh default-constructed map IS the §6.2 table, so diffing
    // against it identifies exactly the rows the user changed — no re-declared seed here.
    const CcLearnMap seed{};
    const CcBinding* live     = map.liveBuffer();
    const CcBinding* seedRows = seed.liveBuffer();

    juce::ValueTree ccLearn{ juce::Identifier{ mw::state::kCcLearnId } };
    for (int cc = 0; cc < CcLearnMap::kNumCc; ++cc)
    {
        const CcBinding& row = live[cc];
        if (sameBinding(row, seedRows[cc]))
            continue;   // unchanged from the seed -> not persisted (compact, default-relative)

        juce::ValueTree binding{ juce::Identifier{ kCcBindingId } };
        binding.setProperty(juce::Identifier{ kCcBindingAttrCc },
                            static_cast<int>(row.ccNumber), nullptr);
        binding.setProperty(juce::Identifier{ kCcBindingAttrParam },
                            static_cast<int>(row.paramIndex), nullptr);
        binding.setProperty(juce::Identifier{ kCcBindingAttrOn }, row.enabled, nullptr);
        ccLearn.appendChild(binding, nullptr);
    }

    // Only attach <ccLearn> when there is at least one non-default row: a session that never
    // used MIDI-learn writes NO node, keeping the blob byte-compatible with pre-023b state.
    if (ccLearn.getNumChildren() > 0)
        extrasNode.appendChild(ccLearn, nullptr);
}

int readCcLearn(const juce::ValueTree& canonical, CcLearnMap& map)
{
    const auto extrasNode =
        canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    if (! extrasNode.isValid())
        return 0;   // no <extras> -> leave the map at its default seed [ADR-021].

    const auto ccLearn =
        extrasNode.getChildWithName(juce::Identifier{ mw::state::kCcLearnId });
    if (! ccLearn.isValid())
        return 0;   // no <ccLearn> -> default seed (pre-023b / never-learned) [ADR-021].

    // Edit the inactive buffer (pre-seeded with the current live contents, i.e. the default
    // seed at this point) and apply every well-formed, validated <binding> over it. The
    // audio thread never reads the inactive buffer, so this cannot race lookup() [§6.3].
    CcBinding* draft = map.editableCopy();

    int applied = 0;
    for (int i = 0; i < ccLearn.getNumChildren(); ++i)
    {
        const auto binding = ccLearn.getChild(i);
        if (! binding.hasType(juce::Identifier{ kCcBindingId }))
            continue;   // skip foreign children defensively.

        const int cc = static_cast<int>(
            binding.getProperty(juce::Identifier{ kCcBindingAttrCc }, -1));
        const std::int32_t paramIndex = static_cast<std::int32_t>(
            static_cast<int>(binding.getProperty(juce::Identifier{ kCcBindingAttrParam },
                                                 CcLearnMap::kUnmapped)));
        const bool enabled = static_cast<bool>(
            binding.getProperty(juce::Identifier{ kCcBindingAttrOn }, false));

        // Reject out-of-range CC or invalid param index: a garbage row is dropped (the seed
        // row stays) rather than failing the whole load [ADR-021 fallback discipline].
        if (cc < 0 || cc >= CcLearnMap::kNumCc)
            continue;
        if (! paramIndexIsValid(paramIndex))
            continue;

        CcBinding& dst = draft[cc];
        dst.ccNumber   = static_cast<std::uint8_t>(cc);
        dst.paramIndex = paramIndex;
        dst.enabled    = enabled;
        ++applied;
    }

    map.publish();   // one atomic swap; the audio thread's next lookup() sees the restore.
    return applied;
}

} // namespace mw::plugin::state
