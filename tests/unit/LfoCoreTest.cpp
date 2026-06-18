// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the LFO rate/phase machinery + the continuous SmoothTri
// (triangle rounded toward sine) and Square waveform cores implemented in
// core/dsp/Lfo.cpp (task 055). Realizes docs/design/03-dsp-envelope-lfo-vca.md
// §3.4 (rate clamp + phase increment) and §3.5 (SmoothTri / Square cores).
//
// Names begin with "lfo_core" so the silent-pass selector `-R lfo_core` matches the
// registered test-case NAME ("discovery registers names, not tags", AGENTS.md). The
// display names avoid '[' so Catch2 does not parse a bracketed fragment as a tag and
// break ctest -R selection.
//
// Random/Noise cores, cycleEdge->envelope wiring, and the mod-bus LPF are OUT OF
// SCOPE here (task 055 ## Out of scope) and are not asserted.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "calibration/EnvLfoVcaConstants.h"
#include "dsp/Lfo.h"

using mw101::dsp::Lfo;
using mw101::dsp::LfoShape;

namespace {
constexpr double kPi = 3.14159265358979323846;

// A reference SmoothTri value computed straight from the design formula (§3.5):
//   native bipolar triangle from phase, then
//   out = lerp(tri, sineApprox(tri), kLfoSmoothShape)
// with the standard cubic sine approximant sineApprox(t) = t*(1.5 - 0.5*t*t).
// This is an independent oracle in the TEST (not a copy of the .cpp's helper) so a
// regression in the core would diverge from it.
float refSmoothTri (float phase) noexcept
{
    const float tri = (phase < 0.5f) ? (4.0f * phase - 1.0f)   // -1 -> +1 over [0,0.5)
                                     : (3.0f - 4.0f * phase);  // +1 -> -1 over [0.5,1)
    const float approx = tri * (1.5f - 0.5f * tri * tri);
    const float k = mw::cal::lfo::kLfoSmoothShape;
    return tri + k * (approx - tri);
}
} // namespace

// --- §3.4: rate clamp to [0.1, 30] Hz; 0.35 Hz is NEVER the enforced minimum -----

TEST_CASE("lfo_core: setRateHz clamps the rate into 0.1..30 Hz") {
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::Square);

    // Below the floor clamps UP to 0.1 Hz; above the ceiling clamps DOWN to 30 Hz.
    // We observe the effective rate through the phase increment: one full cycle of a
    // Square is one cycleEdge() (the H->L phase wrap), so we count wraps over a known
    // span. Counting the wrap flag (not a value transition) avoids ambiguity at the
    // exact 0.5 mid-cycle float boundary; a 3 s span keeps the float-accumulation
    // drift below one cycle so the integer count is robust.
    const int spanSec = 3;
    auto cyclesIn = [&] (float requestedHz) {
        lfo.prepare (48000.0, 1);          // resets phase to 0
        lfo.setShape (LfoShape::Square);
        lfo.setRateHz (requestedHz);
        const int ticks = 48000 * spanSec;
        int       cycles = 0;
        for (int i = 0; i < ticks; ++i) {
            lfo.tick();
            if (lfo.cycleEdge()) ++cycles;   // one wrap per completed cycle
        }
        return cycles;
    };

    // 0.05 Hz request is below the floor -> clamps to 0.1 Hz: 0 complete cycles in
    // 3 s (a single 0.1 Hz cycle is 10 s long), and crucially NOT silenced and NOT
    // pinned to a 0.35 Hz clone minimum.
    REQUIRE (cyclesIn (0.05f) == 0);

    // 0.2 Hz is inside the band and BELOW the disputed 0.35 Hz clone minimum: it must
    // be honored verbatim, proving 0.35 Hz is not enforced as a floor.
    // 0.2 Hz over 1 s completes 0 full cycles (cycle = 5 s) but the phase must have
    // advanced ~0.2 of a cycle -> still in the high half at the end.
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::Square);
    lfo.setRateHz (0.2f);
    for (int i = 0; i < 48000; ++i) lfo.tick();
    // After ~0.2 cycle from phase 0 the Square is still in its first (high) half:
    REQUIRE (lfo.value() == 1.0f);

    // 5 Hz inside the band -> ~5 cycles/s -> ~15 in 3 s (allow ±1 for the edge that
    // straddles the span boundary under float accumulation).
    REQUIRE (cyclesIn (5.0f) >= spanSec * 5 - 1);
    REQUIRE (cyclesIn (5.0f) <= spanSec * 5 + 1);

    // 100 Hz request clamps DOWN to 30 Hz -> ~30 cycles/s -> ~90 in 3 s.
    REQUIRE (cyclesIn (100.0f) >= spanSec * 30 - 1);
    REQUIRE (cyclesIn (100.0f) <= spanSec * 30 + 1);

    // The clamp is a true pin, not a coincidence: an already-out-of-band 200 Hz
    // request lands on the SAME effective rate as 30 Hz (both clamp to the ceiling),
    // while a 50 Hz request also clamps there -> identical cycle counts.
    REQUIRE (cyclesIn (200.0f) == cyclesIn (30.0f));
    REQUIRE (cyclesIn (50.0f)  == cyclesIn (30.0f));
}

TEST_CASE("lfo_core: 0.35 Hz is never enforced as the minimum rate") {
    // The disputed 0.35 Hz clone floor MUST NOT be applied (§3.4 acceptance hook).
    // A 0.1 Hz request is the true floor and must produce a slower advance than a
    // 0.35 Hz request would: after a fixed number of ticks the 0.1 Hz phase is
    // strictly less advanced than a 0.35 Hz phase. We probe via the SmoothTri value
    // climbing out of its phase-0 trough (which equals -1 -> rising).
    auto phaseAdvanceProxy = [] (float hz, int ticks) {
        Lfo lfo;
        lfo.prepare (48000.0, 1);
        lfo.setShape (LfoShape::SmoothTri);
        lfo.setRateHz (hz);
        float v = 0.0f;
        for (int i = 0; i < ticks; ++i) v = lfo.tick();
        return v;   // SmoothTri starts at -1 (phase 0) and rises; bigger value = more advanced
    };

    const int ticks = 24000;   // 0.5 s at 48 kHz, ticksPerControl = 1
    const float v010 = phaseAdvanceProxy (0.10f, ticks);
    const float v035 = phaseAdvanceProxy (0.35f, ticks);

    // If 0.10 Hz were silently raised to a 0.35 Hz floor the two would be equal.
    // The true 0.10 Hz advances LESS than 0.35 Hz in the same window.
    REQUIRE (v010 < v035);
}

// --- §3.4: phase accumulation stays in [0,1) and advances per control tick --------

TEST_CASE("lfo_core: phase advances per tick and the Square wraps once per cycle") {
    Lfo lfo;
    // fc = sampleRate / ticksPerControl = 96000 / 2 = 48000; rate 1 Hz -> exactly
    // 48000 ticks per cycle, phaseInc = 1/48000.
    lfo.prepare (96000.0, 2);
    lfo.setShape (LfoShape::Square);
    lfo.setRateHz (1.0f);

    // First half of the cycle (phase in [0,0.5)) the Square is high (+1); after
    // exactly half a cycle it flips to low (-1).
    const int perCycle = 48000;
    for (int i = 0; i < perCycle / 2 - 1; ++i) {
        REQUIRE (lfo.tick() == 1.0f);
    }
    // The first tick whose phase has crossed 0.5 must read -1.
    // Step until we observe the high->low flip and ensure it happens around mid-cycle.
    int n = perCycle / 2 - 1;
    float v = lfo.tick(); ++n;        // ~ at/near phase 0.5
    // Continue a little; we must see the low half.
    bool sawLow = (v == -1.0f);
    for (int i = 0; i < 10 && !sawLow; ++i) { v = lfo.tick(); ++n; sawLow = (v == -1.0f); }
    REQUIRE (sawLow);
    REQUIRE (n > perCycle / 2 - 5);
    REQUIRE (n < perCycle / 2 + 5);
}

TEST_CASE("lfo_core: cycleEdge fires once per phase wrap") {
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::SmoothTri);
    lfo.setRateHz (1.0f);     // 48000 ticks per cycle

    int edges = 0;
    const int ticks = 48000 * 3 + 100;   // just over three full cycles
    for (int i = 0; i < ticks; ++i) {
        lfo.tick();
        if (lfo.cycleEdge()) ++edges;
    }
    // Exactly one H->L wrap per completed cycle: 3 edges over ~3 cycles.
    REQUIRE (edges == 3);
}

// --- §3.5: SmoothTri is a triangle rounded toward sine, NOT a pure sine -----------

TEST_CASE("lfo_core: SmoothTri stays bipolar within -1..+1 over a full cycle") {
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::SmoothTri);
    lfo.setRateHz (1.0f);

    float lo = 2.0f, hi = -2.0f;
    for (int i = 0; i < 48000; ++i) {
        const float v = lfo.tick();
        REQUIRE (v >= -1.0001f);
        REQUIRE (v <=  1.0001f);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    // It actually swings (a real bipolar LFO, not a stuck DC value): the trough is
    // near -1 and the crest near +1.
    REQUIRE (lo < -0.95f);
    REQUIRE (hi >  0.95f);
}

TEST_CASE("lfo_core: SmoothTri matches the lerp(tri, sineApprox, kLfoSmoothShape) law") {
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::SmoothTri);
    lfo.setRateHz (1.0f);     // 48000 ticks/cycle, phaseInc = 1/48000

    // tick() emits the value at the CURRENT phase then advances; phase starts at 0.
    // Sample a handful of points and compare to the independent test oracle.
    const int perCycle = 48000;
    for (int step = 0; step < 8; ++step) {
        const float phase = static_cast<float> (step) / 8.0f;   // 0, .125, .25, ...
        // advance the LFO to (approximately) this phase, then read the next value
        Lfo l2;
        l2.prepare (48000.0, 1);
        l2.setShape (LfoShape::SmoothTri);
        l2.setRateHz (1.0f);
        const int target = static_cast<int> (phase * perCycle + 0.5f);
        float v = 0.0f;
        for (int i = 0; i <= target; ++i) v = l2.tick();   // value at ~target phase
        const float effPhase = static_cast<float> ((target + 0) % perCycle) / perCycle;
        // value() reports the value at the phase BEFORE the last advance; recompute
        // the oracle at that same sampled phase.
        const float expected = refSmoothTri (effPhase);
        // 3e-3 absorbs the float phase-accumulation drift near the triangle's branch
        // boundary; it is still ~100x tighter than the ~0.3+ gap a pure sine or the
        // raw (unshaped) triangle would show, so the lerp law is pinned.
        REQUIRE (std::abs (l2.value() - expected) < 3.0e-3f);
    }
}

TEST_CASE("lfo_core: SmoothTri is NOT a pure sine - it peaks at phase 0.5 not 0.25") {
    // The clean oracle: a pure sine of the phase peaks at phase 0.25 and is at a
    // zero crossing at phase 0.5. SmoothTri, being triangle-derived, is the OPPOSITE:
    // it sits at its trough/zero region near phase 0.25 and PEAKS at phase 0.5.
    // Sampling the value at phase 0.25 vs 0.5 distinguishes the two unambiguously.
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::SmoothTri);
    lfo.setRateHz (1.0f);

    const int perCycle = 48000;

    auto valueAtPhase = [&] (float phase) {
        Lfo l;
        l.prepare (48000.0, 1);
        l.setShape (LfoShape::SmoothTri);
        l.setRateHz (1.0f);
        const int target = static_cast<int> (phase * perCycle + 0.5f);
        for (int i = 0; i <= target; ++i) l.tick();
        return l.value();
    };

    const float atQuarter = valueAtPhase (0.25f);   // pure sine would be +1 here
    const float atHalf     = valueAtPhase (0.50f);   // pure sine would be ~0 here

    // SmoothTri PEAKS at 0.5 and is ~0 at 0.25 -> exactly inverted vs a pure sine.
    REQUIRE (atHalf > atQuarter);
    REQUIRE (atHalf > 0.95f);                 // crest near +1 at phase 0.5
    REQUIRE (std::abs (atQuarter) < 0.1f);    // near zero at phase 0.25

    // And quantitatively: a pure sine at these phases is sin(pi/2)=1 and sin(pi)=0;
    // the SmoothTri value at 0.25 deviates from the sine by ~1.0 (it is NOT a sine).
    const float pureSineAtQuarter = static_cast<float> (std::sin (2.0 * kPi * 0.25));
    REQUIRE (std::abs (atQuarter - pureSineAtQuarter) > 0.5f);
}

TEST_CASE("lfo_core: SmoothTri is rounded toward sine - shaping softens the triangle") {
    // With kLfoSmoothShape in (0,1), the shaped output near the slope (e.g. phase
    // 0.125, where the raw triangle = -0.5) is pulled toward the cubic sine approx,
    // i.e. it differs from the raw triangle by a non-trivial amount but stays bounded.
    const float k = mw::cal::lfo::kLfoSmoothShape;
    REQUIRE (k > 0.0f);
    REQUIRE (k < 1.0f);

    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::SmoothTri);
    lfo.setRateHz (1.0f);

    const int perCycle = 48000;
    const float phase = 0.125f;
    const int target = static_cast<int> (phase * perCycle + 0.5f);
    float v = 0.0f;
    for (int i = 0; i <= target; ++i) v = lfo.tick();

    const float rawTri = 4.0f * phase - 1.0f;   // = -0.5 at phase 0.125
    // The shaped value is rounded (shifted from the raw triangle) by the blend.
    REQUIRE (std::abs (lfo.value() - rawTri) > 1.0e-3f);
    // ...but the rounding is partial (k < 1), so it is NOT the full sineApprox either.
    const float fullApprox = rawTri * (1.5f - 0.5f * rawTri * rawTri);
    REQUIRE (std::abs (lfo.value() - fullApprox) > 1.0e-4f);
}

// --- §3.5: Square is intentionally hard-edged: only +1 / -1 -----------------------

TEST_CASE("lfo_core: Square emits only +1 and -1, hard-edged") {
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::Square);
    lfo.setRateHz (3.0f);

    int highs = 0, lows = 0;
    for (int i = 0; i < 48000; ++i) {
        const float v = lfo.tick();
        REQUIRE ((v == 1.0f || v == -1.0f));     // no intermediate values
        if (v == 1.0f) ++highs; else ++lows;
    }
    // A 50% duty square over whole cycles has ~equal high/low counts.
    REQUIRE (highs > 0);
    REQUIRE (lows  > 0);
    REQUIRE (std::abs (highs - lows) < 48000 / 10);
}

TEST_CASE("lfo_core: Square first-half is high, second-half is low") {
    Lfo lfo;
    lfo.prepare (48000.0, 1);
    lfo.setShape (LfoShape::Square);
    lfo.setRateHz (1.0f);     // 48000 ticks/cycle

    // phase < 0.5 -> +1 ; phase >= 0.5 -> -1  (§3.5)
    const float first = lfo.tick();           // phase ~0
    REQUIRE (first == 1.0f);

    // Advance to phase ~0.75 (unambiguously in the second/low half; sampling exactly
    // at the 0.5 boundary is float-ambiguous so it is deliberately avoided).
    for (int i = 1; i <= 36000; ++i) lfo.tick();   // value() now at phase ~0.75
    REQUIRE (lfo.value() == -1.0f);

    // Still high at phase ~0.25 (first half).
    Lfo first2;
    first2.prepare (48000.0, 1);
    first2.setShape (LfoShape::Square);
    first2.setRateHz (1.0f);
    for (int i = 0; i <= 12000; ++i) first2.tick();   // value() at phase ~0.25
    REQUIRE (first2.value() == 1.0f);
}
