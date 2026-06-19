// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/Clock.h — the single H->L edge node + 3-way source wrapper + swing
// (task 086). Realizes docs/design/05 §7.1-§7.8 / ADR-007 §Decision 5, C17-C24, C27.
//
// One H->L edge detector lives on one clock node; whatever source feeds it
// (Internal LFO/CLK 0.1-30 Hz, HostSync from absolute PPQ, Ext pulses), the
// downstream arp/seq/RANDOM advance is phase-identical [§7.1; ADR-007 C17]. This
// class is the SINGLE producer of edges for all three sources [§7.7, C27].
//
// Real-time invariants (asserted by tests/unit/ClockTest):
//   - renderEdges() and every setter are noexcept hot paths [§10; ADR-007 C26].
//   - renderEdges() writes ONLY into the caller's pre-sized span; it never
//     allocates, never grows the span, and clamps to its capacity [§7.7; RT-1].
//   - HostSync phase is recomputed from ABSOLUTE PPQ every block, so tempo
//     automation / loop wrap / scrub re-derive the next edge with no cumulative
//     drift [§7.4, C19].
// No `juce::*` type appears here; the plugin layer fills the doc-00 mw::TransportInfo
// POD from juce::AudioPlayHead [ADR-001/D2]. Parameter IDs/ranges are owned by
// docs/design/06 §2 — not minted here [ADR-008].

#pragma once

#include <cstdint>
#include <span>

#include "../BlockContext.h"      // mw::TransportInfo (doc 00 §5.3)
#include "ControlTypes.h"         // ClockSource, HostRate, ClockEdge (doc 05 §7.7)

namespace mw::control {

class Clock {
public:
    // Size internal state; no allocation here or after [§7.7].
    void prepare(double sampleRate) noexcept;

    // The three mutually-exclusive sources [§7.2; ADR-007 §Decision 5].
    void setSource(ClockSource s) noexcept;

    // Internal-only tempo, clamped to the normative 0.1..30 Hz range [§7.3, C18].
    void setInternalRateHz(float hz) noexcept;

    // Host-sync musical rate selector (§7.8 table). Inert under Internal/Ext [C23].
    void setHostRate(HostRate r) noexcept;

    // SWING fraction 0.50..0.75, host-sync only; 0.50 == off [§7.6, C24].
    void setSwing(float fraction) noexcept;

    // Clock-reset-on-keypress flag; default-on [§7.5, C22].
    void setClockResetOnKeypress(bool on) noexcept;

    // Produce all H->L edges for this block into the caller's pre-sized `out` span
    // (no allocation; clamps to out.size()). `numFrames` is the block length in
    // samples (the doc-00 TransportInfo POD carries no sample count, so it is passed
    // explicitly). For Ext, `extPulseOffsets` are the per-block pulse positions; one
    // pulse == one step [§7.2, §7.4]. `outCount` receives the number of edges written.
    void renderEdges(const mw::TransportInfo& t,
                     std::span<const int> extPulseOffsets,
                     std::span<ClockEdge> out,
                     int numFrames,
                     int& outCount) noexcept;

    // Re-phase the edge node to a keypress at `sampleOffset` (clock-reset, §7.5/C22).
    // Internal: resets the phase accumulator so the next edge lands at the keypress.
    // HostSync/Ext: resets the next-boundary reference to the keypress sample. The
    // reset path is reused for host/EXT re-phase [§7.5; ADR-007 §Consequences].
    void resetToKeypress(int sampleOffset) noexcept;

    ClockSource source() const noexcept { return source_; }
    bool clockResetOnKeypress() const noexcept { return resetOnKeypress_; }

    // SWING is active only under HostSync; grayed/inert under Internal/Ext [§7.6, §8].
    bool swingActive() const noexcept { return source_ == ClockSource::HostSync; }

private:
    ClockSource source_ = ClockSource::Internal;
    double sampleRate_ = 48000.0;

    // Internal edge accumulator. We track the fractional sample position (relative
    // to the NEXT block's start) at which the next H->L edge fires; advancing it by
    // the period each edge and carrying the remainder across blocks keeps the
    // internal clock free-running with no per-block drift [§7.3].
    double internalNextEdge_ = 0.0;     // samples-from-block-start of the next edge
    float internalRateHz_ = 1.0f;       // clamped to 0.1..30 (Internal only)

    HostRate hostRate_ = HostRate::Sixteenth;
    float swing_ = 0.5f;                // 0.50 == off
    bool resetOnKeypress_ = true;       // default-on (C22)

    // Keypress re-phase reference (-1 == none pending for this block).
    int pendingKeypress_ = -1;
};

} // namespace mw::control
