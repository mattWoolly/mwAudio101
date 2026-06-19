// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/params/ParamBridge.h — the APVTS <-> normalized-snapshot marshaller
// (task 102). Realizes docs/design/00 §5.2 / §5.4 and ADR-001 C7 / C14.
//
// WHAT IT OWNS. ParamBridge is the thin, separately-tested plugin-side adapter that
// inverts parameters across the core/plugin seam [docs/design/00 §5.4; ADR-001
// Decision]:
//
//   * prepare(apvts) — built once off the audio thread (from prepareToPlay). It walks
//     the JUCE-free param registry (core/params/ParamDefs.h kParamDefs, the single
//     source of truth for the live parameter set, docs/design/06 §3.0) and caches, in
//     registry-index order, each parameter's APVTS atomic pointer + its
//     juce::RangedAudioParameter. This IS the "parameter-id -> APVTS-pointer table"
//     the task scope names; it is built from the schema layout, never hand-listed.
//
//   * snapshot() — RT-safe; reads each cached std::atomic EXACTLY ONCE into an
//     immutable, normalized [0,1] / typed-enum POD (NormalizedParamSnapshot). The core
//     therefore never reads a std::atomic in a tight loop: the whole atomic read is
//     hoisted out of process() into one once-per-block snapshot [§5.4; ADR-001 C7].
//
// NORMALIZED ONLY. The snapshot carries the host-automation NORMALIZED value (always
// 0..1 for continuous params, docs/design/06 §3.0 / ADR-008 C4) plus the typed-enum
// INDEX for Choice/Bool params. The bridge does NOT compute engineering units — the
// normalized->engineering mapping lives in core against core/calibration/Calibration.h
// [docs/design/00 §5.4; module table §3.2]. The bridge produces normalized only.
//
// NO JUCE TYPE CROSSES THE SEAM. NormalizedParamSnapshot is a JUCE-free,
// trivially-copyable, standard-layout POD: a fixed-capacity array of normalized floats
// + a parallel index array, sized by the registry. No juce::* type appears in it, so a
// pointer to it is exactly what the core BlockContext::params expects (the core-owned
// mw::ParamSnapshot aggregate type — sized/named by the param-schema stream, task 006
// — is OUT OF SCOPE here; the bridge produces this normalized POD view) [§5.2;
// ADR-001 C14].
//
// WHY plugin/ AND NOT core/: this TU references juce::AudioProcessorValueTreeState /
// juce::RangedAudioParameter, so it MUST live plugin-side (the no-JUCE-in-core guard
// fails on any JUCE token under core/) [ADR-001 C1]. The registry it consumes stays
// JUCE-free; only this marshaller is JUCE-aware [docs/design/00 §3.3].

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

#include "params/ParamDefs.h"   // mwcore (JUCE-free): mw::params::kParamDefs

namespace mw::plugin {

// The immutable, normalized parameter snapshot — a JUCE-free POD. One slot per LIVE
// kParamDefs entry, in registry-index order. `normalized[i]` is the host-automation
// normalized value in [0,1]; `index[i]` is the typed-enum option index for Choice/Bool
// params (0 for Continuous params). Trivially-copyable + standard-layout so it can be
// snapshotted once per block and handed across the no-JUCE seam by pointer [§5.2/§5.4].
struct NormalizedParamSnapshot
{
    // Compile-time slot count == the live parameter set. No runtime growth.
    static constexpr int kCount = static_cast<int>(mw::params::kParamDefs.size());

    std::array<float,         kCount> normalizedValues{};  // [0,1] host-automation value
    std::array<std::int16_t,  kCount> indexValues{};       // Choice/Bool option index

    static constexpr int count() noexcept { return kCount; }

    // Normalized [0,1] value for registry slot i (continuous: the automation value;
    // choice/bool: the normalized projection of the option index).
    float normalized(int i) const noexcept { return normalizedValues[static_cast<std::size_t>(i)]; }

    // Typed-enum option index for registry slot i (Choice/Bool). 0 for Continuous.
    int index(int i) const noexcept { return indexValues[static_cast<std::size_t>(i)]; }
};

class ParamBridge
{
public:
    ParamBridge() = default;

    // Build the parameter-id -> APVTS-pointer table from the schema layout. Called
    // once off the audio thread (prepareToPlay). Idempotent / re-callable. NOT RT-safe
    // (it touches the APVTS string-keyed lookup); never call from process() [§5.4].
    void prepare(juce::AudioProcessorValueTreeState& apvts);

    // Read each cached atomic EXACTLY ONCE into a fresh normalized POD. RT-safe:
    // one std::atomic load per slot, pure arithmetic for normalization, no allocation,
    // no locks, no string lookup. This is the once-per-block parameter sample [§5.4;
    // ADR-001 C7].
    NormalizedParamSnapshot snapshot() const noexcept;

    // True once prepare() has populated the table (every slot bound).
    bool isPrepared() const noexcept { return prepared_; }

private:
    // One cached binding per LIVE kParamDefs entry, in registry-index order.
    struct Slot
    {
        std::atomic<float>*               atomicValue{ nullptr };  // borrowed; APVTS owns it
        const juce::RangedAudioParameter* param{ nullptr };        // for convertTo0to1
        bool                              isContinuous{ true };
    };

    std::array<Slot, NormalizedParamSnapshot::kCount> slots_{};
    bool prepared_{ false };
};

} // namespace mw::plugin
