// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/CompareFp.h — the CLASS-FP two-stage golden comparer (task 043).
//
// Realizes docs/design/11 §6.1 (Stage gating) and §6.3 (Stage-1 scalar fingerprint,
// Stage-2 windowed-FFT NMSE-in-dB + alias-floor, FpTolerance, FpResult, compareFp).
// Normative contract: ADR-013 C6 (arm64 bit-exact when tol.maxAbsErr==0), C7
// (Linux/Windows banded), C9 (Stage 2 SKIPPED unless Stage 1 flags or full==true),
// C22 / ADR-023 V11 (a compare across a different engine tag — ladder, oversample
// factor, or renderVersion — is REFUSED, never a pass).
//
// Header-only: the design tree lists tests/golden/Comparer.{h,cpp}, but a header-only
// realization keeps the primitive self-contained and avoids touching the shared
// tests/CMakeLists glob set (it compiles tests/unit/*.cpp; a tests/golden/*.cpp would
// not be picked up). This is the same pattern as the sibling tests/golden/Sha256.h
// (task 040) and GoldenKey.h (task 041). This is OFFLINE harness code — RT invariants
// are not in play [docs/design/11 §2.2]; the comparer allocates freely.
//
// Tolerance VALUES are never minted here: FpTolerance is filled per-corpus from the
// MANIFEST [docs/design/11 §6.3/§6.4; ADR-013 C7]. There is NO global tolerance
// #define (forbidden — plan/backlog/043 Out-of-scope). The only constants this header
// reads are the perceptual alias limit and the (PI) Stage-1 flag margin / FFT length,
// centralized in core/calibration/CompareFpConstants.h.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "../../core/calibration/CompareFpConstants.h"
#include "GoldenKey.h"   // EngineTag, sameEngineContext

namespace mw::golden {

// The render result the comparer operates on [docs/design/11 §5.4]. Defined here (not
// in a separate RenderHarness.h) so the comparer is self-contained: it depends only
// on the sample vector, the sample rate, and the engine tag it carries. A future
// RenderHarness can alias / re-use this same aggregate.
struct RenderResult {
    std::vector<float> samples;     // mono f32 (or interleaved if a stereo path)
    double             sampleRate = 0.0;
    EngineTag          engine{};
};

// Stage 1 — the cheap scalar fingerprint [docs/design/11 §6.3].
struct Stage1Fingerprint {
    double rms        = 0.0;        // RMS of `got`
    double peak       = 0.0;        // peak |got|
    double maxAbsErr  = 0.0;        // max |got - blessed|, sample-aligned
    double envelopeErr= 0.0;        // windowed-envelope max error vs blessed
    double rmsErr     = 0.0;        // |rms(got) - rms(blessed)|
};

// Stage 2 — the spectral pass, run only on a Stage-1 flag or full==true
// [docs/design/11 §6.3].
struct Stage2Metrics {
    double nmseDb        = 0.0;     // windowed-FFT normalized MSE in dB (lower = closer)
    double aliasFloorDb  = 0.0;     // residual energy above the perceptual alias limit, dB
};

// Per-corpus tolerance, FROM the manifest — never a global #define [docs/design/11
// §6.3/§6.4; ADR-013 C6-C8]. On arm64 maxAbsErr==0 is the bit-exact gate; on
// Linux/Windows it is the manifest band.
struct FpTolerance {
    double maxAbsErr           = 0.0;   // arm64 = 0 (bit-exact); Linux/Windows = band
    double rmsErr              = 0.0;
    double nmseDbCeiling       = 0.0;   // Stage-2 NMSE must be <= this (dB)
    double aliasFloorDbCeiling = 0.0;   // Stage-2 alias floor must be <= this (dB)
    // The perceptual alias-free limit (Hz) above which Stage 2 sums residual energy.
    // Defaults to the NI=2 PolyBLEP limit; a corpus with a different correction order
    // overrides it [docs/research/10 §8].
    double aliasLimitHz        = mw::cal::golden::kAliasFloorLimitHz;
};

// The comparer result. `refused` is set (and `pass` left false) when the engine tags
// do not match — a refusal is NOT a pass [ADR-013 C22; ADR-023 V11].
struct FpResult {
    bool                         pass      = false;
    bool                         ranStage2 = false;
    bool                         refused   = false;
    Stage1Fingerprint            s1{};
    std::optional<Stage2Metrics> s2{};
};

namespace detail {

inline double rms(const std::vector<float>& x) noexcept {
    if (x.empty()) return 0.0;
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(acc / static_cast<double>(x.size()));
}

inline double peakAbs(const std::vector<float>& x) noexcept {
    double p = 0.0;
    for (float v : x) p = std::max(p, std::abs(static_cast<double>(v)));
    return p;
}

// Max |a - b| over the common prefix; a length mismatch counts the tail of the longer
// buffer as full-magnitude error so a truncated render cannot pass silently.
inline double maxAbsErr(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    double m = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        m = std::max(m, std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    const std::vector<float>& longer = (a.size() >= b.size()) ? a : b;
    for (std::size_t i = n; i < longer.size(); ++i)
        m = std::max(m, std::abs(static_cast<double>(longer[i])));
    return m;
}

// A coarse windowed-envelope max error: compare block-RMS over fixed windows. Cheap,
// catches gross amplitude-envelope divergence the per-sample maxAbsErr might localize.
inline double envelopeErr(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    constexpr std::size_t kWin = 256;
    const std::size_t n = std::min(a.size(), b.size());
    double m = 0.0;
    for (std::size_t start = 0; start < n; start += kWin) {
        const std::size_t end = std::min(start + kWin, n);
        double ea = 0.0, eb = 0.0;
        for (std::size_t i = start; i < end; ++i) {
            ea += static_cast<double>(a[i]) * static_cast<double>(a[i]);
            eb += static_cast<double>(b[i]) * static_cast<double>(b[i]);
        }
        const double cnt = static_cast<double>(end - start);
        m = std::max(m, std::abs(std::sqrt(ea / cnt) - std::sqrt(eb / cnt)));
    }
    return m;
}

// --- A self-contained radix-2 iterative FFT (no external dependency) --------------
// Operates in double precision on a power-of-two buffer; pure arithmetic (no
// transcendentals beyond the twiddle sin/cos, which is offline-only). Returns the
// complex spectrum in `re`/`im`.
inline void fftRadix2(std::vector<double>& re, std::vector<double>& im) noexcept {
    const std::size_t n = re.size();
    if (n < 2) return;
    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    const double pi = 3.14159265358979323846;
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * pi / static_cast<double>(len);
        const double wlenRe = std::cos(ang);
        const double wlenIm = std::sin(ang);
        for (std::size_t i = 0; i < n; i += len) {
            double wRe = 1.0, wIm = 0.0;
            for (std::size_t k = 0; k < (len >> 1); ++k) {
                const std::size_t a = i + k;
                const std::size_t b = i + k + (len >> 1);
                const double uRe = re[a], uIm = im[a];
                const double vRe = re[b] * wRe - im[b] * wIm;
                const double vIm = re[b] * wIm + im[b] * wRe;
                re[a] = uRe + vRe; im[a] = uIm + vIm;
                re[b] = uRe - vRe; im[b] = uIm - vIm;
                const double nwRe = wRe * wlenRe - wIm * wlenIm;
                wIm = wRe * wlenIm + wIm * wlenRe;
                wRe = nwRe;
            }
        }
    }
}

// Smallest power of two >= v (with a floor of 2).
inline std::size_t nextPow2(std::size_t v) noexcept {
    std::size_t p = 2;
    while (p < v) p <<= 1;
    return p;
}

// Hann-window a real buffer into an FFT-length complex buffer (zero-padded), in place
// of `re`/`im`.
inline void hannLoad(const std::vector<float>& x, std::size_t fftLen,
                     std::vector<double>& re, std::vector<double>& im) {
    re.assign(fftLen, 0.0);
    im.assign(fftLen, 0.0);
    const std::size_t n = std::min(x.size(), fftLen);
    if (n <= 1) {
        for (std::size_t i = 0; i < n; ++i) re[i] = static_cast<double>(x[i]);
        return;
    }
    const double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < n; ++i) {
        const double w =
            0.5 * (1.0 - std::cos(2.0 * pi * static_cast<double>(i) /
                                  static_cast<double>(n - 1)));
        re[i] = static_cast<double>(x[i]) * w;
    }
}

// Convert a power ratio to dB, with a hard floor so an exact-zero numerator returns a
// large-negative sentinel rather than -inf.
inline double powerRatioToDb(double num, double den) noexcept {
    constexpr double kFloor = 1.0e-300;
    if (den < kFloor) den = kFloor;
    const double ratio = num / den;
    if (ratio < 1.0e-30) return -300.0;   // sentinel "effectively zero"
    return 10.0 * std::log10(ratio);
}

// Stage 2: windowed-FFT NMSE in dB and the alias-floor metric. NMSE = sum|GOT-BLESS|^2
// / sum|BLESS|^2 over the spectrum (Parseval-equivalent to the time-domain NMSE, but
// computed spectrally so the same path yields the alias-band split) [docs/design/11
// §6.3].
inline Stage2Metrics stage2(const RenderResult& got, const RenderResult& blessed,
                            const FpTolerance& tol) {
    const std::size_t maxLen = std::max(got.samples.size(), blessed.samples.size());
    std::size_t fftLen = std::max(mw::cal::golden::kStage2FftLength, nextPow2(maxLen));
    // Keep the analysis bounded: a single padded frame covering the whole buffer.

    std::vector<double> gRe, gIm, bRe, bIm;
    hannLoad(got.samples, fftLen, gRe, gIm);
    hannLoad(blessed.samples, fftLen, bRe, bIm);
    fftRadix2(gRe, gIm);
    fftRadix2(bRe, bIm);

    const double sr = (blessed.sampleRate > 0.0) ? blessed.sampleRate : got.sampleRate;
    const double binHz = (sr > 0.0) ? sr / static_cast<double>(fftLen) : 0.0;

    double errPow = 0.0;       // sum |GOT - BLESS|^2 over the half spectrum
    double refPow = 0.0;       // sum |BLESS|^2
    double aliasErrPow = 0.0;  // sum |GOT - BLESS|^2 ABOVE the perceptual limit
    double totErrPow   = 0.0;  // total residual power (for the alias-floor dB ratio)

    const std::size_t half = fftLen / 2;   // up to Nyquist
    for (std::size_t k = 0; k <= half; ++k) {
        const double dRe = gRe[k] - bRe[k];
        const double dIm = gIm[k] - bIm[k];
        const double dMag2 = dRe * dRe + dIm * dIm;
        const double rMag2 = bRe[k] * bRe[k] + bIm[k] * bIm[k];
        errPow += dMag2;
        refPow += rMag2;
        totErrPow += dMag2;
        const double freq = static_cast<double>(k) * binHz;
        if (freq > tol.aliasLimitHz) aliasErrPow += dMag2;
    }

    Stage2Metrics m{};
    m.nmseDb = powerRatioToDb(errPow, refPow);
    // Alias floor: fraction of residual energy that sits above the perceptual limit,
    // in dB relative to total residual energy. A residual concentrated above the
    // limit -> near 0 dB; one with no above-limit content -> a large-negative floor.
    m.aliasFloorDb = powerRatioToDb(aliasErrPow, totErrPow);
    return m;
}

} // namespace detail

// The CLASS-FP two-stage compare [docs/design/11 §6.1/§6.3].
//
// 1. REFUSE if the engine tags are not the same context (ladder / oversample /
//    renderVersion) — a refusal is not a pass [ADR-013 C22; ADR-023 V11].
// 2. Stage 1: scalar fingerprint vs the band. With tol.maxAbsErr==0 this is the arm64
//    bit-exact gate (any nonzero diff fails) [ADR-013 C6]; otherwise it is banded
//    [ADR-013 C7].
// 3. Stage 2 runs ONLY on a Stage-1 flag (any metric at/over its band) or full==true
//    [ADR-013 C9]. When it runs, the overall pass additionally requires the spectral
//    metrics to be under their ceilings.
inline FpResult compareFp(const RenderResult& got, const RenderResult& blessed,
                          const FpTolerance& tol, bool full = false) {
    FpResult r{};

    // (1) Engine-context refusal — checked first, before any compare.
    if (!sameEngineContext(got.engine, blessed.engine)) {
        r.refused = true;
        r.pass = false;
        return r;
    }

    // (2) Stage 1 — cheap scalar fingerprint.
    r.s1.rms        = detail::rms(got.samples);
    r.s1.peak       = detail::peakAbs(got.samples);
    r.s1.maxAbsErr  = detail::maxAbsErr(got.samples, blessed.samples);
    r.s1.envelopeErr= detail::envelopeErr(got.samples, blessed.samples);
    r.s1.rmsErr     = std::abs(r.s1.rms - detail::rms(blessed.samples));

    // Stage-1 in-band test. maxAbsErr==0 => strict bit-exact (any nonzero fails).
    const bool maxAbsOk =
        (tol.maxAbsErr == 0.0) ? (r.s1.maxAbsErr == 0.0)
                               : (r.s1.maxAbsErr <= tol.maxAbsErr);
    const bool rmsOk =
        (tol.rmsErr == 0.0) ? (r.s1.rmsErr == 0.0)
                            : (r.s1.rmsErr <= tol.rmsErr);
    const bool stage1Pass = maxAbsOk && rmsOk;

    // The Stage-1 FLAG that escalates to Stage 2: any out-of-band metric, OR a metric
    // that has reached the (PI) fast-reject margin of its band [docs/design/11 §6.1].
    bool stage1Flag = !stage1Pass;
    if (!stage1Flag && tol.maxAbsErr > 0.0) {
        const double margin = mw::cal::golden::kStage1FlagMargin;
        if (r.s1.maxAbsErr >= tol.maxAbsErr * margin) stage1Flag = true;
    }

    // (3) Stage 2 gating.
    if (full || stage1Flag) {
        r.ranStage2 = true;
        const Stage2Metrics m = detail::stage2(got, blessed, tol);
        r.s2 = m;
        const bool nmseOk  = (m.nmseDb <= tol.nmseDbCeiling);
        const bool aliasOk = (m.aliasFloorDb <= tol.aliasFloorDbCeiling);
        r.pass = stage1Pass && nmseOk && aliasOk;
    } else {
        // Cheap path: Stage 1 within tolerance and not forced full -> Stage 2 skipped.
        r.pass = stage1Pass;
    }

    return r;
}

} // namespace mw::golden
