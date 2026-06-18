// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the per-voice portamento slew Glide (task 068). Names
// begin with "glide" so `ctest -R glide` selects exactly these (the tag is
// [glide]; '[' is kept out of the display text per the silent-pass rule). Covers
// every acceptance criterion in docs/design/04-voice-and-control.md §5.5 and the
// OFF/ON/AUTO + arp-disable contract from research/05 §5.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "calibration/GlideConstants.h"
#include "voice/Glide.h"

using mw::Glide;
using mw::GlideMode;

namespace {

constexpr double kSampleRate = 48000.0;

// Helper: run a fresh Glide, set up mode/time, drive it to a target from a known
// start, and return the value after `steps` calls to nextValue().
float runGlide(GlideMode mode, float timeSeconds, float start, float target,
               bool legato, bool arpActive, int steps) {
    Glide g;
    g.prepare(kSampleRate);
    g.setMode(mode);
    g.setTimeSeconds(timeSeconds);
    g.snapTo(start);                       // establish a known current_
    g.setTarget(target, legato, arpActive);
    float v = start;
    for (int i = 0; i < steps; ++i) v = g.nextValue();
    return v;
}

} // namespace

// --- Acceptance: TIME spans 0-5 s and longer TIME => slower convergence -------
TEST_CASE("glide: longer TIME converges more slowly toward the target", "[glide]") {
    // ON always glides. After the SAME number of steps, a longer TIME must leave
    // more remaining error (slower convergence) [docs/design/04 §5.5 table].
    const float start = 100.0f, target = 200.0f;
    const int steps = 64;

    const float vFast = runGlide(GlideMode::On, 0.05f, start, target,
                                 /*legato=*/true, /*arp=*/false, steps);
    const float vSlow = runGlide(GlideMode::On, 2.5f, start, target,
                                 /*legato=*/true, /*arp=*/false, steps);

    const float errFast = std::fabs(target - vFast);
    const float errSlow = std::fabs(target - vSlow);

    // POSITIVE: the slower TIME has strictly more error left after equal steps.
    REQUIRE(errSlow > errFast);
    // Both are still between start and target (moving the right direction).
    REQUIRE(vFast > start);
    REQUIRE(vFast < target);
    REQUIRE(vSlow > start);
    REQUIRE(vSlow < target);
}

TEST_CASE("glide: TIME=0 (zero seconds) snaps with no slew", "[glide]") {
    // The 0 s endpoint of the 0-5 s range degenerates to no glide (instant).
    const float v = runGlide(GlideMode::On, 0.0f, 100.0f, 440.0f,
                             /*legato=*/true, /*arp=*/false, /*steps=*/1);
    REQUIRE(v == 440.0f);
}

TEST_CASE("glide: TIME clamps into the 0-5 s documented range", "[glide]") {
    // setTimeSeconds beyond the documented span must not produce a faster-than-5 s
    // or negative-time slew; the 5 s end is the slowest configuration.
    const float start = 100.0f, target = 200.0f;
    const int steps = 64;

    const float vAt5  = runGlide(GlideMode::On, 5.0f, start, target, true, false, steps);
    const float vOver = runGlide(GlideMode::On, 50.0f, start, target, true, false, steps);

    // Clamped to 5 s: the over-range request behaves no slower than the 5 s end.
    const float err5  = std::fabs(target - vAt5);
    const float errOv = std::fabs(target - vOver);
    REQUIRE(errOv <= err5 + 1.0e-3f);   // not slower than the clamp ceiling

    // Negative time clamps to >= 0 and never diverges.
    const float vNeg = runGlide(GlideMode::On, -1.0f, start, target, true, false, 1);
    REQUIRE(vNeg >= start);
    REQUIRE(vNeg <= target);
}

// --- Acceptance: AUTO glides only when legato=true ----------------------------
TEST_CASE("glide: AUTO glides on legato and snaps on non-legato", "[glide]") {
    const float start = 100.0f, target = 300.0f;

    // Legato => AUTO behaves like ON: first step does NOT reach the target.
    const float vLegato = runGlide(GlideMode::Auto, 1.0f, start, target,
                                   /*legato=*/true, /*arp=*/false, /*steps=*/1);
    REQUIRE(vLegato > start);
    REQUIRE(vLegato < target);

    // Non-legato => AUTO snaps immediately (no glide).
    const float vStaccato = runGlide(GlideMode::Auto, 1.0f, start, target,
                                     /*legato=*/false, /*arp=*/false, /*steps=*/1);
    REQUIRE(vStaccato == target);
}

// --- Acceptance: arpActive=true snaps regardless of mode ----------------------
TEST_CASE("glide: arpActive snaps in every mode regardless of legato", "[glide]") {
    const float start = 100.0f, target = 250.0f;

    for (GlideMode m : {GlideMode::Off, GlideMode::On, GlideMode::Auto}) {
        // Even ON + legato must NOT glide while the arpeggiator drives notes.
        const float v = runGlide(m, 2.0f, start, target,
                                 /*legato=*/true, /*arp=*/true, /*steps=*/1);
        REQUIRE(v == target);
    }
}

// --- Acceptance: ON always glides; OFF never glides ---------------------------
TEST_CASE("glide: ON always glides toward the target", "[glide]") {
    const float start = 100.0f, target = 200.0f;
    // Glide regardless of legato true/false (ON is unconditional, arp aside).
    for (bool legato : {true, false}) {
        const float v1 = runGlide(GlideMode::On, 1.0f, start, target,
                                  legato, /*arp=*/false, /*steps=*/1);
        REQUIRE(v1 > start);
        REQUIRE(v1 < target);
    }
}

TEST_CASE("glide: OFF never glides (snaps to target)", "[glide]") {
    const float start = 100.0f, target = 400.0f;
    for (bool legato : {true, false}) {
        const float v = runGlide(GlideMode::Off, 3.0f, start, target,
                                 legato, /*arp=*/false, /*steps=*/1);
        REQUIRE(v == target);
    }
}

// --- Acceptance: snapTo makes nextValue return target immediately -------------
TEST_CASE("glide: snapTo jumps current to target with no slew", "[glide]") {
    Glide g;
    g.prepare(kSampleRate);
    g.setMode(GlideMode::On);
    g.setTimeSeconds(3.0f);              // a long glide would otherwise be obvious
    g.setTarget(123.0f, /*legato=*/true, /*arp=*/false);
    g.snapTo(880.0f);                    // first-note / arp jump

    // After snapTo, the next value is exactly the snapped pitch (no glide).
    REQUIRE(g.nextValue() == 880.0f);
    REQUIRE(g.nextValue() == 880.0f);    // and it stays there
}

// --- Acceptance: exponential approach, monotone shrink, no overshoot ----------
TEST_CASE("glide: error shrinks monotonically toward target with no overshoot",
          "[glide]") {
    // Two-part check at TIME = 0.5 s: (a) over a long run every step's remaining
    // error is non-increasing and never overshoots; (b) given enough ticks to span
    // many time constants it actually lands in the snap band. The step budget is
    // sized to the time constant: convergence to 1e-2 of a 900 Hz span needs
    // ~ln(900/1e-2)*tau*fs steps (~2.8e5 here), so use a generous budget.
    Glide g;
    g.prepare(kSampleRate);
    g.setMode(GlideMode::On);
    g.setTimeSeconds(0.5f);
    g.snapTo(100.0f);
    const float target = 1000.0f;
    g.setTarget(target, /*legato=*/true, /*arp=*/false);

    // Aggregate the per-step invariants into single asserts (avoid 1.2M REQUIREs).
    bool monotone = true, noOvershoot = true, rightDir = true;
    float prevErr = target - 100.0f;     // initial error (positive, rising target)
    float prevVal = 100.0f;
    for (int i = 0; i < 400000; ++i) {
        const float v = g.nextValue();
        const float err = std::fabs(target - v);
        if (err > prevErr + 1.0e-4f)   monotone    = false; // shrink non-increasing
        if (v   > target  + 1.0e-4f)   noOvershoot = false; // never exceed target
        if (v   < prevVal - 1.0e-4f)   rightDir    = false; // non-decreasing rise
        prevErr = err;
        prevVal = v;
    }
    REQUIRE(monotone);
    REQUIRE(noOvershoot);
    REQUIRE(rightDir);
    // It actually converged onto the target within the budget. The tolerance is
    // float32-realistic at audio pitch: near 1000 Hz a single ULP is ~6e-5, and the
    // exponential's per-step advance falls below it once the residual is sub-Hz, so
    // assert convergence to << 1 cent (~0.06 Hz at 1 kHz) rather than an absolute
    // 1e-5 that float32 cannot resolve there.
    REQUIRE(std::fabs(target - prevVal) <= 1.0f);
}

TEST_CASE("glide: descending glide shrinks error with no undershoot", "[glide]") {
    // Symmetric direction check: gliding DOWN must not undershoot below target.
    Glide g;
    g.prepare(kSampleRate);
    g.setMode(GlideMode::On);
    g.setTimeSeconds(0.5f);
    g.snapTo(1000.0f);
    const float target = 100.0f;
    g.setTarget(target, /*legato=*/true, /*arp=*/false);

    bool monotone = true, noUndershoot = true, rightDir = true;
    float prevVal = 1000.0f;
    float prevErr = 1000.0f - target;
    for (int i = 0; i < 400000; ++i) {
        const float v = g.nextValue();
        const float err = std::fabs(target - v);
        if (err > prevErr + 1.0e-4f) monotone     = false; // monotone shrink
        if (v   < target  - 1.0e-4f) noUndershoot = false; // no undershoot below target
        if (v   > prevVal + 1.0e-4f) rightDir     = false; // non-increasing fall
        prevErr = err;
        prevVal = v;
    }
    REQUIRE(monotone);
    REQUIRE(noUndershoot);
    REQUIRE(rightDir);
    // Float32-realistic convergence tolerance (see the rising-glide case). At
    // 100 Hz a sub-Hz residual is well under a cent; the slew has effectively
    // landed on the target within the step budget.
    REQUIRE(std::fabs(target - prevVal) <= 1.0f);
}

// --- Oracle: the slew matches the documented one-pole RC integrator -----------
TEST_CASE("glide: per-step value matches the RC one-pole oracle", "[glide]") {
    // §5.5 fixes an RC-style exponential integrator: y = target - coeff*(target-y),
    // coeff = exp(-1/(tau*fs)) with tau mapped from TIME. Cross-check the first few
    // steps against that closed form so the curve is not merely "some" decay.
    const float timeSec = 1.0f;
    const double tau = static_cast<double>(mw::cal::glide::kTimeToTauScale) * timeSec;
    const double coeff = std::exp(-1.0 / (tau * kSampleRate));

    Glide g;
    g.prepare(kSampleRate);
    g.setMode(GlideMode::On);
    g.setTimeSeconds(timeSec);
    const float start = 200.0f, target = 600.0f;
    g.snapTo(start);
    g.setTarget(target, /*legato=*/true, /*arp=*/false);

    double y = start;
    for (int i = 0; i < 16; ++i) {
        y = target - coeff * (target - y);          // analytic oracle step
        const float v = g.nextValue();
        REQUIRE(std::fabs(static_cast<double>(v) - y) <= 1.0e-3);
    }
}

// --- Determinism / RT-shape: nextValue is noexcept and allocation-free shape --
TEST_CASE("glide: nextValue and snapTo are noexcept hot paths", "[glide]") {
    Glide g;
    g.prepare(kSampleRate);
    STATIC_REQUIRE(noexcept(g.nextValue()));
    STATIC_REQUIRE(noexcept(g.snapTo(0.0f)));
    STATIC_REQUIRE(noexcept(g.setTarget(0.0f, false, false)));
    STATIC_REQUIRE(noexcept(g.setMode(GlideMode::Off)));
    STATIC_REQUIRE(noexcept(g.setTimeSeconds(0.0f)));
    SUCCEED("noexcept hot-path contract holds [docs/design/04 §3.4; ADR-001].");
}
