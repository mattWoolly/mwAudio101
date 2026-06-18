// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/MinBlepTable.h — 64x Blackman-windowed minimum-phase step residual
// table + per-voice allocation-free overlap-add applicator (task 027).
//
// Realizes docs/design/01-dsp-oscillators.md §3.2 (table) and §3.3 (applicator),
// the real-time invariants of §2.4, and ADR-002 C8 (built once in prepareToPlay,
// read-only on the audio thread) / C11 (no heap alloc, no locks on any audio-thread
// path). The (PI) constants kOversampling / kZeroCrossings are read from
// core/calibration/MinBlepConstants.h and NEVER inlined here [§3.2, §10].

#pragma once

#include <vector>

#include "../calibration/MinBlepConstants.h"

namespace mw101::dsp {

// -----------------------------------------------------------------------------
// MinBlepTable
//
// The minimum-phase band-limited step RESIDUAL, built once off the audio thread.
// The stored residual r[k] = blep[k] - 1, where blep is the band-limited unit step
// rising 0 -> 1 across the table. r therefore rises from ~ -1 (before the step has
// played out) up to ~ 0 (after), so adding amp*r to a held DC level of amp yields
// the band-limited step shape and asymptotes to amp [docs/design/01 §3.2, §3.3].
//
// Read-only on the audio thread after build() [§2.4; ADR-002 C8, C11].
// -----------------------------------------------------------------------------
class MinBlepTable
{
public:
    // Oversampling factor of the residual table (Blackman-windowed) [research/10 §5.1].
    static constexpr int kOversampling = mw::cal::minblep::kOversampling;   // 64x
    // (PI) half-width in periods; sourced from calibration, not duplicated [§3.2, §10].
    static constexpr int kZeroCrossings = mw::cal::minblep::kZeroCrossings;
    // Total residual length in TABLE samples = 2*kZeroCrossings*kOversampling.

    // Built once, off the audio thread (prepareToPlay). Allocates here ONLY.
    void build();                                     // not noexcept; init-time only
    [[nodiscard]] bool isBuilt() const noexcept;

    // Pure read; noexcept; no allocation. `tableIndex` in [0, tableLength()).
    [[nodiscard]] float residualAt (int tableIndex) const noexcept;

    // Residual length in BASE samples (== 2*kZeroCrossings) [§3.2].
    [[nodiscard]] int length() const noexcept;

    // Residual length in TABLE samples (== length()*kOversampling).
    [[nodiscard]] int tableLength() const noexcept;

private:
    std::vector<float> residual_;                     // sized in build(); read-only after
};

// -----------------------------------------------------------------------------
// MinBlepApplicator
//
// Per-voice overlap-add accumulator of scheduled band-limited steps. Pre-sized in
// prepare(); purely arithmetic and allocation/lock-free on the audio thread
// [docs/design/01 §3.3, §2.4; ADR-002 C11].
// -----------------------------------------------------------------------------
class MinBlepApplicator
{
public:
    void prepare (const MinBlepTable& table, double sampleRate);  // sizes the ring
    void reset() noexcept;

    // Schedule a band-limited step of signed amplitude `amp` at fractional
    // sub-sample offset `frac` in [0,1). noexcept, no allocation.
    void scheduleStep (float amp, float frac) noexcept;

    // Pop the accumulated correction for the current sample and advance. noexcept.
    [[nodiscard]] float next() noexcept;

private:
    const MinBlepTable* table_ = nullptr;             // non-owning, read-only
    std::vector<float>  ring_;                         // pre-sized; circular accumulator
    int                 ringSize_ = 0;
    int                 head_     = 0;
    float               level_    = 0.0f;              // held DC the steps settle to
};

} // namespace mw101::dsp
