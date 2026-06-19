// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/OversampledZone.cpp — the per-voice oversampled-zone wrapper (task 047).
// See OversampledZone.h and ADR-004 items 1-5, C8, C9, C11, C12 and docs/design/00
// §4.1 / §8.5 V15/V16 / §9.1 RT-5/RT-6.
//
// Only prepare() allocates (the high-rate scratch + the underlying resampler scratch),
// off the audio thread. reset(), setFactor() and the templated process() (header) are
// noexcept and allocation-free. The factor-resolution / OS_CEILING clamp logic lives
// here so the audio-thread setters stay branch-light. mwcore is JUCE-free.

#include "dsp/OversampledZone.h"

namespace mw::dsp {

void OversampledZone::resolveActiveFactor(int requested) noexcept {
    // Clamp the request into the kernel-supported range first.
    int req = requested < 1 ? 1 : requested;
    if (req > kMaxFactor) req = kMaxFactor;
    requestedFactor_ = req;

    // OS_CEILING clamp: if running `req`-times oversampling at the host rate would push
    // the internal rate STRICTLY above the ceiling (192 kHz internal), force the active
    // factor to 1x so the filter-stability guard continues to hold, and record the
    // clamp for provenance [docs/design/00 §8.5 V15/V16; ADR-023 V15/V16]. A 1x request
    // is already eco and is never "clamped".
    if (req > 1 && cal::oszone::wouldExceedCeiling(hostFsHz_, req)) {
        activeFactor_ = 1;
        clampedToEco_ = true;
    } else {
        activeFactor_ = req;
        clampedToEco_ = false;
    }

    // Drive the underlying kernel's active stride to match (no allocation).
    os_.setFactor(activeFactor_);
}

void OversampledZone::prepare(double hostFsHz, int maxBlockSize, int factor) {
    hostFsHz_     = hostFsHz;
    maxBlockSize_ = maxBlockSize < 0 ? 0 : maxBlockSize;

    // Size the high-rate scratch to the worst case (maxBlockSize * kMaxFactor) so a
    // later setFactor() up to the prepared max never needs to grow it [ADR-004 C15;
    // §9.1 RT-6]. This is the ONLY allocator.
    scratch_.assign(static_cast<std::size_t>(maxBlockSize_) * static_cast<std::size_t>(kMaxFactor),
                    0.0f);

    // Prepare the underlying realtime resampler kernel to the same worst case.
    os_.prepare(maxBlockSize_, kMaxFactor);

    // Resolve the active factor under the OS_CEILING clamp now that hostFsHz_ is known.
    resolveActiveFactor(factor);

    prepared_ = true;
    reset();
}

void OversampledZone::reset() noexcept {
    os_.reset();
    upCount_   = 0;
    downCount_ = 0;
    lastUp_    = 0;
    lastDown_  = 0;
}

void OversampledZone::setFactor(int factor) noexcept {
    // Re-resolve under the same OS_CEILING clamp; stride-only, no allocation
    // [ADR-004 C15, C9].
    resolveActiveFactor(factor);
}

} // namespace mw::dsp
