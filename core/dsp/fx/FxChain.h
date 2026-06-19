// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/fx/FxChain.h — the post-voice FX chain orchestrator (task 094).
//
// Realizes docs/design/07-fx-section.md §3 (chain-level interface), §3.1 (this exact
// class signature), §3.2 (chain-wide RT invariants), §3.3 (dry/wet, mono-to-stereo,
// bypass rules), §3.4 (one-block processing order), §6.1/§6.3 (constant FX latency +
// dry-pad alignment), and ADR-010 FX-1/FX-2/FX-3/FX-4/FX-9/FX-10, ADR-017 L2/L5/L6/L8.
//
// FxChain owns the three FX stages in FIXED order Drive -> Chorus -> Delay, the
// dry/wet mono sum, the dry-pad alignment delay, the master / all-blocks-off bypass
// early-out, the per-block bypass dispatch, the global Mono Output collapse, and the
// constant FX latency report [§3; ADR-010 FX-2; ADR-017 §Decision].
//
// Threading [§3.1, §3.2; ADR-010 FX-10]: setParams() is called on the control thread
// and publishes a new FxParams snapshot via a lock-free double buffer (an atomic index
// swap with release/acquire ordering); process() reads the active snapshot with
// memory_order_acquire on the audio thread. No mutex, no allocation on either path.
//
// Latency [§6.1/§6.3; ADR-017 L2/L5/L8]: getLatencySamples() == Drive::latencySamples()
// only (the FX Drive 2x oversampler is the only contributing FX source); Chorus and
// Delay return 0 (intended musical delay, ADR-017 L3). It is CONSTANT for the instance
// lifetime, invariant to drive.on / masterBypass / per-block bypass. The dry-pad delay
// line (dryPad_) is applied on EVERY block so FX-off output sits at exactly that
// constant worst-case offset, keeping FX-off bit-exact at the declared offset
// [§6.3; ADR-017 L5/L6].
//
// This file carries no JUCE types [ADR-001]. Out of scope here (owned elsewhere):
// summing the FX latency with the per-voice zone delay and the setLatencySamples host
// call (plugin/, ADR-017 L4); the individual stage DSP internals (fx-4/5/6); the
// FX-off golden capture/bless (golden-harness).

#pragma once

#include <atomic>
#include <vector>

#include "Drive.h"
#include "Chorus.h"
#include "Delay.h"
#include "FractionalDelayLine.h"
#include "FxParams.h"

namespace mw::fx {

// Owns the three stages, the dry/wet sum, the global Mono Output collapse, the
// worst-case latency report, and the master/all-off early-out. Class signature is
// fixed verbatim by docs/design/07-fx-section.md §3.1.
class FxChain {
public:
    FxChain() noexcept = default;

    // Allocate all ring buffers / oversampler state / scratch at max size. Called off
    // the audio thread; the ONLY allocation site [§3.1, §3.2; ADR-010 FX-10]. Sizes
    // the dry-pad line to getLatencySamples() (Drive's fixed 2x group delay) so the
    // FX-off / short path sits at the declared worst-case offset [§6.3; ADR-017 L5].
    // Re-callable / idempotent on sample-rate / block-size change [ADR-017 L10].
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    // Clear all state to a known silent start; no allocation [§3.1].
    void reset() noexcept;

    // Publish a new parameter snapshot. Called on the control thread; the audio thread
    // reads the active snapshot lock-free (double buffer + atomic index swap, acquire/
    // release). Never blocks, never allocates [§3.1, §3.2; ADR-010 FX-10].
    void setParams(const FxParams& p) noexcept;

    // Process one block. mono == the post-voice mono voice sum (numSamples). out ==
    // stereo (2 x numSamples). RT hot path: noexcept, no alloc, no lock. Implements the
    // §3.4 order: dryPad (always) -> master/all-off early-out -> Drive(if on) -> sum to
    // L/R -> Chorus(if !=Off) -> Delay(if on) -> Mono Output collapse.
    void process(const float* mono, float* const* out, int numSamples) noexcept;

    // Deterministic FX group delay in samples at the prepared sample rate. CONSTANT for
    // the instance lifetime; computed in prepare(). == Drive::latencySamples() (the FX
    // Drive 2x oversampler), invariant to FX bypass [§6.1; ADR-017 L2, L5, L8].
    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples_; }

private:
    // Apply a decoded snapshot to the three stages. Called on the audio thread at the
    // top of process() after reading the published snapshot (so stage-internal smoother
    // targets / cached conversions track the snapshot without crossing a lock) [§3.4
    // step 1; §3.2]. noexcept, no alloc.
    void applySnapshot(const FxParams& p) noexcept;

    Drive  drive_{};
    Chorus chorus_{};
    Delay  delay_{};

    // Worst-case dry-pad alignment line: a fixed INTEGER delay of getLatencySamples()
    // applied on the dry/short path every block so FX-off sits at the constant offset
    // [§6.3; ADR-017 L5/L6].
    FractionalDelayLine dryPad_{};

    // Preallocated scratch (sized to maxBlockSize in prepare): the mono working buffer
    // the dry pad writes into and the stages process. Output channels are caller-owned.
    std::vector<float> mono_{};

    // --- Lock-free double-buffered FxParams snapshot [§3.1, §3.2] -----------------
    // Two slots; the control thread writes the inactive slot then publishes its index
    // with memory_order_release; the audio thread reads activeSlot_ with acquire and
    // copies that slot. Single-writer (control) / single-reader (audio): a plain
    // double buffer is correct and lock-free.
    FxParams            slots_[2]{};
    std::atomic<int>    activeSlot_{ 0 };
    // The snapshot the audio thread has most recently consumed (so process() can detect
    // a republish and re-apply to the stages only when it changed is unnecessary here —
    // we re-apply every block, which is cheap and keeps stage smoothers tracking).

    int    latencySamples_ = 0;   // == drive_.latencySamples(); set in prepare().
    double sampleRate_     = 48000.0;
    int    maxBlockSize_   = 0;
};

} // namespace mw::fx
