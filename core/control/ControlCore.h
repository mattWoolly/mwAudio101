// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ControlCore.h — the shared 6-bit-DAC/4052/S&H control object
// (docs/design/04-voice-and-control.md §7).
//
// TASK 070 SCOPE: this file currently declares ONLY the pure, static VINTAGE
// pitch pipeline — assemblePitchCounts (key + range base + octave + key-shift in the
// integer DAC-count domain) and countsToVolts (1 count == 1 semitone, 12 counts ==
// 1 octave == 1 V) — plus the D7:D6 4052 route model [docs/design/04 §7.2, §7.3;
// ADR-005 §Decision items 1 & 2]. These are the only members §7.8 marks `static`.
//
// EXPLICITLY OUT OF SCOPE (a sibling voice-control task owns these, §7.4-§7.7):
// the prepare/advance control-tick loop, the sample counter, loop-time jitter, the
// MODERN sub-block tick, per-feature auto-engage (effectivePole), the macro pole /
// crossfade, and the RANDOM-on-clock-edge regeneration. They are intentionally NOT
// declared here so this task stays atomic; the sibling task extends this class.
//
// No `juce::*` type appears here; mwcore is JUCE-free [ADR-001/D2].

#pragma once

#include "../calibration/PitchAssemblyConstants.h"

namespace mw {

// The control core's MODERN/VINTAGE macro pole selector [docs/design/04 §7.8;
// ADR-016 R-1 default MODERN]. Declared here (the type §7.8 attaches to ControlCore)
// so the VINTAGE pitch path has its named pole; the pole-driven control-tick
// behavior is implemented by the sibling control-tick task.
enum class VintageControlPole : std::uint8_t { Modern = 0, Vintage = 1 };

// Re-export of the D7:D6 4052 route, so call sites name `mw::DacRoute::Vco` rather
// than reaching into the calibration namespace [docs/design/04 §7.2].
using DacRoute = cal::pitch::DacRoute;

class ControlCore {
public:
    // -----------------------------------------------------------------------
    // §7.3 — VINTAGE integer DAC-count pitch assembly.
    //
    // Assembles the VCO pitch CV as INTEGER DAC counts:
    //     counts = midiNote + rangeBase + octaveOffset + keyShift
    // in the count domain (1 count == 1 semitone), so portamento and glide later
    // genuinely stair-step through 6-bit counts. Conversion to volts happens ONLY
    // at the S/H boundary, via countsToVolts [docs/design/04 §7.3; ADR-005
    // §Decision items 1 & 2; research/07 §4.2].
    //
    //   midiNote     : key number (semitones); origin supplied by the caller.
    //   rangeBase    : one of cal::pitch::kRangeBase{16,8,4,2}ft (12/24/36/48).
    //   octaveOffset : the mid-relative octave-switch contribution in counts,
    //                  one of cal::pitch::kOctaveOffset{Down,Mid,Up} (-12/0/+12).
    //   keyShift     : the stored KEY TRANSPOSE shift, added verbatim.
    //
    // Pure, static, noexcept, allocation-free, lock-free. Integer-only.
    // -----------------------------------------------------------------------
    static int assemblePitchCounts(int midiNote, int rangeBase,
                                   int octaveOffset, int keyShift) noexcept;

    // -----------------------------------------------------------------------
    // §7.3 — count -> volt conversion at the S/H boundary.
    //
    // 1 count == 1 semitone; 12 counts == 1 octave == 1 V (1 V/octave). So
    //     volts = counts / 12.
    // Pure, static, noexcept [docs/design/04 §7.3; ADR-005 §Decision item 2].
    // -----------------------------------------------------------------------
    static float countsToVolts(int counts) noexcept;
};

} // namespace mw
