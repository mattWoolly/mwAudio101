// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the VINTAGE 6-bit DAC pitch pipeline (task 070). Test-case
// names begin with "pitchcounts" so `-R pitchcounts` selects exactly this suite (the
// silent-pass rule). Each TEST_CASE maps to a 070 acceptance criterion and to the
// cited docs/design/04-voice-and-control.md §7.2/§7.3 and ADR-005 §Decision items 1/2.
//
// Coverage:
//   - ranges land exactly 12 counts apart; 16'/8'/4'/2' = 12/24/36/48 counts ==
//     1/2/3/4 V via countsToVolts (the documented Range Data oracle, §7.3 table)
//   - 1 count == 1 semitone; 12 counts == 1 octave == 1 V; octave offsets add
//     -12/0/+12; keyShift adds verbatim (§7.3)
//   - assemblePitchCounts / countsToVolts are pure static noexcept; integer-count
//     domain before the volt boundary (§7.3; ADR-005 item 1)
//   - D7:D6 route-bit constants match the §7.2 table (00/01/10/11)

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "calibration/PitchAssemblyConstants.h"
#include "control/ControlCore.h"

using mw::ControlCore;
using mw::DacRoute;
namespace pitch = mw::cal::pitch;

namespace {
// Tight tolerance for the 1 V/octave volt math; the conversion is a single
// count/12 division so this is effectively exact in float.
constexpr float kVoltTol = 1.0e-6f;
} // namespace

// --- Pure / static / noexcept contract (§7.3; ADR-005 item 1) ----------------

TEST_CASE("pitchcounts: assemble and countsToVolts are static noexcept functions", "[pitchcounts]") {
    // Both are static members callable without an instance (pure functions).
    STATIC_REQUIRE(noexcept(ControlCore::assemblePitchCounts(0, 0, 0, 0)));
    STATIC_REQUIRE(noexcept(ControlCore::countsToVolts(0)));

    // assemblePitchCounts works in the INTEGER count domain (return type is int).
    STATIC_REQUIRE(std::is_same_v<decltype(ControlCore::assemblePitchCounts(0, 0, 0, 0)), int>);
    // The count->volt boundary is the only float (return type is float).
    STATIC_REQUIRE(std::is_same_v<decltype(ControlCore::countsToVolts(0)), float>);

    // Callable as constant expressions of the pure functions (no hidden state).
    (void) ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, 0);
}

// --- Range bases: exactly 12 counts apart, 1/2/3/4 V (§7.3 table) ------------

TEST_CASE("pitchcounts: range bases are 12 counts apart (16/8/4/2 ft = 12/24/36/48)", "[pitchcounts]") {
    // The documented Range Data oracle: 16'/8'/4'/2' = 12/24/36/48 DAC counts.
    STATIC_REQUIRE(pitch::kRangeBase16ft == 12);
    STATIC_REQUIRE(pitch::kRangeBase8ft  == 24);
    STATIC_REQUIRE(pitch::kRangeBase4ft  == 36);
    STATIC_REQUIRE(pitch::kRangeBase2ft  == 48);
    // Hex spellings from the table (0x0C/0x18/0x24/0x30).
    STATIC_REQUIRE(pitch::kRangeBase16ft == 0x0C);
    STATIC_REQUIRE(pitch::kRangeBase8ft  == 0x18);
    STATIC_REQUIRE(pitch::kRangeBase4ft  == 0x24);
    STATIC_REQUIRE(pitch::kRangeBase2ft  == 0x30);

    // Adjacent ranges differ by exactly 12 counts (one octave) -- the spacing law.
    STATIC_REQUIRE(pitch::kRangeBase8ft - pitch::kRangeBase16ft == pitch::kCountsPerOctave);
    STATIC_REQUIRE(pitch::kRangeBase4ft - pitch::kRangeBase8ft  == pitch::kCountsPerOctave);
    STATIC_REQUIRE(pitch::kRangeBase2ft - pitch::kRangeBase4ft  == pitch::kCountsPerOctave);
}

TEST_CASE("pitchcounts: range bases convert to 1/2/3/4 V via countsToVolts (S/H oracle)", "[pitchcounts]") {
    // §7.3 table: 16'->1 V, 8'->2 V, 4'->3 V, 2'->4 V. The count domain holds the
    // bases as integers; only countsToVolts produces the volt oracle.
    REQUIRE(std::abs(ControlCore::countsToVolts(pitch::kRangeBase16ft) - 1.0f) < kVoltTol);
    REQUIRE(std::abs(ControlCore::countsToVolts(pitch::kRangeBase8ft)  - 2.0f) < kVoltTol);
    REQUIRE(std::abs(ControlCore::countsToVolts(pitch::kRangeBase4ft)  - 3.0f) < kVoltTol);
    REQUIRE(std::abs(ControlCore::countsToVolts(pitch::kRangeBase2ft)  - 4.0f) < kVoltTol);

    // Adjacent ranges are exactly 1 V apart (12 counts == 1 V).
    REQUIRE(std::abs((ControlCore::countsToVolts(pitch::kRangeBase8ft)
                      - ControlCore::countsToVolts(pitch::kRangeBase16ft)) - 1.0f) < kVoltTol);
    REQUIRE(std::abs((ControlCore::countsToVolts(pitch::kRangeBase2ft)
                      - ControlCore::countsToVolts(pitch::kRangeBase4ft)) - 1.0f) < kVoltTol);
}

// --- 1 count == 1 semitone, 12 counts == 1 octave == 1 V (§7.3) --------------

TEST_CASE("pitchcounts: 1 count is 1 semitone and 12 counts is 1 octave == 1 V", "[pitchcounts]") {
    STATIC_REQUIRE(pitch::kCountsPerSemitone == 1);
    STATIC_REQUIRE(pitch::kCountsPerOctave == 12);
    STATIC_REQUIRE(pitch::kVoltsPerOctave == 1.0);

    // One semitone (one count) is 1/12 V; twelve of them make exactly 1 V.
    const float oneCount = ControlCore::countsToVolts(1);
    REQUIRE(std::abs(oneCount - (1.0f / 12.0f)) < kVoltTol);

    const float oneOctave = ControlCore::countsToVolts(pitch::kCountsPerOctave);
    REQUIRE(std::abs(oneOctave - 1.0f) < kVoltTol);

    // 0 counts == 0 V; the conversion is linear (24 counts == 2 V, 48 == 4 V).
    REQUIRE(std::abs(ControlCore::countsToVolts(0) - 0.0f) < kVoltTol);
    REQUIRE(std::abs(ControlCore::countsToVolts(24) - 2.0f) < kVoltTol);
    REQUIRE(std::abs(ControlCore::countsToVolts(48) - 4.0f) < kVoltTol);

    // Each successive count adds exactly one semitone of volts (constant step).
    for (int c = 0; c < 60; ++c) {
        const float step = ControlCore::countsToVolts(c + 1) - ControlCore::countsToVolts(c);
        REQUIRE(std::abs(step - (1.0f / 12.0f)) < kVoltTol);
    }
}

// --- assemblePitchCounts: additive integer assembly (§7.3) -------------------

TEST_CASE("pitchcounts: assemble adds key + range + octave + keyShift as counts", "[pitchcounts]") {
    // Baseline: key only, no range/octave/shift.
    REQUIRE(ControlCore::assemblePitchCounts(60, 0, 0, 0) == 60);

    // Adding a range base shifts by exactly the base count.
    REQUIRE(ControlCore::assemblePitchCounts(60, pitch::kRangeBase16ft, 0, 0) == 60 + 12);
    REQUIRE(ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft,  0, 0) == 60 + 24);
    REQUIRE(ControlCore::assemblePitchCounts(60, pitch::kRangeBase4ft,  0, 0) == 60 + 36);
    REQUIRE(ControlCore::assemblePitchCounts(60, pitch::kRangeBase2ft,  0, 0) == 60 + 48);

    // The four terms add verbatim (no clamping, no rounding) -- pure integer sum.
    REQUIRE(ControlCore::assemblePitchCounts(40, 24, 12, 3) == 40 + 24 + 12 + 3);

    // Switching range up one position adds exactly 12 counts (== 1 octave).
    const int at8  = ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, 0, 0);
    const int at16 = ControlCore::assemblePitchCounts(60, pitch::kRangeBase16ft, 0, 0);
    REQUIRE(at8 - at16 == pitch::kCountsPerOctave);
}

// --- Octave offsets add -12/0/+12 (§7.3) -------------------------------------

TEST_CASE("pitchcounts: octave offsets add -12 / 0 / +12 counts", "[pitchcounts]") {
    // Semantic (mid-relative) octave offsets.
    STATIC_REQUIRE(pitch::kOctaveOffsetDown == -12);
    STATIC_REQUIRE(pitch::kOctaveOffsetMid  == 0);
    STATIC_REQUIRE(pitch::kOctaveOffsetUp   == +12);

    // Raw additive DAC values for the octave switch (0x00/0x0C/0x18).
    STATIC_REQUIRE(pitch::kOctaveRawDown == 0x00);
    STATIC_REQUIRE(pitch::kOctaveRawMid  == 0x0C);
    STATIC_REQUIRE(pitch::kOctaveRawUp   == 0x18);
    // The raw mid/up values are themselves 12 counts apart (down->mid->up).
    STATIC_REQUIRE(pitch::kOctaveRawMid - pitch::kOctaveRawDown == pitch::kCountsPerOctave);
    STATIC_REQUIRE(pitch::kOctaveRawUp  - pitch::kOctaveRawMid  == pitch::kCountsPerOctave);

    const int mid  = ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, 0);
    const int down = ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, pitch::kOctaveOffsetDown, 0);
    const int up   = ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, pitch::kOctaveOffsetUp, 0);

    REQUIRE(mid - down == 12);   // down is one octave below mid
    REQUIRE(up - mid  == 12);    // up is one octave above mid
    REQUIRE(up - down == 24);    // two octaves end to end

    // And those octave deltas are exactly 1 V / 2 V at the S/H boundary.
    REQUIRE(std::abs((ControlCore::countsToVolts(mid) - ControlCore::countsToVolts(down)) - 1.0f) < kVoltTol);
    REQUIRE(std::abs((ControlCore::countsToVolts(up)  - ControlCore::countsToVolts(down)) - 2.0f) < kVoltTol);
}

// --- keyShift adds verbatim (§7.3 KEY TRANSPOSE) -----------------------------

TEST_CASE("pitchcounts: keyShift is added verbatim (positive and negative)", "[pitchcounts]") {
    const int base = ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, 0);

    for (int shift : {-24, -7, -1, 0, 1, 5, 12, 24}) {
        const int shifted =
            ControlCore::assemblePitchCounts(60, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, shift);
        REQUIRE(shifted - base == shift);
    }
}

// --- Cross-check: a full assembly resolves to the right absolute volts --------

TEST_CASE("pitchcounts: full assembly maps counts to the expected volts at S/H", "[pitchcounts]") {
    // midiNote 0 + 8' base (24) + mid octave (0) + no shift => 24 counts => 2 V.
    const int counts0 = ControlCore::assemblePitchCounts(0, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, 0);
    REQUIRE(counts0 == 24);
    REQUIRE(std::abs(ControlCore::countsToVolts(counts0) - 2.0f) < kVoltTol);

    // Raise the octave by +12 counts => 36 counts => 3 V (one volt higher).
    const int counts1 = ControlCore::assemblePitchCounts(0, pitch::kRangeBase8ft, pitch::kOctaveOffsetUp, 0);
    REQUIRE(counts1 == 36);
    REQUIRE(std::abs(ControlCore::countsToVolts(counts1) - 3.0f) < kVoltTol);

    // A 12-count key span (one octave on the keyboard) is exactly 1 V.
    const int low  = ControlCore::assemblePitchCounts(36, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, 0);
    const int high = ControlCore::assemblePitchCounts(48, pitch::kRangeBase8ft, pitch::kOctaveOffsetMid, 0);
    REQUIRE(high - low == 12);
    REQUIRE(std::abs((ControlCore::countsToVolts(high) - ControlCore::countsToVolts(low)) - 1.0f) < kVoltTol);
}

// --- D7:D6 4052 route bits (§7.2 table 00/01/10/11) --------------------------

TEST_CASE("pitchcounts: D7 D6 route bits match the 4052 table 00/01/10/11", "[pitchcounts]") {
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<DacRoute>, std::uint8_t>);

    // 00 = CV OUT, 01 = VCO, 10 = RANDOM, 11 = parked / idle.
    STATIC_REQUIRE(static_cast<std::uint8_t>(DacRoute::CvOut)  == 0b00);
    STATIC_REQUIRE(static_cast<std::uint8_t>(DacRoute::Vco)    == 0b01);
    STATIC_REQUIRE(static_cast<std::uint8_t>(DacRoute::Random) == 0b10);
    STATIC_REQUIRE(static_cast<std::uint8_t>(DacRoute::Parked) == 0b11);

    // The route occupies D7:D6 of the control byte: shift 6, mask 0b11000000.
    STATIC_REQUIRE(pitch::kRouteBitShift == 6);
    STATIC_REQUIRE(pitch::kRouteBitMask == 0b11000000);

    // Placing a route into D7:D6 and reading it back is lossless for all four routes.
    for (auto r : {DacRoute::CvOut, DacRoute::Vco, DacRoute::Random, DacRoute::Parked}) {
        const std::uint8_t byte =
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(r) << pitch::kRouteBitShift);
        const std::uint8_t recovered =
            static_cast<std::uint8_t>((byte & pitch::kRouteBitMask) >> pitch::kRouteBitShift);
        REQUIRE(recovered == static_cast<std::uint8_t>(r));
    }

    // The parked idle state is the all-ones D7:D6 == 11xxxxxx form (§7.2).
    const std::uint8_t parkedByte =
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(DacRoute::Parked) << pitch::kRouteBitShift);
    REQUIRE((parkedByte & pitch::kRouteBitMask) == 0b11000000);
}
