// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/FilterTables.cpp — implementation of the per-sample-rate FilterTables
// (task 035). See FilterTables.h and docs/design/02-dsp-filter.md §7 (§5.2, §6).
//
// build() is the ONLY allocator-free hot work done off the audio thread; it fills
// the preallocated arrays. The lookups are noexcept, allocation-free, lock-free and
// touch only the prebuilt storage [ADR-003 F-11]. Transcendentals (exp / exp2) are
// confined to build() — never reached from a lookup [ADR-003 F-10]. All (PI)
// constants come from core/calibration/FilterTablesConstants.h (the mw::cal::vcf
// namespace) — no (PI) literal is inlined here [ADR-003 F-15].

#include "dsp/FilterTables.h"

#include <algorithm>
#include <cmath>

#include "calibration/FilterTablesConstants.h"

namespace mw::dsp {

namespace {

// Mathematical 2*pi (NOT a (PI) pragmatic-invention constant; it is the exact math
// constant in the Huovilainen coefficient formula g = 1 - exp(-2*pi*fc/fs_os)).
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Huovilainen small-signal one-pole coefficient at fc (Hz) and oversampled rate
// fsOs (Hz): g = 1 - exp(-2*pi*fc/fsOs) [docs/design/02 §5.2; docs/research/10
// §3.6 eq.21; ADR-003 F-01]. Off-thread only.
[[nodiscard]] float coeffG(double fcHz, double fsOs) noexcept {
    if (fsOs <= 0.0) return 0.0f;
    const double g = 1.0 - std::exp(-kTwoPi * fcHz / fsOs);
    return static_cast<float>(g);
}

// Linear interpolation over a fixed-size table given a clamped [0,1) position.
// Extent is the centralized (PI) table size — never an inlined literal [ADR-003 F-15].
[[nodiscard]] float lerpTable(
    const std::array<float, cal::vcf::kFilterTableSize>& tbl, float pos01) noexcept {
    constexpr int n = cal::vcf::kFilterTableSize;
    // pos01 in [0,1]; map to [0, n-1] fractional index.
    const float fidx = pos01 * static_cast<float>(n - 1);
    int i0 = static_cast<int>(fidx);
    if (i0 < 0) i0 = 0;
    if (i0 > n - 2) i0 = n - 2;     // ensure i0+1 valid
    const float frac = fidx - static_cast<float>(i0);
    return tbl[i0] + frac * (tbl[i0 + 1] - tbl[i0]);
}

} // namespace

void FilterTables::build(double fsOsHz) noexcept {
    fsOs_ = fsOsHz;

    // Stability/prewarp guard: fc never exceeds min(kFcMaxHz, 0.45*fs_os) [F-08, §6].
    const double guard = cal::vcf::kFcGuardFracOfFsOs * fsOsHz;
    double fcMax = static_cast<double>(cal::vcf::kFcMaxHz);
    if (guard < fcMax) fcMax = guard;
    if (fcMax < static_cast<double>(cal::vcf::kFcMinHz)) {
        fcMax = static_cast<double>(cal::vcf::kFcMinHz);
    }
    fcMaxHz_ = static_cast<float>(fcMax);

    const double fcMin   = static_cast<double>(cal::vcf::kFcMinHz);
    const double fcRef   = static_cast<double>(cal::vcf::kFcRefHz);
    const double cvMin   = static_cast<double>(cal::vcf::kCvTableMinVolts);
    const double cvMax   = static_cast<double>(cal::vcf::kCvTableMaxVolts);

    constexpr int n = cal::vcf::kFilterTableSize;  // (PI) table size — centralized [ADR-003 F-15]

    // --- CV-domain g table: fc = fcRef * 2^v (1 V/oct), clamped, then g(fc) -----
    for (int i = 0; i < n; ++i) {
        const double t  = static_cast<double>(i) / static_cast<double>(n - 1);
        const double cv = cvMin + t * (cvMax - cvMin);
        double fc = fcRef * std::exp2(cv);              // 1 V/oct
        if (fc < fcMin) fc = fcMin;
        if (fc > fcMax) fc = fcMax;                     // prewarp/stability guard (F-08)
        gByCv_[static_cast<std::size_t>(i)] = coeffG(fc, fsOsHz);
    }

    // --- Tuning-comp table: comp(g) = c0 + c1*g + c2*g^2 over g in [gMin,gMax) ---
    const float c0 = cal::vcf::kCompFit[0];
    const float c1 = cal::vcf::kCompFit[1];
    const float c2 = cal::vcf::kCompFit[2];
    const double gLo = static_cast<double>(cal::vcf::kCompGMin);
    const double gHi = static_cast<double>(cal::vcf::kCompGMax);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);
        const float  g = static_cast<float>(gLo + t * (gHi - gLo));
        compByG_[static_cast<std::size_t>(i)] = c0 + g * (c1 + g * c2);
    }
}

float FilterTables::clampFcHz(float fcHz) const noexcept {
    float fc = fcHz;
    if (fc < cal::vcf::kFcMinHz) fc = cal::vcf::kFcMinHz;
    if (fc > fcMaxHz_)           fc = fcMaxHz_;
    return fc;
}

float FilterTables::cvToG(float cutoffVolts) const noexcept {
    // Clamp CV to the built table span, map to [0,1), interpolate.
    float cv = cutoffVolts;
    if (cv < cal::vcf::kCvTableMinVolts) cv = cal::vcf::kCvTableMinVolts;
    if (cv > cal::vcf::kCvTableMaxVolts) cv = cal::vcf::kCvTableMaxVolts;
    const float span = cal::vcf::kCvTableMaxVolts - cal::vcf::kCvTableMinVolts;
    const float pos  = (cv - cal::vcf::kCvTableMinVolts) / span;
    return lerpTable(gByCv_, pos);
}

float FilterTables::hzToG(float fcHz) const noexcept {
    // Clamp fc to the stable range, convert to CV (1 V/oct), reuse the CV table so
    // hzToG and cvToG agree exactly. No exp() at audio rate: log2 is only used to
    // index the precomputed table; the g VALUE comes from the table, not a
    // transcendental of fc. (log2 here is a pure index map, not the coefficient.)
    const float fc = clampFcHz(fcHz);
    // CV = log2(fc / fcRef). std::log2 is a transcendental but is used purely to
    // address the table; the design permits hzToG for the reference/test path and
    // pre-resolved callers (§5.2). The audio hot path uses cvToG, whose index map is
    // a plain affine transform.
    const float cv = std::log2(fc / cal::vcf::kFcRefHz);
    return cvToG(cv);
}

float FilterTables::resoTuningComp(float g) const noexcept {
    float gg = g;
    if (gg < cal::vcf::kCompGMin) gg = cal::vcf::kCompGMin;
    if (gg > cal::vcf::kCompGMax) gg = cal::vcf::kCompGMax;
    const float span = cal::vcf::kCompGMax - cal::vcf::kCompGMin;
    const float pos  = span > 0.0f ? (gg - cal::vcf::kCompGMin) / span : 0.0f;
    return lerpTable(compByG_, pos);
}

} // namespace mw::dsp
