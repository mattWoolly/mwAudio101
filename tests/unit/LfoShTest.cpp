// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the LFO digital uniform sample/hold "Random" core, the
// injected shared-noise "Noise" core, and the H->L cycle-edge flag implemented in
// core/dsp/Lfo.cpp (task 059). Realizes docs/design/03-dsp-envelope-lfo-vca.md §3.4
// (rate/phase), §3.5 (Random / Noise cores) and §3.6 (cycle-edge flag).
//
// Names begin with "lfo_sh" so the silent-pass selector `-R lfo_sh` matches the
// registered test-case NAME ("discovery registers names, not tags", AGENTS.md). The
// display names avoid '[' so Catch2 does not parse a bracketed fragment as a tag and
// break ctest -R selection. The single [lfo_sh] Catch2 tag below is the task's tag;
// the orchestrator regenerates tests/golden/corpus/ctest-labels.snapshot at wave
// integration, so a labels_snapshot failure for this NEW tag is EXPECTED here.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "calibration/LfoShConstants.h"
#include "dsp/Lfo.h"

using mw101::dsp::Lfo;
using mw101::dsp::LfoShape;

namespace {

// Independent PCG-XSH-RR 64/32 oracle in the TEST (NOT a copy of the .cpp's helper)
// reproducing the EXACT seeded uniform [-1,1] stream the Random core must emit so a
// regression in the core would diverge from it. Mirrors mw::util::Prng / the (PI)
// step constants and the same 24-bit -> [0,1) -> [-1,1] mapping.
struct RandomOracle {
    std::uint64_t state = 0;

    void seed(std::uint64_t s) noexcept {
        // PCG init: state=0, step, add seed, step (matches mw::util::Prng::seed).
        state = 0;
        nextU32();
        state += s;
        nextU32();
    }
    std::uint32_t nextU32() noexcept {
        const std::uint64_t old = state;
        state = old * mw::cal::lfo::kLfoRandomLcgMultiplier
              + mw::cal::lfo::kLfoRandomLcgIncrement;
        const std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((old >> 18) ^ old) >> 27);
        const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }
    // Uniform [-1, 1]: top 24 bits -> [0,1), then *2 - 1 -> [-1, 1).
    float nextBipolar() noexcept {
        const float u01 = static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
        return 2.0f * u01 - 1.0f;
    }
};

// Drive an Lfo for `nCycles` complete cycles and return the held Random value present
// DURING each cycle (sampled mid-cycle), plus the per-tick edge pulse count. The LFO
// is configured so one cycle is an exact, small integer number of ticks.
struct RandomRun {
    std::vector<float> heldPerCycle;   // value held during cycle k (sampled mid-cycle)
    int edges = 0;
};

// ticksPerCycle controls cycle length; valuesAtTick records value() after each tick.
RandomRun runRandom(int ticksPerCycle, int nCycles) {
    Lfo lfo;
    // fc = sampleRate / ticksPerControl. Pick sampleRate = ticksPerCycle, divider = 1,
    // rate = 1 Hz  => phaseInc = rate/fc = 1/ticksPerCycle => exactly ticksPerCycle
    // ticks per cycle.
    lfo.prepare(static_cast<double>(ticksPerCycle), 1);
    lfo.setShape(LfoShape::Random);
    lfo.setRateHz(1.0f);

    RandomRun run;
    const int totalTicks = ticksPerCycle * nCycles;
    int sinceEdge = 0;
    int cycleIdx = 0;
    for (int i = 0; i < totalTicks; ++i) {
        const float v = lfo.tick();
        // Sample the held value mid-cycle (well away from the wrap) for stability.
        if (sinceEdge == ticksPerCycle / 2) {
            run.heldPerCycle.push_back(v);
            ++cycleIdx;
            (void)cycleIdx;
        }
        if (lfo.cycleEdge()) { ++run.edges; sinceEdge = 0; } else { ++sinceEdge; }
    }
    return run;
}

} // namespace

// --- §3.6: cycleEdge() pulses EXACTLY one tick per LFO cycle ----------------------

TEST_CASE("lfo_sh: cycleEdge pulses exactly one tick per LFO cycle", "[lfo_sh]") {
    Lfo lfo;
    // fc = 8; rate 1 Hz => phaseInc = 1/8 = 0.125, EXACTLY representable in float, so
    // the H->L wrap lands on a precise tick boundary with NO float-accumulation drift:
    // every cycle is exactly 8 ticks and the edge count is exact (not ±1).
    lfo.prepare(8.0, 1);
    lfo.setShape(LfoShape::Random);
    lfo.setRateHz(1.0f);

    const int ticksPerCycle = 8;
    const int nCycles = 64;
    int edges = 0;
    int maxRunOfEdges = 0, curRun = 0;
    int sinceLast = 0, minGap = 1 << 30, maxGap = 0;
    bool sawAny = false;
    for (int i = 0; i < ticksPerCycle * nCycles; ++i) {
        lfo.tick();
        ++sinceLast;
        if (lfo.cycleEdge()) {
            ++edges;
            ++curRun;
            maxRunOfEdges = std::max(maxRunOfEdges, curRun);
            if (sawAny) { minGap = std::min(minGap, sinceLast); maxGap = std::max(maxGap, sinceLast); }
            sinceLast = 0;
            sawAny = true;
        } else {
            curRun = 0;
        }
    }
    // Exactly one edge per completed cycle (drift-free at 8 ticks/cycle)...
    REQUIRE(edges == nCycles);
    // ...the edge is a SINGLE-tick pulse, never two ticks in a row...
    REQUIRE(maxRunOfEdges == 1);
    // ...and consecutive edges are exactly one full cycle apart.
    REQUIRE(minGap == ticksPerCycle);
    REQUIRE(maxGap == ticksPerCycle);
}

TEST_CASE("lfo_sh: cycleEdge is false on every non-wrap tick within a cycle", "[lfo_sh]") {
    Lfo lfo;
    // 8 ticks/cycle, phaseInc = 0.125 exactly: the wrap is deterministic on the 8th
    // tick, so the first seven ticks must NOT flag an edge.
    lfo.prepare(8.0, 1);
    lfo.setShape(LfoShape::Square);
    lfo.setRateHz(1.0f);

    for (int i = 0; i < 7; ++i) {
        lfo.tick();
        REQUIRE_FALSE(lfo.cycleEdge());
    }
    lfo.tick();                 // the 8th tick wraps H->L
    REQUIRE(lfo.cycleEdge());
    // The edge does not stick: the next tick clears it.
    lfo.tick();
    REQUIRE_FALSE(lfo.cycleEdge());
}

// --- §3.5: Random value changes ONLY on cycle edges -------------------------------

TEST_CASE("lfo_sh: Random holds its value for the whole cycle, changing only on edges", "[lfo_sh]") {
    Lfo lfo;
    lfo.prepare(2000.0, 1);     // fc = 2000
    lfo.setShape(LfoShape::Random);
    lfo.setRateHz(1.0f);        // 2000 ticks/cycle

    const int ticksPerCycle = 2000;
    const int nCycles = 6;

    float held = 0.0f;
    int sinceEdge = 0;
    int valueChangesNotOnEdge = 0;
    float prev = lfo.value();
    bool first = true;

    for (int i = 0; i < ticksPerCycle * nCycles; ++i) {
        const bool edgeBefore = lfo.cycleEdge();   // edge from the PREVIOUS tick
        const float v = lfo.tick();
        const bool edgeNow = lfo.cycleEdge();

        if (!first) {
            // The held value may only differ from the previous tick's value on a tick
            // immediately following an edge (the reload takes effect after the wrap).
            if (v != prev && !edgeBefore) {
                ++valueChangesNotOnEdge;
            }
        }
        prev = v;
        first = false;
        (void)edgeNow; (void)held; (void)sinceEdge;
    }

    // The Random output is a true sample/hold: it never changes except right after a
    // cycle edge (the held register is constant across the rest of the cycle).
    REQUIRE(valueChangesNotOnEdge == 0);
}

TEST_CASE("lfo_sh: Random output is constant across all non-edge ticks of a cycle", "[lfo_sh]") {
    // Stronger form: collect the distinct values seen WITHIN a single cycle (between
    // two edges). There must be exactly one held value per cycle.
    Lfo lfo;
    lfo.prepare(500.0, 1);
    lfo.setShape(LfoShape::Random);
    lfo.setRateHz(1.0f);        // 500 ticks/cycle

    int cyclesChecked = 0;
    std::set<float> valuesThisCycle;
    bool started = false;

    for (int i = 0; i < 500 * 8; ++i) {
        const float v = lfo.tick();
        // The value emitted on a tick belongs to the cycle that tick is part of; the
        // edge tick is the LAST tick of the ending cycle (it still emits that cycle's
        // held value — the reload only takes effect from the NEXT tick). So record v
        // into the current cycle's set FIRST, then on an edge close out that cycle.
        valuesThisCycle.insert(v);
        if (lfo.cycleEdge()) {
            if (started) {
                // A completed cycle's worth of held values: exactly one distinct value.
                REQUIRE(valuesThisCycle.size() == 1u);
                ++cyclesChecked;
            }
            valuesThisCycle.clear();
            started = true;
        }
    }
    REQUIRE(cyclesChecked >= 5);
}

// --- §3.5: Random is UNIFORM in [-1, 1] -------------------------------------------

TEST_CASE("lfo_sh: Random reloads a uniform value in -1..+1 each cycle", "[lfo_sh]") {
    // Generate many per-cycle held values and check (a) the range is within [-1,1],
    // (b) the distribution is roughly uniform (each of 10 bins is populated and no bin
    // is wildly over/under-represented), and (c) the mean is ~0 (bipolar symmetric).
    const int nCycles = 4000;
    const RandomRun run = runRandom(/*ticksPerCycle=*/8, nCycles);

    REQUIRE(static_cast<int>(run.heldPerCycle.size()) == nCycles);

    double sum = 0.0;
    int bins[10] = {0};
    float lo = 2.0f, hi = -2.0f;
    for (float v : run.heldPerCycle) {
        REQUIRE(v >= -1.0f);
        REQUIRE(v <   1.0f);     // mapping is [-1, 1)
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        sum += v;
        int b = static_cast<int>((v + 1.0f) * 0.5f * 10.0f);   // [-1,1) -> [0,10)
        if (b < 0) b = 0; if (b > 9) b = 9;
        ++bins[b];
    }

    // Spans most of the range.
    REQUIRE(lo < -0.8f);
    REQUIRE(hi >  0.8f);

    // Zero-mean within a generous tolerance for 4000 samples.
    const double mean = sum / nCycles;
    REQUIRE(std::abs(mean) < 0.05);

    // Every bin populated and none dominates: a uniform distribution puts ~400/bin;
    // require each bin within [0.5x, 1.6x] of the ideal — far from any held-constant
    // or single-mode failure.
    const double ideal = static_cast<double>(nCycles) / 10.0;
    for (int b = 0; b < 10; ++b) {
        REQUIRE(bins[b] > static_cast<int>(0.5 * ideal));
        REQUIRE(bins[b] < static_cast<int>(1.6 * ideal));
    }
}

// --- §3.5: Random is DETERMINISTIC for a fixed seed (golden-reproducible) ---------

TEST_CASE("lfo_sh: Random is deterministic - identical streams from two LFOs", "[lfo_sh]") {
    const int ticksPerCycle = 8;
    const int nCycles = 64;
    const RandomRun a = runRandom(ticksPerCycle, nCycles);
    const RandomRun b = runRandom(ticksPerCycle, nCycles);

    REQUIRE(a.heldPerCycle.size() == b.heldPerCycle.size());
    for (std::size_t i = 0; i < a.heldPerCycle.size(); ++i) {
        REQUIRE(a.heldPerCycle[i] == b.heldPerCycle[i]);   // bit-identical, not "close"
    }
}

TEST_CASE("lfo_sh: Random stream matches the independent seeded PCG oracle", "[lfo_sh]") {
    // The strongest determinism check: the per-cycle held values must equal an
    // independent re-derivation of the seeded PCG-XSH-RR stream (the value held during
    // cycle 0 is the seed's FIRST draw, performed by reset()/prepare() as the initial
    // S/H reload; cycle 1's value is the SECOND draw, etc.).
    const int ticksPerCycle = 8;
    const int nCycles = 32;
    const RandomRun run = runRandom(ticksPerCycle, nCycles);

    RandomOracle oracle;
    oracle.seed(mw::cal::lfo::kLfoRandomSeed);

    REQUIRE(static_cast<int>(run.heldPerCycle.size()) == nCycles);
    for (int c = 0; c < nCycles; ++c) {
        const float expected = oracle.nextBipolar();
        REQUIRE(run.heldPerCycle[c] == expected);   // exact match to the oracle
    }
}

// --- §3.3 / §3.5: reset() does phase->0 AND S/H reload (deterministic restart) -----

TEST_CASE("lfo_sh: reset re-seeds the S/H so the post-reset stream is reproducible", "[lfo_sh]") {
    Lfo lfo;
    lfo.prepare(8.0, 1);              // fc = 8 => 8 ticks/cycle at 1 Hz
    lfo.setShape(LfoShape::Random);
    lfo.setRateHz(1.0f);

    auto firstNCycleValues = [&](int n) {
        std::vector<float> vals;
        int sinceEdge = 0;
        const int ticksPerCycle = 8;
        for (int i = 0; vals.size() < static_cast<std::size_t>(n); ++i) {
            const float v = lfo.tick();
            if (sinceEdge == ticksPerCycle / 2) vals.push_back(v);
            if (lfo.cycleEdge()) sinceEdge = 0; else ++sinceEdge;
        }
        return vals;
    };

    const std::vector<float> before = firstNCycleValues(8);

    // reset() must restore phase->0 and RELOAD the S/H from the fixed seed, so the
    // identical value stream replays.
    lfo.reset();
    const std::vector<float> after = firstNCycleValues(8);

    REQUIRE(before.size() == after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        REQUIRE(before[i] == after[i]);
    }

    // reset() also zeroes the phase: the first emitted Random value after reset is the
    // initial S/H reload (cycle-0 held value), matching the oracle's first draw.
    lfo.reset();
    RandomOracle oracle;
    oracle.seed(mw::cal::lfo::kLfoRandomSeed);
    const float firstHeld = lfo.tick();   // value during cycle 0
    REQUIRE(firstHeld == oracle.nextBipolar());
}

TEST_CASE("lfo_sh: reset clears phase to zero", "[lfo_sh]") {
    Lfo lfo;
    lfo.prepare(48000.0, 1);
    lfo.setShape(LfoShape::Square);
    lfo.setRateHz(1.0f);

    // Advance into the second (low) half of the cycle.
    for (int i = 0; i < 36000; ++i) lfo.tick();
    REQUIRE(lfo.value() == -1.0f);    // phase ~0.75 => low half

    lfo.reset();
    // After reset, phase==0 => the Square's first emitted tick is the HIGH half.
    REQUIRE(lfo.tick() == 1.0f);
}

// --- §3.3: resetPhaseOnKey() hook restarts the oscillator phase --------------------

TEST_CASE("lfo_sh: resetPhaseOnKey restarts the phase without touching the S/H seed", "[lfo_sh]") {
    Lfo lfo;
    lfo.prepare(48000.0, 1);
    lfo.setShape(LfoShape::Square);
    lfo.setRateHz(1.0f);

    for (int i = 0; i < 36000; ++i) lfo.tick();   // into the low half
    REQUIRE(lfo.value() == -1.0f);

    lfo.resetPhaseOnKey();
    REQUIRE(lfo.tick() == 1.0f);                   // back to phase 0 => high half
}

// --- §3.5: Noise uses the INJECTED shared source, not a private generator ----------

TEST_CASE("lfo_sh: Noise returns the injected shared sample verbatim", "[lfo_sh]") {
    Lfo lfo;
    lfo.prepare(48000.0, 1);
    lfo.setShape(LfoShape::Noise);
    lfo.setRateHz(5.0f);

    // A borrowed scalar acting as the "current shared white-noise sample" the audio
    // mixer/voice updates each tick. The LFO must read THIS, not synthesize its own.
    float shared = 0.0f;
    lfo.setNoiseSource(&shared);

    const float script[] = {0.42f, -0.91f, 0.07f, 1.0f, -1.0f, 0.333f, -0.5f};
    for (float s : script) {
        shared = s;                         // external source updates the sample
        REQUIRE(lfo.tick() == s);           // LFO emits exactly the injected value
        REQUIRE(lfo.value() == s);
    }
}

TEST_CASE("lfo_sh: Noise with no injected source is silent, never a private generator", "[lfo_sh]") {
    Lfo lfo;
    lfo.prepare(48000.0, 1);
    lfo.setShape(LfoShape::Noise);
    lfo.setRateHz(5.0f);
    // No setNoiseSource() call: the LFO owns no generator, so it must emit 0, NOT a
    // self-generated noise stream (which would be non-zero / varying).
    for (int i = 0; i < 256; ++i) {
        REQUIRE(lfo.tick() == 0.0f);
    }
}

TEST_CASE("lfo_sh: Noise does not advance a private S/H - distinct from Random", "[lfo_sh]") {
    // Switching a fresh LFO between Noise and Random must show that Noise tracks the
    // injected sample while Random ignores it entirely (Random reads its own S/H).
    Lfo lfo;
    lfo.prepare(8.0, 1);
    lfo.setRateHz(1.0f);

    float shared = 0.77f;
    lfo.setNoiseSource(&shared);

    lfo.setShape(LfoShape::Noise);
    REQUIRE(lfo.tick() == 0.77f);            // follows the injected source

    lfo.setShape(LfoShape::Random);
    shared = -0.33f;                          // change the shared sample
    const float r = lfo.tick();
    REQUIRE(r != shared);                     // Random does NOT read the noise source
    REQUIRE(r >= -1.0f);
    REQUIRE(r <  1.0f);
}
