// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the offline TPT/ZDF 4-pole ladder reference oracle
// (task 034). Test-case names begin with "vcf-tpt" so `-R vcf-tpt` selects them.
// They verify the analytic properties the oracle MUST reproduce: the
// H(0) = 1/(1+k) bass droop (docs/design/02 §8.2, ADR-003 F-07), the 24 dB/oct
// (4-pole) low-resonance roll-off (§8.2), and self-oscillation near cutoff as
// k -> 4 (§8.2, ADR-003 F-05/F-13).
//
// The oracle is LINEAR (no embedded tanh), double precision, base rate.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/LadderReferenceTPT.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// Drive the oracle with a sine of amplitude 1 for `cycles` periods after a warmup
// of `warmupCycles` periods, and return the steady-state peak output amplitude.
double measureMagnitude(mw::dsp::LadderReferenceTPT& f, double fsHz, double freqHz,
                        int settleSamples) {
    f.reset();
    const double w = 2.0 * kPi * freqHz / fsHz;
    // Settle the transient.
    double phase = 0.0;
    for (int n = 0; n < settleSamples; ++n) {
        (void) f.processSample(std::sin(phase));
        phase += w;
    }
    // Measure peak over an integer-ish number of further cycles.
    const int measSamples = static_cast<int>(std::ceil(fsHz / freqHz)) * 8;
    double peak = 0.0;
    for (int n = 0; n < measSamples; ++n) {
        const double y = f.processSample(std::sin(phase));
        phase += w;
        peak = std::max(peak, std::abs(y));
    }
    return peak;
}

// Measured DC gain: feed a constant 1.0 until the integrator states settle, return
// the steady output.
double measureDcGain(mw::dsp::LadderReferenceTPT& f, int settleSamples) {
    f.reset();
    double y = 0.0;
    for (int n = 0; n < settleSamples; ++n) {
        y = f.processSample(1.0);
    }
    return y;
}

} // namespace

// --- Acceptance: bass droop H(0) = 1/(1+k) across a k sweep (§8.2, ADR-003 F-07) ---
TEST_CASE("vcf-tpt: DC gain equals 1/(1+k) across a resonance sweep", "[vcf-tpt]") {
    mw::dsp::LadderReferenceTPT f;
    f.prepare(48000.0);
    f.setCutoffHz(1000.0);

    // Settle long relative to the slowest pole at this cutoff.
    const int settle = 200000;
    for (const double k : {0.0, 0.5, 1.0, 2.0, 3.0, 3.9}) {
        f.setResonanceK(k);
        const double measured = measureDcGain(f, settle);
        const double expected = 1.0 / (1.0 + k);
        INFO("k = " << k << " measured = " << measured << " expected = " << expected);
        REQUIRE(std::abs(measured - expected) < 1.0e-3);
    }
}

// Negative control: a NON-zero resonance MUST droop below the k=0 unity DC gain.
TEST_CASE("vcf-tpt: DC gain droops below unity when resonance is engaged", "[vcf-tpt]") {
    mw::dsp::LadderReferenceTPT f;
    f.prepare(48000.0);
    f.setCutoffHz(800.0);
    const int settle = 200000;

    f.setResonanceK(0.0);
    const double g0 = measureDcGain(f, settle);
    f.setResonanceK(3.0);
    const double g3 = measureDcGain(f, settle);

    REQUIRE(std::abs(g0 - 1.0) < 1.0e-3); // k=0 => unity at DC
    REQUIRE(g3 < g0);                     // engaging resonance droops the bass
    REQUIRE(g3 > 0.0);
}

// --- Acceptance: low-resonance 24 dB/oct (4-pole) roll-off one octave above fc ---
TEST_CASE("vcf-tpt: low resonance rolls off at 24 dB/oct one octave above cutoff",
          "[vcf-tpt]") {
    const double fs = 192000.0; // generous headroom so the test octaves stay << Nyquist
    const double fc = 500.0;
    mw::dsp::LadderReferenceTPT f;
    f.prepare(fs);
    f.setCutoffHz(fc);
    f.setResonanceK(0.0); // linear, no resonant peak

    const int settle = 120000;
    // The -24 dB/oct asymptote only holds well ABOVE the corner; measure the slope
    // between +3 and +4 octaves where the four-pole asymptote is clean (and both
    // points are still far below Nyquist at this sample rate).
    const double mLow  = measureMagnitude(f, fs, 8.0 * fc, settle);  // +3 octaves (4 kHz)
    const double mHigh = measureMagnitude(f, fs, 16.0 * fc, settle); // +4 octaves (8 kHz)

    const double slopeDbPerOct =
        20.0 * std::log10(mHigh / mLow); // one octave apart => dB/oct directly

    INFO("slope = " << slopeDbPerOct << " dB/oct (mLow=" << mLow << " mHigh=" << mHigh << ")");
    REQUIRE(slopeDbPerOct < -23.0);
    REQUIRE(slopeDbPerOct > -25.0);
}

// --- Acceptance: as k -> 4 the model self-oscillates near the cutoff frequency ---
TEST_CASE("vcf-tpt: as k approaches 4 the model self-oscillates near cutoff",
          "[vcf-tpt]") {
    const double fs = 48000.0;
    const double fc = 1000.0;
    mw::dsp::LadderReferenceTPT f;
    f.prepare(fs);
    f.setCutoffHz(fc);

    // Below threshold with no input: energy decays toward zero (stable).
    f.setResonanceK(3.5);
    f.reset();
    double belowPeak = 0.0;
    // Kick it once, then let it run with zero input.
    (void) f.processSample(1.0);
    for (int n = 0; n < 200000; ++n) {
        belowPeak = std::max(belowPeak, std::abs(f.processSample(0.0)));
    }
    // The last stretch should be much quieter than a sustained oscillation.
    double belowTail = 0.0;
    for (int n = 0; n < 4000; ++n) {
        belowTail = std::max(belowTail, std::abs(f.processSample(0.0)));
    }
    REQUIRE(belowTail < 0.05 * belowPeak + 1.0e-6); // decaying, not sustained

    // At/just below the k=4 threshold the loop sustains an oscillation from a kick.
    f.setResonanceK(3.999);
    f.reset();
    (void) f.processSample(1.0); // impulse kick
    // Let it develop.
    for (int n = 0; n < 100000; ++n) {
        (void) f.processSample(0.0);
    }
    // Now estimate the oscillation frequency by counting zero crossings over a window.
    const int window = 48000; // 1 second
    std::vector<double> buf;
    buf.reserve(window);
    for (int n = 0; n < window; ++n) {
        buf.push_back(f.processSample(0.0));
    }
    double sustainPeak = 0.0;
    for (const double v : buf) {
        sustainPeak = std::max(sustainPeak, std::abs(v));
    }
    // It actually sustains a non-trivial oscillation (does NOT decay to ~0).
    REQUIRE(sustainPeak > 1.0e-3);

    int crossings = 0;
    for (std::size_t n = 1; n < buf.size(); ++n) {
        if ((buf[n - 1] <= 0.0 && buf[n] > 0.0)) ++crossings;
    }
    // crossings per second of upward zero crossings == oscillation frequency in Hz.
    const double measuredHz = static_cast<double>(crossings) * (fs / window);
    INFO("self-osc measured = " << measuredHz << " Hz, expected near " << fc
                                << " Hz, sustainPeak = " << sustainPeak);
    // Self-oscillation sits near the cutoff frequency (within 15%).
    REQUIRE(measuredHz > 0.85 * fc);
    REQUIRE(measuredHz < 1.15 * fc);
}

// --- Determinism: same inputs -> bit-identical output (oracle is the bless baseline) ---
TEST_CASE("vcf-tpt: processSample is deterministic run-to-run", "[vcf-tpt]") {
    auto run = []() {
        mw::dsp::LadderReferenceTPT f;
        f.prepare(44100.0);
        f.setCutoffHz(1200.0);
        f.setResonanceK(2.5);
        f.reset();
        double acc = 0.0;
        double phase = 0.0;
        for (int n = 0; n < 4096; ++n) {
            acc += f.processSample(std::sin(phase));
            phase += 2.0 * kPi * 220.0 / 44100.0;
        }
        return acc;
    };
    REQUIRE(run() == run()); // bit-identical (no NaN, no run-to-run drift)
}
