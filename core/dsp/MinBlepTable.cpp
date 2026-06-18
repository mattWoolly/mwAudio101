// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/MinBlepTable.cpp — build of the 64x Blackman-windowed minimum-phase
// step residual table + the per-voice overlap-add applicator (task 027).
//
// Construction (init-time only; allocation is permitted ONLY here, off the audio
// thread per ADR-002 C8/C11, docs/design/01 §2.4):
//   1. Build a band-limited impulse: a sinc with cutoff at the BASE Nyquist over a
//      64x-oversampled grid, Blackman-windowed and truncated to kZeroCrossings
//      lobes each side [docs/design/01 §3.2; research/10 §5.1].
//   2. Convert that linear-phase prototype to MINIMUM phase via the real-cepstrum
//      method (Oppenheim & Schafer): minimum phase front-loads the residual energy,
//      which is what a minBLEP needs so the correction is causal at the edge.
//   3. Integrate (running sum) to get the band-limited step (a "blep") and normalize
//      it to rise 0 -> 1.
//   4. Store the RESIDUAL r = blep - 1, so adding amp*r to a held DC of amp produces
//      the band-limited step and asymptotes to amp [§3.3].
//
// The DFTs below are direct O(N^2) transforms. N = 2*kZeroCrossings*kOversampling
// (2048 by default); this runs a handful of times at prepareToPlay only, never on
// the audio thread, so the naive transform is acceptable and keeps mwcore free of
// any FFT dependency [docs/design/00 §3.3 ZERO JUCE / freestanding].

#include "MinBlepTable.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace mw101::dsp {

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kTwoPi   = 2.0 * kPi;

using Cplx = std::complex<double>;

// Naive forward DFT: X[k] = sum_n x[n] e^{-j 2pi k n / N}.
void dft (const std::vector<Cplx>& x, std::vector<Cplx>& X)
{
    const std::size_t N = x.size();
    X.assign (N, Cplx (0.0, 0.0));
    for (std::size_t k = 0; k < N; ++k)
    {
        Cplx acc (0.0, 0.0);
        const double w0 = -kTwoPi * static_cast<double> (k) / static_cast<double> (N);
        for (std::size_t n = 0; n < N; ++n)
        {
            const double ang = w0 * static_cast<double> (n);
            acc += x[n] * Cplx (std::cos (ang), std::sin (ang));
        }
        X[k] = acc;
    }
}

// Naive inverse DFT: x[n] = (1/N) sum_k X[k] e^{+j 2pi k n / N}.
void idft (const std::vector<Cplx>& X, std::vector<Cplx>& x)
{
    const std::size_t N = X.size();
    x.assign (N, Cplx (0.0, 0.0));
    for (std::size_t n = 0; n < N; ++n)
    {
        Cplx acc (0.0, 0.0);
        const double w0 = kTwoPi * static_cast<double> (n) / static_cast<double> (N);
        for (std::size_t k = 0; k < N; ++k)
        {
            const double ang = w0 * static_cast<double> (k);
            acc += X[k] * Cplx (std::cos (ang), std::sin (ang));
        }
        x[n] = acc / static_cast<double> (N);
    }
}

} // namespace

// -----------------------------------------------------------------------------
// MinBlepTable
// -----------------------------------------------------------------------------

void MinBlepTable::build()
{
    const int    tableLen = 2 * kZeroCrossings * kOversampling;   // (PI)-derived length
    const std::size_t N   = static_cast<std::size_t> (tableLen);

    // --- 1. Linear-phase band-limited impulse: windowed sinc, cutoff = base Nyquist.
    // On the 64x grid the base-rate Nyquist sits at normalized 1/kOversampling, so the
    // sinc argument steps by 1/kOversampling per table sample.
    std::vector<double> proto (N, 0.0);
    const double center = static_cast<double> (tableLen - 1) * 0.5;
    for (int i = 0; i < tableLen; ++i)
    {
        const double m = static_cast<double> (i) - center;          // samples from center
        const double x = m / static_cast<double> (kOversampling);   // base-rate samples
        double sinc;
        if (std::abs (x) < 1.0e-12)
            sinc = 1.0;
        else
            sinc = std::sin (kPi * x) / (kPi * x);

        // Blackman window over [0, tableLen-1] [docs/design/01 §3.2].
        const double t = static_cast<double> (i) / static_cast<double> (tableLen - 1);
        const double w = 0.42 - 0.5 * std::cos (kTwoPi * t) + 0.08 * std::cos (2.0 * kTwoPi * t);

        proto[i] = sinc * w;
    }

    // --- 2. Minimum-phase conversion via the real cepstrum (Oppenheim & Schafer).
    std::vector<Cplx> spec, ceps, logMin, hMinSpec, hMin;
    {
        std::vector<Cplx> x (N);
        for (std::size_t i = 0; i < N; ++i)
            x[i] = Cplx (proto[static_cast<std::size_t> (i)], 0.0);

        dft (x, spec);

        // log-magnitude spectrum (guard the magnitude floor for the windowed stopband).
        std::vector<Cplx> logMag (N);
        for (std::size_t k = 0; k < N; ++k)
        {
            double mag = std::abs (spec[k]);
            if (mag < 1.0e-12) mag = 1.0e-12;
            logMag[k] = Cplx (std::log (mag), 0.0);
        }

        idft (logMag, ceps);                       // real cepstrum

        // Minimum-phase lifter: keep c[0] & c[N/2], double the causal half, zero the rest.
        std::vector<Cplx> lifted (N, Cplx (0.0, 0.0));
        const std::size_t half = N / 2;
        lifted[0] = ceps[0];
        for (std::size_t n = 1; n < half; ++n)
            lifted[n] = ceps[n] * 2.0;
        lifted[half] = ceps[half];
        // n in (half, N) stay zero.

        dft (lifted, logMin);                      // log of minimum-phase spectrum
        for (std::size_t k = 0; k < N; ++k)
            logMin[k] = std::exp (logMin[k]);      // minimum-phase spectrum
        idft (logMin, hMin);                       // minimum-phase impulse response
    }

    // --- 3. Integrate to the band-limited step (running sum of the impulse response).
    std::vector<double> blep (N, 0.0);
    double run = 0.0;
    for (std::size_t i = 0; i < N; ++i)
    {
        run += hMin[i].real();
        blep[i] = run;
    }

    // Normalize so the step rises 0 -> 1 (the asymptotic DC value is the final sum).
    const double finalLevel = blep[N - 1];
    if (std::abs (finalLevel) > 1.0e-12)
        for (std::size_t i = 0; i < N; ++i)
            blep[i] /= finalLevel;

    // --- 4. Residual = blep - 1 (rises from ~ -1 up to 0).
    residual_.resize (N);
    for (std::size_t i = 0; i < N; ++i)
        residual_[i] = static_cast<float> (blep[i] - 1.0);
}

bool MinBlepTable::isBuilt() const noexcept
{
    return ! residual_.empty();
}

float MinBlepTable::residualAt (int tableIndex) const noexcept
{
    return residual_[static_cast<std::size_t> (tableIndex)];
}

int MinBlepTable::length() const noexcept
{
    return 2 * kZeroCrossings;            // residual length in BASE samples
}

int MinBlepTable::tableLength() const noexcept
{
    return 2 * kZeroCrossings * kOversampling;
}

// -----------------------------------------------------------------------------
// MinBlepApplicator
// -----------------------------------------------------------------------------

void MinBlepApplicator::prepare (const MinBlepTable& table, double /*sampleRate*/)
{
    table_ = &table;
    // ring_ length >= MinBlepTable::length() [docs/design/01 §3.3]. +1 head room so a
    // step scheduled at the current head writes [0, length()) without wrapping onto the
    // sample being popped this call.
    ringSize_ = table.length() + 1;
    ring_.assign (static_cast<std::size_t> (ringSize_), 0.0f);
    head_  = 0;
    level_ = 0.0f;
}

void MinBlepApplicator::reset() noexcept
{
    for (float& v : ring_) v = 0.0f;
    head_  = 0;
    level_ = 0.0f;
}

void MinBlepApplicator::scheduleStep (float amp, float frac) noexcept
{
    if (table_ == nullptr)
        return;

    const int   os       = MinBlepTable::kOversampling;
    const int   baseLen  = table_->length();          // residual length in base samples
    const int   tableLen = table_->tableLength();

    // Sub-sample placement: a step landing `frac` into the current sample reads the
    // residual starting `(1 - frac)` of a base sample in, mapped onto the 64x grid.
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    const int phase = static_cast<int> ((1.0f - frac) * static_cast<float> (os) + 0.5f);

    // Overlap-add amp * residual into the ring at successive base-sample positions.
    for (int n = 0; n < baseLen; ++n)
    {
        const int idx = n * os + phase;
        if (idx >= tableLen)
            break;
        const int slot = (head_ + n) % ringSize_;
        ring_[static_cast<std::size_t> (slot)] += amp * table_->residualAt (idx);
    }

    // The step's asymptotic value is taken immediately into the held DC; the residual
    // (which starts near -amp) cancels it until the band-limited edge plays out.
    level_ += amp;
}

float MinBlepApplicator::next() noexcept
{
    const int   slot = head_;
    const float out  = level_ + ring_[static_cast<std::size_t> (slot)];
    ring_[static_cast<std::size_t> (slot)] = 0.0f;     // consumed; clear for reuse
    head_ = (head_ + 1) % ringSize_;
    return out;
}

} // namespace mw101::dsp
