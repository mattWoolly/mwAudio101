// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/OversamplerFirConstants.h — FROZEN linear-phase FIR halfband
// prototype taps for the OFFLINE/RENDER up/downsampler (task 037, core-filter-7).
//
// This is a NEW per-module (PI) constants header. It #includes the shared
// core/calibration/Calibration.h and EXTENDS the mw::cal namespace with a new
// sub-namespace (mw::cal::osfir); it does NOT edit Calibration.h (parallel-fleet
// conflict-avoidance). The orchestrator wires any cross-module aggregation later.
//
// Every value here is (PI) — a pragmatic engineering invention with NO measured
// SH-101 oracle [docs/design/00 §2.3; AGENTS.md]. These are the FROZEN, versioned
// coefficients for renderVersion 1; they MUST NOT be edited in place — a re-tune
// ships a NEW renderVersion-keyed set [ADR-004 C14; docs/design/00 §8.3].
//
// Design (ADR-004 Decision item 4, C7): a LINEAR-PHASE (symmetric) FIR halfband
// reserved for the offline/bounce render tier and for any future in-zone summing of
// correlated sources, per the §5.2 phase-linearity caveat. It is intent-equivalent
// to the realtime polyphase IIR halfband (OversamplerConstants.h) — same 2x ratio —
// but phase-LINEAR, and therefore audibly phase-divergent from the realtime path
// (documented, acceptable for bounce) [ADR-004 C7, C16].
//
// Prototype: a windowed-sinc (Kaiser, ~100 dB design attenuation) halfband lowpass
// at the high-rate quarter-band (cutoff = pi/2), with the exact halfband zero
// pattern forced (center tap = 1/2; every other even-offset tap is identically 0)
// and DC gain renormalized to unity. Length N = 129 is chosen so that:
//   * N is odd  -> exact linear phase (Type-I symmetric FIR);
//   * N mod 4 == 1  -> the high-rate group delay (N-1)/2 = 64 is EVEN, so the up+down
//     round-trip group delay (N-1) high-rate = (N-1)/2 = 64 BASE-rate samples is an
//     exact integer the host can compensate (C14);
//   * measured stopband worst case is about -106 dB (>= the -90 dB design bound and
//     inside ADR-004 C12's -90..-100 dBFS aliasing-floor target).
//
// Frozen, full-precision doubles (no fast-math reassociation, identical on macOS
// arm64 and Linux x64 — that is the whole point of pinning them here) [ADR-004 C14;
// docs/design/00 §9.1 RT-7].

#pragma once

#include <array>
#include <cstddef>

#include "Calibration.h"

namespace mw::cal::osfir {

// Halfband prototype length. (PI) — odd (Type-I linear phase) and == 1 (mod 4) so the
// integer base-rate latency property holds [ADR-004 C7, C14].
inline constexpr std::size_t kNumTaps = 129;

// High-rate (2x) constant group delay = (N-1)/2 samples. The prototype is symmetric,
// so this is exact and frequency-independent (true linear phase) [ADR-004 C7, C14].
inline constexpr std::size_t kGroupDelayHi = (kNumTaps - 1) / 2;  // 64

// Up + down round-trip group delay expressed in BASE-rate samples. An upsample adds
// kGroupDelayHi high-rate samples and a downsample adds another kGroupDelayHi; the
// sum (N-1) high-rate samples decimates to (N-1)/2 base-rate samples. This is the
// value callers declare to the host via setLatencySamples (C14).
inline constexpr std::size_t kRoundTripLatencyBase = (kNumTaps - 1) / 2;  // 64

// Halfband stopband design bound (dB). Measured worst case is about -106 dB; the
// bound asserted by tests is the conservative lower edge of ADR-004 C12's
// -90..-100 dBFS aliasing-floor target. (PI).
inline constexpr double kStopbandBoundDb = -90.0;

// FROZEN, versioned linear-phase halfband prototype taps (renderVersion 1). Symmetric
// about the center tap; every other even-offset tap is identically 0 (halfband). (PI).
inline constexpr std::array<double, kNumTaps> kProtoTaps = {
    +0.00000000000000000e+00,
    -3.30609324349214196e-06,
    +0.00000000000000000e+00,
    +8.84701454428689555e-06,
    +0.00000000000000000e+00,
    -1.87234070194581732e-05,
    +0.00000000000000000e+00,
    +3.49557280686834805e-05,
    +0.00000000000000000e+00,
    -6.01368537516046314e-05,
    +0.00000000000000000e+00,
    +9.75124862597759995e-05,
    +0.00000000000000000e+00,
    -1.51059255861117224e-04,
    +0.00000000000000000e+00,
    +2.25559242681406749e-04,
    +0.00000000000000000e+00,
    -3.26670598323537296e-04,
    +0.00000000000000000e+00,
    +4.60995402453985624e-04,
    +0.00000000000000000e+00,
    -6.36147980000682811e-04,
    +0.00000000000000000e+00,
    +8.60829827174288938e-04,
    +0.00000000000000000e+00,
    -1.14492135322649918e-03,
    +0.00000000000000000e+00,
    +1.49960632967971225e-03,
    +0.00000000000000000e+00,
    -1.93755306256997986e-03,
    +0.00000000000000000e+00,
    +2.47318824615109113e-03,
    +0.00000000000000000e+00,
    -3.12311761382875032e-03,
    +0.00000000000000000e+00,
    +3.90677613174513112e-03,
    +0.00000000000000000e+00,
    -4.84743736330403227e-03,
    +0.00000000000000000e+00,
    +5.97379145626576995e-03,
    +0.00000000000000000e+00,
    -7.32244276517095023e-03,
    +0.00000000000000000e+00,
    +8.94194044599288043e-03,
    +0.00000000000000000e+00,
    -1.08994658691486893e-02,
    +0.00000000000000000e+00,
    +1.32923517507618754e-02,
    +0.00000000000000000e+00,
    -1.62689206708229832e-02,
    +0.00000000000000000e+00,
    +2.00686467307445299e-02,
    +0.00000000000000000e+00,
    -2.51061882485711547e-02,
    +0.00000000000000000e+00,
    +3.21674745051794414e-02,
    +0.00000000000000000e+00,
    -4.29418732090346561e-02,
    +0.00000000000000000e+00,
    +6.18313521004327030e-02,
    +0.00000000000000000e+00,
    -1.04995765386059800e-01,
    +0.00000000000000000e+00,
    +3.17939552285941784e-01,
    +5.00000700091720041e-01,
    +3.17939552285941784e-01,
    +0.00000000000000000e+00,
    -1.04995765386059800e-01,
    +0.00000000000000000e+00,
    +6.18313521004327030e-02,
    +0.00000000000000000e+00,
    -4.29418732090346561e-02,
    +0.00000000000000000e+00,
    +3.21674745051794414e-02,
    +0.00000000000000000e+00,
    -2.51061882485711547e-02,
    +0.00000000000000000e+00,
    +2.00686467307445299e-02,
    +0.00000000000000000e+00,
    -1.62689206708229832e-02,
    +0.00000000000000000e+00,
    +1.32923517507618754e-02,
    +0.00000000000000000e+00,
    -1.08994658691486893e-02,
    +0.00000000000000000e+00,
    +8.94194044599288043e-03,
    +0.00000000000000000e+00,
    -7.32244276517095023e-03,
    +0.00000000000000000e+00,
    +5.97379145626576995e-03,
    +0.00000000000000000e+00,
    -4.84743736330403227e-03,
    +0.00000000000000000e+00,
    +3.90677613174513112e-03,
    +0.00000000000000000e+00,
    -3.12311761382875032e-03,
    +0.00000000000000000e+00,
    +2.47318824615109113e-03,
    +0.00000000000000000e+00,
    -1.93755306256997986e-03,
    +0.00000000000000000e+00,
    +1.49960632967971225e-03,
    +0.00000000000000000e+00,
    -1.14492135322649918e-03,
    +0.00000000000000000e+00,
    +8.60829827174288938e-04,
    +0.00000000000000000e+00,
    -6.36147980000682811e-04,
    +0.00000000000000000e+00,
    +4.60995402453985624e-04,
    +0.00000000000000000e+00,
    -3.26670598323537296e-04,
    +0.00000000000000000e+00,
    +2.25559242681406749e-04,
    +0.00000000000000000e+00,
    -1.51059255861117224e-04,
    +0.00000000000000000e+00,
    +9.75124862597759995e-05,
    +0.00000000000000000e+00,
    -6.01368537516046314e-05,
    +0.00000000000000000e+00,
    +3.49557280686834805e-05,
    +0.00000000000000000e+00,
    -1.87234070194581732e-05,
    +0.00000000000000000e+00,
    +8.84701454428689555e-06,
    +0.00000000000000000e+00,
    -3.30609324349214196e-06,
    +0.00000000000000000e+00,
};

// ---- Compile-time freeze guards: a bad freeze cannot ship [ADR-004 C14] ----------

// The prototype MUST be exactly symmetric (true linear phase, Type-I FIR).
consteval bool mwTapsAreSymmetric() {
    for (std::size_t n = 0; n < kNumTaps / 2; ++n) {
        if (kProtoTaps[n] != kProtoTaps[kNumTaps - 1 - n]) return false;
    }
    return true;
}
static_assert(mwTapsAreSymmetric(),
              "OversamplerFirConstants: prototype taps MUST be symmetric for linear "
              "phase [ADR-004 C7, C14].");

// Halfband structural zeros: every tap an even offset (!= 0) from the center is 0.
consteval bool mwHalfbandZeros() {
    const std::size_t c = kGroupDelayHi;  // center index
    for (std::size_t n = 0; n < kNumTaps; ++n) {
        const std::ptrdiff_t m =
            static_cast<std::ptrdiff_t>(n) - static_cast<std::ptrdiff_t>(c);
        if (m != 0 && (m % 2 == 0) && kProtoTaps[n] != 0.0) return false;
    }
    return true;
}
static_assert(mwHalfbandZeros(),
              "OversamplerFirConstants: every off-center even-offset tap MUST be 0 "
              "(halfband structure) [ADR-004 C7, C14].");

// N is odd and == 1 (mod 4): the integer-base-rate-latency precondition for C14.
static_assert(kNumTaps % 2 == 1 && kNumTaps % 4 == 1,
              "OversamplerFirConstants: kNumTaps must be odd and == 1 (mod 4) so the "
              "round-trip group delay is an integer number of base-rate samples "
              "[ADR-004 C14].");

} // namespace mw::cal::osfir
