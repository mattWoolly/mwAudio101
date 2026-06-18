// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Oversampler.h — realtime polyphase IIR halfband up/downsampler kernels
// for the per-voice 2x nonlinear zone (task 036).
//
// Realizes ADR-004 Decision item 4 + C4-C8, C10, C14, C15 and docs/design/00 §9.1
// RT-5/RT-6. This is the realtime resampler that enters (1x->2x upsample) and leaves
// (2x->1x downsample) the per-voice nonlinear zone [ADR-004 §Decision, Contract rows
// 4-8]. It is NOT the zone wrapper (core-filter-8) and NOT the offline linear-phase
// FIR (core-filter-7) — see this task's `## Out of scope`.
//
// Structure: a two-path elliptic polyphase IIR halfband (frozen coefficients in
// core/calibration/OversamplerConstants.h):
//     H(z) = 1/2 [ A0(z) + z^-1 A1(z) ]
// Each branch A is a cascade of first-order allpass sections
//     y[n] = a*(x[n] - y[n-1]) + x[n-1].
// The up/down branch pairing is CROSSED (downsampler applies branch1 to the sample
// the upsampler emitted via branch0 and vice versa) so the per-branch half-sample
// delays cancel across an up->down round trip [ADR-004 C4].
//
// Real-time contract [ADR-004 C15; docs/design/00 §9.1 RT-5/RT-6]:
//   * prepare(maxBlockSize, maxFactor) is the ONLY allocator; it preallocates all
//     interleaved high-rate scratch. The up/down kernels and reset() are noexcept and
//     allocate nothing and lock nothing.
//   * A factor change varies the active stride ONLY and never allocates on the audio
//     thread.
//   * Delay-line state is anti-denormal-flushed inside the recursion so self-osc /
//     long-decay tails never enter a denormal CPU stall (FTZ/DAZ is set by the engine
//     at process entry; this guard is belt-and-suspenders inside the loop).
//
// No fast-math reassociation: the frozen coefficients + the fixed evaluation order
// here are bit-identical on macOS arm64 (reference) and Linux x64 [ADR-004 C14;
// docs/design/00 §9.1 RT-7]. mwcore is JUCE-free.

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "../calibration/OversamplerConstants.h"

namespace mw::dsp {

class Oversampler {
public:
    // Supported oversampling factors (the active stride). 2x is the blessed default
    // [ADR-004 C10, Contract rows 10/11]; 1x is the eco/pass-through tier. A 4x HQ
    // tier is out of scope for this task (it re-derives compensation, ADR-004 C11).
    static constexpr int kFactor1x = 1;
    static constexpr int kFactor2x = 2;
    static constexpr int kMaxFactor = 2;

    Oversampler() noexcept = default;

    // ---- Off the audio thread: the ONLY allocator [ADR-004 C15; §9.1 RT-6] -------
    // Preallocates the interleaved high-rate scratch buffer to the worst case
    // (maxBlockSize * maxFactor) and zero-initializes all delay-line state. Idempotent
    // / re-callable on block-size change. maxFactor is clamped into [1, kMaxFactor].
    void prepare(int maxBlockSize, int maxFactor);

    // ---- Audio thread: noexcept, no alloc, no lock -------------------------------
    // Clears all delay-line state to silence; does NOT free or resize. [ADR-004 C15]
    void reset() noexcept;

    // Vary the ACTIVE oversampling factor (stride). No allocation; clamps to the
    // prepared maximum. Safe to call on the audio thread [ADR-004 C15].
    void setFactor(int factor) noexcept {
        int f = factor < 1 ? 1 : factor;
        if (f > maxFactor_) f = maxFactor_;
        activeFactor_ = f;
    }

    [[nodiscard]] int factor() const noexcept { return activeFactor_; }
    [[nodiscard]] int maxFactor() const noexcept { return maxFactor_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // Upsample one base-rate sample into the internal high-rate scratch, returning a
    // pointer to `factor()` contiguous high-rate samples (valid until the next call).
    // At 1x this is a pass-through (the single sample). noexcept, no alloc.
    [[nodiscard]] const float* upsampleSample(float x) noexcept {
        if (activeFactor_ == 1) {
            scratch_[0] = x;
            return scratch_.data();
        }
        // 2x: branch0 -> first high-rate sample, branch1 -> second.
        scratch_[0] = static_cast<float>(up0_.process(static_cast<double>(x)));
        scratch_[1] = static_cast<float>(up1_.process(static_cast<double>(x)));
        return scratch_.data();
    }

    // Downsample `factor()` high-rate samples back to one base-rate sample.
    // At 1x this returns hi[0]. noexcept, no alloc. The branch pairing is CROSSED vs
    // the upsampler so the round trip reconstructs in-band [ADR-004 C4].
    [[nodiscard]] float downsampleSample(const float* hi) noexcept {
        if (activeFactor_ == 1) {
            return hi[0];
        }
        const double y1 = down1_.process(static_cast<double>(hi[0]));
        const double y0 = down0_.process(static_cast<double>(hi[1]));
        return static_cast<float>(0.5 * (y1 + y0));
    }

private:
    // Flush subnormal state to zero to avoid denormal CPU stalls in the recursion
    // [docs/design/00 §9.1 RT-5]. Branch-light; leaves normal values untouched so the
    // result stays bit-identical across platforms under FTZ/DAZ.
    static double flushDenormal(double v) noexcept {
        // 1e-30 is far below any audible/normal value but above the subnormal range;
        // anything smaller in magnitude is treated as silence.
        return (v < 1.0e-30 && v > -1.0e-30) ? 0.0 : v;
    }

    // One first-order allpass section: y[n] = a*(x[n]-y[n-1]) + x[n-1].
    struct AllpassSection {
        double a  = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;

        [[nodiscard]] double process(double x) noexcept {
            const double y = a * (x - y1) + x1;
            x1 = x;
            y1 = flushDenormal(y);
            return y1;
        }
        void reset() noexcept { x1 = 0.0; y1 = 0.0; }
    };

    // One polyphase branch = a fixed cascade of allpass sections. Coefficients are
    // installed from the frozen (PI) table; the count is a compile-time constant so
    // there is no heap and no per-sample branching on length.
    struct Branch {
        std::array<AllpassSection, cal::osiir::kSectionsPerBranch> sections{};

        void setCoeffs(const std::array<double, cal::osiir::kSectionsPerBranch>& c) noexcept {
            for (std::size_t i = 0; i < c.size(); ++i) sections[i].a = c[i];
        }
        [[nodiscard]] double process(double x) noexcept {
            for (auto& s : sections) x = s.process(x);
            return x;
        }
        void reset() noexcept { for (auto& s : sections) s.reset(); }
    };

    // Install frozen coefficients into the branch pairs (called from prepare()).
    void installCoeffs() noexcept {
        up0_.setCoeffs(cal::osiir::kBranch0Coeffs);
        up1_.setCoeffs(cal::osiir::kBranch1Coeffs);
        down0_.setCoeffs(cal::osiir::kBranch0Coeffs);
        down1_.setCoeffs(cal::osiir::kBranch1Coeffs);
    }

    Branch up0_, up1_;       // upsampler branches (branch0 / branch1)
    Branch down0_, down1_;   // downsampler branches (branch0 / branch1)

    std::vector<float> scratch_;   // interleaved high-rate scratch (maxBlockSize*maxFactor)
    int   maxFactor_    = 1;
    int   activeFactor_ = 1;
    int   maxBlockSize_ = 0;
    bool  prepared_     = false;
};

} // namespace mw::dsp
