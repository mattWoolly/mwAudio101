// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/params/ParamBridge.cpp — ParamBridge implementation (task 102).
// Realizes docs/design/00 §5.2 / §5.4 and ADR-001 C7 / C14.

#include "params/ParamBridge.h"

namespace mw::plugin {

void ParamBridge::prepare(juce::AudioProcessorValueTreeState& apvts)
{
    // Build the id -> APVTS-pointer table once, in registry-index order, from the
    // JUCE-free schema (kParamDefs is the single source of truth for the live set,
    // docs/design/06 §3.0). The string lookups happen HERE (off the audio thread), so
    // snapshot() never touches an APVTS string map [§5.4].
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
    {
        const auto& def = mw::params::kParamDefs[i];

        Slot slot{};
        // The raw (engineering / index) atomic the host writes — read once per block.
        slot.atomicValue = apvts.getRawParameterValue(def.id);
        // The ranged parameter gives the exact NormalisableRange for normalization;
        // cached so snapshot() does pure arithmetic, never a string lookup.
        slot.param        = apvts.getParameter(def.id);
        slot.isContinuous = (def.type == mw::params::ParamType::Continuous);

        // The layout is a pure function of kParamDefs (buildParameterLayout), so every
        // registry id resolves; assert it loudly if the layout ever drifts.
        jassert(slot.atomicValue != nullptr);
        jassert(slot.param != nullptr);

        slots_[i] = slot;
    }

    prepared_ = true;
}

NormalizedParamSnapshot ParamBridge::snapshot() const noexcept
{
    NormalizedParamSnapshot out{};

    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        const Slot& slot = slots_[i];

        // The ONE atomic read for this parameter this block. relaxed: the snapshot is
        // a value sample, not a synchronization point; the host's store is the only
        // writer and ordering across params does not matter [§5.4; ADR-001 C7].
        const float raw = (slot.atomicValue != nullptr)
                              ? slot.atomicValue->load(std::memory_order_relaxed)
                              : 0.0f;

        // Normalize to [0,1] using the cached range — pure arithmetic, no atomic, no
        // lookup. The bridge emits NORMALIZED only; engineering mapping stays in core.
        float norm = 0.0f;
        if (slot.param != nullptr)
            norm = slot.param->getNormalisableRange().convertTo0to1(raw);

        // Clamp defensively; convertTo0to1 is already in-range for in-range inputs.
        norm = juce::jlimit(0.0f, 1.0f, norm);

        out.normalizedValues[i] = norm;

        // For Choice/Bool, the raw atomic value IS the option index (0..N-1); store the
        // typed-enum index. Continuous params keep index 0 (unused).
        out.indexValues[i] = slot.isContinuous
                                 ? std::int16_t{ 0 }
                                 : static_cast<std::int16_t>(juce::roundToInt(raw));
    }

    return out;
}

} // namespace mw::plugin
