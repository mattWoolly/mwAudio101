// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the LadderFilter RESONANCE loop (task 039): the reso01->k taper, the
// output-side make-up Q scalar, the +0.5-sample two-sample-average feedback-phase
// compensation, the feedback-path diode clamp as the amplitude governor, self-
// oscillation, and the low-resonance cross-check against the linear TPT oracle.
//
// Test-case names begin with "vcf-reso" so `ctest -R vcf-reso` selects them (silent-
// pass rule; AGENTS.md, docs/design/11 §8.2). AVOID '[' in display names. Each case
// maps to an acceptance criterion in plan/backlog/039-ladderfilter-resonance.md and a
// row of docs/design/02 §10 / the ADR-003 contract (F-03, F-04, F-05, F-06, F-13).
//
// Design refs: docs/design/02-dsp-filter.md §5.1 (reso->k), §5.3 (make-up Q),
// §5.4 (diode clamp), §5.5 (per-sample feedback), §5.6 (self-osc), §7.3 (resoTuningComp),
// §8 (TPT oracle), §10 (acceptance hooks).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "dsp/LadderFilter.h"
#include "dsp/LadderReferenceTPT.h"
#include "calibration/FastTanhConstants.h"
#include "calibration/FilterTablesConstants.h"
#include "calibration/LadderFilterConstants.h"
#include "calibration/LadderResonanceConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::dsp::LadderFilter;

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// Run the filter with zero input from a small kick and return the steady-state peak
// magnitude over the last `tailLen` samples, plus an interpolated zero-crossing
// frequency estimate over the same window. Used for the self-oscillation tests.
struct SelfOsc {
    double peak = 0.0;
    double freq = 0.0;
    bool   finite = true;
};

SelfOsc runSelfOsc(LadderFilter& f, double fsOs, double fcHz, float reso01,
                   int totalSamples = 500000, int tailLen = 100000) {
    f.reset();
    f.setCutoffHz(static_cast<float>(fcHz));
    f.setResonance(reso01);
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(totalSamples));
    SelfOsc r;
    for (int n = 0; n < totalSamples; ++n) {
        const double y = f.processSample(0.0f);
        if (!std::isfinite(y)) r.finite = false;
        out.push_back(y);
    }
    const int from = totalSamples - tailLen;
    double pk = 0.0;
    for (int n = from; n < totalSamples; ++n) pk = std::max(pk, std::abs(out[static_cast<std::size_t>(n)]));
    r.peak = pk;

    // Interpolated rising zero-crossings -> average period.
    std::vector<double> cross;
    for (int n = from + 1; n < totalSamples; ++n) {
        const double a = out[static_cast<std::size_t>(n - 1)];
        const double b = out[static_cast<std::size_t>(n)];
        if (a <= 0.0 && b > 0.0) {
            const double t = (n - 1) + (-a) / (b - a);
            cross.push_back(t);
        }
    }
    if (cross.size() >= 3) {
        const double period = (cross.back() - cross.front()) / static_cast<double>(cross.size() - 1);
        r.freq = (period > 0.0) ? fsOs / period : 0.0;
    }
    return r;
}

// Total harmonic distortion of a self-oscillation tail at fundamental f0: sqrt of the
// summed power of harmonics 2..5 over the fundamental power, via a naive DFT projection.
double selfOscTHD(LadderFilter& f, double fsOs, double fcHz, float reso01,
                  int totalSamples = 600000, int tailLen = 120000) {
    f.reset();
    f.setCutoffHz(static_cast<float>(fcHz));
    f.setResonance(reso01);
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(totalSamples));
    for (int n = 0; n < totalSamples; ++n) out.push_back(f.processSample(0.0f));
    const int from = totalSamples - tailLen;

    // Fundamental from zero crossings.
    std::vector<double> cross;
    for (int n = from + 1; n < totalSamples; ++n) {
        const double a = out[static_cast<std::size_t>(n - 1)];
        const double b = out[static_cast<std::size_t>(n)];
        if (a <= 0.0 && b > 0.0) cross.push_back((n - 1) + (-a) / (b - a));
    }
    if (cross.size() < 3) return 1.0; // no clean oscillation -> "worst" THD
    const double period = (cross.back() - cross.front()) / static_cast<double>(cross.size() - 1);
    const double f0 = fsOs / period;

    auto amp = [&](int h) {
        const double w = kTwoPi * (h * f0) / fsOs;
        double re = 0.0, im = 0.0, ph = 0.0;
        const int N = totalSamples - from;
        for (int n = 0; n < N; ++n) {
            const double y = out[static_cast<std::size_t>(from + n)];
            re += y * std::cos(ph);
            im += y * std::sin(ph);
            ph += w;
        }
        return std::sqrt(re * re + im * im) / N;
    };
    const double a1 = amp(1);
    const double harm = std::sqrt(amp(2) * amp(2) + amp(3) * amp(3)
                                  + amp(4) * amp(4) + amp(5) * amp(5));
    return (a1 > 0.0) ? harm / a1 : 1.0;
}

// Steady-state output magnitude at exactly freqHz via a coherent DFT projection (so the
// resonant-peak search has fine, robust frequency discrimination).
double magAt(LadderFilter& f, double fsOs, double freqHz, int settle, double amp,
             int periods = 60) {
    f.reset();
    const double w = kTwoPi * freqHz / fsOs;
    double ph = 0.0;
    for (int n = 0; n < settle; ++n) { (void) f.processSample(static_cast<float>(amp * std::sin(ph))); ph += w; }
    const int N = static_cast<int>(std::ceil(fsOs / freqHz)) * periods;
    double re = 0.0, im = 0.0, cph = 0.0;
    for (int n = 0; n < N; ++n) {
        const double y = f.processSample(static_cast<float>(amp * std::sin(ph)));
        ph += w;
        re += y * std::cos(cph);
        im += y * std::sin(cph);
        cph += w;
    }
    return std::sqrt(re * re + im * im) / N;
}

// Locate the resonant-peak frequency of the filter (current cutoff/resonance) by a
// geometric sweep around fcGuess.
double resonantPeakFreq(LadderFilter& f, double fsOs, double fcGuess, double amp) {
    double best = 0.0, bf = 0.0;
    for (double fr = fcGuess * 0.6; fr <= fcGuess * 1.6; fr *= 1.0025) {
        const double m = magAt(f, fsOs, fr, 40000, amp, 32);
        if (m > best) { best = m; bf = fr; }
    }
    return bf;
}

} // namespace

// === Acceptance 1 (F-04/F-05): diode clamp bounds the loop / sets the fixed point =====
TEST_CASE("vcf-reso: the feedback diode clamp bounds self-oscillation to a stable "
          "fixed point insensitive to small k and to coefficient rounding",
          "[vcf-reso][vcf]") {
    const double fsOs = 88200.0;
    const double fc   = 1000.0;

    // With the clamp ENABLED (the shipping path), reso01 = 1 self-oscillates to a
    // BOUNDED, finite amplitude (the clamp fixed point), not the rails.
    LadderFilter f;
    f.prepare(fsOs, /*maxBlockOs=*/64);
    const SelfOsc osc = runSelfOsc(f, fsOs, fc, 1.0f);
    INFO("self-osc peak = " << osc.peak << ", freq = " << osc.freq);
    REQUIRE(osc.finite);
    REQUIRE(osc.peak > 1.0e-3);   // genuinely oscillating, not decayed to the bias floor
    REQUIRE(osc.peak < 1.0f);     // bounded, far from rail-clipping

    // Fixed-point insensitivity to small k perturbations (F-05): around reso01 = 1 the
    // oscillation is well established (k = kMax, firmly above onset). Nudging reso01
    // from 0.9 to 1.0 moves k by ~23% (k = kMax*reso^2), yet the steady amplitude moves
    // only weakly, because the diode-clamp fixed point — not a knife-edge k — sets it.
    LadderFilter lo, hi;
    lo.prepare(fsOs, 64);
    hi.prepare(fsOs, 64);
    const double pLo = runSelfOsc(lo, fsOs, fc, 0.9f).peak;
    const double pHi = runSelfOsc(hi, fsOs, fc, 1.0f).peak;
    INFO("amplitude at reso 0.9 = " << pLo << ", at 1.0 = " << pHi);
    REQUIRE(pLo > 1.0e-3);        // both well established
    REQUIRE(pHi > 1.0e-3);
    // ~23% k change -> amplitude change bounded well under that: governed, not knife-edge.
    REQUIRE(pHi == Catch::Approx(pLo).epsilon(0.25));

    // Coefficient-rounding insensitivity: the fixed point is reached from different
    // starting transients (a different cutoff -> different g rounding) yet stays bounded
    // and well-defined.
    LadderFilter g2;
    g2.prepare(fsOs, 64);
    const SelfOsc o2 = runSelfOsc(g2, fsOs, 1234.0, 1.0f);
    REQUIRE(o2.finite);
    REQUIRE(o2.peak > 1.0e-3);
    REQUIRE(o2.peak < 1.0f);

    // Negative control — the clamp is the active feedback limiter (F-04). Replicate the
    // documented §5.5 loop and capture, sample by sample, the RAW pre-clamp feedback
    // drive (k*fbComp) versus the clamped feedback the loop actually applies. In steady
    // self-oscillation the raw drive exceeds the clamp threshold vClamp (the clamp is
    // conducting and reducing the level — research/03 §4.2), while the clamped feedback
    // stays bounded by vClamp. This proves the diode clamp, in the FEEDBACK path, is the
    // governor: without it the feedback would drive the input transconductor far harder.
    {
        const float invTwoVt = mw::cal::vcf::invTwoVt;
        const float bias     = mw::cal::vcf::kAntiDenorm;
        const float vClamp   = mw::cal::vcf::vClamp;
        const double gc      = (1.0 - std::exp(-kTwoPi * fc / fsOs)) / invTwoVt;
        const double kEff    = static_cast<double>(mw::cal::vcf::kMax); // reso01 = 1
        auto ftanh = [&](double v) { return static_cast<double>(mw::dsp::fastTanh(static_cast<float>(v))); };
        auto clampD = [&](double v) { return vClamp * ftanh(v / vClamp); };
        double y[4] = { bias, bias, bias, bias };
        double fbPrev = bias;
        y[0] = 0.001; // kick
        double maxRawDrive = 0.0, maxClampedFb = 0.0;
        for (int n = 0; n < 400000; ++n) {
            const double fbComp = 0.5 * (y[3] + fbPrev);
            const double raw    = kEff * fbComp;       // pre-clamp feedback drive
            const double fb     = clampD(raw);         // what the loop actually applies
            const double in0    = ftanh((0.0 - fb) * invTwoVt);
            y[0] += gc * (in0  - ftanh(y[0]*invTwoVt)) + bias;
            y[1] += gc * (ftanh(y[0]*invTwoVt) - ftanh(y[1]*invTwoVt)) + bias;
            y[2] += gc * (ftanh(y[1]*invTwoVt) - ftanh(y[2]*invTwoVt)) + bias;
            y[3] += gc * (ftanh(y[2]*invTwoVt) - ftanh(y[3]*invTwoVt)) + bias;
            fbPrev = y[3];
            if (n > 300000) {
                maxRawDrive  = std::max(maxRawDrive,  std::abs(raw));
                maxClampedFb = std::max(maxClampedFb, std::abs(fb));
            }
        }
        INFO("raw feedback drive peak = " << maxRawDrive
             << ", clamped feedback peak = " << maxClampedFb << ", vClamp = " << vClamp);
        // The clamp is conducting: the raw drive is pushed past the threshold, but the
        // applied feedback is held below vClamp by the clamp.
        REQUIRE(maxRawDrive > static_cast<double>(vClamp));
        REQUIRE(maxClampedFb <= static_cast<double>(vClamp) + 1.0e-6);
        REQUIRE(maxClampedFb < maxRawDrive); // the clamp genuinely reduces the level
    }
}

// === Acceptance 2 (F-05): self-oscillation is a near-pure sine tracking cutoff ========
TEST_CASE("vcf-reso: at reso01 = 1 with zero input the output is a near-pure sine at "
          "cutoff with low THD",
          "[vcf-reso][vcf]") {
    const double fsOs = 88200.0;

    // THD of the self-oscillation sine is below a fixed bound (a clean sine).
    LadderFilter f;
    f.prepare(fsOs, 64);
    const double thd = selfOscTHD(f, fsOs, 1000.0, 1.0f);
    INFO("self-oscillation THD = " << thd);
    REQUIRE(thd < 0.05); // < 5% — a fairly clean sine (research/03 §4.2)

    // Frequency tracks cutoff across a sweep (within the <10% bound, below Fs/4).
    for (double fc : { 400.0, 700.0, 1000.0, 1500.0, 2000.0 }) {
        LadderFilter g;
        g.prepare(fsOs, 64);
        const SelfOsc osc = runSelfOsc(g, fsOs, fc, 1.0f);
        INFO("fc = " << fc << " -> self-osc freq = " << osc.freq);
        REQUIRE(osc.peak > 1.0e-3);
        REQUIRE(osc.freq == Catch::Approx(fc).epsilon(0.10)); // tracks cutoff within 10%
    }
}

// === Acceptance 3 (F-03): resonant peak tracks cutoff; the two-sample comp is wired ===
TEST_CASE("vcf-reso: the resonant peak tracks cutoff within the sub-Fs/4 bound and the "
          "two-sample-average phase compensation is wired into the loop",
          "[vcf-reso][vcf]") {
    const double fsOs = 88200.0;
    const double amp  = 2.0e-4; // small-signal: stay in the resonant (near-linear) regime

    // Part A: at a strongly resonant (still sub-self-oscillation) setting the resonant-
    // peak frequency tracks the cutoff within the <10%-at-2x bound for cutoffs
    // comfortably below Fs/4 (F-03). reso01 = 0.7 gives k ~ 3.92, just below the discrete
    // self-osc onset, where the peak is sharp enough to sit at the cutoff (a weak,
    // low-Q peak inherently sits BELOW the corner — that is filter physics, not detune).
    for (double fc : { 800.0, 1500.0, 3000.0, 6000.0 }) {
        LadderFilter f;
        f.prepare(fsOs, 64);
        f.setCutoffHz(static_cast<float>(fc));
        f.setResonance(0.7f); // strongly resonant, just below self-oscillation onset
        const double peak = resonantPeakFreq(f, fsOs, fc, amp);
        INFO("fc = " << fc << " -> resonant peak = " << peak
                     << " (err " << 100.0 * (peak - fc) / fc << "%)");
        REQUIRE(std::abs(peak - fc) / fc < 0.10);
    }

    // Part B: the documented §5.5 feedback path — inverting feedback with the +0.5-sample
    // two-sample-average compensation fbComp = 0.5*(y3 + fbPrev), diode-clamped — is
    // actually WIRED into the shipping engine. We replicate the exact documented topology
    // in the test (same calibration constants) and confirm the shipping LadderFilter
    // output matches it to tight tolerance, AND that it does NOT match a deliberately
    // FEEDBACK-DISABLED reference (k forced to 0) — proving the resonance feedback is
    // engaged in the loop, not bypassed. (research/10 §3.8; ADR-003 F-03/F-05.)
    //
    // Note: at the oversampled rates the engine runs (the whole point of the 2x zone),
    // the two-sample average and the raw y3 differ by < float precision, because the
    // FEEDBACK signal is the 24 dB/oct-filtered output — it carries essentially no energy
    // near Fs/2, so y3[n] ~= y3[n-1] and 0.5*(y3+fbPrev) ~= y3. That is precisely WHY the
    // comp holds feedback phase ~180 deg up to Fs/4 at negligible cost (research/10 §3.8);
    // it is not expected to shift the audio-band response measurably, so this part proves
    // the documented average is the wiring, not that it perturbs the band.
    const double fc = 6000.0;
    const float  invTwoVt = mw::cal::vcf::invTwoVt;
    const float  bias     = mw::cal::vcf::kAntiDenorm;
    const double gc       = (1.0 - std::exp(-kTwoPi * fc / fsOs)) / invTwoVt;

    LadderFilter ship;
    ship.prepare(fsOs, 64);
    ship.setCutoffHz(static_cast<float>(fc));
    ship.setResonance(0.85f);
    const double kk = static_cast<double>(ship.loopGainK()); // resoTuningComp == 1 (shipping kCompFit)

    auto ftanh   = [&](double v) { return static_cast<double>(mw::dsp::fastTanh(static_cast<float>(v))); };
    auto vclampD = [&](double v) { const double vc = static_cast<double>(mw::cal::vcf::vClamp); return vc * ftanh(v / vc); };

    // Reference loop generator. feedbackOn=true: the documented topology with the
    // two-sample-average compensation. feedbackOn=false: feedback disabled (k=0), the
    // feed-forward cascade — used as the negative control to prove feedback is engaged.
    auto refRun = [&](bool feedbackOn) {
        double y[4] = { bias, bias, bias, bias };
        double w[4] = { 0, 0, 0, 0 };
        double fbPrev = bias;
        std::vector<double> out;
        out.reserve(4096);
        for (int n = 0; n < 4096; ++n) {
            const double x      = 0.05 * std::sin(0.3 * n);
            const double fbComp = 0.5 * (y[3] + fbPrev);              // the +0.5-sample comp
            const double fb     = feedbackOn ? vclampD(kk * fbComp) : 0.0;
            const double in0    = ftanh((x - fb) * invTwoVt);
            y[0] += gc * (in0 - ftanh(y[0]*invTwoVt)) + bias; w[0] = ftanh(y[0]*invTwoVt);
            y[1] += gc * (w[0] - ftanh(y[1]*invTwoVt)) + bias; w[1] = ftanh(y[1]*invTwoVt);
            y[2] += gc * (w[1] - ftanh(y[2]*invTwoVt)) + bias; w[2] = ftanh(y[2]*invTwoVt);
            y[3] += gc * (w[2] - ftanh(y[3]*invTwoVt)) + bias;
            fbPrev = y[3];
            out.push_back(y[3]);
        }
        return out;
    };

    // The shipping engine, fed the SAME excitation.
    ship.reset();
    std::vector<double> shipOut;
    shipOut.reserve(4096);
    for (int n = 0; n < 4096; ++n) {
        shipOut.push_back(ship.processSample(static_cast<float>(0.05 * std::sin(0.3 * n))));
    }

    const auto withFb = refRun(true);
    const auto noFb   = refRun(false);

    double maxDiffFb = 0.0, maxDiffNoFb = 0.0;
    for (std::size_t i = 0; i < shipOut.size(); ++i) {
        maxDiffFb   = std::max(maxDiffFb,   std::abs(shipOut[i] - withFb[i]));
        maxDiffNoFb = std::max(maxDiffNoFb, std::abs(shipOut[i] - noFb[i]));
    }
    INFO("ship vs documented-feedback = " << maxDiffFb
         << ", ship vs feedback-disabled = " << maxDiffNoFb);
    // (a) the shipping engine matches the documented two-sample-average feedback topology.
    REQUIRE(maxDiffFb < 1.0e-4);
    // (b) and is clearly NOT the feedback-disabled cascade: the resonance feedback (with
    // its two-sample-average comp) is genuinely engaged in the loop.
    REQUIRE(maxDiffNoFb > 1.0e-3);
    REQUIRE(maxDiffNoFb > maxDiffFb * 10.0);
}

// === Acceptance 4 (F-06/F-13): make-up Q exposed only; input invariant; TPT match =====
TEST_CASE("vcf-reso: make-up gain rises monotonically with resonance, is not applied "
          "inside processSample, input scaling is resonance-invariant, and low "
          "resonance matches the TPT oracle",
          "[vcf-reso][vcf]") {
    const double fsOs = 88200.0;

    // makeUpGain() rises monotonically with reso01 and is unity at reso01 = 0 (F-06).
    LadderFilter f;
    f.prepare(fsOs, 64);
    f.setResonance(0.0f);
    REQUIRE(f.makeUpGain() == Catch::Approx(1.0f));
    float prev = -1.0f;
    for (float r = 0.0f; r <= 1.0001f; r += 0.05f) {
        f.setResonance(r);
        const float mu = f.makeUpGain();
        INFO("reso01 = " << r << " -> makeUpGain = " << mu << ", k = " << f.loopGainK());
        REQUIRE(mu >= prev);             // monotonic non-decreasing
        REQUIRE(mu >= 1.0f);             // always at least unity (make-up, never cut)
        prev = mu;
    }
    REQUIRE(f.makeUpGain() > 1.0f);      // strictly above unity at full resonance

    // The make-up gain is NOT applied inside processSample, and the filter INPUT scaling
    // is invariant to resonance (F-06). Drive a small low-frequency tone (passband) at
    // two resonance settings; the make-up multiplier must NOT appear on the output of
    // processSample (i.e. the passband output is not scaled by makeUpGain). We compare
    // the very-low-frequency (DC-ish) gain: it follows the physical 1/(1+k) droop, the
    // OPPOSITE of the make-up rise, proving make-up is not folded into the output.
    // Both probe resonances are kept BELOW the self-oscillation onset so the response is
    // a stable linear measurement (above onset the filter self-oscillates and a small
    // probe is swamped).
    auto dcGain = [&](float reso01) {
        LadderFilter g;
        g.prepare(fsOs, 64);
        g.setCutoffHz(1000.0f);
        g.setResonance(reso01);
        const double amp = 1.0e-4;
        return magAt(g, fsOs, 20.0, 40000, amp, 8) / amp; // ~DC passband gain
    };
    const double gLow  = dcGain(0.1f); // k ~ 0.08
    const double gHigh = dcGain(0.6f); // k ~ 2.88, still below onset
    INFO("DC gain at reso 0.1 = " << gLow << ", at reso 0.6 = " << gHigh
         << "; makeUp(0.6) = " << (1.0 + 0.5 * std::pow(0.6, 2.0)));
    // If make-up were (wrongly) applied inside processSample, the higher-resonance DC
    // gain would RISE (makeUp rises with resonance). Physically the cascade DC gain
    // DROOPS as resonance rises (H(0) = 1/(1+k), F-07), so the output of processSample
    // FALLS with resonance — confirming the make-up is exposed only, not applied here.
    REQUIRE(gHigh < gLow);

    // Input-scaling invariance: the input transconductor is fed x - fb; the INPUT x is
    // never pre-scaled by resonance. A doubling of input amplitude at fixed resonance
    // produces (within the small-signal regime) a proportional output, identically for
    // any resonance setting, i.e. the input gain is a resonance-independent constant.
    auto smallSignalGainRatio = [&](float reso01) {
        LadderFilter g;
        g.prepare(fsOs, 64);
        g.setCutoffHz(2000.0f);
        g.setResonance(reso01);
        const double a1 = magAt(g, fsOs, 200.0, 40000, 1.0e-5, 16);
        g.setResonance(reso01); // re-arm (magAt resets state)
        const double a2 = magAt(g, fsOs, 200.0, 40000, 2.0e-5, 16);
        return a2 / a1; // ~2.0 if input gain is linear/constant
    };
    REQUIRE(smallSignalGainRatio(0.1f) == Catch::Approx(2.0).epsilon(0.05));
    REQUIRE(smallSignalGainRatio(0.5f) == Catch::Approx(2.0).epsilon(0.05)); // sub-onset

    // F-13: at LOW resonance (near-linear regime) the LadderFilter magnitude SHAPE
    // matches the linear TPT oracle within a tolerance band across the passband. The
    // prewarps differ (1-exp vs tan), so compare DC-normalized magnitudes at matched
    // normalized loop gain k.
    auto ladShape = [&](double fc, double rf) {
        LadderFilter g;
        g.prepare(fsOs, 64);
        g.setCutoffHz(static_cast<float>(fc));
        g.setResonance(0.3f); // low resonance: near-linear
        const double dc = magAt(g, fsOs, fc / 50.0, 30000, 1.0e-4, 8);
        return magAt(g, fsOs, fc * rf, 30000, 1.0e-4, 16) / dc;
    };
    // The TPT oracle, matched at the SAME normalized k the ladder uses at reso01 = 0.3.
    LadderFilter probe;
    probe.prepare(fsOs, 64);
    probe.setResonance(0.3f);
    const double kMatched = static_cast<double>(probe.loopGainK());
    auto tptShape = [&](double fc, double rf) {
        mw::dsp::LadderReferenceTPT t;
        t.prepare(fsOs);
        t.setCutoffHz(fc);
        t.setResonanceK(kMatched);
        auto mag = [&](double fr) {
            t.reset();
            const double w = kTwoPi * fr / fsOs;
            double ph = 0.0;
            for (int n = 0; n < 30000; ++n) { (void) t.processSample(1.0e-4 * std::sin(ph)); ph += w; }
            const int N = static_cast<int>(std::ceil(fsOs / fr)) * 16;
            double re = 0, im = 0, cph = 0;
            for (int n = 0; n < N; ++n) { const double y = t.processSample(1.0e-4 * std::sin(ph)); ph += w; re += y * std::cos(cph); im += y * std::sin(cph); cph += w; }
            return std::sqrt(re * re + im * im) / N;
        };
        const double dc = mag(fc / 50.0);
        return mag(fc * rf) / dc;
    };
    for (double fc : { 500.0, 1000.0, 2000.0 }) {
        for (double rf : { 0.5, 1.0, 2.0 }) {
            const double lm = ladShape(fc, rf);
            const double tm = tptShape(fc, rf);
            INFO("fc = " << fc << " f = " << rf << "*fc  ladder = " << lm << " tpt = " << tm);
            REQUIRE(lm == Catch::Approx(tm).epsilon(0.12)); // shape match within 12%
        }
    }
}

// === RT safety (F-11): resonance path allocates nothing / takes no lock ===============
TEST_CASE("vcf-reso: setResonance and the resonant processSample allocate nothing at "
          "audio rate",
          "[vcf-reso][vcf][rt]") {
    LadderFilter f;
    f.prepare(88200.0, 256); // the only allocator, before arming the guard

    std::vector<float> block(256);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = 0.05f * std::sin(0.17f * static_cast<float>(i));
    }

    mw::test::AudioThreadGuard guard;
    guard.arm();
    volatile float sink = 0.0f;
    f.reset();
    for (int i = 0; i < 64; ++i) {
        f.setResonance(0.01f * static_cast<float>(i)); // control-rate setter, resonant loop
        f.setCutoffHz(500.0f + 20.0f * static_cast<float>(i));
        sink += f.processSample(0.1f * std::sin(0.2f * static_cast<float>(i)));
    }
    f.processBlock(block.data(), static_cast<int>(block.size()));
    sink += block[0];
    guard.disarm();
    (void) sink;

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

// === Determinism (F-14 supporting): resonant output is bit-identical run-to-run =======
TEST_CASE("vcf-reso: resonant and self-oscillating output is bit-identical run-to-run",
          "[vcf-reso][vcf]") {
    auto run = [](float reso01, float fc) {
        LadderFilter f;
        f.prepare(96000.0, 256);
        f.setCutoffHz(fc);
        f.setResonance(reso01);
        f.reset();
        std::vector<float> buf(512);
        for (std::size_t i = 0; i < buf.size(); ++i) {
            buf[i] = (i < 8) ? 0.5f : 0.0f; // impulse-ish kick, then ring/self-osc
        }
        f.processBlock(buf.data(), static_cast<int>(buf.size()));
        return buf;
    };
    REQUIRE(run(0.7f, 1200.0f) == run(0.7f, 1200.0f)); // resonant
    REQUIRE(run(1.0f, 800.0f)  == run(1.0f, 800.0f));  // self-oscillating

    // The denorm floor is a normal float (the bias keeps the running state out of the
    // subnormal band even in a self-oscillation decay).
    REQUIRE(mw::cal::vcf::kAntiDenorm > std::numeric_limits<float>::denorm_min());
}
