// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/Arpeggiator.h — UP / U&D / DOWN over a fixed 32-key held bitmap
// (task 084). Realizes docs/design/05 §5.1–§5.4.
//
// Three mutually-exclusive play directions over a fixed 32-bit held-key bitmap with
// a HOLD latch, NO automatic octave expansion (octave shift is the global TRANSPOSE
// only, consumed by the pitch/voice layer — out of scope here), engaging on a
// chord/legato, advancing one key per clock H->L edge [docs/design/05 §5.1; ADR-007
// C7, C8, C9, C10]. The U&D turnaround (whether the endpoints repeat) is a documented
// switchable choice, defaulting to the calibration (PI) value — NOT bit-exact
// [docs/design/05 §5.2; ADR-007 C11].
//
// This is a held-note bitmap (up to 32 keys), NOT 4-note polyphony [ADR-007 C8;
// research/06 §2.4]. No `juce::*` type appears here; mwcore is JUCE-free [ADR-001/D2].
// The hot path (advanceOnEdge + accessors) is noexcept, touches only the fixed
// bitmaps + integer cursor, and never allocates or locks [docs/design/05 §5.4, §10;
// ADR-007 C26].

#pragma once

#include <cstdint>

#include "ControlTypes.h"   // mw::control::ArpMode (§5.4 enum)
#include "../calibration/ArpConstants.h"   // mw::cal::arp::kArpUandDRepeatEndpoints (§5.2)

namespace mw::control {

class Arpeggiator {
public:
    // Clear the bitmaps and reset cursor/direction; no allocation after [§5.4].
    void prepare() noexcept;

    void setMode(ArpMode m) noexcept;
    void setHold(bool latched) noexcept;             // panel + pedal OR'd by caller [§5.1]
    void setUandDRepeatEndpoints(bool b) noexcept;   // §5.2 switchable choice

    // Held-key maintenance (driven by Keyboard Read). RT-safe [§5.4].
    void noteOn(int key) noexcept;                   // set bit
    void noteOff(int key) noexcept;                  // clear bit unless HOLD latched

    // Engaged when a chord/legato is held (>= 2 keys) or HOLD is latched [§5.1/C10].
    bool isEngaged() const noexcept;

    // Advance one step on a clock H->L edge; returns the key to sound, or -1 when no
    // keys are held [§5.4]. UP/DOWN choose direction; U&D oscillates per §5.2.
    int advanceOnEdge() noexcept;

    std::uint32_t heldBitmap() const noexcept { return held_; }
    int heldCount() const noexcept;                  // popcount(held_)

private:
    // Resolve the n-th (0-based) set bit of held_ to its key index, ascending.
    int keyAtOrderedIndex(int n) const noexcept;

    std::uint32_t held_ = 0;        // the SOUNDING set (retains latched keys under HOLD)
    std::uint32_t physical_ = 0;    // keys physically down right now
    ArpMode mode_ = ArpMode::Up;
    bool hold_ = false;
    bool repeatEndpoints_ = mw::cal::arp::kArpUandDRepeatEndpoints;   // §5.2 default
    int cursor_ = 0;                // index into the ordered held list
    int dir_ = +1;                  // for U&D oscillation
};

} // namespace mw::control
