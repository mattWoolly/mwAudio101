// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/state/SeqPatternCodec.cpp — readSeqPattern implementation (task 111c). The
// inverse of StateSerializer::captureState's <seq> write. Message-thread only.

#include "SeqPatternCodec.h"

#include <algorithm>

#include "StateSerializer.h"   // kSeqAttrStepCount / kSeqStepAttr* (the on-tree <step> shape)
#include "state/StateTree.h"   // mw::state::kExtrasId / kSeqId / kStepId / kExtras*

namespace mw::plugin::state {

mw::state::Extras readSeqPattern(const juce::ValueTree& canonical)
{
    mw::state::Extras extras{};

    const auto extrasNode =
        canonical.getChildWithName(juce::Identifier{ mw::state::kExtrasId });
    if (! extrasNode.isValid())
        return extras;   // no <extras> -> empty default pattern.

    // Non-seq <extras> scalars (so a full <extras> round-trips through the handoff).
    extras.arpLatch =
        static_cast<bool>(extrasNode.getProperty(juce::Identifier{ mw::state::kExtrasArpLatch }, false));
    extras.driftSeed = static_cast<std::int64_t>(
        static_cast<juce::int64>(extrasNode.getProperty(juce::Identifier{ mw::state::kExtrasDriftSeed }, 0)));
    extras.seedLocked =
        static_cast<bool>(extrasNode.getProperty(juce::Identifier{ mw::state::kExtrasSeedLocked }, false));

    const auto seq = extrasNode.getChildWithName(juce::Identifier{ mw::state::kSeqId });
    if (! seq.isValid())
        return extras;

    // Declared count, clamped to the fixed capacity; read only that many <step>
    // children so the POD never overflows [docs/design/06 §5.4; ADR-021].
    const int declared =
        static_cast<int>(seq.getProperty(juce::Identifier{ kSeqAttrStepCount }, 0));
    const int activeSteps = std::clamp(declared, 0, mw::state::kMaxSeqSteps);

    int written = 0;
    for (int i = 0; i < seq.getNumChildren() && written < activeSteps; ++i)
    {
        const auto step = seq.getChild(i);
        if (! step.hasType(juce::Identifier{ mw::state::kStepId }))
            continue;

        mw::state::SeqStep& dst = extras.steps[static_cast<std::size_t>(written)];
        dst.noteSemitone = static_cast<std::int8_t>(
            static_cast<int>(step.getProperty(juce::Identifier{ kSeqStepAttrNote }, 0)));
        dst.gate = static_cast<bool>(step.getProperty(juce::Identifier{ kSeqStepAttrGate }, false));
        dst.tie  = static_cast<bool>(step.getProperty(juce::Identifier{ kSeqStepAttrTie  }, false));
        dst.rest = static_cast<bool>(step.getProperty(juce::Identifier{ kSeqStepAttrRest }, false));
        ++written;
    }

    extras.stepCount = written;
    return extras;
}

} // namespace mw::plugin::state
