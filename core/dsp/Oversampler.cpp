// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Oversampler.cpp — out-of-line prepare/reset for the realtime polyphase
// IIR halfband resampler (task 036). The kernels live inline in the header; only the
// off-audio-thread allocator (prepare) and the state-clear (reset) are defined here.
// See Oversampler.h for the design + ADR-004 / docs/design/00 §9.1 citations.

#include "Oversampler.h"

namespace mw::dsp {

void Oversampler::prepare(int maxBlockSize, int maxFactor) {
    // The ONLY place this module allocates [ADR-004 C15; docs/design/00 §9.1 RT-6].
    maxBlockSize_ = maxBlockSize < 0 ? 0 : maxBlockSize;

    int mf = maxFactor < 1 ? 1 : maxFactor;
    if (mf > kMaxFactor) mf = kMaxFactor;
    maxFactor_ = mf;

    // Worst-case interleaved high-rate scratch: one base block fully expanded by the
    // max factor. At least kMaxFactor entries so single-sample up/down always has room.
    std::size_t n = static_cast<std::size_t>(maxBlockSize_) * static_cast<std::size_t>(maxFactor_);
    if (n < static_cast<std::size_t>(kMaxFactor)) n = static_cast<std::size_t>(kMaxFactor);
    scratch_.assign(n, 0.0f);          // allocate + zero (off the audio thread)

    installCoeffs();
    reset();

    // Default active factor to the blessed 2x when supported, else 1x [ADR-004 C10].
    activeFactor_ = (maxFactor_ >= kFactor2x) ? kFactor2x : kFactor1x;
    prepared_ = true;
}

void Oversampler::reset() noexcept {
    // No allocation; clears delay-line state to a known (silent) start [ADR-004 C15].
    up0_.reset();
    up1_.reset();
    down0_.reset();
    down1_.reset();
    for (float& s : scratch_) s = 0.0f;
}

} // namespace mw::dsp
