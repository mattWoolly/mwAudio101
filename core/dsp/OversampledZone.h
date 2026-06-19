// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/OversampledZone.h — the per-voice oversampled-zone WRAPPER (task 047,
// core-filter-8).
//
// Single source of truth: ADR-004 Decision items 1-5, C8, C9, C11, C12 and
// docs/design/00 §4.1 (the per-voice 2x nonlinear zone) / §8.5 V15/V16 (the
// OS_CEILING clamp) / §9.1 RT-5/RT-6 (real-time invariants).
//
// WHAT THIS IS. The wrapper that bounds the per-voice nonlinear chain (IR3109 ladder
// + diode-clamp resonance + BA662 VCA drive) with EXACTLY ONE upsample and ONE
// downsample [ADR-004 C8, Contract row 8]. The caller supplies the nonlinear DSP as a
// callback that runs ON THE HIGH-RATE BUFFER between the up and the down; the wrapper
// never down-then-up between adjacent nonlinear stages — that is the caller's job to
// keep contiguous, and the wrapper makes it natural by exposing one high-rate buffer
// for the whole chain. The factor (1x eco / 2x default) is selected per voice
// [ADR-004 C9, C11]; 1x BYPASSES the resamplers entirely (the callback runs at base
// rate, bit-exact, no reconstruction error). Above OS_CEILING_HZ (192 kHz internal)
// the factor is CLAMPED to 1x and the clamp is recorded for provenance
// [docs/design/00 §8.5 V15/V16].
//
// WHAT THIS IS NOT. It is NOT the resampler kernel (that is dsp/Oversampler.h, task
// 036, which this composes), NOT the offline FIR (dsp/FirOversampler.h, task 037),
// NOT the filter/VCA/Drive DSP bodies (supplied as the callback), and NOT the quality
// enum / factor derivation (consumed from the param schema; ADR-018) — see the task's
// `## Out of scope`.
//
// Real-time contract [ADR-004 C15; docs/design/00 §9.1 RT-5/RT-6]:
//   * prepare(hostFsHz, maxBlockSize, factor) is the ONLY allocator. It sizes the
//     high-rate scratch to maxBlockSize * kMaxFactor and resolves the active factor
//     (applying the OS_CEILING clamp). It runs off the audio thread.
//   * process<Fn>() and reset() and setFactor() are noexcept, allocate no heap and
//     take no lock. The callback is a template parameter so it is INLINED — no
//     std::function, no type-erasure heap allocation on the audio thread.
//   * A factor change varies the active stride ONLY and never allocates.
//
// mwcore is JUCE-free [ADR-001].

#pragma once

#include <cstddef>
#include <vector>

#include "dsp/Oversampler.h"
#include "calibration/OversampledZoneConstants.h"

namespace mw::dsp {

class OversampledZone {
public:
    // Mirror the underlying realtime kernel's supported strides. 2x is the blessed
    // default; 1x is the eco / pass-through tier. A 4x HQ tier is out of scope here
    // and unsupported by the kernel (Oversampler::kMaxFactor == 2) [ADR-004 C10, C11].
    static constexpr int kFactor1x  = cal::oszone::kFactor1x;   // 1
    static constexpr int kFactor2x  = cal::oszone::kFactor2x;   // 2
    static constexpr int kMaxFactor = Oversampler::kMaxFactor;  // 2

    OversampledZone() noexcept = default;

    // ---- Off the audio thread: the ONLY allocator [ADR-004 C15; §9.1 RT-6] -------
    // hostFsHz is the host (base) sample rate; `factor` is the REQUESTED quality
    // stride (1x or 2x), typically derived from the Quality param by the caller (out
    // of scope here). Preallocates the high-rate scratch to maxBlockSize * kMaxFactor,
    // prepares the underlying resampler, and resolves the ACTIVE factor by clamping the
    // request to [1, kMaxFactor] and then to 1x when factor*hostFsHz would push the
    // internal rate strictly above OS_CEILING_HZ (recording the clamp for provenance)
    // [docs/design/00 §8.5 V15/V16]. Idempotent / re-callable on rate / block-size /
    // factor change.
    void prepare(double hostFsHz, int maxBlockSize, int factor);

    // ---- Audio thread: noexcept, no alloc, no lock -------------------------------
    // Clears the underlying resampler delay-line state to silence and zeroes the
    // per-block up/down counters; does NOT free or resize. [ADR-004 C15]
    void reset() noexcept;

    // Vary the ACTIVE oversampling factor (stride). No allocation; clamps to the
    // prepared maximum AND re-applies the OS_CEILING clamp resolved at prepare(). Safe
    // on the audio thread [ADR-004 C15, C9]. A request that the ceiling forces down is
    // recorded as a clamp.
    void setFactor(int factor) noexcept;

    // ---- Introspection (no audio-rate cost) --------------------------------------
    [[nodiscard]] int  factor() const noexcept { return activeFactor_; }
    [[nodiscard]] int  requestedFactor() const noexcept { return requestedFactor_; }
    [[nodiscard]] int  maxFactor() const noexcept { return os_.maxFactor(); }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double hostSampleRate() const noexcept { return hostFsHz_; }

    // True iff the active stride > 1x, i.e. the resamplers actually run. At 1x they are
    // bypassed and the callback runs at base rate [ADR-004 Contract row 8].
    [[nodiscard]] bool resamplersActive() const noexcept { return activeFactor_ > 1; }

    // Provenance flag: true iff a factor of 2x was REQUESTED but the OS_CEILING clamp
    // forced the active factor down to 1x (running unblessed at this host rate)
    // [docs/design/00 §8.5 V15/V16; ADR-023 V15/V16]. A 1x request is never "clamped".
    [[nodiscard]] bool clampedToEco() const noexcept { return clampedToEco_; }

    // Total up/down conversions since the last reset() (for the structural acceptance
    // check that exactly one up + one down bound the zone per voice block).
    [[nodiscard]] long upsampleCount() const noexcept { return upCount_; }
    [[nodiscard]] long downsampleCount() const noexcept { return downCount_; }
    // The up/down conversions performed by the MOST RECENT process() call (== 1 each
    // for a single voice block at any active factor, including 1x) [ADR-004 C8].
    [[nodiscard]] int lastBlockUpsamples() const noexcept { return lastUp_; }
    [[nodiscard]] int lastBlockDownsamples() const noexcept { return lastDown_; }

    // ---- The wrapped zone process: exactly one up + one down per voice block ------
    // `block` holds `numFrames` base-rate samples (in place input/output). `nonlinear`
    // is the caller-supplied nonlinear chain; it is invoked EXACTLY ONCE with the
    // contiguous HIGH-RATE buffer and its length (factor*numFrames). It must process
    // that buffer in place. The wrapper:
    //   1) upsamples `block` -> high-rate scratch (one up),     [2x only]
    //   2) runs `nonlinear(scratch, factor*numFrames)` once,
    //   3) downsamples the scratch -> `block` (one down).       [2x only]
    // At 1x the resamplers are bypassed: the callback runs directly on `block`
    // (numHi == numFrames) and the round trip is bit-exact. The callback is a template
    // parameter, so it is inlined — no heap, no std::function on the audio thread.
    // noexcept (the callback MUST itself be noexcept on the hot path) [ADR-004 C8, C15].
    template <typename Fn>
    void process(float* block, int numFrames, Fn&& nonlinear) noexcept {
        lastUp_ = 0;
        lastDown_ = 0;
        if (numFrames <= 0) return;

        if (activeFactor_ == 1) {
            // 1x: resamplers bypassed. One logical up + one logical down still bound
            // the zone conceptually, but they are identities — count them so the
            // "exactly one up + one down" contract holds uniformly across factors.
            ++lastUp_; ++upCount_;
            nonlinear(block, numFrames);
            ++lastDown_; ++downCount_;
            return;
        }

        // 2x: ONE upsample of the whole base block into the contiguous high-rate
        // scratch, ONE callback over the high-rate buffer, ONE downsample back.
        const int numHi = numFrames * activeFactor_;
        float* hi = scratch_.data();
        for (int n = 0; n < numFrames; ++n) {
            const float* up = os_.upsampleSample(block[n]);   // factor contiguous samples
            for (int k = 0; k < activeFactor_; ++k)
                hi[n * activeFactor_ + k] = up[k];
        }
        ++lastUp_; ++upCount_;

        nonlinear(hi, numHi);                                  // the full nonlinear chain

        for (int n = 0; n < numFrames; ++n)
            block[n] = os_.downsampleSample(hi + n * activeFactor_);
        ++lastDown_; ++downCount_;
    }

private:
    // Resolve the active factor from a requested factor under the OS_CEILING clamp,
    // updating clampedToEco_. clamps the request into [1, kMaxFactor] first.
    void resolveActiveFactor(int requested) noexcept;

    Oversampler        os_{};                // the realtime polyphase IIR halfband kernel
    std::vector<float> scratch_;             // high-rate scratch (maxBlockSize*kMaxFactor)

    double hostFsHz_       = 48000.0;
    int    maxBlockSize_   = 0;
    int    requestedFactor_ = cal::oszone::kDefaultFactor;
    int    activeFactor_    = 1;
    bool   clampedToEco_    = false;
    bool   prepared_        = false;

    long upCount_   = 0;     // cumulative ups since reset
    long downCount_ = 0;     // cumulative downs since reset
    int  lastUp_    = 0;     // ups in the most recent process() call
    int  lastDown_  = 0;     // downs in the most recent process() call
};

} // namespace mw::dsp
