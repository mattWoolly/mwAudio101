// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/ParamSnapshot.h — the concrete, JUCE-free, per-block normalized
// parameter POD the engine reads (task 102b).
//
// WHAT IT IS. mw::ParamSnapshot is the immutable, normalized view of every LIVE
// parameter for one process() block — the concrete type that core/BlockContext.h
// only forward-declares (`struct ParamSnapshot;`) and holds by pointer
// (`const ParamSnapshot* params;`). Realizes docs/design/00 §5.2 / §5.4 and
// ADR-001 C7 / C14: the shell reads APVTS atomics ONCE per block into this immutable
// normalized snapshot, then the core maps normalized -> engineering units internally
// against core/calibration/Calibration.h and drives the control-rate smoothers; the
// core therefore never reads a std::atomic in a tight loop [docs/design/00 §5.4].
//
// SHAPE. One slot per LIVE kParamDefs entry, in registry-index order (the single
// source of truth for the live parameter set is core/params/ParamDefs.h kParamDefs,
// docs/design/06 §3.0). `normalizedValues[i]` is the host-automation normalized value
// in [0,1] for registry slot i (continuous params; for choice/bool it carries the
// normalized projection of the option index). `indexValues[i]` is the typed-enum
// option index for Choice/Bool params (0 for Continuous). This canonicalizes — in the
// JUCE-free core — the interim plugin-side mw::plugin::NormalizedParamSnapshot the
// ParamBridge (task 102) emits; wiring the bridge to emit THIS type and the engine
// reading/applying it to modules is the full-processor task (111), OUT OF SCOPE here.
//
// JUCE-FREE POD. No juce::* type appears; the struct is an aggregate of two
// std::array members, so it is trivially-copyable + standard-layout. It is
// constructed/filled OFF the audio thread (by the bridge), then handed to the core by
// const pointer across the no-JUCE seam [docs/design/00 §5.2; ADR-001 C3, C14]. The
// core never allocates, copies, grows, or frees it on the hot path — it dereferences
// the borrowed const pointer only.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ParamDefs.h"   // mw::params::kParamDefs — the LIVE field universe (§3.0)

namespace mw {

// The immutable, normalized parameter snapshot for one block — a JUCE-free POD.
// One slot per LIVE kParamDefs entry, in registry-index order. Default-constructs to
// all-zero; the bridge fills it off the audio thread [docs/design/00 §5.4].
struct ParamSnapshot {
    // Compile-time slot count == the live parameter set (docs/design/06 §3.0). No
    // runtime growth: the field universe is fixed by the registry.
    static constexpr int kCount = static_cast<int>(mw::params::kParamDefs.size());

    // Parallel, registry-index-keyed storage. Public so the type stays an aggregate
    // POD the off-thread bridge fills directly; the accessors below are read-side
    // conveniences for the engine.
    std::array<float,        static_cast<std::size_t>(kCount)> normalizedValues{}; // [0,1]
    std::array<std::int16_t, static_cast<std::size_t>(kCount)> indexValues{};      // choice/bool index

    // Number of live parameter slots (== kCount).
    static constexpr int count() noexcept { return kCount; }

    // Normalized [0,1] value for registry slot i. Continuous: the host-automation
    // value. Choice/Bool: the normalized projection of the option index.
    constexpr float normalized(int i) const noexcept {
        return normalizedValues[static_cast<std::size_t>(i)];
    }

    // Typed-enum option index for registry slot i (Choice/Bool). 0 for Continuous.
    constexpr int index(int i) const noexcept {
        return static_cast<int>(indexValues[static_cast<std::size_t>(i)]);
    }
};

} // namespace mw
