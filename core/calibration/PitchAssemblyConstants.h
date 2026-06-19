// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/PitchAssemblyConstants.h — the VINTAGE 6-bit DAC pitch-assembly
// constant set (task 070).
//
// These are the cross-module constants for the ControlCore VINTAGE pitch pipeline
// (range bases, octave-transpose offsets, the 12-counts/octave law, and the
// D7:D6 4052 route bits). They live in the mw::cal namespace (a sibling of the
// central core/calibration/Calibration.h table) so no ControlCore call site inlines
// a literal [docs/design/00 §1.2 calibration policy; docs/design/06 §3.10]. The
// orchestrator wires this header into Calibration.h.
//
// NOTE ON PROVENANCE: unlike a typical (PI) entry, these values are NOT pragmatic
// inventions. They are *documented circuit behavior* — the range-base counts and the
// 12-counts/octave (1 V/octave) law come from the disassembly + service-manual Range
// Data table, and the D7:D6 route map comes from the 4052-mux model
// [docs/research/07-cpu-key-assigner.md §4.1, §4.2; docs/design/04-voice-and-control.md
// §7.2, §7.3; ADR-005 §Decision items 1 & 2, §Contract C1]. They are centralized here
// only for the same single-source-of-truth discipline, and are therefore tagged with a
// research trace rather than (PI).

#pragma once

#include <cstdint>

#include "Calibration.h"

namespace mw::cal::pitch {

// ---------------------------------------------------------------------------
// §7.3 — VINTAGE pitch-count law (integer DAC-count domain).
//
// Pitch CV is assembled as integer DAC counts; ranges land exactly 12 counts
// apart; 1 count == 1 semitone; 12 counts == 1 octave; volts are 1 V/octave so
// 12 counts == 1 V. counts -> volts conversion happens ONLY at the S/H boundary
// [docs/design/04 §7.3; ADR-005 §Decision item 2; research/07 §4.2].
// ---------------------------------------------------------------------------

inline constexpr int kCountsPerOctave  = 12;          // 12 DAC counts == 1 octave [§7.3]
inline constexpr int kCountsPerSemitone = 1;          // 1 count == 1 semitone [§7.3]
inline constexpr double kVoltsPerOctave = 1.0;        // 1 V/octave [§7.3; ADR-005]
inline constexpr double kVoltsPerCount  = kVoltsPerOctave / kCountsPerOctave; // 1/12 V

// ---------------------------------------------------------------------------
// §7.3 — Range base DAC counts (the 16'/8'/4'/2' switch).
//
// Bases spaced exactly 12 counts apart (== 1 V apart) [docs/design/04 §7.3 table;
// ADR-005 §Decision item 2; research/07 §4.2].
//   16' = 0x0C (12) = 1 V
//    8' = 0x18 (24) = 2 V
//    4' = 0x24 (36) = 3 V
//    2' = 0x30 (48) = 4 V
// ---------------------------------------------------------------------------

inline constexpr int kRangeBase16ft = 0x0C;  // 12 counts  -> 1 V [§7.3]
inline constexpr int kRangeBase8ft  = 0x18;  // 24 counts  -> 2 V [§7.3]
inline constexpr int kRangeBase4ft  = 0x24;  // 36 counts  -> 3 V [§7.3]
inline constexpr int kRangeBase2ft  = 0x30;  // 48 counts  -> 4 V [§7.3]

// ---------------------------------------------------------------------------
// §7.3 — Octave-transpose offsets (the down/mid/up switch).
//
// The octave switch adds 0x00 (down) / 0x0C (mid) / 0x18 (up) as a raw DAC value
// [docs/design/04 §7.3; research/07 §4.2]. Relative to the mid position those raw
// values are -12 / 0 / +12 semitone-counts, i.e. one octave each way. The
// semantic (mid-relative) offsets are the integer-count contributions
// assemblePitchCounts consumes; the raw additive values are documented here for
// trace fidelity.
// ---------------------------------------------------------------------------

// Raw additive DAC values for the octave switch [§7.3; research/07 §4.2].
inline constexpr int kOctaveRawDown = 0x00;  // down
inline constexpr int kOctaveRawMid  = 0x0C;  // mid (== one range step)
inline constexpr int kOctaveRawUp   = 0x18;  // up

// Semantic (mid-relative) octave offsets in DAC counts == semitones [§7.3].
inline constexpr int kOctaveOffsetDown = -kCountsPerOctave;  // -12
inline constexpr int kOctaveOffsetMid  = 0;                  //   0
inline constexpr int kOctaveOffsetUp   = +kCountsPerOctave;  // +12

// ---------------------------------------------------------------------------
// §7.2 — D7:D6 4052 route bits (single 6-bit DAC time-multiplexed via the 4052).
//
// The two route bits select the S/H destination [docs/design/04 §7.2 table;
// ADR-005 §Decision item 1, §Contract C1; research/07 §4.1]:
//   00 = CV OUT, 01 = VCO, 10 = RANDOM, 11 = parked / idle (bus parked between updates).
// ---------------------------------------------------------------------------

enum class DacRoute : std::uint8_t {
    CvOut  = 0b00,  // 00 = rear-panel CV OUT
    Vco    = 0b01,  // 01 = VCO pitch CV
    Random = 0b10,  // 10 = RANDOM voltage
    Parked = 0b11   // 11 = parked / idle (modeled bus-park state)
};

// The route bits live in D7:D6 of the 6-bit-DAC control byte [§7.2; research/07 §4.1].
inline constexpr int kRouteBitShift = 6;                 // route occupies bits 7..6
inline constexpr std::uint8_t kRouteBitMask = 0b11000000; // D7:D6 mask

} // namespace mw::cal::pitch
