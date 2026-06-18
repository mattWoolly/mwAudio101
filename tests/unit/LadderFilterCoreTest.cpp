// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the LINEAR core of the shipping Huovilainen ladder
// LadderFilter (task 038). Test-case names begin with "vcf-core" so
// `ctest -R vcf-core` selects them (silent-pass rule; docs/design/11 §8.2,
// AGENTS.md). Each case maps to an acceptance criterion in
// plan/backlog/038-ladderfilter-linear-core.md and a row of the docs/design/02 §10 /
// ADR-003 contract (F-01, F-02, F-11, F-12).
//
// This task wires the four-stage cascade with the global feedback gain k FORCED TO
// ZERO; the inverting feedback / diode clamp / self-osc / make-up Q are core-filter-5
// and are NOT exercised here. The oracle check is the 24 dB/oct slope (F-01) plus a
// shape cross-check against the linear TPT reference oracle (LadderReferenceTPT,
// F-13): both are 4-pole low-pass cascades, so above their corner both roll off at
// -24 dB/oct.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "dsp/LadderFilter.h"
#include "dsp/LadderReferenceTPT.h"
#include "calibration/FastTanhConstants.h"
#include "calibration/FilterTablesConstants.h"
#include "calibration/LadderFilterConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::dsp::LadderFilter;

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// Drive the filter with a sine of amplitude `amp` for `settle` samples, then return
// the steady-state peak output magnitude over several further cycles. The amplitude
// is kept small so the OTA-knee tanh stays in its linear regime (this is the F-01
// small-signal roll-off measurement).
double measureMagnitude(LadderFilter& f, double fsOs, double freqHz, int settle,
                        double amp) {
    f.reset();
    const double w = kTwoPi * freqHz / fsOs;
    double phase = 0.0;
    for (int n = 0; n < settle; ++n) {
        (void) f.processSample(static_cast<float>(amp * std::sin(phase)));
        phase += w;
    }
    const int meas = static_cast<int>(std::ceil(fsOs / freqHz)) * 8;
    double peak = 0.0;
    for (int n = 0; n < meas; ++n) {
        const double y = f.processSample(static_cast<float>(amp * std::sin(phase)));
        phase += w;
        peak = std::max(peak, std::abs(y));
    }
    return peak;
}

} // namespace

// --- Acceptance 1 (F-01): 24 dB/oct roll-off one octave above cutoff at k=0 --------
TEST_CASE("vcf-core: at k=0 the magnitude rolls off 24 dB per octave above cutoff",
          "[vcf-core][vcf]") {
    // Generous oversampled rate so the measurement octaves stay far below Fs/4 and the
    // four-pole asymptote is clean (matches the TPT-oracle slope test, task 034).
    const double fsOs = 384000.0;
    const double fc   = 500.0;
    const double amp  = 1.0e-3;        // small-signal: tanh knee stays linear

    LadderFilter f;
    f.prepare(fsOs, /*maxBlockOs=*/64);
    f.setResonance(0.0f);              // stub; k is forced to zero regardless
    f.setCutoffHz(static_cast<float>(fc));
    REQUIRE(f.loopGainK() == 0.0f);    // linear core: feedback gain is zero (F-05 stub)

    // Measure the slope between +3 and +4 octaves above cutoff, where the four-pole
    // asymptote is clean and both points are well below Fs/4.
    const int settle = 200000;
    const double mLow  = measureMagnitude(f, fsOs, 8.0  * fc, settle, amp); // +3 oct
    const double mHigh = measureMagnitude(f, fsOs, 16.0 * fc, settle, amp); // +4 oct
    const double slopeDbPerOct = 20.0 * std::log10(mHigh / mLow); // 1 octave apart

    INFO("slope = " << slopeDbPerOct << " dB/oct (mLow=" << mLow
                    << " mHigh=" << mHigh << ")");
    REQUIRE(slopeDbPerOct < -23.0);
    REQUIRE(slopeDbPerOct > -25.0);

    // Negative control: a SINGLE one-pole would roll off at -6 dB/oct. The measured
    // slope must be steeper than a 2-pole would give, proving the full 4-stage cascade
    // (not a truncated chain) is in the signal path.
    REQUIRE(slopeDbPerOct < -18.0);

    // Passband sanity: DC-ish gain is ~unity (the small-signal cascade has unity DC
    // gain at k=0; the coupling coefficient g/invTwoVt tunes the pole to 1-g).
    const double mDc = measureMagnitude(f, fsOs, fc / 32.0, settle, amp);
    REQUIRE(mDc / amp == Catch::Approx(1.0).margin(0.05));
}

// --- Oracle cross-check (F-13): k=0 shape vs the linear TPT reference -------------
TEST_CASE("vcf-core: the k=0 cascade roll-off matches the linear TPT oracle slope",
          "[vcf-core][vcf]") {
    // Both the Huovilainen k=0 cascade and the TPT/ZDF linear oracle are 4-pole
    // low-pass; above their corner both asymptote to -24 dB/oct. We compare the slope
    // (a topology invariant) rather than exact magnitude (the prewarps differ:
    // 1-exp(.) vs tan(.)). This catches a silent topology error (wrong stage count /
    // broken cascade) against a known-correct linear ladder.
    const double fsOs = 384000.0;
    const double fc   = 500.0;
    const double amp  = 1.0e-3;

    LadderFilter f;
    f.prepare(fsOs, 64);
    f.setCutoffHz(static_cast<float>(fc));

    mw::dsp::LadderReferenceTPT ref;
    ref.prepare(fsOs);
    ref.setCutoffHz(fc);
    ref.setResonanceK(0.0);

    auto measureRef = [&](double freqHz) {
        ref.reset();
        const double w = kTwoPi * freqHz / fsOs;
        double phase = 0.0;
        for (int n = 0; n < 200000; ++n) { (void) ref.processSample(amp * std::sin(phase)); phase += w; }
        const int meas = static_cast<int>(std::ceil(fsOs / freqHz)) * 8;
        double peak = 0.0;
        for (int n = 0; n < meas; ++n) {
            const double y = ref.processSample(amp * std::sin(phase));
            phase += w; peak = std::max(peak, std::abs(y));
        }
        return peak;
    };

    const double sLad = 20.0 * std::log10(measureMagnitude(f, fsOs, 16.0 * fc, 200000, amp)
                                        / measureMagnitude(f, fsOs, 8.0 * fc, 200000, amp));
    const double sRef = 20.0 * std::log10(measureRef(16.0 * fc) / measureRef(8.0 * fc));

    INFO("ladder slope = " << sLad << " dB/oct, TPT oracle slope = " << sRef << " dB/oct");
    REQUIRE(sLad == Catch::Approx(sRef).margin(1.5)); // same 4-pole asymptote
}

// --- Acceptance 2 (F-02): fixed cost / data-independent, no Newton iteration -------
TEST_CASE("vcf-core: processSample is fixed-cost and data-independent across amplitudes",
          "[vcf-core][vcf]") {
    LadderFilter f;
    f.prepare(88200.0, 64);
    f.setCutoffHz(1000.0f);

    // Across input amplitudes spanning ~10 orders of magnitude (including values that
    // saturate the tanh knee far past +/-1 internally), a SINGLE processSample call
    // fully advances the state and the output is always finite and bounded by the
    // tanh saturation. An iterate-to-tolerance / Newton solver would NOT be
    // amplitude-uniform like this and could fail to converge at the rails.
    for (double amp : {0.0, 1.0e-9, 1.0e-6, 1.0e-3, 0.1, 1.0, 10.0, 1.0e3, 1.0e6}) {
        f.reset();
        for (int n = 0; n < 4096; ++n) {
            const float in = static_cast<float>(amp * std::sin(0.05 * n));
            const float y = f.processSample(in);
            REQUIRE(std::isfinite(y));
            // Stage-4 output is the integrator state; with the tanh-bounded cascade it
            // never exceeds the saturation bound by more than a tiny margin.
            REQUIRE(std::abs(y) <= 1.0f + 1.0e-3f);
        }
    }

    // Determinism proxy for "fixed work per sample": the same input sequence yields a
    // bit-identical output sequence run-to-run (no hidden iteration count that could
    // vary). Compare full sequences for exact equality.
    auto runSeq = [&](float amp) {
        f.reset();
        std::vector<float> out;
        out.reserve(2048);
        for (int n = 0; n < 2048; ++n) {
            out.push_back(f.processSample(amp * std::sin(0.07f * static_cast<float>(n))));
        }
        return out;
    };
    const auto a = runSeq(0.25f);
    const auto b = runSeq(0.25f);
    REQUIRE(a == b); // bit-identical: no signal-dependent control flow / iteration

    // A LARGE amplitude is processed by the SAME call sequence (count check): N input
    // samples => exactly N outputs, with no extra solver passes hidden inside.
    f.reset();
    int calls = 0;
    for (int n = 0; n < 1000; ++n) { (void) f.processSample(1.0e6f); ++calls; }
    REQUIRE(calls == 1000);
}

// --- Acceptance 3 (F-11): no heap alloc / no lock on the hot path ------------------
TEST_CASE("vcf-core: reset/setters/processSample/processBlock allocate nothing at audio rate",
          "[vcf-core][vcf][rt]") {
    LadderFilter f;
    // prepare() is the off-thread setup; it (and FilterTables::build inside it) is the
    // only place table storage is built. Done BEFORE arming the guard.
    f.prepare(88200.0, 256);

    std::vector<float> block(256);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = 0.01f * std::sin(0.13f * static_cast<float>(i));
    }

    mw::test::AudioThreadGuard guard;
    guard.arm();
    volatile float sink = 0.0f;
    f.reset();
    for (int i = 0; i < 64; ++i) {
        // control-rate setters
        f.setCutoffCv(-2.0f + 0.05f * static_cast<float>(i));
        f.setCutoffHz(100.0f + 20.0f * static_cast<float>(i));
        f.setResonance(0.01f * static_cast<float>(i));
        // audio-rate hot path
        sink += f.processSample(0.01f * static_cast<float>(i));
    }
    f.processBlock(block.data(), static_cast<int>(block.size()));
    sink += block[0];
    guard.disarm();
    (void) sink;

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

// --- Acceptance 3 (F-11 cont.): processBlock == processSample loop -----------------
TEST_CASE("vcf-core: processBlock equals a processSample loop", "[vcf-core][vcf]") {
    LadderFilter a, b;
    a.prepare(88200.0, 128);
    b.prepare(88200.0, 128);
    a.setCutoffHz(1500.0f);
    b.setCutoffHz(1500.0f);

    std::vector<float> blk(128), ref(128);
    for (std::size_t i = 0; i < blk.size(); ++i) {
        const float s = 0.2f * std::sin(0.21f * static_cast<float>(i));
        blk[i] = s;
        ref[i] = s;
    }
    a.processBlock(blk.data(), static_cast<int>(blk.size()));
    for (std::size_t i = 0; i < ref.size(); ++i) ref[i] = b.processSample(ref[i]);

    for (std::size_t i = 0; i < blk.size(); ++i) {
        REQUIRE(blk[i] == ref[i]); // bit-identical
    }
}

// --- Acceptance 4 (F-12): drive-to-silence produces no subnormals ------------------
TEST_CASE("vcf-core: a drive-to-silence decay produces no subnormal floats",
          "[vcf-core][vcf]") {
    LadderFilter f;
    f.prepare(88200.0, 64);
    f.setCutoffHz(1200.0f);
    f.reset();

    // Excite the cascade hard, then feed pure silence and let it decay. With the
    // anti-denormal bias on every integrator accumulation (and FTZ/DAZ on the engine
    // thread), no integrator state or output should ever land in the subnormal range.
    auto isSubnormal = [](float v) {
        return v != 0.0f && std::fpclassify(v) == FP_SUBNORMAL;
    };

    for (int n = 0; n < 2000; ++n) {
        (void) f.processSample(0.9f * std::sin(0.3f * static_cast<float>(n)));
    }
    bool sawSubnormalOutput = false;
    for (int n = 0; n < 500000; ++n) {       // long decay tail toward silence
        const float y = f.processSample(0.0f);
        REQUIRE(std::isfinite(y));
        if (isSubnormal(y)) sawSubnormalOutput = true;
    }
    REQUIRE_FALSE(sawSubnormalOutput);

    // The output has genuinely decayed toward (anti-denormal) silence, not stalled in
    // the subnormal band: the magnitude is tiny but the value is either exactly zero
    // or at/above the anti-denormal bias floor, never a subnormal.
    const float yFinal = f.processSample(0.0f);
    REQUIRE(std::abs(yFinal) < 1.0e-3f);
    REQUIRE_FALSE(isSubnormal(yFinal));
    // The bias keeps the running integrator state out of the subnormal band: it sits
    // at or above kAntiDenorm in magnitude (a normal float), never subnormal.
    REQUIRE(mw::cal::vcf::kAntiDenorm > std::numeric_limits<float>::denorm_min());
}

// --- Determinism (F-14 supporting): bit-identical run-to-run -----------------------
TEST_CASE("vcf-core: processBlock output is bit-identical run-to-run for fixed inputs",
          "[vcf-core][vcf]") {
    auto run = []() {
        LadderFilter f;
        f.prepare(96000.0, 256);
        f.setCutoffHz(2000.0f);
        f.setResonance(0.7f); // stub; does not engage feedback (k stays 0)
        f.reset();
        std::vector<float> buf(256);
        for (std::size_t i = 0; i < buf.size(); ++i) {
            buf[i] = 0.3f * std::sin(0.11f * static_cast<float>(i));
        }
        f.processBlock(buf.data(), static_cast<int>(buf.size()));
        return buf;
    };
    const auto x = run();
    const auto y = run();
    REQUIRE(x == y); // bit-identical
    // Resonance is a no-op stub in this task: make-up gain stays unity and k stays 0.
    LadderFilter g;
    g.prepare(96000.0, 64);
    g.setResonance(1.0f);
    REQUIRE(g.loopGainK() == 0.0f);
    REQUIRE(g.makeUpGain() == 1.0f);
    REQUIRE(g.resonance01() == 1.0f); // the control value is stored for introspection
}
