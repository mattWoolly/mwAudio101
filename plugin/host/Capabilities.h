// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/host/Capabilities.h — capability-rung enums + ResolvedCapabilities POD
// (task 098). Realizes docs/design/09 §7.2/§8.2 and ADR-022 items 1-2.
//
// These are pure PODs/enums with NO JUCE dependency, so they compile and unit-test
// in the headless mwcore test binary. The wrapper is capability-aware; the engine
// is capability-agnostic and receives the SAME normalized per-voice offset
// regardless of rung [ADR-022 C1-C4].

#pragma once

#include <cstdint>
#include <type_traits>

namespace mw::plugin {

// Enum order is FROZEN by docs/design/09 §7.2.
enum class NoteExpressionRung : std::uint8_t {
    Native = 0,    // CLAP typed note-expressions
    MpeOverMidi,   // VST3/AU/LV2/Standalone: reconstruct per-channel MIDI -> per-voice
    Collapsed      // global channel bend + channel pressure (universal floor)
};

enum class TransportRung : std::uint8_t {
    SampleAccurate = 0, // CLAP transport event, sub-block edges
    BlockQuantized,     // VST3/AU/LV2: one PositionInfo/block, edge at block boundary
    FreeRun             // no transport: INTERNAL clock at the RATE knob
};

// All five wrappers [docs/design/09 §8.2].
enum class PluginFormat : std::uint8_t {
    VST3 = 0,
    AU,
    CLAP,
    Standalone,
    LV2
};

// Published via atomic ptr (the §6.3 pattern). Trivially copyable POD.
struct ResolvedCapabilities {
    NoteExpressionRung noteExpr;
    TransportRung      transport;
};

static_assert(std::is_trivially_copyable_v<ResolvedCapabilities>,
              "ResolvedCapabilities MUST be a trivially copyable POD [docs/design/09 §7.2].");

} // namespace mw::plugin
