// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/FxOversampler2x.cpp — out-of-line prepare/reset + the off-audio-thread
// group-delay measurement for the dedicated post-voice FX Drive 2x halfband pair
// (task 090). The up/down kernels live inline in the header; only the allocator
// (prepare), the state-clear (reset), and the one-time round-trip group-delay
// measurement are defined here. See FxOversampler2x.h for the design +
// ADR-017 / docs/design/07 / docs/design/00 §9.1 citations.

#include "FxOversampler2x.h"

#include <cmath>

namespace mw::fx {

int FxOversampler2x::measureRoundTripLatency() noexcept {
    // Off the audio thread only (called from prepare). Drives a unit impulse through
    // up->down on a CLEAN copy of the kernel state, computes the energy-weighted
    // centroid of the round-trip impulse response (a robust group-delay proxy for an
    // IIR), and rounds to the nearest base-rate sample. Deterministic and
    // input-independent: it depends solely on the frozen coefficients, so it yields
    // the same fixed integer every time for any block size [ADR-017 L2, L4, L5].
    //
    // We snapshot/restore the live state so the measurement leaves the resampler in
    // its post-reset (silent) condition, exactly as prepare() expects.
    const Branch su0 = up0_, su1 = up1_, sd0 = down0_, sd1 = down1_;

    up0_.reset();
    up1_.reset();
    down0_.reset();
    down1_.reset();

    constexpr int kImpLen = 2048;   // far longer than the IIR settling tail
    double num = 0.0, den = 0.0;
    for (int n = 0; n < kImpLen; ++n) {
        const float x = (n == 0) ? 1.0f : 0.0f;
        const float* hi = upsampleSample(x);
        const float y = downsampleSample(hi);
        const double e = static_cast<double>(y) * static_cast<double>(y);
        num += e * static_cast<double>(n);
        den += e;
    }

    // Restore the live state (the resampler is reset() again right after in prepare).
    up0_ = su0;
    up1_ = su1;
    down0_ = sd0;
    down1_ = sd1;

    if (den <= 0.0) return 0;
    const long rounded = std::lround(num / den);
    return (rounded < 0) ? 0 : static_cast<int>(rounded);
}

void FxOversampler2x::prepare(int maxBlockSize) {
    // The ONLY place this module allocates [ADR-017 L10; docs/design/00 §9.1 RT-6].
    maxBlockSize_ = maxBlockSize < 0 ? 0 : maxBlockSize;

    // Worst-case interleaved high-rate scratch: one base block fully expanded by 2x.
    // At least kFactor entries so a single-sample up/down always has room.
    std::size_t n = static_cast<std::size_t>(maxBlockSize_) * static_cast<std::size_t>(kFactor);
    if (n < static_cast<std::size_t>(kFactor)) n = static_cast<std::size_t>(kFactor);
    scratch_.assign(n, 0.0f);          // allocate + zero (off the audio thread)

    installCoeffs();
    reset();

    // Measure the fixed round-trip group delay ONCE here (never on the audio thread)
    // and freeze it for the instance lifetime [ADR-017 L2, L4, L10].
    latencySamples_ = measureRoundTripLatency();

    // Leave the kernel in a clean, silent state after the measurement.
    reset();

    prepared_ = true;
}

void FxOversampler2x::reset() noexcept {
    // No allocation; clears delay-line state to a known (silent) start [ADR-017 L10].
    up0_.reset();
    up1_.reset();
    down0_.reset();
    down1_.reset();
    for (float& s : scratch_) s = 0.0f;
}

} // namespace mw::fx
