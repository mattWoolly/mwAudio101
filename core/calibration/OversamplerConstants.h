// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/OversamplerConstants.h — FROZEN polyphase IIR halfband
// coefficients for the realtime up/downsampler (task 036).
//
// This is a NEW per-module (PI) constants header (the orchestrator wires its
// include into core/calibration/Calibration.h later; this task does NOT edit the
// shared Calibration.h). Every value here is (PI) — a pragmatic engineering
// invention with NO measured-SH-101 oracle [docs/design/00 §2.3; AGENTS.md]. The
// numbers are the FROZEN, versioned coefficient set for renderVersion 1; they MUST
// NOT be edited in place — a re-tune ships a new renderVersion-keyed set
// [ADR-004 C10, C14; docs/design/00 §8.3].
//
// Design (ADR-004 Decision item 4, C4): an elliptic polyphase IIR halfband built
// from two first-order-allpass branches —
//     H(z) = 1/2 [ A0(z) + z^-1 A1(z) ]
// where each branch A is a cascade of first-order allpass sections
//     y[n] = a*(x[n] - y[n-1]) + x[n-1].
// The coefficient set below was produced by the Valenzuela-Constantinides /
// de Soras elliptic halfband design with 10 sections and a 0.02*Fs transition
// half-band; its measured stopband worst case is about -96 dB (>= the -90 dB design
// bound and inside ADR-004 C12's -90..-100 dBFS aliasing-floor target). Branch 0
// takes the even-indexed sections, branch 1 the odd-indexed sections.
//
// Frozen, full-precision doubles (no fast-math reassociation, identical on macOS
// arm64 and Linux x64 — that is the whole point of pinning them here) [ADR-004 C14;
// docs/design/00 §9.1 RT-7].

#pragma once

#include <array>
#include <cstddef>

namespace mw::cal::osiir {

// Number of first-order allpass sections per branch and total. (PI) — sized by the
// elliptic design to meet the stopband bound, not a textbook order [ADR-004 C4, §5].
inline constexpr std::size_t kSectionsPerBranch = 5;
inline constexpr std::size_t kTotalSections     = 2 * kSectionsPerBranch;  // 10

// Branch 0 allpass coefficients (even-indexed sections). FROZEN (PI) [ADR-004 C14].
inline constexpr std::array<double, kSectionsPerBranch> kBranch0Coeffs = {
    0.038198144521241255,
    0.284326749234349030,
    0.577049051804713200,
    0.790200596391607400,
    0.923995927877366800,
};

// Branch 1 allpass coefficients (odd-indexed sections). FROZEN (PI) [ADR-004 C14].
inline constexpr std::array<double, kSectionsPerBranch> kBranch1Coeffs = {
    0.141848414466810540,
    0.436500581449427400,
    0.695524100512395000,
    0.864465799904681800,
    0.975286561376401700,
};

// Halfband stopband design bound (dB). Measured worst case is about -96 dB; the
// bound asserted by tests is the conservative lower edge of ADR-004 C12's
// -90..-100 dBFS aliasing-floor target. (PI).
inline constexpr double kStopbandBoundDb = -90.0;

// All allpass coefficients must lie strictly in (0, 1) for stable, well-behaved
// first-order allpass sections; assert it at compile time so a bad freeze cannot
// ship [ADR-004 C14].
consteval bool mwAllCoeffsInUnitInterval() {
    for (const double a : kBranch0Coeffs) {
        if (!(a > 0.0 && a < 1.0)) return false;
    }
    for (const double a : kBranch1Coeffs) {
        if (!(a > 0.0 && a < 1.0)) return false;
    }
    return true;
}
static_assert(mwAllCoeffsInUnitInterval(),
              "OversamplerConstants: every halfband allpass coefficient MUST be in (0,1) "
              "for section stability [ADR-004 C14].");

} // namespace mw::cal::osiir
