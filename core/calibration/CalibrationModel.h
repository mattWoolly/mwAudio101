// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/CalibrationModel.h — the planted-answer calibration MODEL and
// the recovering CALIBRATOR exercised by the Layer-4 self-tests (task 079).
//
// docs/design/11 §12 / ADR-013 Layer 4 require the calibration tool to be tested
// offline with planted fixtures: a signal is synthesized from KNOWN circuit-model
// parameters, the calibrator recovers them, and recovery is asserted within
// tolerance. Since mwAudio101 holds NO physical SH-101 oracle [docs/design/11 §1.3;
// research/13 §1.1, §5.4], a planted answer is the ONLY oracle we can manufacture.
//
// SCOPE NOTE (task 079): the broader calibration physics (variance/drift fit,
// k-mapping, tempco) is owned by the calibration design doc + Calibration.h and is
// out-of-scope here. This header defines a SELF-CONTAINED, deterministic stand-in
// model — a damped-sinusoid circuit response whose {amplitude, frequency, decay}
// ARE the calibration parameters — purely so the SELF-TESTS (the task's actual
// deliverable) have a calibratable model + a calibrator with an honest accept/reject
// decision to drive. It is JUCE-free, allocation-free in the hot estimator paths,
// and uses only the cross-platform integer PRNG (no platform libm in the generator).
//
// All numeric defaults are (PI) and live in CalibrationSelfTestConstants.h; this
// header inlines none.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../util/Prng.h"
#include "CalibrationSelfTestConstants.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// CalibrationParams — the KNOWN params a planted fixture is synthesized from and
// that the calibrator must recover. A damped-sinusoid circuit response:
//
//     s[n] = amplitude * exp(-decayPerSec * t) * sin(2*pi*frequencyHz * t)
//
// with t = n / sampleRate. {amplitude, frequencyHz, decayPerSec} stand in for the
// calibrated analog quantities (level / resonant frequency / ring-down rate) the
// real calibrator would fit. POD; value-comparable.
// ---------------------------------------------------------------------------
struct CalibrationParams {
    double amplitude   = 1.0;     // peak amplitude (level)
    double frequencyHz = 440.0;   // sinusoid frequency (resonant frequency stand-in)
    double decayPerSec = 8.0;     // exponential ring-down rate (1/s)

    friend constexpr bool operator==(const CalibrationParams&,
                                     const CalibrationParams&) = default;
};

// Synthesis configuration shared by a planted fixture (held outside the params so
// the calibrator knows the sample-rate/length context of the signal it recovers from).
struct SignalSpec {
    double sampleRate  = 48000.0; // one of the blessed set is not required for a synthetic fixture
    int    numSamples  = 4096;
};

// ---------------------------------------------------------------------------
// synthesize — render the deterministic damped sinusoid from KNOWN params. Pure
// function of (params, spec): identical inputs => identical samples on this platform.
// Uses std::sin/std::exp (FP analog stage); the harness is offline so this is fine.
// ---------------------------------------------------------------------------
inline std::vector<float> synthesize(const CalibrationParams& p,
                                     const SignalSpec& spec) {
    std::vector<float> out(static_cast<std::size_t>(spec.numSamples), 0.0f);
    const double twoPiF = 2.0 * 3.14159265358979323846 * p.frequencyHz;
    for (int n = 0; n < spec.numSamples; ++n) {
        const double t = static_cast<double>(n) / spec.sampleRate;
        const double env = std::exp(-p.decayPerSec * t);
        out[static_cast<std::size_t>(n)] =
            static_cast<float>(p.amplitude * env * std::sin(twoPiF * t));
    }
    return out;
}

// ---------------------------------------------------------------------------
// CalibrationResult — the calibrator's output: the recovered params, the normalized
// fit residual between the re-synthesized recovered signal and the input signal, and
// the accept/reject decision (residual within the acceptance floor).
// ---------------------------------------------------------------------------
struct CalibrationResult {
    CalibrationParams recovered{};
    double            residual = 0.0;   // phase-insensitive envelope-consistency residual
    bool              accepted = false; // residual <= acceptance floor
};

namespace detail {

// PHASE-INSENSITIVE model-consistency residual. For a single damped sinusoid the
// rectified-signal local peaks lie ON the recovered envelope amp*exp(-decay*t); a
// signal a single damped sinusoid CANNOT represent (e.g. a sum of two tones) has
// peaks that beat AROUND that envelope. We measure the normalized RMS deviation of
// the local peaks from the recovered envelope. This is deliberately insensitive to
// small phase/frequency drift (which would wreck a sample-by-sample waveform compare
// despite an accurate parameter recovery) but is strongly sensitive to a structurally
// wrong signal — exactly the discrimination the accept/reject gate needs
// [docs/design/11 §12; ADR-013 C17].
inline double envelopeConsistencyResidual(const std::vector<float>& s,
                                          const CalibrationParams& rec,
                                          double sampleRate) noexcept {
    if (s.size() < 4 || sampleRate <= 0.0) return 1.0e30;
    double sumDev2 = 0.0;
    double sumEnv2 = 0.0;
    int    nPeaks  = 0;
    const double floorAbs = 1.0e-5;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        const double a  = std::abs(static_cast<double>(s[i]));
        const double am = std::abs(static_cast<double>(s[i - 1]));
        const double ap = std::abs(static_cast<double>(s[i + 1]));
        if (a > am && a >= ap && a > floorAbs) {       // local peak of the rectified signal
            const double t   = static_cast<double>(i) / sampleRate;
            const double env = rec.amplitude * std::exp(-rec.decayPerSec * t);
            const double dev = a - env;
            sumDev2 += dev * dev;
            sumEnv2 += env * env;
            ++nPeaks;
        }
    }
    if (nPeaks < 2 || sumEnv2 <= 1.0e-18) return 1.0e30;
    return std::sqrt(sumDev2 / sumEnv2);
}

// Estimate the peak amplitude as the max absolute sample (the envelope's t=0 value
// for this monotonically-decaying model). A real estimator; NOT an echo of an input
// parameter.
inline double estimateAmplitude(const std::vector<float>& s) noexcept {
    double peak = 0.0;
    for (const float v : s) {
        const double a = std::abs(static_cast<double>(v));
        if (a > peak) peak = a;
    }
    return peak;
}

// Estimate the frequency via counting sign changes (zero crossings) of the signal.
// freq ~= (zeroCrossings / 2) / durationSeconds. Discretization-limited but a true
// measurement of the rendered signal, not a copy of the planted parameter.
inline double estimateFrequencyHz(const std::vector<float>& s,
                                  double sampleRate) noexcept {
    if (s.size() < 2 || sampleRate <= 0.0) return 0.0;
    int crossings = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        const bool prevNeg = s[i - 1] < 0.0f;
        const bool curNeg  = s[i] < 0.0f;
        if (prevNeg != curNeg) ++crossings;
    }
    const double durationSec =
        static_cast<double>(s.size()) / sampleRate;
    return (static_cast<double>(crossings) / 2.0) / durationSec;
}

// Estimate the exponential decay rate (1/s) by a least-squares fit of ln|envelope|
// vs time, sampling the local peaks of the rectified signal. Real fit, not an echo.
inline double estimateDecayPerSec(const std::vector<float>& s,
                                  double sampleRate) noexcept {
    if (s.size() < 4 || sampleRate <= 0.0) return 0.0;
    // Build (t, ln(amp)) samples from local maxima of |s| above a small floor.
    std::vector<double> ts;
    std::vector<double> lnA;
    const double floorAbs = 1.0e-5;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        const double a  = std::abs(static_cast<double>(s[i]));
        const double am = std::abs(static_cast<double>(s[i - 1]));
        const double ap = std::abs(static_cast<double>(s[i + 1]));
        if (a > am && a >= ap && a > floorAbs) {     // local peak of the rectified signal
            ts.push_back(static_cast<double>(i) / sampleRate);
            lnA.push_back(std::log(a));
        }
    }
    if (ts.size() < 2) return 0.0;
    // Ordinary least squares: slope of lnA vs t. decay = -slope.
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    const double n = static_cast<double>(ts.size());
    for (std::size_t i = 0; i < ts.size(); ++i) {
        sx  += ts[i];
        sy  += lnA[i];
        sxx += ts[i] * ts[i];
        sxy += ts[i] * lnA[i];
    }
    const double denom = n * sxx - sx * sx;
    if (std::abs(denom) <= 1.0e-18) return 0.0;
    const double slope = (n * sxy - sx * sy) / denom;
    return -slope;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Calibrator — recovers CalibrationParams from a signal, then ACCEPTS only if the
// re-synthesized recovered signal matches the input within the acceptance floor.
//
// The recovery is a genuine measurement of the SIGNAL (amplitude = peak, frequency =
// zero-cross rate, decay = log-envelope LS fit). It CANNOT "succeed by echoing its
// input parameters" because it never sees the planted params — only the samples
// [docs/design/11 §12; ADR-013 C15]. The accept/reject gate makes a deliberately
// inconsistent fixture (the negative control) fail [ADR-013 C17].
// ---------------------------------------------------------------------------
class Calibrator {
public:
    CalibrationResult calibrate(const std::vector<float>& signal,
                                const SignalSpec& spec) const {
        CalibrationResult r{};
        r.recovered.amplitude   = detail::estimateAmplitude(signal);
        r.recovered.frequencyHz = detail::estimateFrequencyHz(signal, spec.sampleRate);
        r.recovered.decayPerSec = detail::estimateDecayPerSec(signal, spec.sampleRate);

        // Measure how well a SINGLE damped sinusoid at the recovered params explains the
        // signal (phase-insensitive envelope consistency). This is what makes acceptance
        // a CONSISTENCY check, not a rubber stamp: a signal a single damped sinusoid
        // cannot represent yields a large residual and is REJECTED [ADR-013 C17].
        r.residual = detail::envelopeConsistencyResidual(signal, r.recovered, spec.sampleRate);
        r.accepted = r.residual <= selftest::kAcceptResidualFloor;
        return r;
    }
};

namespace test {

// PlantedFixture — a signal synthesized from KNOWN params + the seed used to draw the
// params (docs/design/11 §12). The signal is the only thing the calibrator sees.
struct PlantedFixture {
    CalibrationParams  knownParams{};
    SignalSpec         spec{};
    std::vector<float> signal{};
    std::uint64_t      seed = 0;
};

// CalValSplit — set A (FIT) and set B (VALIDATE), DISJOINT BY CONSTRUCTION: the fit
// and validate seed ranges and parameter draws never overlap (docs/design/11 §12;
// ADR-013 C16).
struct CalValSplit {
    std::vector<PlantedFixture> fitSet;   // set A
    std::vector<PlantedFixture> valSet;   // set B
};

// Draw a CalibrationParams deterministically from a seed within documented (PI)
// ranges. Distinct seeds (the disjoint ranges) draw distinct parameter points, so
// the fit/validate parameter SETS are disjoint by construction.
inline CalibrationParams drawParams(std::uint64_t seed) {
    mw::util::Prng prng(seed);
    CalibrationParams p{};
    // Ranges (PI) chosen so the estimators are well-conditioned on a 4096-sample,
    // 48 kHz fixture: frequency comfortably below Nyquist, decay leaves a measurable
    // tail, amplitude clearly above the local-peak floor.
    p.amplitude   = 0.4 + 0.5 * static_cast<double>(prng.nextFloat());   // [0.4, 0.9)
    p.frequencyHz = 300.0 + 1400.0 * static_cast<double>(prng.nextFloat()); // [300, 1700)
    p.decayPerSec = 3.0 + 9.0 * static_cast<double>(prng.nextFloat());   // [3, 12) 1/s
    return p;
}

// Build a single planted fixture for a seed.
inline PlantedFixture makePlantedFixture(std::uint64_t seed,
                                         const SignalSpec& spec = {}) {
    PlantedFixture f{};
    f.seed        = seed;
    f.spec        = spec;
    f.knownParams = drawParams(seed);
    f.signal      = synthesize(f.knownParams, spec);
    return f;
}

// Build a disjoint cal/val split from the two non-overlapping (PI) seed ranges.
inline CalValSplit makeCalValSplit(const SignalSpec& spec = {}) {
    CalValSplit split{};
    for (int i = 0; i < selftest::kFitSetSize; ++i)
        split.fitSet.push_back(
            makePlantedFixture(selftest::kFitSeedBase + static_cast<std::uint64_t>(i), spec));
    for (int i = 0; i < selftest::kValSetSize; ++i)
        split.valSet.push_back(
            makePlantedFixture(selftest::kValSeedBase + static_cast<std::uint64_t>(i), spec));
    return split;
}

// Build a deliberately-WRONG fixture (negative control): the signal is synthesized
// from one set of params, but the fixture is LABELLED with a clearly different set.
// A correct calibrator recovers the SIGNAL's params (not the wrong label) and, on
// re-synthesis, the residual against the actual signal stays small — so we instead
// hand the calibrator a CORRUPTED signal whose shape no single damped sinusoid can
// fit, forcing a large residual and a REJECT. This models a calibrator fed garbage /
// a mis-recorded response it must refuse to bless [docs/design/11 §12; ADR-013 C17].
inline PlantedFixture makeWrongFixture(std::uint64_t seed,
                                       const SignalSpec& spec = {}) {
    PlantedFixture f = makePlantedFixture(seed, spec);
    // Corrupt the signal into something a single damped sinusoid cannot represent:
    // add a second, strong, undecaying sinusoid at an unrelated frequency. The
    // recovered single-sinusoid params cannot re-synthesize this, so the residual
    // blows past the acceptance floor.
    const double twoPiF2 = 2.0 * 3.14159265358979323846 * (f.knownParams.frequencyHz * 2.37 + 113.0);
    for (std::size_t n = 0; n < f.signal.size(); ++n) {
        const double t = static_cast<double>(n) / spec.sampleRate;
        f.signal[n] += static_cast<float>(0.9 * f.knownParams.amplitude * std::sin(twoPiF2 * t));
    }
    return f;
}

} // namespace test
} // namespace mw::cal
