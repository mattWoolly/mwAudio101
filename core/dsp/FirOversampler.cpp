// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/FirOversampler.cpp — out-of-line prepare/reset for the offline
// linear-phase FIR halfband resampler (task 037). The kernels live inline in the
// header; only the (off-audio-thread) allocator (prepare) and the state-clear
// (reset) are defined here. See FirOversampler.h for the design + ADR-004 /
// docs/design/00 §7, §9.1 citations.

#include "FirOversampler.h"

namespace mw::dsp {

void FirOversampler::prepare(int maxBlockSize) {
    // The ONLY place this module allocates [ADR-004 C15; docs/design/00 §9.1 RT-6].
    maxBlockSize_ = maxBlockSize < 0 ? 0 : maxBlockSize;

    // Worst-case interleaved high-rate scratch: one base block expanded by 2x. At
    // least kFactor entries so a single-sample upsample always has room.
    std::size_t n =
        static_cast<std::size_t>(maxBlockSize_) * static_cast<std::size_t>(kFactor);
    if (n < static_cast<std::size_t>(kFactor)) n = static_cast<std::size_t>(kFactor);
    scratch_.assign(n, 0.0f);   // allocate + zero (off the audio thread)

    reset();
    prepared_ = true;
}

void FirOversampler::reset() noexcept {
    // No allocation; clears the FIR history to a known (silent) start [ADR-004 C15].
    for (std::size_t i = 0; i < kNumTaps; ++i) {
        upHist_[i]   = 0.0;
        downHist_[i] = 0.0;
    }
    for (float& s : scratch_) s = 0.0f;
}

} // namespace mw::dsp
