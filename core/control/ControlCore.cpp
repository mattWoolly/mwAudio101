// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/ControlCore.cpp — VINTAGE integer DAC-count pitch pipeline (task 070).
//
// Implements the two pure static functions declared in ControlCore.h per
// docs/design/04-voice-and-control.md §7.3 and ADR-005 §Decision items 1 & 2. Counts
// are assembled and combined in the INTEGER domain; the only float appears at the
// counts -> volts S/H boundary [docs/design/04 §7.3; research/07 §4.2].

#include "ControlCore.h"

namespace mw {

int ControlCore::assemblePitchCounts(int midiNote, int rangeBase,
                                     int octaveOffset, int keyShift) noexcept {
    // Pure integer additive assembly — key + range base + octave + key-shift, all
    // in the DAC-count domain (1 count == 1 semitone). No volts here: the count
    // domain is preserved until the S/H boundary so portamento/glide stair-step
    // [docs/design/04 §7.3; ADR-005 §Decision item 1 (counts->volts at S/H only)].
    return midiNote + rangeBase + octaveOffset + keyShift;
}

float ControlCore::countsToVolts(int counts) noexcept {
    // 1 count == 1 semitone, 12 counts == 1 octave == 1 V (1 V/octave). The single
    // count->volt conversion, at the S/H boundary [docs/design/04 §7.3; ADR-005
    // §Decision item 2]. kVoltsPerCount == 1.0/12.0.
    return static_cast<float>(static_cast<double>(counts) * cal::pitch::kVoltsPerCount);
}

} // namespace mw
