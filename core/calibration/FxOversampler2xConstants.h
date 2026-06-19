// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/FxOversampler2xConstants.h — FROZEN polyphase IIR halfband
// coefficients + (PI) constants for the dedicated post-voice FX Drive 2x
// up/downsampler (task 090).
//
// This is a NEW per-module (PI) constants header. Per AGENTS.md every invented
// constant is tagged (PI) and centralized in a calibration header; to avoid
// serializing on the single shared Calibration.h while the development fleet runs
// in parallel, this module's (PI) constants live in their own header in a NEW
// mw::cal::fxos namespace. The orchestrator wires the include into Calibration.h
// later; this task does NOT edit the shared Calibration.h.
//
// SCOPE / placement [ADR-017 §A2, L2, L9; docs/design/07-fx-section.md §2.1, §4.1]:
// these coefficients are for the FX-rate (post-voice, once-on-the-mono-sum) Drive
// oversampler. It is a DISTINCT instance from the per-voice voice oversampler
// (core/dsp/Oversampler + core/calibration/OversamplerConstants.h, task 036). The
// two oversamplers are separate sources of constant group delay [ADR-017 L1 vs L2];
// keeping their (PI) tables in separate namespaces makes that separation mechanical.
//
// Design (a two-path elliptic polyphase IIR halfband):
//     H(z) = 1/2 [ A0(z) + z^-1 A1(z) ]
// where each branch A is a cascade of first-order allpass sections
//     y[n] = a*(x[n] - y[n-1]) + x[n-1].
// The branch coefficient values are the same proven, frozen elliptic halfband set
// the per-voice path uses (a 0.02*Fs transition half-band, measured stopband worst
// case about -96 dB, comfortably below the design bound). They are duplicated here
// (not re-#included from the voice namespace) so this FX module owns its own frozen
// set and a future independent re-tune of either path cannot silently move the
// other [ADR-017 L9; docs/design/00 §8.3]. Every value is (PI) — a pragmatic
// engineering invention with NO measured-SH-101 oracle (the FX Drive curve and its
// oversampler are outside the bit-exact voice contract [ADR-017 §A2; docs/design/07
// §1.2]).
//
// Full-precision doubles, no fast-math reassociation: identical on macOS arm64
// (reference) and Linux x64 [docs/design/00 §9.1 RT-7].

#pragma once

#include <array>
#include <cstddef>

namespace mw::cal::fxos {

// Number of first-order allpass sections per branch and total. (PI) — sized by the
// elliptic design to meet the stopband bound [docs/design/07-fx-section.md §4.1].
inline constexpr std::size_t kSectionsPerBranch = 5;
inline constexpr std::size_t kTotalSections     = 2 * kSectionsPerBranch;  // 10

// Branch 0 allpass coefficients (even-indexed sections). FROZEN (PI).
inline constexpr std::array<double, kSectionsPerBranch> kBranch0Coeffs = {
    0.038198144521241255,
    0.284326749234349030,
    0.577049051804713200,
    0.790200596391607400,
    0.923995927877366800,
};

// Branch 1 allpass coefficients (odd-indexed sections). FROZEN (PI).
inline constexpr std::array<double, kSectionsPerBranch> kBranch1Coeffs = {
    0.141848414466810540,
    0.436500581449427400,
    0.695524100512395000,
    0.864465799904681800,
    0.975286561376401700,
};

// Halfband stopband design bound (dB). Measured worst case is about -96 dB; the
// bound asserted by tests is the conservative lower edge of the aliasing-floor
// target. The anti-alias decimation on the FX-Drive downsample must beat this for
// a tone near the original Nyquist [ADR-017 L2; docs/research/10 §5]. (PI).
inline constexpr double kStopbandBoundDb = -90.0;

// Fixed reported group delay (in BASE-RATE samples) of the FX Drive up->down
// halfband round trip — the FX contribution to the host's constant PDC
// [ADR-017 L2, L4, L10]. The crossed-branch round trip of this 5-section-per-branch
// elliptic polyphase IIR halfband has a small, fixed, input-independent group delay;
// its energy-weighted impulse-response centroid (a robust IIR group-delay proxy)
// rounds to 10 base-rate samples. It is NONZERO (so the host actually compensates)
// and CONSTANT for the instance lifetime, independent of input and block size.
// (PI) — MEASURED from the round-trip impulse response of the frozen coefficients;
// FxOversampler2x::prepare() re-measures the same value deterministically and the
// acceptance test asserts measured == this declared constant (so this number cannot
// silently drift from the coefficients it is derived from). Any future coefficient
// re-tune that moves it ships a new renderVersion-keyed set [docs/design/00 §8.3].
inline constexpr int kReportedLatencySamples = 10;

// All allpass coefficients must lie strictly in (0, 1) for stable, well-behaved
// first-order allpass sections; assert it at compile time so a bad freeze cannot
// ship.
consteval bool mwFxosAllCoeffsInUnitInterval() {
    for (const double a : kBranch0Coeffs) {
        if (!(a > 0.0 && a < 1.0)) return false;
    }
    for (const double a : kBranch1Coeffs) {
        if (!(a > 0.0 && a < 1.0)) return false;
    }
    return true;
}
static_assert(mwFxosAllCoeffsInUnitInterval(),
              "FxOversampler2xConstants: every halfband allpass coefficient MUST be in "
              "(0,1) for section stability.");

static_assert(kReportedLatencySamples > 0,
              "FxOversampler2xConstants: reported latency MUST be nonzero so the host "
              "compensates the FX Drive oversampler group delay [ADR-017 L2].");

} // namespace mw::cal::fxos
