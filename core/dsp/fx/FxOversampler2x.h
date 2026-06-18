// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/FxOversampler2x.h — the dedicated post-voice 2x up/down halfband pair
// used ONLY by the FX Drive (task 090).
//
// Realizes docs/design/07-fx-section.md §2.1 (this file is the FxOversampler2x the
// backlog creates) and §4.1 (Drive's own 2x up/down pair, band-limiting Drive's
// generated harmonics before Chorus/Delay can fold them back in), and
// ADR-017 L2/L9/§A2: the FX Drive runs ONCE on the mono voice-sum (post
// poly/unison/drift) and has its OWN FX-rate 2x oversampler — a DISTINCT instance
// from the per-voice voice oversampler (core/dsp/Oversampler.h, task 036). That
// distinctness is structural: this is a separate class in the mw::fx namespace with
// its own frozen (PI) coefficient set (core/calibration/FxOversampler2xConstants.h),
// so the two oversamplers are two distinct, independently measured sources of
// constant group delay [ADR-017 L1 vs L2].
//
// Fixed 2x. Unlike the per-voice Oversampler (which has a 1x eco tier / runtime
// factor switch tied to the Quality control), the FX Drive oversampler is ALWAYS 2x
// (kDriveOSFactor = 2, fixed [docs/design/07 §4.3]); there is no factor switch here.
//
// Structure: a two-path elliptic polyphase IIR halfband (frozen coefficients in
// core/calibration/FxOversampler2xConstants.h):
//     H(z) = 1/2 [ A0(z) + z^-1 A1(z) ]
// Each branch A is a cascade of first-order allpass sections
//     y[n] = a*(x[n] - y[n-1]) + x[n-1].
// The up/down branch pairing is CROSSED (the downsampler applies branch1 to the
// sample the upsampler emitted via branch0, and vice versa) so the per-branch
// half-sample delays cancel across an up->down round trip and the in-band signal
// reconstructs to near unity.
//
// Real-time contract [ADR-017 L10; docs/design/00 §9.1 RT-5/RT-6]:
//   * prepare(maxBlockSize) is the ONLY allocator; it preallocates all interleaved
//     high-rate scratch and measures the fixed round-trip group delay once. The
//     up/down kernels, latencySamples(), and reset() are noexcept and allocate
//     nothing and lock nothing.
//   * latencySamples() returns a FIXED, deterministic, input-independent integer for
//     the instance lifetime — the FX contribution to constant PDC [ADR-017 L2, L4].
//   * Delay-line state is anti-denormal-flushed inside the recursion so a hot Drive's
//     decay tails never enter a denormal CPU stall (FTZ/DAZ is set by the engine at
//     process entry; this guard is belt-and-suspenders inside the loop).
//
// No fast-math reassociation: the frozen coefficients + the fixed evaluation order
// here are bit-identical on macOS arm64 (reference) and Linux x64
// [docs/design/00 §9.1 RT-7]. mwcore is JUCE-free [ADR-001].

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "../../calibration/FxOversampler2xConstants.h"

namespace mw::fx {

class FxOversampler2x {
public:
    // Fixed FX-rate oversampling factor: ALWAYS 2x (kDriveOSFactor = 2, fixed)
    // [docs/design/07 §4.3]. There is no 1x eco tier on the FX Drive path.
    static constexpr int kFactor = 2;

    FxOversampler2x() noexcept = default;

    // ---- Off the audio thread: the ONLY allocator [ADR-017 L10; §9.1 RT-6] -------
    // Preallocates the interleaved high-rate scratch buffer to the worst case
    // (maxBlockSize * 2), zero-initializes all delay-line state, and measures the
    // fixed round-trip group delay once. Idempotent / re-callable on block-size
    // change. The measured group delay is the same for any block size (it is a
    // property of the halfband, not the buffer).
    void prepare(int maxBlockSize);

    // ---- Audio thread: noexcept, no alloc, no lock -------------------------------
    // Clears all delay-line state to silence; does NOT free, resize, or change the
    // reported latency. [ADR-017 L10]
    void reset() noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // Fixed, deterministic, input-independent group delay (in BASE-RATE samples) of
    // the up->down round trip, measured once in prepare() and frozen for the instance
    // lifetime. This is the FX Drive's contribution to the host's constant PDC; it is
    // NONZERO and NEVER changes on the audio thread [ADR-017 L2, L4, L5, L10].
    [[nodiscard]] int latencySamples() const noexcept { return latencySamples_; }

    // Upsample one base-rate sample into the internal high-rate scratch, returning a
    // pointer to 2 contiguous high-rate samples (valid until the next call). noexcept,
    // no alloc. branch0 -> first high-rate sample, branch1 -> second.
    [[nodiscard]] const float* upsampleSample(float x) noexcept {
        scratch_[0] = static_cast<float>(up0_.process(static_cast<double>(x)));
        scratch_[1] = static_cast<float>(up1_.process(static_cast<double>(x)));
        return scratch_.data();
    }

    // Downsample 2 high-rate samples back to one base-rate sample. noexcept, no
    // alloc. The branch pairing is CROSSED vs the upsampler so the round trip
    // reconstructs in-band and the image band is rejected (anti-alias decimation).
    [[nodiscard]] float downsampleSample(const float* hi) noexcept {
        const double y1 = down1_.process(static_cast<double>(hi[0]));
        const double y0 = down0_.process(static_cast<double>(hi[1]));
        return static_cast<float>(0.5 * (y1 + y0));
    }

private:
    // Flush subnormal state to zero to avoid denormal CPU stalls in the recursion
    // [docs/design/00 §9.1 RT-5]. Branch-light; leaves normal values untouched so the
    // result stays bit-identical across platforms under FTZ/DAZ.
    static double flushDenormal(double v) noexcept {
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

    // One polyphase branch = a fixed cascade of allpass sections. The count is a
    // compile-time constant so there is no heap and no per-sample branching on length.
    struct Branch {
        std::array<AllpassSection, cal::fxos::kSectionsPerBranch> sections{};

        void setCoeffs(const std::array<double, cal::fxos::kSectionsPerBranch>& c) noexcept {
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
        up0_.setCoeffs(cal::fxos::kBranch0Coeffs);
        up1_.setCoeffs(cal::fxos::kBranch1Coeffs);
        down0_.setCoeffs(cal::fxos::kBranch0Coeffs);
        down1_.setCoeffs(cal::fxos::kBranch1Coeffs);
    }

    // Measure the fixed round-trip group delay (in base-rate samples) once, off the
    // audio thread, from the up->down impulse response. Defined out-of-line in the
    // .cpp (it uses temporary state and is never called on the hot path).
    int measureRoundTripLatency() noexcept;

    Branch up0_, up1_;       // upsampler branches (branch0 / branch1)
    Branch down0_, down1_;   // downsampler branches (branch0 / branch1)

    std::vector<float> scratch_;   // interleaved high-rate scratch (maxBlockSize*2)
    int   maxBlockSize_   = 0;
    int   latencySamples_ = cal::fxos::kReportedLatencySamples;
    bool  prepared_       = false;
};

} // namespace mw::fx
