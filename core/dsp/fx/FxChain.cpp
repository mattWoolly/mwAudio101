// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/FxChain.cpp — implementation of the post-voice FX chain orchestrator
// (task 094). See FxChain.h for the contract. Realizes docs/design/07-fx-section.md
// §3.2/§3.3/§3.4 and §6.1/§6.3, ADR-010 FX-1/FX-2/FX-3/FX-4/FX-9/FX-10, ADR-017
// L2/L5/L6/L8.

#include "FxChain.h"

#include <algorithm>

namespace mw::fx {

void FxChain::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_   = (sampleRate > 0.0) ? sampleRate : 48000.0;
    maxBlockSize_ = (maxBlockSize > 0) ? maxBlockSize : 0;

    // The ONLY allocation site [§3.2; ADR-010 FX-10]. Prepare the stages first so the
    // Drive's fixed 2x group delay is measured before we size the dry pad to it.
    drive_.prepare(sampleRate_, maxBlockSize_);
    chorus_.prepare(sampleRate_, maxBlockSize_);
    delay_.prepare(sampleRate_, maxBlockSize_);

    // FX latency contribution = Drive 2x group delay ONLY (the only contributing FX
    // source); Chorus/Delay return 0 [§6.1; ADR-017 L2/L3/L8]. CONSTANT for the
    // instance lifetime.
    latencySamples_ = drive_.latencySamples();

    // Dry-pad alignment line: a fixed INTEGER delay of latencySamples_ on the dry/short
    // path so FX-off sits at the declared worst-case offset [§6.3; ADR-017 L5/L6]. The
    // FractionalDelayLine processBlock() tap is the fixed integer delay; size the ring
    // to hold that much history.
    dryPad_.prepare(latencySamples_, /*fixedIntegerDelay=*/latencySamples_);

    // Preallocate the mono working buffer to the worst-case block size.
    mono_.assign(static_cast<std::size_t>(maxBlockSize_), 0.0f);

    reset();
}

void FxChain::reset() noexcept {
    drive_.reset();
    chorus_.reset();
    delay_.reset();
    dryPad_.reset();
    std::fill(mono_.begin(), mono_.end(), 0.0f);
}

void FxChain::setParams(const FxParams& p) noexcept {
    // Lock-free double-buffer publish [§3.1, §3.2]. Write the INACTIVE slot, then
    // publish its index with release so a process() that observes the new index also
    // observes the fully-written slot. Single control-thread writer.
    const int active   = activeSlot_.load(std::memory_order_relaxed);
    const int inactive = active ^ 1;
    slots_[inactive] = p;                                   // flat POD copy, no alloc
    activeSlot_.store(inactive, std::memory_order_release); // publish
}

void FxChain::applySnapshot(const FxParams& p) noexcept {
    // Push the decoded snapshot into the three stages. Drive/Chorus take the plain
    // sub-struct; Delay also needs the host BPM for its (cached) tempo-sync conversion
    // [§5.2.3]. These calls only set smoother targets / cached values — no alloc/lock.
    drive_.setParams(p.drive);
    chorus_.setParams(p.chorus);
    delay_.setParams(p.delay, p.hostBpm);
}

void FxChain::process(const float* mono, float* const* out, int numSamples) noexcept {
    float* L = out[0];
    float* R = out[1];

    // 1. Read the published snapshot (acquire) and apply it to the stages [§3.4 step 1].
    const int active = activeSlot_.load(std::memory_order_acquire);
    const FxParams snap = slots_[active]; // local POD copy
    applySnapshot(snap);

    // 2. dry = dryPad_.processBlock(mono, N): the constant worst-case alignment delay,
    //    applied on EVERY block whether or not FX run [§3.4 step 2; §6.3; ADR-017 L5].
    //    Copy the borrowed mono input into our scratch, then delay it in place.
    for (int n = 0; n < numSamples; ++n) mono_[static_cast<std::size_t>(n)] = mono[n];
    dryPad_.processBlock(mono_.data(), numSamples);

    // 3. Master bypass OR all-blocks-off early-out: copy the padded mono dry equally to
    //    L/R and run NO FX DSP (~0 cost) [§3.4 step 3; §3.3 rule 3; ADR-010 FX-1].
    const bool allBlocksOff =
        !snap.drive.on
        && (snap.chorus.mode == static_cast<int>(Chorus::Mode::Off))
        && !snap.delay.on;

    if (snap.masterBypass || allBlocksOff) {
        for (int n = 0; n < numSamples; ++n) {
            const float dry = mono_[static_cast<std::size_t>(n)];
            L[n] = dry;
            R[n] = dry; // out[L] == out[R] == padded mono dry (FX-1) [ADR-017 L6]
        }
        return;
    }

    // 4. m = padded dry (mono working buffer) — already in mono_ [§3.4 step 4].
    // 5. if drive.on: m = drive_.process(m, N) — mono->mono, oversampled (per-block
    //    bypass is a TRUE early-out: a bypassed Drive is never entered) [§3.4 step 5;
    //    §3.3 rule 4; ADR-010 FX-3].
    if (snap.drive.on)
        drive_.process(mono_.data(), numSamples);

    // 6. (L,R) = (m, m): the dry (post-Drive mono) summed EQUALLY to L/R; stereo is
    //    born only inside Chorus/Delay wet content [§3.4 step 6; §3.3 rule 1/2;
    //    ADR-010 FX-4].
    for (int n = 0; n < numSamples; ++n) {
        const float m = mono_[static_cast<std::size_t>(n)];
        L[n] = m;
        R[n] = m;
    }

    // 7. if chorus.mode != Off: chorus adds stereo wet to L/R from the mono input
    //    [§3.4 step 7; §3.3 rule 4]. The Mode==Off early-out skips it entirely.
    if (snap.chorus.mode != static_cast<int>(Chorus::Mode::Off))
        chorus_.process(mono_.data(), L, R, numSamples);

    // 8. if delay.on: stereo in/out [§3.4 step 8; §3.3 rule 4]. Bypassed => skipped.
    if (snap.delay.on)
        delay_.process(L, R, numSamples);

    // 9. if monoOutput: collapse to a phase-coherent mono sum m=0.5*(L+R); L=R=m,
    //    regardless of Chorus/Delay width [§3.4 step 9; §3.3 rule 5; ADR-010 FX-9].
    if (snap.monoOutput) {
        for (int n = 0; n < numSamples; ++n) {
            const float m = 0.5f * (L[n] + R[n]);
            L[n] = m;
            R[n] = m;
        }
    }
}

} // namespace mw::fx
