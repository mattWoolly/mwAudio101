// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/FirOversampler.h — OFFLINE linear-phase FIR halfband up/downsampler for
// the bounce/render tier of the per-voice 2x nonlinear zone (task 037,
// core-filter-7).
//
// Realizes ADR-004 Decision item 4 + C7, C14, C16 and docs/design/00 §7 (PDC),
// §9.1 RT-5/RT-6/RT-7. This is the OFFLINE/RENDER resampler: a LINEAR-PHASE
// (symmetric-tap) FIR halfband, intent-equivalent to the realtime polyphase IIR
// halfband (dsp/Oversampler.h) — SAME 2x ratio — but phase-LINEAR. Because it is
// phase-linear it is audibly phase-divergent from the realtime IIR path; that
// divergence is EXPECTED and acceptable for offline bounce and is documented here
// and in OversamplerFirConstants.h [ADR-004 C7, C16].
//
// It is NOT the zone wrapper (core-filter-8) and does NOT call the host's
// setLatencySamples itself — it only EXPOSES firLatencySamples() so the
// plugin-processor (a separate task) can declare the latency. The render path's
// fixed group delay MUST be reported so host PDC stays correct; it is never
// introduced silently [ADR-004 C14; docs/design/00 §7.2].
//
// Structure — a Type-I (odd-length, symmetric) linear-phase halfband prototype with
// the exact halfband zero pattern (center tap = 1/2; all other even-offset taps = 0),
// frozen in core/calibration/OversamplerFirConstants.h:
//
//   * Upsample (1 -> 2): the base input is zero-stuffed (each sample followed by a
//     zero) into a high-rate stream, convolved with the prototype, and emitted with
//     a x2 interpolation gain so the passband is unity. Two high-rate samples are
//     produced per base input.
//   * Downsample (2 -> 1): the high-rate stream is convolved with the SAME prototype
//     (anti-alias) and decimated by 2. The output is aligned to the EVEN high-rate
//     phase so the up->down round trip group delay is an EXACT INTEGER number of
//     base-rate samples (see below) — never a half-sample the host cannot compensate.
//
// Each direction has a constant group delay of (N-1)/2 high-rate samples; the
// cascade is two symmetric FIRs in series, so the up->down round trip is itself
// linear-phase with group delay 2*(N-1)/2 = (N-1) high-rate samples = (N-1)/2
// base-rate samples = cal::osfir::kRoundTripLatencyBase. With even-phase decimation
// this lands on an exact integer base-rate sample (verified by an impulse-peak test),
// so the host can compensate it precisely [ADR-004 C14].
//
// Real-time / determinism contract [ADR-004 C15; docs/design/00 §9.1 RT-5/6/7]:
//   * prepare(maxBlockSize) is the ONLY allocator; it preallocates the high-rate
//     scratch. The kernels and reset() are noexcept, allocate nothing and lock
//     nothing — the render tier still respects the no-alloc-in-loop discipline so it
//     shares the same kernels as any future streaming use, and a long-decay tail
//     feeding the FIR history is anti-denormal-flushed.
//   * Frozen taps + a fixed evaluation order => bit-identical output run-to-run and
//     across macOS arm64 / Linux x64; no fast-math reassociation [ADR-004 C14].
//
// mwcore is JUCE-free.

#pragma once

#include <cstddef>
#include <vector>

#include "../calibration/OversamplerFirConstants.h"

namespace mw::dsp {

class FirOversampler {
public:
    static constexpr int         kFactor  = 2;  // offline render tier is fixed 2x.
    static constexpr std::size_t kNumTaps = cal::osfir::kNumTaps;

    FirOversampler() noexcept = default;

    // ---- Off the audio thread: the ONLY allocator [ADR-004 C15; §9.1 RT-6] -------
    // Preallocates the interleaved high-rate scratch (maxBlockSize * 2) and zeroes
    // all FIR history. Idempotent / re-callable on block-size change.
    void prepare(int maxBlockSize);

    // ---- noexcept, no alloc, no lock ---------------------------------------------
    // Clears all FIR delay-line history to silence; does NOT free or resize.
    void reset() noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // The CONSTANT group delay (in base-rate samples) the up->down round trip adds.
    // A caller (plugin-processor) declares this to the host via setLatencySamples;
    // it is exact and frequency-independent because the prototype is symmetric
    // [ADR-004 C14; docs/design/00 §7.2]. Never introduce this latency silently.
    [[nodiscard]] static constexpr int firLatencySamples() noexcept {
        return static_cast<int>(cal::osfir::kRoundTripLatencyBase);
    }

    // One-direction group delay in HIGH-rate (2x) samples. Exposed for callers that
    // build their own up/down topology and need each leg's delay separately.
    [[nodiscard]] static constexpr int firGroupDelayHiSamples() noexcept {
        return static_cast<int>(cal::osfir::kGroupDelayHi);
    }

    // ---- Streaming single-sample kernels -----------------------------------------
    // Upsample one base-rate sample into two contiguous high-rate samples written to
    // outHi[0..1]. noexcept, no alloc. Unity passband gain.
    void upsampleSample(float x, float* outHi) noexcept {
        // Zero-stuff: the base sample, then an implicit zero, are pushed through the
        // high-rate FIR. The x2 interpolation gain restores unity passband.
        pushHi(static_cast<double>(x));
        outHi[0] = static_cast<float>(2.0 * convolveUp());
        pushHi(0.0);
        outHi[1] = static_cast<float>(2.0 * convolveUp());
    }

    // Downsample two contiguous high-rate samples (hi[0]=even phase, hi[1]=odd phase)
    // to one base-rate output. noexcept, no alloc. The output is taken on the EVEN
    // phase (after hi[0]) so the round-trip latency is an exact integer [ADR-004 C14].
    [[nodiscard]] float downsampleSample(const float* hi) noexcept {
        pushDown(static_cast<double>(hi[0]));
        const double y = convolveDown();   // even-phase output
        pushDown(static_cast<double>(hi[1]));
        return static_cast<float>(y);
    }

    // ---- Block kernels (the typical offline-render entry points) -----------------
    // Interpolate numFrames base-rate samples in `in` to 2*numFrames high-rate
    // samples in `outHi`. outHi must hold >= 2*numFrames. noexcept, no alloc.
    void upsampleBlock(const float* in, int numFrames, float* outHi) noexcept {
        for (int n = 0; n < numFrames; ++n)
            upsampleSample(in[n], outHi + 2 * n);
    }

    // Decimate 2*numFrames high-rate samples in `hi` to numFrames base-rate samples
    // in `out`. noexcept, no alloc.
    void downsampleBlock(const float* hi, int numFrames, float* out) noexcept {
        for (int n = 0; n < numFrames; ++n)
            out[n] = downsampleSample(hi + 2 * n);
    }

private:
    // Anti-denormal flush so long-decay tails feeding the FIR history never enter a
    // denormal CPU stall [docs/design/00 §9.1 RT-5]. Branch-light; leaves normal
    // values bit-identical across platforms.
    static double flushDenormal(double v) noexcept {
        return (v < 1.0e-30 && v > -1.0e-30) ? 0.0 : v;
    }

    // High-rate history shared by the upsampler (newest at index 0).
    void pushHi(double x) noexcept {
        for (std::size_t i = kNumTaps - 1; i > 0; --i) upHist_[i] = upHist_[i - 1];
        upHist_[0] = flushDenormal(x);
    }
    [[nodiscard]] double convolveUp() const noexcept {
        double acc = 0.0;
        for (std::size_t k = 0; k < kNumTaps; ++k)
            acc += cal::osfir::kProtoTaps[k] * upHist_[k];
        return acc;
    }

    // High-rate history for the downsampler (newest at index 0).
    void pushDown(double x) noexcept {
        for (std::size_t i = kNumTaps - 1; i > 0; --i) downHist_[i] = downHist_[i - 1];
        downHist_[0] = flushDenormal(x);
    }
    [[nodiscard]] double convolveDown() const noexcept {
        double acc = 0.0;
        for (std::size_t k = 0; k < kNumTaps; ++k)
            acc += cal::osfir::kProtoTaps[k] * downHist_[k];
        return acc;
    }

    std::vector<float> scratch_;                 // interleaved high-rate scratch
    double upHist_[kNumTaps]   = {0.0};          // upsampler high-rate history
    double downHist_[kNumTaps] = {0.0};          // downsampler high-rate history
    int  maxBlockSize_ = 0;
    bool prepared_     = false;
};

} // namespace mw::dsp
