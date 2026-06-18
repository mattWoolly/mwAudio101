// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/FilterTables.h — per-sample-rate coefficient/compensation tables for the
// IR3109 ladder (task 035). Built off the audio thread in prepare(), read-only at
// audio rate. Realizes docs/design/02-dsp-filter.md §7 (and §5.2, §6) under the
// normative ADR-003 contract rows F-08, F-11, F-14.
//
// What it precomputes so no transcendental runs at audio rate [ADR-003 F-10, F-11]:
//   - cvToG(volts): folds fc = fcRefHz * 2^v (1 V/oct) and the Huovilainen
//     small-signal coefficient g = 1 - exp(-2*pi*fc/fs_os) into a single
//     interpolated lookup, with fc clamped to [10, min(20000, 0.45*fs_os)] (§5.2,
//     §6, F-08).
//   - hzToG(Hz): the same fc -> g map keyed directly in Hz (for setCutoffHz / the
//     reference oracle).
//   - resoTuningComp(g): the residual half-sample feedback-tuning correction
//     (absorbing the <10%-at-2x error) from the frozen compFit constants (§7.3,
//     F-14).
//
// Real-time invariant: build() is the ONLY allocator and runs only in prepare();
// the lookups touch only the preallocated std::array storage and are noexcept,
// allocation-free, lock-free [ADR-003 F-11]. The table contents are frozen/versioned
// and bit-identical across runs for a fixed fs_os (part of the bless contract)
// [ADR-003 F-14].

#pragma once

#include <array>

namespace mw::dsp {

class FilterTables {
public:
    FilterTables() noexcept = default;

    // Off-thread; fills the preallocated arrays for the OVERSAMPLED rate fsOsHz
    // (factor * host rate). The only place table storage is written [ADR-003 F-11].
    // Idempotent: a repeated call with the same fsOsHz yields bit-identical tables
    // (frozen constants, deterministic loop) [ADR-003 F-14].
    void build(double fsOsHz) noexcept;

    // Map summed cutoff CV (volts, 1 V/oct) -> prewarped coeff g, via interpolated
    // table. Folds fc = fcRefHz * 2^v and g = 1 - exp(-2*pi*fc/fs_os) (§5.2, F-08).
    // CV is clamped to the table's built span; fc is clamped to
    // [fcMinHz, min(fcMaxHz, 0.45*fs_os)] (§6, F-08). noexcept, no alloc.
    [[nodiscard]] float cvToG(float cutoffVolts) const noexcept;

    // Map cutoff Hz -> g directly (for setCutoffHz / reference). fc is clamped to
    // [fcMinHz, min(fcMaxHz, 0.45*fs_os)] (§6, F-08). noexcept, no alloc.
    [[nodiscard]] float hzToG(float fcHz) const noexcept;

    // Residual feedback-tuning compensation factor for the current g (§7.3, F-14).
    // Interpolated from the comp table; near unity by construction. noexcept, no
    // alloc.
    [[nodiscard]] float resoTuningComp(float g) const noexcept;

    // The clamped fc that the table would use for a given Hz request (exposed for
    // tests / the guard check; computed without a transcendental). noexcept.
    [[nodiscard]] float clampFcHz(float fcHz) const noexcept;

    [[nodiscard]] double sampleRateOs() const noexcept { return fsOs_; }

private:
    double fsOs_   = 0.0;
    float  fcMaxHz_ = 0.0f;  // min(kFcMaxHz, 0.45*fs_os) for this fs_os (the guard)

    static constexpr int kTableSize = 1024;        // (PI) resolution; frozen for bless
    std::array<float, kTableSize> gByCv_  {};      // CV-domain g table
    std::array<float, kTableSize> compByG_{};      // tuning-comp table
};

} // namespace mw::dsp
