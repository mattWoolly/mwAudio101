// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/FractionalDelayLine.h — shared header-only ring buffer with an
// interpolated fractional read (task 089).
//
// Realizes docs/design/07-fx-section.md §5.3 (signature, interpolation contract)
// and §3.2 / ADR-010 FX-10 (real-time invariants: preallocate once in prepare();
// write/read/processBlock perform no heap allocation and take no locks; noexcept
// hot paths). Used by Chorus (two modulated lines), Delay (one core line), and the
// FxChain dry-pad alignment line (§6.3). This file carries no JUCE types [ADR-001].
//
// Indexing model: write(x) pushes one sample and advances the write head. read(d)
// returns the sample d samples in the past — read(0.0f) is the most recently
// written sample, read(D) for integer D is the sample written D writes earlier.
// Interpolation is at least linear; the (PI) interpolation-order constant lives in
// core/calibration/FractionalDelayLineConstants.h [§5.3].

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "../../calibration/FractionalDelayLineConstants.h"

namespace mw::fx {

class FractionalDelayLine {
public:
    FractionalDelayLine() noexcept = default;

    // Allocate the ring buffer once, sized to hold at least maxLengthSamples of
    // history. Called off the audio thread; this is the ONLY allocation site
    // [§3.2; ADR-010 FX-10]. The buffer is rounded up to a power of two so the
    // read/write wrap is a branch-free mask, never a modulo on the hot path.
    //
    // fixedIntegerDelay configures the processBlock() dry-pad tap (§6.3, the
    // FxChain alignment delay). It defaults to 0 so the §5.3 single-argument form
    // prepare(maxLengthSamples) is preserved for the Chorus/Delay fractional users.
    void prepare(int maxLengthSamples, int fixedIntegerDelay = 0) noexcept {
        // Need room for the largest tap we will read: the fractional users index up
        // to maxLengthSamples (+1 for the interpolation bracket), and the dry pad
        // indexes fixedIntegerDelay. Size to the max, then round up to a power of 2.
        int need = maxLengthSamples;
        if (fixedIntegerDelay > need) need = fixedIntegerDelay;
        if (need < 1) need = 1;
        // +1 so read(maxLengthSamples) still has a valid bracketing tap at +1.
        const std::size_t minSize = static_cast<std::size_t>(need) + 1u;

        std::size_t size = 1u;
        while (size < minSize) size <<= 1u;

        buf_.assign(size, 0.0f);     // allocate + zero, once
        mask_ = size - 1u;
        writeIndex_ = 0;
        fixedDelay_ = (fixedIntegerDelay < 0) ? 0 : fixedIntegerDelay;
    }

    // Clear all stored samples without reallocating (no alloc) [§3.2].
    void reset() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        writeIndex_ = 0;
    }

    // Push one sample; advance the write head. noexcept, no alloc.
    void write(float x) noexcept {
        buf_[writeIndex_] = x;
        writeIndex_ = static_cast<int>((static_cast<std::size_t>(writeIndex_) + 1u) & mask_);
    }

    // Interpolated read of the sample delaySamples in the past. delaySamples == 0
    // returns the most recently written sample. At-least-linear interpolation
    // between the two bracketing integer taps [§5.3]. noexcept, no alloc.
    [[nodiscard]] float read(float delaySamples) const noexcept {
        if (delaySamples < 0.0f) delaySamples = 0.0f;

        const float fIntDelay = std::floor(delaySamples);
        const float frac = delaySamples - fIntDelay;
        const int   intDelay = static_cast<int>(fIntDelay);

        const float a = tapAt(intDelay);       // sample intDelay in the past
        if (frac == 0.0f || mw::cal::fracdelay::kInterpolationOrder < 1)
            return a;
        const float b = tapAt(intDelay + 1);   // one sample further back
        // Linear interpolation: a is the nearer (smaller-delay) tap, b the further.
        return a + frac * (b - a);
    }

    // Integer-delay block helper for the FxChain dry-pad alignment line (§6.3):
    // delays the input block in-place by the fixed integer delay configured in
    // prepare(). A zero delay is a pass-through (identity). noexcept, no alloc.
    void processBlock(float* inOut, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            write(inOut[i]);
            inOut[i] = tapAt(fixedDelay_);
        }
    }

private:
    // Read the integer tap `delay` samples in the past (delay 0 == most recent
    // write). Wrap with the power-of-two mask — no modulo, no branch on the wrap.
    [[nodiscard]] float tapAt(int delay) const noexcept {
        // The most recent sample sits at writeIndex_ - 1 (writeIndex_ points at the
        // NEXT slot after a write). Subtract one more per sample of delay.
        const std::size_t idx =
            (static_cast<std::size_t>(writeIndex_) + buf_.size() - 1u
             - static_cast<std::size_t>(delay)) & mask_;
        return buf_[idx];
    }

    std::vector<float> buf_;          // sized once in prepare; never reallocated
    std::size_t        mask_ = 0u;    // power-of-two wrap mask (size - 1)
    int                writeIndex_ = 0;
    int                fixedDelay_ = 0; // integer dry-pad tap for processBlock (§6.3)
};

} // namespace mw::fx
