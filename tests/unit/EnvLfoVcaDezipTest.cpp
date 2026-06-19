// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EnvLfoVcaDezipTest.cpp — the PAIRED positive/negative de-zipper class
// verification for the Envelope / LFO subsystem (task 061). Realizes
// plan/backlog/061-env-lfo-param-de-zipper.md scope:
//   docs/design/03-dsp-envelope-lfo-vca.md §6.1 (de-zipper policy: continuous knob
//     values de-zippered, the generated envelope CONTOUR and LFO VALUE are NOT) and
//   ADR-020 S2 (fast sonic continuous → one-pole de-zipper, ~10 ms),
//             S7 (stepped/choice selector → NOT value-smoothed; no smearing through
//                 wrong indices),
//             S10 (one-pole + deterministic snap threshold),
//             S11 (de-zippers advance on the control-rate tick cadence),
//             S12 (block-boundary update + snap bookkeeping is CLASS-EXACT /
//                  bit-identical run-to-run; paired positive/negative property test).
//
// This is a QA / property tier, NOT an algorithm-correctness tier: the OnePoleSmoother
// itself (SmootherTest), the LFO waveform cores (LfoCoreTest) and the envelope curve
// (EnvelopeCurveTest) are owned elsewhere (task 061 ## Out of scope). Here we assert
// the cross-cutting de-zipper CLASS contract as a single paired test:
//   POSITIVE — a continuous param target (env time / LFO depth, S2) fed a STEP to the
//     shared OnePoleSmoother de-zippers smoothly toward target and terminates at the
//     deterministic snap threshold (S2/S10);
//   NEGATIVE — the stepped mw101.lfo.shape selector index changes DISCRETELY: a shape
//     switch emits the new shape's value immediately, never interpolating through an
//     intermediate / wrong shape value (S7);
//   CADENCE — the de-zipper advances exactly one step per control-rate tick (S11);
//   NOT-DEZIPPERED — the generated envelope CONTOUR and LFO VALUE are NOT parameter
//     de-zippered: they move on their own per-tick dynamics, not via the param
//     smoother's snap band (§6.1);
//   DETERMINISM — the smoother trajectory and the integer snap-tick bookkeeping are
//     bit-identical run-to-run (S12, CLASS-EXACT boundary).
//
// Test-case names begin with "envlfovca_dezip" so `ctest -R envlfovca_dezip
// --no-tests=error` selects exactly these (silent-pass rule); display text avoids '['
// so Catch2 does not mis-parse a tag (AGENTS.md / docs/design/11 §8).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "dsp/Lfo.h"
#include "dsp/Envelope.h"
#include "params/Smoother.h"
#include "params/SmoothingClass.h"
#include "calibration/Calibration.h"
#include "calibration/EnvLfoVcaConstants.h"

using mw::params::OnePoleSmoother;
using mw::params::SmoothingClass;
using mw::params::smoothingTimeConstantSeconds;
using mw101::dsp::Lfo;
using mw101::dsp::LfoShape;
using mw101::dsp::Envelope;
using mw101::dsp::EnvParams;
using mw101::dsp::EnvTrigMode;

namespace {

// The control-rate tick the env/LFO and the param de-zipper share (§6.2 / S11): one
// process()/tick() call == one control-rate tick. We drive a per-tick control rate so
// the cadence claim is exercised one step at a time.
constexpr double kSr     = 48000.0;
constexpr int    kCtlDiv = 1;
constexpr double kTickHz = kSr / static_cast<double>(kCtlDiv);

// The shared one-pole smoother kind, configured for the S2 fast-sonic class (env time /
// LFO depth) using the SAME calibration-table time constant the registry would (S13) —
// never an inlined literal. This is the de-zipper that knob VALUES feed; the smoother
// type and class accessor are dependencies, NOT defined here (task 061 ## Out of scope).
OnePoleSmoother makeS2Smoother() {
    OnePoleSmoother s;
    s.prepare(/*tau=*/smoothingTimeConstantSeconds(SmoothingClass::Fast), kTickHz);
    return s;
}

} // namespace

// ============================================================================
// POSITIVE (S2 / S10): a continuous param (env time / LFO depth) de-zippers a step
//   input — smooth approach, never an instantaneous jump, with a deterministic snap
//   termination. The de-zipper advances on the control-rate tick cadence (S11).
// ============================================================================

TEST_CASE("envlfovca_dezip: a continuous env-time param de-zippers a step input toward target",
          "[envlfovca_dezip]") {
    // §6.1 / ADR-020 S2: an env-time KNOB VALUE is a fast-sonic continuous param, so a
    // host step (e.g. attack 0.003 s -> 0.080 s) must NOT land as a stair-step; the
    // shared one-pole de-zipper ramps it. Confirmed by feeding the smoother the step and
    // observing a monotone, non-instantaneous approach.
    OnePoleSmoother s = makeS2Smoother();
    const double from = static_cast<double>(EnvParams{}.attackSec); // 0.003 s
    const double to   = 0.080;                                      // a coarse host jump
    s.reset(from);
    s.setTarget(to);

    const double t1 = s.process();          // one control-rate tick
    // POSITIVE: the first tick moves TOWARD but does NOT reach the target (de-zippered).
    REQUIRE(t1 > from);
    REQUIRE(t1 < to);

    double prev = t1;
    for (int i = 0; i < 200; ++i) {
        const double v = s.process();
        REQUIRE(v >= prev);                 // monotone non-decreasing toward target
        REQUIRE(v <= to);                   // never overshoots
        prev = v;
    }
    REQUIRE(prev > t1);                      // it actually advanced past the first tick

    // S10: the exponential tail TERMINATES at the deterministic snap threshold so the
    // integer "is-smoothing" state is reproducible. Keep ticking unconditionally well
    // past the snap horizon (at ~10 ms / 48 kHz a 0.077 step needs a few thousand ticks
    // to enter the 1e-5 snap band; the snapping process() call itself sets current ==
    // target, so we must keep calling process() through the transition, not stop at the
    // first non-smoothing observation).
    for (int i = 0; i < 200000; ++i) (void) s.process();
    REQUIRE_FALSE(s.isSmoothing());
    REQUIRE(s.current() == to);             // snapped exactly to target
}

TEST_CASE("envlfovca_dezip: a continuous LFO-depth param de-zippers a step and advances one step per control-rate tick",
          "[envlfovca_dezip]") {
    // ADR-020 S2 + S11: the LFO-depth knob (mw101.lfo.depth_*) is S2 fast-sonic, and the
    // de-zipper MUST advance on the control-rate tick cadence — one step per process()
    // call, not at an independent rate. We assert BOTH: a step de-zippers, and exactly N
    // process() calls advance the trajectory by exactly N one-pole steps.
    OnePoleSmoother s = makeS2Smoother();
    s.reset(0.0);                            // depth 0
    s.setTarget(1.0);                        // host snaps depth to full

    // CADENCE: each process() == one control-rate tick. Re-derive the one-pole recurrence
    // independently (y = target - a*(target - y_prev)) and require the smoother to match
    // it tick-for-tick — proving it advances exactly once per call, no faster/slower.
    const double tau   = smoothingTimeConstantSeconds(SmoothingClass::Fast);
    const double a     = std::exp(-1.0 / (tau * kTickHz));
    double oracle      = 0.0;
    for (int i = 0; i < 50; ++i) {
        oracle = 1.0 - a * (1.0 - oracle);   // one independent control-rate step
        const double v = s.process();        // one tick of the de-zipper
        REQUIRE(v == oracle);                // exactly one step per tick (S11 cadence)
        REQUIRE(v > 0.0);
        REQUIRE(v < 1.0);                    // still de-zippering, not jumped (S2)
    }
}

// ============================================================================
// NEGATIVE (S7): the stepped mw101.lfo.shape selector index changes DISCRETELY — a
//   switch emits the new shape's value immediately and never interpolates through an
//   intermediate / wrong shape value.
// ============================================================================

TEST_CASE("envlfovca_dezip: the stepped LFO shape selector switches discretely and never smears through wrong shapes",
          "[envlfovca_dezip]") {
    // ADR-020 S7 / §3.2: mw101.lfo.shape is a stepped/choice selector — its INDEX MUST
    // NOT be value-smoothed. Low-passing a 4-way selector index would sweep the LFO
    // through wrong shapes on the way to the target. We prove the opposite: at a FROZEN
    // phase, the LFO emits EXACTLY the selected shape's deterministic value the instant
    // setShape() is called, with no intermediate blend between shapes.
    //
    // Construct two LFOs at the SAME phase. One stays SmoothTri throughout (the
    // reference); the other is switched SmoothTri -> Square. If the index were smeared,
    // the switched LFO would briefly emit a value strictly BETWEEN the SmoothTri value
    // and the Square value. We require it to emit the Square value on the very first tick
    // after the switch — an exact, discrete change.

    // Reference: a continuously-SmoothTri LFO. Sample its value at a representative,
    // non-degenerate phase (well inside the rising quarter so SmoothTri != Square).
    Lfo ref;
    ref.prepare(kSr, kCtlDiv);
    ref.reset();
    ref.setShape(LfoShape::SmoothTri);
    ref.setRateHz(2.0f);
    // Advance to a phase where the two shapes differ clearly, capturing the SmoothTri
    // value emitted AT that phase.
    float smoothTriValue = 0.0f;
    constexpr int kWarm = 1000;              // lands mid-rising-quarter at 2 Hz / 48 kHz
    for (int i = 0; i < kWarm; ++i) smoothTriValue = ref.tick();

    // A Square LFO advanced to the IDENTICAL phase: its value at that phase is the
    // discrete target the switched LFO must jump straight to.
    Lfo sq;
    sq.prepare(kSr, kCtlDiv);
    sq.reset();
    sq.setShape(LfoShape::Square);
    sq.setRateHz(2.0f);
    float squareValue = 0.0f;
    for (int i = 0; i < kWarm; ++i) squareValue = sq.tick();

    // The two shapes are genuinely different at this phase (so "no smear" is a real test).
    REQUIRE(smoothTriValue != squareValue);

    // The switched LFO: SmoothTri up to the same phase, then setShape(Square). The VERY
    // NEXT emitted value must equal the discrete Square value exactly — no interpolation
    // through an intermediate shape index (S7).
    Lfo sw;
    sw.prepare(kSr, kCtlDiv);
    sw.reset();
    sw.setShape(LfoShape::SmoothTri);
    sw.setRateHz(2.0f);
    for (int i = 0; i < kWarm; ++i) (void) sw.tick();   // reach the same phase as ref
    sw.setShape(LfoShape::Square);                      // DISCRETE stepped switch
    const float afterSwitch = sw.tick();

    REQUIRE(afterSwitch == squareValue);                // jumped straight to Square
    // And it is NOT the old shape's value, nor anything strictly between the two — a
    // smeared index would have produced exactly such an in-between value.
    REQUIRE(afterSwitch != smoothTriValue);
    const float lo = std::min(smoothTriValue, squareValue);
    const float hi = std::max(smoothTriValue, squareValue);
    const bool strictlyBetween = (afterSwitch > lo) && (afterSwitch < hi);
    REQUIRE_FALSE(strictlyBetween);                     // never smeared through an index
}

TEST_CASE("envlfovca_dezip: the LFO shape output is always one of the discrete shape values, never an inter-shape blend",
          "[envlfovca_dezip]") {
    // ADR-020 S7 reinforced over a SWEEP: stepping the selector across ALL four indices
    // every tick must, on each tick, emit EXACTLY the value the singly-selected shape
    // would emit at that same phase — the selector picks a branch, it does not blend two
    // branches. We compare against four reference LFOs (one per shape) advanced in
    // phase-lockstep; every switched-LFO sample must equal its current shape's reference
    // sample bit-for-bit (no interpolation).
    const float kNoise = 0.321f;             // a fixed injected mod-bus noise sample

    auto makeRef = [&](LfoShape shape) {
        Lfo l;
        l.prepare(kSr, kCtlDiv);
        l.reset();
        l.setShape(shape);
        l.setRateHz(4.0f);
        l.setNoiseSource(&kNoise);
        return l;
    };
    Lfo refTri   = makeRef(LfoShape::SmoothTri);
    Lfo refSq    = makeRef(LfoShape::Square);
    Lfo refRnd   = makeRef(LfoShape::Random);
    Lfo refNoise = makeRef(LfoShape::Noise);

    Lfo sw = makeRef(LfoShape::SmoothTri);   // the one we keep re-selecting

    constexpr int kN = 4000;
    const LfoShape order[4] = { LfoShape::SmoothTri, LfoShape::Square,
                                LfoShape::Random, LfoShape::Noise };
    for (int i = 0; i < kN; ++i) {
        const LfoShape shape = order[i & 3];   // change the index every tick
        sw.setShape(shape);

        // Advance all references in lockstep so phases stay identical.
        const float vTri   = refTri.tick();
        const float vSq    = refSq.tick();
        const float vRnd   = refRnd.tick();
        const float vNoise = refNoise.tick();
        const float vSw    = sw.tick();

        // The switched LFO's value is EXACTLY the active shape's reference value — a
        // discrete pick, not a smoothed crossfade through the selector index (S7).
        switch (shape) {
            case LfoShape::SmoothTri: REQUIRE(vSw == vTri);   break;
            case LfoShape::Square:    REQUIRE(vSw == vSq);    break;
            case LfoShape::Random:    REQUIRE(vSw == vRnd);   break;
            case LfoShape::Noise:     REQUIRE(vSw == vNoise); break;
        }
    }
}

// ============================================================================
// §6.1: the generated envelope CONTOUR and LFO VALUE are NOT parameter de-zippered.
//   De-zippering applies to KNOB VALUES, not to the signals the env/LFO generate.
// ============================================================================

TEST_CASE("envlfovca_dezip: the envelope contour is a generated signal, not a parameter de-zipper snap band",
          "[envlfovca_dezip]") {
    // §6.1 / ADR-020 S5/S6 distinction: the envelope CONTOUR is a generated signal — it
    // is NOT routed through the parameter de-zipper, so it keeps moving on its own
    // per-tick stage dynamics rather than snapping to a steady target inside the
    // OnePoleSmoother snap band. We prove the contour MOVES tick-to-tick during a stage
    // (so it is not a held de-zippered param value).
    Envelope env;
    env.prepare(kSr, kCtlDiv);
    env.reset();
    EnvParams p;
    p.attackSec  = 0.020f;                   // slow enough that attack spans many ticks
    p.decaySec   = 0.050f;
    p.sustain    = 0.5f;
    p.releaseSec = 0.060f;
    p.trig       = EnvTrigMode::GateTrig;
    env.setParams(p);

    env.noteOn(/*legato=*/false);
    // During attack the CONTOUR must change every tick (a generated ramp), not sit on a
    // single de-zippered value. Count how many ticks see a change as it rises.
    float prev = env.tick();
    int movedTicks = 0;
    for (int i = 0; i < 400; ++i) {
        const float cur = env.tick();
        if (cur != prev) ++movedTicks;
        prev = cur;
    }
    // A generated contour moves on the vast majority of attack ticks; a (wrongly)
    // de-zippered param value held at a single target would barely move at all.
    REQUIRE(movedTicks > 300);
    REQUIRE(env.level() > 0.0f);             // the contour genuinely rose
}

TEST_CASE("envlfovca_dezip: the LFO value is a generated signal, not a parameter de-zipper snap band",
          "[envlfovca_dezip]") {
    // §6.1: the LFO VALUE is generated, NOT de-zippered. A SmoothTri LFO sweeps -1..+1
    // continuously; its value changes every tick (it is an oscillator, not a held param
    // value snapping into a threshold). Confirm the value moves on essentially every
    // tick and traverses a wide bipolar range — behavior impossible for a de-zippered
    // param target sitting in the snap band.
    Lfo lfo;
    lfo.prepare(kSr, kCtlDiv);
    lfo.reset();
    lfo.setShape(LfoShape::SmoothTri);
    lfo.setRateHz(5.0f);

    constexpr int kN = 4000;
    float lo = 2.0f, hi = -2.0f;
    float prev = lfo.tick();
    int movedTicks = 0;
    for (int i = 0; i < kN; ++i) {
        const float v = lfo.tick();
        if (v != prev) ++movedTicks;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        prev = v;
    }
    REQUIRE(movedTicks > kN - 10);           // moves on (essentially) every tick
    REQUIRE(lo < -0.5f);                      // genuinely traverses the negative half
    REQUIRE(hi >  0.5f);                      // ... and the positive half (a real LFO)
}

// ============================================================================
// S12: block-boundary update + snap bookkeeping is CLASS-EXACT / deterministic.
//   The paired positive/negative test's bookkeeping is bit-identical run-to-run.
// ============================================================================

TEST_CASE("envlfovca_dezip: the de-zipper trajectory and the snap-tick bookkeeping are bit-identical run-to-run",
          "[envlfovca_dezip]") {
    // ADR-020 S12: the de-zipper VALUE is CLASS-FP but the block-boundary update + the
    // integer "is-smoothing"/snap bookkeeping is CLASS-EXACT and MUST be bit-identical
    // (here, run-to-run on this platform). We capture the full per-tick trajectory AND
    // the exact tick index at which the smoother snaps (stops smoothing), and require two
    // identical runs to agree bit-for-bit.
    auto run = [&]() {
        OnePoleSmoother s = makeS2Smoother();
        s.reset(0.0);
        s.setTarget(1.0);
        std::vector<double> traj;
        int snapTick = -1;
        // 20000 ticks comfortably exceeds the ~5.5k-tick snap horizon for a unit step at
        // ~10 ms / 48 kHz, so the run always reaches the snap terminus inside the window.
        for (int i = 0; i < 20000; ++i) {
            traj.push_back(s.process());
            if (snapTick < 0 && !s.isSmoothing()) snapTick = i;  // first non-smoothing tick
        }
        return std::pair<std::vector<double>, int>{traj, snapTick};
    };

    const auto r1 = run();
    const auto r2 = run();

    REQUIRE(r1.first == r2.first);           // CLASS-FP value trajectory: bit-identical
    REQUIRE(r1.second == r2.second);         // CLASS-EXACT integer snap-tick: identical
    REQUIRE(r1.second >= 0);                  // it actually reached the snap terminus (S10)
}

TEST_CASE("envlfovca_dezip: paired property — the same step de-zippers continuously while the selector index stays discrete",
          "[envlfovca_dezip]") {
    // ADR-020 S7/S12 PAIRED positive/negative property, the §6.1 acceptance hook in one
    // test: over an identical control-tick timeline, a CONTINUOUS param produces MANY
    // distinct intermediate values (de-zippered, S2), while the STEPPED selector produces
    // ONLY the discrete set of its enum values (never an intermediate, S7).

    // POSITIVE side: a continuous depth param stepped 0 -> 1 yields a strictly growing set
    // of distinct intermediate values as it de-zippers (proof of a continuous ramp).
    OnePoleSmoother depth = makeS2Smoother();
    depth.reset(0.0);
    depth.setTarget(1.0);
    std::set<double> continuousValues;
    for (int i = 0; i < 40; ++i) continuousValues.insert(depth.process());
    // Many distinct intermediate values — a de-zippered continuous sweep, not a step.
    REQUIRE(continuousValues.size() > 20);
    for (double v : continuousValues) {
        REQUIRE(v >= 0.0);
        REQUIRE(v <= 1.0);
    }

    // NEGATIVE side: a stepped selector cycled across its indices emits, on every tick,
    // EXACTLY the value the singly-selected shape would emit at that same phase — the
    // index picks a branch, it never produces a value smeared BETWEEN two shapes (which is
    // what low-passing the selector index would yield). We compare against four
    // phase-locked reference LFOs (one per shape) advanced in lockstep with the switched
    // LFO, so phase / Random-PRNG / Noise state all stay aligned; every switched sample
    // must equal its current shape's reference sample bit-for-bit.
    const float kNoise = -0.4f;
    auto makeRef = [&](LfoShape shape) {
        Lfo l;
        l.prepare(kSr, kCtlDiv);
        l.reset();
        l.setShape(shape);
        l.setRateHz(4.0f);
        l.setNoiseSource(&kNoise);
        return l;
    };
    Lfo refTri   = makeRef(LfoShape::SmoothTri);
    Lfo refSq    = makeRef(LfoShape::Square);
    Lfo refRnd   = makeRef(LfoShape::Random);
    Lfo refNoise = makeRef(LfoShape::Noise);
    Lfo lfo      = makeRef(LfoShape::SmoothTri);

    const LfoShape order[4] = { LfoShape::SmoothTri, LfoShape::Square,
                                LfoShape::Random, LfoShape::Noise };
    for (int i = 0; i < 200; ++i) {
        const LfoShape shape = order[i & 3];
        lfo.setShape(shape);                 // DISCRETE stepped switch every tick

        const float vTri   = refTri.tick();
        const float vSq    = refSq.tick();
        const float vRnd   = refRnd.tick();
        const float vNoise = refNoise.tick();
        const float v      = lfo.tick();

        // The emitted value is an EXACT member of the discrete shape set at this phase —
        // the index never smears to an intermediate value (S7), the opposite of the
        // continuous de-zippered sweep above.
        std::set<float> discreteSet = { vTri, vSq, vRnd, vNoise };
        REQUIRE(discreteSet.count(v) == 1);

        // And specifically equals the CURRENTLY-selected shape's value (a clean pick).
        switch (shape) {
            case LfoShape::SmoothTri: REQUIRE(v == vTri);   break;
            case LfoShape::Square:    REQUIRE(v == vSq);    break;
            case LfoShape::Random:    REQUIRE(v == vRnd);   break;
            case LfoShape::Noise:     REQUIRE(v == vNoise); break;
        }
    }
}
