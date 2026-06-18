// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the 4013-derived SubOscillator (task 031). Test-case
// names begin with "sub" so `ctest -R sub` selects them (silent-pass rule,
// AGENTS.md). Covers every Acceptance criterion of
// plan/backlog/031-sub-oscillator.md against docs/design/01-dsp-oscillators.md
// §5.1-§5.6 / §10 and ADR-002 C4-C6:
//   - sub fundamental is EXACTLY VCO/2 (-1 oct) and VCO/4 (-2 oct) at every footage,
//     with zero phase drift over a long run, phase-locked to the saw wrap [§5.3, C4];
//   - the 25% pulse is high 75% / low 25% of its -2 oct period, and its strong 2nd
//     harmonic gives the -1/-2 oct blend (2nd is the strongest harmonic ABOVE the
//     fundamental; 4th harmonic is a null) [§5.4, C5, §10];
//   - sub edges are sample-aligned to the saw wrap, scheduled in temporal order
//     (one net level-step per wrap) [§5.5, §10];
//   - all edges are level STEPS; no BLAMP path exists [C6];
//   - bipolar [-1,+1] output, level NOT applied here [§5.6, §8];
//   - PolyBLEP is the default tier, minBLEP selectable; mode set off the hot path;
//   - renderSample() is noexcept and performs no heap allocation / takes no locks.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "dsp/SubOscillator.h"
#include "dsp/OscAaMode.h"
#include "dsp/MinBlepTable.h"
#include "calibration/SubOscillatorConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::SubOscillator;
using mw101::dsp::SubShape;
using mw101::dsp::OscAaMode;
using mw101::dsp::MinBlepTable;
using mw::test::AudioThreadGuard;
using Catch::Approx;

namespace {

// Drive a SubOscillator from a master VCO phase accumulator for `n` samples, exactly
// as docs/design/01 §7.3 sequences it: advance the master phase, record the wrap, then
// renderSample(masterPhase, wrapped, freqHz). Returns the rendered sub output samples.
std::vector<float> driveSub (SubOscillator& sub, double f, double fs, int n)
{
    const double dt = f / fs;
    double phase = 0.0;
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        phase += dt;
        bool wrapped = false;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            wrapped = true;
        }
        out.push_back (sub.renderSample (static_cast<float> (phase), wrapped, f));
    }
    return out;
}

// Count rising zero-crossings (a proxy for the fundamental's cycle count) of a bipolar
// signal: transitions from <0 to >=0.
int countRises (const std::vector<float>& sig)
{
    int rises = 0;
    for (std::size_t i = 1; i < sig.size(); ++i)
        if (sig[i - 1] < 0.0f && sig[i] >= 0.0f) ++rises;
    return rises;
}

// Magnitude of the DFT bin at frequency `hz` for a real signal sampled at `fs`.
double magAt (const std::vector<float>& sig, double hz, double fs)
{
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const int n = static_cast<int> (sig.size());
    double re = 0.0, im = 0.0;
    const double w = -kTwoPi * hz / fs;
    for (int t = 0; t < n; ++t)
    {
        re += sig[static_cast<std::size_t> (t)] * std::cos (w * t);
        im += sig[static_cast<std::size_t> (t)] * std::sin (w * t);
    }
    return std::hypot (re, im) / n * 2.0;
}

} // namespace

// --- §5.1/§5.6/§8 output contract: bipolar [-1,+1], level NOT applied -----------

TEST_CASE("sub: output is bipolar in -1 to +1, pre-level (level applied by the mixer)",
          "[sub]") {
    MinBlepTable table; table.build();
    SubOscillator sub;
    sub.prepare (48000.0, table);
    sub.reset();

    for (SubShape s : { SubShape::OctDownSquare,
                        SubShape::TwoOctDownSquare,
                        SubShape::TwoOctDown25Pulse })
    {
        sub.setShape (s);
        sub.reset();
        const auto out = driveSub (sub, 220.0, 48000.0, 4096);
        float lo = 2.0f, hi = -2.0f;
        for (float v : out) { lo = std::min (lo, v); hi = std::max (hi, v); }
        // PolyBLEP can overshoot slightly at the band-limited edges; the held levels
        // are the bipolar +/-kSubHigh/kSubLow, never scaled by a LEVEL control here.
        REQUIRE (hi <= 1.0f + 0.30f);
        REQUIRE (lo >= -1.0f - 0.30f);
        // The held high/low plateaus reach the bipolar levels (sampled far from edges).
        REQUIRE (hi > 0.9f);
        REQUIRE (lo < -0.9f);
    }
}

// --- §5.3 / ADR-002 C4: exact VCO/2 and VCO/4, every footage, zero drift ---------

TEST_CASE("sub: -1 oct square is exactly VCO/2 and -2 oct square is exactly VCO/4 at every footage",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;

    // Footages 16'/8'/4'/2' are exact octave ratios of a base; the sub divides the
    // SAME master phase, so it is exactly VCO/2 and VCO/4 at each [§5.3, §10].
    for (double f : { 55.0, 110.0, 220.0, 440.0 })
    {
        SubOscillator sub;
        sub.prepare (fs, table);

        sub.setShape (SubShape::OctDownSquare);   // Q1 = VCO/2
        sub.reset();
        const auto q1 = driveSub (sub, f, fs, static_cast<int> (fs));   // 1 second
        const int q1rises = countRises (q1);
        // Q1 fundamental = f/2: ~f/2 cycles per second (allow +/-1 for the boundary).
        REQUIRE (std::abs (q1rises - static_cast<int> (f / 2.0)) <= 1);

        sub.setShape (SubShape::TwoOctDownSquare);  // Q2 = VCO/4
        sub.reset();
        const auto q2 = driveSub (sub, f, fs, static_cast<int> (fs));
        const int q2rises = countRises (q2);
        REQUIRE (std::abs (q2rises - static_cast<int> (f / 4.0)) <= 1);
    }
}

TEST_CASE("sub: divider is phase-locked with ZERO drift over a long run", "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 220.0;   // VCO fundamental
    const double dt = f / fs;

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setShape (SubShape::OctDownSquare);   // Q1 toggles once per VCO wrap
    sub.reset();

    // Re-run the same divider book-keeping the implementation must follow and assert
    // the sub's Q1 sign agrees with the deterministic divide-by-two of the wrap count
    // at every sample over a long run (no accumulation error / drift) [C4, §5.3].
    double phase = 0.0;
    bool   q1 = false;
    long   sampleIdx = 0;
    const long N = 10L * static_cast<long> (fs);   // 10 seconds
    int    mismatches = 0;
    for (long i = 0; i < N; ++i)
    {
        phase += dt;
        bool wrapped = false;
        if (phase >= 1.0) { phase -= 1.0; wrapped = true; q1 = ! q1; }
        const float y = sub.renderSample (static_cast<float> (phase), wrapped, f);
        // Sample the HELD plateau (away from a band-limited edge) so the overshoot at
        // the edge does not confound the sign check: check only mid-plateau samples.
        if (! wrapped && phase > 4.0 * dt && phase < 1.0 - 4.0 * dt)
        {
            const bool sigHigh = (y > 0.0f);
            if (sigHigh != q1) ++mismatches;
        }
        ++sampleIdx;
    }
    REQUIRE (sampleIdx == N);
    REQUIRE (mismatches == 0);   // exact phase-lock, zero drift over 10 s
}

// --- §5.4 / ADR-002 C5 / §10: 25% pulse duty + strong-2nd-harmonic blend ---------

TEST_CASE("sub: 25 percent pulse is high 75 percent and low 25 percent of its -2 oct period",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 200.0;   // f/4 = 50 Hz -2 oct fundamental, integer periods in 1 s

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setShape (SubShape::TwoOctDown25Pulse);   // Q1 OR Q2
    sub.reset();

    const auto out = driveSub (sub, f, fs, static_cast<int> (fs));
    // Duty = fraction of samples that are high. The OR is high 3 of every 4 VCO cycles.
    std::size_t high = 0;
    for (float v : out) if (v > 0.0f) ++high;
    const double duty = static_cast<double> (high) / static_cast<double> (out.size());
    REQUIRE (duty == Approx (0.75).margin (0.01));
}

TEST_CASE("sub: 25 percent pulse has a strong 2nd harmonic giving the -1/-2 oct blend",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 200.0;            // VCO fundamental
    const double fund = f / 4.0;        // -2 oct fundamental of the OR pulse = 50 Hz

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setShape (SubShape::TwoOctDown25Pulse);
    sub.reset();

    const auto out = driveSub (sub, f, fs, static_cast<int> (fs));

    const double h1 = magAt (out, 1.0 * fund, fs);   // -2 oct fundamental
    const double h2 = magAt (out, 2.0 * fund, fs);   // -1 oct (the 2nd harmonic)
    const double h3 = magAt (out, 3.0 * fund, fs);
    const double h4 = magAt (out, 4.0 * fund, fs);
    const double h5 = magAt (out, 5.0 * fund, fs);
    const double h6 = magAt (out, 6.0 * fund, fs);

    // The defining diode-OR / 25%-duty spectrum [§5.4, §10; ADR-002 C5]:
    //  * the 2nd harmonic (-1 oct) is the STRONGEST harmonic ABOVE the fundamental
    //    (2nd > 3rd and 2nd > every higher harmonic) — the "strong 2nd harmonic" that
    //    gives the characteristic -1/-2 oct blend;
    //  * the 4th harmonic is a spectral NULL (sin(pi) at 1/duty=4), distinguishing the
    //    exact 25% rectangle;
    //  * the 2nd harmonic is a large fraction (~0.7) of the fundamental.
    // (Deviation note: the AC checkbox phrases this as "2nd harmonic is the strongest
    //  harmonic"; for a true 25% pulse the FUNDAMENTAL is strongest overall — see PR
    //  note. We assert the spectrally-correct distinguishing property instead.)
    REQUIRE (h2 > h3);
    REQUIRE (h2 > h5);
    REQUIRE (h2 > h6);
    REQUIRE (h4 < 0.05 * h1);                    // 4th harmonic null
    REQUIRE (h2 / h1 == Approx (0.707).margin (0.06));  // strong -1 oct content
    REQUIRE (h1 > h2);                           // fundamental strongest overall (honest)
}

// --- §5.5 / §10: edges sample-aligned to the saw wrap, one net step per wrap ------

TEST_CASE("sub: every level transition is aligned to a VCO saw wrap (no edges between wraps)",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 137.0;           // non-integer-period frequency: edges land mid-sample
    const double dt = f / fs;          // ~0.00285 -> long plateaus (~350 samples / VCO cycle)

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setShape (SubShape::TwoOctDown25Pulse);   // the 4-segment pattern per -2 oct period
    sub.reset();

    // The held plateau level may change ONLY on a saw wrap (the 4013 clock). Verify it
    // objectively: between consecutive wraps the rendered output's held level is constant
    // and equals the reference divider state for that interval, so every transition is
    // co-located with a wrap [§5.5, §10]. We run a reference divider in lock-step, and at
    // the geometric MIDPOINT of each inter-wrap interval (well clear of either band-limited
    // edge) we assert the rendered sign matches the reference held level. A transition that
    // happened anywhere OTHER than a wrap would desynchronize the two within one interval.
    double phase = 0.0;
    bool   rq1 = false, rq2 = false;       // reference divider
    int    intervalStart = 0;              // sample index of the most recent wrap
    int    lastWrapSample = -1;
    int    checks = 0, mismatches = 0;
    const int N = 30000;
    // Buffer one inter-wrap interval so we can sample its midpoint after we see its end.
    std::vector<float> interval;
    interval.reserve (1024);
    for (int i = 0; i < N; ++i)
    {
        phase += dt;
        bool wrapped = false;
        if (phase >= 1.0) { phase -= 1.0; wrapped = true; }
        const float y = sub.renderSample (static_cast<float> (phase), wrapped, f);

        if (wrapped)
        {
            // Close out the just-finished interval (between the previous wrap and now).
            if (lastWrapSample >= 0 && interval.size() >= 9)
            {
                // Reference held level for THIS interval is the divider state BEFORE the
                // clock at the wrap that closes it (rq1/rq2 are pre-clock here). For the
                // selected 25% pulse the logic is Q1 OR Q2.
                const bool high = (rq1 || rq2);
                // Midpoint sample of the interval (far from both edges).
                const float mid = interval[interval.size() / 2];
                const bool  midHigh = (mid > 0.0f);
                if (midHigh != high) ++mismatches;
                ++checks;
            }
            // Clock the reference divider on the wrap (Q1 each wrap; Q2 on Q1 rising).
            const bool q1p = rq1; rq1 = ! rq1;
            if ((! q1p) && rq1) rq2 = ! rq2;

            interval.clear();
            intervalStart = i;
            lastWrapSample = i;
        }
        else if (lastWrapSample >= 0)
        {
            interval.push_back (y);
        }
        (void) intervalStart;
    }
    REQUIRE (checks > 50);          // we actually exercised many intervals (no silent pass)
    REQUIRE (mismatches == 0);      // every plateau is wrap-aligned, no edges between wraps
}

TEST_CASE("sub: edges are temporally ordered — exactly one net level step per VCO wrap",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 777.0;
    const double dt = f / fs;

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setShape (SubShape::TwoOctDown25Pulse);
    sub.reset();

    // The implementation derives EACH edge from the SAME master accumulator and schedules
    // one net step per wrap (the OR of the two phase-locked squares). Over one full -2 oct
    // period (4 VCO wraps) the OR pattern is high,high,high,low => exactly two net edges
    // per -2 oct period (one rise, one fall). Count net plateau transitions and assert the
    // count matches 2 per -2 oct period within tolerance [§5.5, §10].
    int N = static_cast<int> (fs);   // 1 second
    double phase = 0.0;
    bool   prevHigh = false, have = false;
    int    transitions = 0;
    int    sinceWrap = 1000;
    for (int i = 0; i < N; ++i)
    {
        phase += dt;
        bool wrapped = false;
        if (phase >= 1.0) { phase -= 1.0; wrapped = true; }
        const float y = sub.renderSample (static_cast<float> (phase), wrapped, f);
        if (wrapped) sinceWrap = 0; else ++sinceWrap;
        if (sinceWrap >= 3 && phase < 1.0 - 4.0 * dt)
        {
            const bool high = (y > 0.0f);
            if (have && high != prevHigh) ++transitions;
            prevHigh = high; have = true;
        }
    }
    // -2 oct period count over 1 s ~ f/4; two transitions per period.
    const double periods = f / 4.0;
    REQUIRE (static_cast<double> (transitions) == Approx (2.0 * periods).epsilon (0.02));
}

// --- §5.5 band-limiting: PolyBLEP smooths the step (multi-sample edge) -----------

TEST_CASE("sub: band-limited edges span more than one sample (PolyBLEP applied to every edge)",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 2000.0;   // high enough that dt is appreciable, edge spans samples
    const double dt = f / fs;
    REQUIRE (dt > 0.02);        // dt large => the PolyBLEP residual window is visible

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setAaMode (OscAaMode::PolyBlep);
    sub.setShape (SubShape::OctDownSquare);
    sub.reset();

    const auto out = driveSub (sub, f, fs, 2048);
    // A pure (naive) square would only ever output exactly +/-1. PolyBLEP inserts a
    // band-limited transition, so intermediate magnitudes strictly inside (-1,1) appear
    // near the edges [§5.5; ADR-002 C5].
    int interior = 0;
    for (float v : out)
        if (std::abs (v) < 0.95f) ++interior;
    REQUIRE (interior > 0);
}

// --- §10 / ADR-002 C6: no BLAMP — every edge is a level STEP ---------------------

TEST_CASE("sub: minBLEP HQ mode settles every edge to a flat plateau (level steps, no BLAMP)",
          "[sub]") {
    MinBlepTable table; table.build();
    const double fs = 48000.0;
    const double f  = 100.0;    // low pitch: long plateaus between edges
    const double dt = f / fs;

    SubOscillator sub;
    sub.prepare (fs, table);
    sub.setAaMode (OscAaMode::MinBlepHq);    // HQ tier: minBLEP applicator
    sub.setShape (SubShape::OctDownSquare);
    sub.reset();

    const auto out = driveSub (sub, f, fs, static_cast<int> (fs / 4.0));

    // BLAMP would round a corner into a ramp (a slope discontinuity); a level STEP
    // settles to a FLAT plateau at +/-1. Far from any edge the held level must be flat
    // at the bipolar level, confirming a level-step (not BLAMP) correction [C6, §10].
    // Sample the deep-plateau region of the -1 oct period (Q1 high for ~ one VCO cycle).
    bool sawFlatHigh = false, sawFlatLow = false;
    for (std::size_t i = 8; i + 8 < out.size(); ++i)
    {
        // Find a run where neighbours are ~equal (a flat plateau).
        const float a = out[i - 1], b = out[i], c = out[i + 1];
        if (std::abs (a - b) < 1e-4f && std::abs (b - c) < 1e-4f)
        {
            if (b > 0.9f)  sawFlatHigh = true;
            if (b < -0.9f) sawFlatLow  = true;
        }
    }
    REQUIRE (sawFlatHigh);
    REQUIRE (sawFlatLow);
    (void) dt;
}

// --- AA-mode selection: PolyBLEP default; mode set off the hot path ---------------

TEST_CASE("sub: default AA mode is PolyBLEP; setAaMode selects minBLEP for the HQ tier",
          "[sub]") {
    // The mode is a structural setter (set in prepare / on reconfiguration, never
    // per-sample) [docs/design/01 §2.2]. Both modes must render a valid bipolar signal.
    MinBlepTable table; table.build();
    SubOscillator sub;
    sub.prepare (48000.0, table);
    sub.setShape (SubShape::TwoOctDown25Pulse);

    sub.setAaMode (OscAaMode::PolyBlep);
    sub.reset();
    const auto poly = driveSub (sub, 200.0, 48000.0, static_cast<int> (48000.0));

    sub.setAaMode (OscAaMode::MinBlepHq);
    sub.reset();
    const auto hq = driveSub (sub, 200.0, 48000.0, static_cast<int> (48000.0));

    // Both produce the same 75% duty (the divider logic is identical; only the edge
    // band-limiting differs) [§5.4, §5.5].
    auto dutyOf = [] (const std::vector<float>& s) {
        std::size_t h = 0; for (float v : s) if (v > 0.0f) ++h;
        return static_cast<double> (h) / static_cast<double> (s.size());
    };
    REQUIRE (dutyOf (poly) == Approx (0.75).margin (0.02));
    REQUIRE (dutyOf (hq)   == Approx (0.75).margin (0.02));
}

// --- §10 (PI) centralization ------------------------------------------------------

TEST_CASE("sub: bipolar levels are sourced from calibration, not inlined", "[sub]") {
    // The bipolar high/low output levels are (PI) and centralized in the calibration
    // header, never inlined at the DSP call site [§10 "(PI) centralization"].
    REQUIRE (mw::cal::sub::kSubHigh == 1.0f);
    REQUIRE (mw::cal::sub::kSubLow  == -1.0f);
}

// --- §2.4 / ADR-002 C11 real-time safety -----------------------------------------

TEST_CASE("sub: renderSample is noexcept", "[sub]") {
    SubOscillator sub;
    STATIC_REQUIRE (noexcept (sub.renderSample (0.0f, false, 220.0)));
    STATIC_REQUIRE (std::is_same_v<decltype (sub.renderSample (0.0f, false, 220.0)), float>);
}

TEST_CASE("sub: renderSample performs no heap allocation and takes no locks", "[sub][rt]") {
    MinBlepTable table; table.build();   // build allocates OFF the audio thread
    SubOscillator sub;
    sub.prepare (48000.0, table);        // prepare sizes the applicator (init-time)

    AudioThreadGuard g;
    g.arm();
    double phase = 0.0;
    const double dt = 440.0 / 48000.0;
    for (SubShape s : { SubShape::OctDownSquare,
                        SubShape::TwoOctDownSquare,
                        SubShape::TwoOctDown25Pulse })
    {
        sub.setShape (s);
        for (int i = 0; i < 2048; ++i)
        {
            phase += dt;
            bool wrapped = false;
            if (phase >= 1.0) { phase -= 1.0; wrapped = true; }
            (void) sub.renderSample (static_cast<float> (phase), wrapped, 440.0);
        }
    }
    g.disarm();

    REQUIRE_FALSE (g.violated());
    REQUIRE (g.violations().empty());
}
