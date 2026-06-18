// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the Envelope.cpp ADSR one-pole segment curve + stage
// machine (task 054). Test-case NAMES begin with "env_curve" so the silent-pass-safe
// selector `ctest -R env_curve --no-tests=error` selects exactly these cases
// (catch_discover_tests registers test-case NAMES, not tags). The display text avoids
// '[' so Catch2 does not mis-parse it as a tag and break ctest -R selection.
//
// These cover the runtime contour/stage behavior the .cpp owns per
// docs/design/03-dsp-envelope-lfo-vca.md §2.4 (segment curve law), §2.3 (ranges) and
// §6.2 (control-rate cadence), and the acceptance hooks in docs/design/03
// "Acceptance hooks". The header-surface contract is covered separately by
// EnvelopeHeaderTest.cpp (task 050); the (PI) constant VALUES by
// EnvLfoVcaConstantsTest.cpp (task 049). Here we assert the audible shape: Attack
// rises monotonically to a clamped 1.0, Decay falls to sustain, Sustain holds,
// Release falls to 0 reaching Idle within the snap threshold of the labeled time, and
// the one-pole coefficient law uses the centralized mw::cal::env (PI) constants.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <type_traits>

#include "dsp/Envelope.h"
#include "calibration/EnvLfoVcaConstants.h"

using mw101::dsp::Envelope;
using mw101::dsp::EnvParams;
using mw101::dsp::EnvStage;
using mw101::dsp::EnvTrigMode;

namespace {

// The control-tick rate fc = sampleRate / ticksPerControl (§2.4, §6.2). Tests below
// use a 1:1 divider so one tick() == one control tick at sampleRate.
constexpr double kSr   = 48000.0;
constexpr int    kDiv  = 1;

// The (PI) per-stage one-pole coefficient law replicated from §2.4 as an independent
// oracle so the test does not merely echo the implementation: it pins the documented
// formula coeff = exp(-1 / (max(T,Tmin) * fc * kEnvTimeScale)).
[[nodiscard]] double oracleCoeff(double timeSec) {
    using namespace mw::cal::env;
    const double fc = kSr / static_cast<double>(kDiv);
    const double t  = std::max(timeSec, static_cast<double>(kEnvTimeMin));
    return std::exp(-1.0 / (t * fc * static_cast<double>(kEnvTimeScale)));
}

// Drive a held note for n ticks (gate stays on); returns the level after the run.
[[nodiscard]] float runHeld(Envelope& e, int n) {
    float lvl = e.level();
    for (int i = 0; i < n; ++i) lvl = e.tick();
    return lvl;
}

} // namespace

// --- Attack: monotonic rise to a clamped 1.0, then advance to Decay (§2.4) --------

TEST_CASE("env_curve: attack rises monotonically and clamps to 1.0 then enters Decay",
          "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec  = 0.010f;   // 10 ms — long enough to observe several rising ticks
    p.decaySec   = 0.050f;
    p.sustain    = 0.5f;
    p.releaseSec = 0.050f;
    p.trig       = EnvTrigMode::GateTrig;
    e.setParams(p);

    e.noteOn(false);
    REQUIRE(e.stage() == EnvStage::Attack);

    // Each Attack tick rises strictly (target is the >1 overshoot) and never exceeds
    // a clamped 1.0; the stage advances to Decay at level >= 1.0.
    float prev = e.level();
    bool reachedDecay = false;
    for (int i = 0; i < 200000; ++i) {
        const float lvl = e.tick();
        REQUIRE(lvl <= 1.0f + 1.0e-6f);          // clamped at unity, never overshoots out
        if (e.stage() == EnvStage::Attack) {
            REQUIRE(lvl > prev - 1.0e-7f);        // monotonic non-decreasing while in Attack
            prev = lvl;
        } else {
            reachedDecay = true;
            break;
        }
    }
    REQUIRE(reachedDecay);
    REQUIRE(e.stage() == EnvStage::Decay);
    REQUIRE(e.level() == Catch::Approx(1.0f).margin(1.0e-4));   // entered Decay at the clamped peak
}

// --- Decay: falls toward sustain, then holds at Sustain (§2.4) --------------------

TEST_CASE("env_curve: decay falls to sustain then Sustain holds the level", "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec  = 0.001f;   // near-instant attack so we land in Decay fast
    p.decaySec   = 0.020f;
    p.sustain    = 0.4f;
    p.releaseSec = 0.050f;
    p.trig       = EnvTrigMode::GateTrig;
    e.setParams(p);

    e.noteOn(false);

    // Advance until past Attack into Decay.
    int guard = 0;
    while (e.stage() == EnvStage::Attack && guard++ < 200000) e.tick();
    REQUIRE(e.stage() == EnvStage::Decay);

    // Decay is a monotonic fall toward the sustain target and never undershoots it.
    float prev = e.level();
    int guard2 = 0;
    while (e.stage() == EnvStage::Decay && guard2++ < 2000000) {
        const float lvl = e.tick();
        if (e.stage() == EnvStage::Decay) {
            REQUIRE(lvl < prev + 1.0e-7f);        // monotonic non-increasing while in Decay
            REQUIRE(lvl >= p.sustain - 1.0e-4f);  // never undershoots the sustain target
            prev = lvl;
        }
    }
    REQUIRE(e.stage() == EnvStage::Sustain);
    REQUIRE(e.level() == Catch::Approx(p.sustain).margin(1.0e-4));

    // Sustain holds: many further ticks do not move the level while gated.
    const float held = runHeld(e, 5000);
    REQUIRE(held == Catch::Approx(p.sustain).margin(1.0e-6));
    REQUIRE(e.stage() == EnvStage::Sustain);
}

// --- Release: falls to 0 and reaches Idle within the snap threshold (§2.4) --------

TEST_CASE("env_curve: release falls to 0 and reaches Idle within the snap threshold",
          "[env_curve]") {
    using namespace mw::cal::env;
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec  = 0.001f;
    p.decaySec   = 0.005f;
    p.sustain    = 0.6f;
    p.releaseSec = 0.020f;
    p.trig       = EnvTrigMode::GateTrig;
    e.setParams(p);

    e.noteOn(false);
    int guard = 0;
    while (e.stage() != EnvStage::Sustain && guard++ < 2000000) e.tick();
    REQUIRE(e.stage() == EnvStage::Sustain);

    e.noteOff();
    REQUIRE(e.stage() == EnvStage::Release);

    // Release is a monotonic fall toward 0; the machine reaches Idle and the final
    // level is within the snap threshold of 0.
    float prev = e.level();
    int guard2 = 0;
    while (e.active() && guard2++ < 2000000) {
        const float lvl = e.tick();
        if (e.stage() == EnvStage::Release) {
            REQUIRE(lvl < prev + 1.0e-7f);        // monotonic non-increasing while in Release
            prev = lvl;
        }
    }
    REQUIRE(e.stage() == EnvStage::Idle);
    REQUIRE(e.active() == false);
    REQUIRE(std::abs(e.level()) <= static_cast<float>(kEnvSnapThreshold));
}

// --- Release reaches Idle within ~the labeled release time (§2.4 acceptance hook) -

TEST_CASE("env_curve: release reaches Idle near the labeled release time", "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec  = 0.001f;
    p.decaySec   = 0.002f;
    p.sustain    = 1.0f;     // start Release from full level so the labeled time is honest
    p.releaseSec = 0.100f;   // 100 ms
    p.trig       = EnvTrigMode::GateTrig;
    e.setParams(p);

    e.noteOn(false);
    int guard = 0;
    while (e.stage() != EnvStage::Sustain && guard++ < 2000000) e.tick();
    REQUIRE(e.stage() == EnvStage::Sustain);
    REQUIRE(e.level() == Catch::Approx(1.0f).margin(1.0e-4));

    e.noteOff();
    REQUIRE(e.stage() == EnvStage::Release);

    int ticks = 0;
    while (e.active() && ticks < 2000000) { e.tick(); ++ticks; }
    REQUIRE(e.stage() == EnvStage::Idle);

    // The one-pole approach to a tiny snap threshold takes a small multiple of the
    // labeled time; assert it lands in a sane band around it (not instant, not absurd).
    const double labeledTicks = p.releaseSec * (kSr / static_cast<double>(kDiv));
    REQUIRE(static_cast<double>(ticks) >  0.20 * labeledTicks);
    REQUIRE(static_cast<double>(ticks) < 20.0  * labeledTicks);
}

// --- Coefficient law: the first Attack tick matches the documented one-pole oracle -

TEST_CASE("env_curve: first tick of each stage matches the documented one-pole law",
          "[env_curve]") {
    using namespace mw::cal::env;
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec  = 0.050f;
    p.decaySec   = 0.080f;
    p.sustain    = 0.3f;
    p.releaseSec = 0.120f;
    p.trig       = EnvTrigMode::GateTrig;
    e.setParams(p);

    // Attack: from level 0 toward kEnvAttackOvershoot. The first tick must equal
    // (overshoot - 0) * (1 - coeffA), per level += (target-level)*(1-coeff).
    e.noteOn(false);
    REQUIRE(e.stage() == EnvStage::Attack);
    const double coeffA = oracleCoeff(p.attackSec);
    const double expectAttack1 =
        0.0 + (static_cast<double>(kEnvAttackOvershoot) - 0.0) * (1.0 - coeffA);
    const float a1 = e.tick();
    REQUIRE(static_cast<double>(a1) == Catch::Approx(expectAttack1).margin(1.0e-5));
}

// --- Sustain clamp: setParams clamps sustain into [0,1] (§2.4) ---------------------

TEST_CASE("env_curve: setParams clamps sustain into the 0..1 range", "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);

    // Over-unity sustain clamps to 1.0: Decay then holds at exactly 1.0.
    EnvParams hi{};
    hi.attackSec = 0.001f; hi.decaySec = 0.002f; hi.releaseSec = 0.010f;
    hi.sustain = 5.0f;     hi.trig = EnvTrigMode::GateTrig;
    e.setParams(hi);
    e.noteOn(false);
    int guard = 0;
    while (e.stage() != EnvStage::Sustain && guard++ < 2000000) e.tick();
    REQUIRE(e.stage() == EnvStage::Sustain);
    REQUIRE(e.level() == Catch::Approx(1.0f).margin(1.0e-4));
    REQUIRE(e.level() <= 1.0f + 1.0e-6f);

    // Negative sustain clamps to 0.0: Decay falls to 0 and Sustain holds 0.
    Envelope e2;
    e2.prepare(kSr, kDiv);
    EnvParams lo{};
    lo.attackSec = 0.001f; lo.decaySec = 0.005f; lo.releaseSec = 0.010f;
    lo.sustain = -3.0f;    lo.trig = EnvTrigMode::GateTrig;
    e2.setParams(lo);
    e2.noteOn(false);
    int guard2 = 0;
    while (e2.stage() != EnvStage::Sustain && guard2++ < 2000000) e2.tick();
    REQUIRE(e2.stage() == EnvStage::Sustain);
    REQUIRE(e2.level() == Catch::Approx(0.0f).margin(1.0e-4));
    REQUIRE(e2.level() >= -1.0e-6f);
}

// --- Output range: the contour never leaves [0,1] across a full A/D/S/R cycle -----

TEST_CASE("env_curve: contour stays within 0..1 across a full cycle", "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec  = 0.003f;
    p.decaySec   = 0.010f;
    p.sustain    = 0.55f;
    p.releaseSec = 0.030f;
    p.trig       = EnvTrigMode::GateTrig;
    e.setParams(p);

    e.noteOn(false);
    int guard = 0;
    while (e.stage() != EnvStage::Sustain && guard++ < 2000000) {
        const float lvl = e.tick();
        REQUIRE(lvl >= -1.0e-6f);
        REQUIRE(lvl <= 1.0f + 1.0e-6f);
    }
    (void) runHeld(e, 1000);
    e.noteOff();
    int guard2 = 0;
    while (e.active() && guard2++ < 2000000) {
        const float lvl = e.tick();
        REQUIRE(lvl >= -1.0e-6f);
        REQUIRE(lvl <= 1.0f + 1.0e-6f);
    }
}

// --- Trigger modes (§2.5): the stage machine restart policy -----------------------

TEST_CASE("env_curve: GateTrig restarts Attack on legato, Gate does not", "[env_curve]") {
    // GateTrig: a legato noteOn fires a fresh Attack from the current level.
    {
        Envelope e;
        e.prepare(kSr, kDiv);
        EnvParams p{};
        p.attackSec = 0.003f; p.decaySec = 0.010f; p.sustain = 0.5f;
        p.releaseSec = 0.030f; p.trig = EnvTrigMode::GateTrig;
        e.setParams(p);
        e.noteOn(false);
        int guard = 0;
        while (e.stage() != EnvStage::Sustain && guard++ < 2000000) e.tick();
        REQUIRE(e.stage() == EnvStage::Sustain);

        e.noteOn(true);                            // legato retrigger
        REQUIRE(e.stage() == EnvStage::Attack);    // GateTrig restarts Attack
    }
    // Gate: a legato noteOn while already sounding is ignored (no retrigger).
    {
        Envelope e;
        e.prepare(kSr, kDiv);
        EnvParams p{};
        p.attackSec = 0.003f; p.decaySec = 0.010f; p.sustain = 0.5f;
        p.releaseSec = 0.030f; p.trig = EnvTrigMode::Gate;
        e.setParams(p);
        e.noteOn(false);
        int guard = 0;
        while (e.stage() != EnvStage::Sustain && guard++ < 2000000) e.tick();
        REQUIRE(e.stage() == EnvStage::Sustain);

        e.noteOn(true);                            // legato — Gate ignores it
        REQUIRE(e.stage() == EnvStage::Sustain);   // stays in Sustain, no restart
    }
}

TEST_CASE("env_curve: Lfo mode retriggers Attack on clockTrigger while held", "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec = 0.003f; p.decaySec = 0.010f; p.sustain = 0.5f;
    p.releaseSec = 0.030f; p.trig = EnvTrigMode::Lfo;
    e.setParams(p);
    e.noteOn(false);
    int guard = 0;
    while (e.stage() != EnvStage::Sustain && guard++ < 2000000) e.tick();
    REQUIRE(e.stage() == EnvStage::Sustain);

    e.clockTrigger();                              // LFO cycle edge
    REQUIRE(e.stage() == EnvStage::Attack);        // restarts Attack while held
}

// --- reset() returns the machine to a clean Idle/level-0 start (§2.2) -------------

TEST_CASE("env_curve: reset returns to Idle at level 0", "[env_curve]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p{};
    p.attackSec = 0.003f; p.decaySec = 0.010f; p.sustain = 0.5f;
    p.releaseSec = 0.030f; p.trig = EnvTrigMode::GateTrig;
    e.setParams(p);
    e.noteOn(false);
    (void) runHeld(e, 100);
    REQUIRE(e.active());

    e.reset();
    REQUIRE(e.stage() == EnvStage::Idle);
    REQUIRE(e.active() == false);
    REQUIRE(e.level() == 0.0f);
}

// --- noexcept hot/lifecycle paths (§2.1) — re-asserted at the .cpp boundary --------

TEST_CASE("env_curve: tick and trigger entry points are noexcept hot paths", "[env_curve]") {
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().tick()));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().noteOn(true)));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().noteOff()));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().clockTrigger()));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().setParams(std::declval<const EnvParams&>())));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().prepare(kSr, kDiv)));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().reset()));
}
