// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EnvelopeTriggerTest.cpp — the ADSR TRIGGER STATE MACHINE
// (GATE+TRIG / GATE / LFO) acceptance suite (task 058).
//
// Test-case NAMES begin with "env_trig" so the silent-pass-safe selector
// `ctest --preset default -R env_trig --no-tests=error` selects exactly this suite
// (catch_discover_tests registers test-case NAMES, not tags; AGENTS.md "Tests").
// The display text avoids '[' so Catch2 does not mis-parse it as a tag and break the
// `-R env_trig` selection.
//
// Scope (task 058) — the noteOn(legato)/noteOff/clockTrigger() semantics of the three
// trigger modes [docs/design/03-dsp-envelope-lfo-vca.md §2.5, §2.3 acceptance hook;
// §2.2 release]. This is the behavioral state machine only; the segment-curve math is
// owned by EnvelopeCurveTest.cpp (task 054) and the (PI) constant VALUES by
// EnvLfoVcaConstantsTest.cpp (task 049).
//
// Each TEST_CASE maps to a task-058 acceptance criterion, covering EVERY mode
// positive AND negative:
//   GateTrig — retriggers on legato (positive) ............... §2.5
//   Gate     — legato is ignored while non-Idle (negative) .... §2.5
//   Lfo      — clockTrigger() retriggers while held (positive)  §2.5/§3.6
//   Lfo      — a legato press while sounding does NOT retrigger (negative) §2.5
//   GateTrig — retrigger continues FROM the current level (no snap-to-0,
//              no discontinuity) — the v1 (PI) open-gap choice .. §2.5
//   noteOff  — -> Release in all three modes ................... §2.2
//   clockTrigger() — no effect outside Lfo mode (negative) ..... §2.5
//
// Real-time invariant: the trigger entry points and tick() are noexcept and take no
// heap allocation / no lock under the armed AudioThreadGuard sentinel
// [docs/design/03 §2.1; docs/design/00 §9.1 RT-1/RT-2/RT-4; ADR-001 C3/C4/C5].

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <type_traits>

#include "dsp/Envelope.h"
#include "calibration/EnvLfoVcaConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::Envelope;
using mw101::dsp::EnvParams;
using mw101::dsp::EnvStage;
using mw101::dsp::EnvTrigMode;

namespace {

// Control-tick rate fc = sampleRate / ticksPerControl (§2.4, §6.2). A 1:1 divider so
// one tick() == one control tick at sampleRate.
constexpr double kSr  = 48000.0;
constexpr int    kDiv = 1;

// Build a fresh, prepared, configured envelope for a given trigger mode. Times are
// generous (multi-ms) so each stage is observable across many ticks and an attack
// never completes in a single tick.
[[nodiscard]] EnvParams makeParams(EnvTrigMode mode) {
    EnvParams p{};
    p.attackSec  = 0.010f;   // 10 ms — several rising ticks before clamping to 1.0
    p.decaySec   = 0.050f;
    p.sustain    = 0.5f;
    p.releaseSec = 0.050f;
    p.trig       = mode;
    return p;
}

// Advance the held envelope until it reaches Sustain (gate on); returns false if it
// did not converge within a sane guard. Used to put the machine into a known steady
// state before exercising a retrigger.
[[nodiscard]] bool runToSustain(Envelope& e) {
    int guard = 0;
    while (e.stage() != EnvStage::Sustain && guard++ < 2'000'000) e.tick();
    return e.stage() == EnvStage::Sustain;
}

} // namespace

// =====================================================================================
// GateTrig (positive): every note-on, INCLUDING legato, fires a fresh Attack. §2.5
// =====================================================================================

TEST_CASE("env_trig: GateTrig restarts Attack on a legato note-on", "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    e.setParams(makeParams(EnvTrigMode::GateTrig));

    e.noteOn(/*legato=*/false);
    REQUIRE(e.stage() == EnvStage::Attack);   // initial gate opens Attack
    REQUIRE(runToSustain(e));                 // settle to a steady held state

    e.noteOn(/*legato=*/true);                // legato re-press (a trill)
    REQUIRE(e.stage() == EnvStage::Attack);   // GateTrig ALWAYS restarts Attack

    // A second legato re-press also restarts (rapid restarts / trills are the point).
    REQUIRE(runToSustain(e));
    e.noteOn(/*legato=*/true);
    REQUIRE(e.stage() == EnvStage::Attack);
}

// =====================================================================================
// Gate (negative): a legato note-on while non-Idle is IGNORED; only the initial
// gate (from Idle) starts Attack. §2.5
// =====================================================================================

TEST_CASE("env_trig: Gate ignores a legato note-on while sounding and only the initial gate attacks",
          "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    e.setParams(makeParams(EnvTrigMode::Gate));

    e.noteOn(/*legato=*/false);
    REQUIRE(e.stage() == EnvStage::Attack);   // initial (non-legato) gate opens Attack
    REQUIRE(runToSustain(e));

    e.noteOn(/*legato=*/true);                // legato re-press while sounding
    REQUIRE(e.stage() == EnvStage::Sustain);  // IGNORED — no restart, stays in Sustain

    // Even mid-Attack, a legato press must not re-attack (one shot per held gate).
    Envelope e2;
    e2.prepare(kSr, kDiv);
    e2.setParams(makeParams(EnvTrigMode::Gate));
    e2.noteOn(/*legato=*/false);
    e2.tick();                                 // partway up the Attack
    REQUIRE(e2.stage() == EnvStage::Attack);
    const float midAttack = e2.level();
    e2.noteOn(/*legato=*/true);                // ignored
    REQUIRE(e2.stage() == EnvStage::Attack);   // still the SAME attack, not restarted
    REQUIRE(e2.level() == Catch::Approx(midAttack).margin(1.0e-7));  // level untouched
}

// =====================================================================================
// Lfo (positive): clockTrigger() restarts Attack while held, independent of new key
// presses. §2.5 / §3.6
// =====================================================================================

TEST_CASE("env_trig: Lfo mode retriggers Attack on clockTrigger while a key is held",
          "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    e.setParams(makeParams(EnvTrigMode::Lfo));

    e.noteOn(/*legato=*/false);
    REQUIRE(e.stage() == EnvStage::Attack);   // initial key press from Idle opens it
    REQUIRE(runToSustain(e));

    e.clockTrigger();                          // LFO cycle edge
    REQUIRE(e.stage() == EnvStage::Attack);    // restarts Attack while held

    // Each subsequent edge re-fires, independent of any key press.
    REQUIRE(runToSustain(e));
    e.clockTrigger();
    REQUIRE(e.stage() == EnvStage::Attack);
}

// =====================================================================================
// Lfo (negative): a legato key press while already sounding does NOT retrigger — only
// the clock edge does. §2.5
// =====================================================================================

TEST_CASE("env_trig: Lfo mode does not retrigger on a legato note-on while sounding",
          "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    e.setParams(makeParams(EnvTrigMode::Lfo));

    e.noteOn(/*legato=*/false);
    REQUIRE(runToSustain(e));

    e.noteOn(/*legato=*/true);                 // a new key while held — NOT a clock edge
    REQUIRE(e.stage() == EnvStage::Sustain);   // unchanged; only clockTrigger() re-fires

    // Confirm the edge is what fires it (positive control for this negative test).
    e.clockTrigger();
    REQUIRE(e.stage() == EnvStage::Attack);
}

// =====================================================================================
// (PI) open-gap choice: a GateTrig retrigger continues FROM the current level — it
// does NOT snap to 0 — so there is no discontinuity / click. §2.5; research/04 §5.3.
// This is the load-bearing acceptance criterion: "GateTrig retrigger continues from
// current level (no discontinuity)".
// =====================================================================================

TEST_CASE("env_trig: GateTrig retrigger re-attacks from the current level with no discontinuity",
          "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    EnvParams p = makeParams(EnvTrigMode::GateTrig);
    p.sustain = 0.6f;        // a clearly non-zero level to retrigger from
    e.setParams(p);

    e.noteOn(/*legato=*/false);
    REQUIRE(runToSustain(e));
    const float levelBefore = e.level();
    REQUIRE(levelBefore == Catch::Approx(p.sustain).margin(1.0e-4));
    REQUIRE(levelBefore > 0.1f);                 // genuinely non-zero starting point

    e.noteOn(/*legato=*/true);                   // GateTrig retrigger
    REQUIRE(e.stage() == EnvStage::Attack);

    // The (PI) v1 choice: the level is CONTINUOUS across the retrigger — it is NOT
    // snapped to 0. Immediately after re-attacking, the level still equals the level
    // we held before the trigger (the trigger only re-points the stage/target).
    REQUIRE(e.level() == Catch::Approx(levelBefore).margin(1.0e-6));

    // The first tick of the new Attack moves the level UP toward the >1 overshoot
    // (no downward jump, no zero crossing) — a smooth, click-free restart.
    const float afterTick = e.tick();
    REQUIRE(afterTick > levelBefore - 1.0e-6f);  // monotone non-decreasing, no snap-to-0
    REQUIRE(afterTick >= levelBefore);           // genuinely re-charging upward

    // Retrigger from a HIGH point too (mid-Attack, above sustain): still continuous.
    Envelope e2;
    e2.prepare(kSr, kDiv);
    e2.setParams(p);
    e2.noteOn(/*legato=*/false);
    for (int i = 0; i < 5 && e2.stage() == EnvStage::Attack; ++i) e2.tick();
    REQUIRE(e2.stage() == EnvStage::Attack);
    const float highLevel = e2.level();
    REQUIRE(highLevel > 0.0f);
    e2.noteOn(/*legato=*/true);
    REQUIRE(e2.stage() == EnvStage::Attack);
    REQUIRE(e2.level() == Catch::Approx(highLevel).margin(1.0e-6));  // continuous
}

// =====================================================================================
// noteOff -> Release in ALL three modes. §2.2
// =====================================================================================

TEST_CASE("env_trig: noteOff drives Release in every trigger mode", "[env_trig]") {
    for (EnvTrigMode mode : {EnvTrigMode::GateTrig, EnvTrigMode::Gate, EnvTrigMode::Lfo}) {
        Envelope e;
        e.prepare(kSr, kDiv);
        e.setParams(makeParams(mode));

        e.noteOn(/*legato=*/false);
        REQUIRE(runToSustain(e));
        const float held = e.level();
        REQUIRE(held > 0.0f);

        e.noteOff();
        REQUIRE(e.stage() == EnvStage::Release);   // every mode releases on gate-off
        REQUIRE(e.level() == Catch::Approx(held).margin(1.0e-6));  // release is continuous too

        // Release runs down to Idle.
        int guard = 0;
        while (e.active() && guard++ < 2'000'000) e.tick();
        REQUIRE(e.stage() == EnvStage::Idle);
        REQUIRE_FALSE(e.active());
    }
}

// =====================================================================================
// noteOff from Idle is a no-op (nothing to release). §2.2
// =====================================================================================

TEST_CASE("env_trig: noteOff from Idle stays Idle", "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    e.setParams(makeParams(EnvTrigMode::GateTrig));
    REQUIRE(e.stage() == EnvStage::Idle);

    e.noteOff();                               // no gate to release
    REQUIRE(e.stage() == EnvStage::Idle);
    REQUIRE(e.level() == 0.0f);
}

// =====================================================================================
// clockTrigger() (negative): is a no-op OUTSIDE Lfo mode — only the LFO clock owns the
// envelope's retrigger; GateTrig/Gate ignore a spurious clock edge. §2.5
// =====================================================================================

TEST_CASE("env_trig: clockTrigger is a no-op outside Lfo mode", "[env_trig]") {
    for (EnvTrigMode mode : {EnvTrigMode::GateTrig, EnvTrigMode::Gate}) {
        Envelope e;
        e.prepare(kSr, kDiv);
        e.setParams(makeParams(mode));

        e.noteOn(/*legato=*/false);
        REQUIRE(runToSustain(e));
        const float held = e.level();

        e.clockTrigger();                       // spurious clock edge
        REQUIRE(e.stage() == EnvStage::Sustain);  // NOT re-attacked
        REQUIRE(e.level() == Catch::Approx(held).margin(1.0e-7));  // untouched
    }

    // From Idle, a clockTrigger() in GateTrig/Gate also does nothing.
    Envelope idle;
    idle.prepare(kSr, kDiv);
    idle.setParams(makeParams(EnvTrigMode::Gate));
    idle.clockTrigger();
    REQUIRE(idle.stage() == EnvStage::Idle);
}

// =====================================================================================
// Lfo: an initial key press from Idle opens Attack (the gate still arms the machine)
// even though subsequent retriggers come from the clock, not new presses. §2.5
// =====================================================================================

TEST_CASE("env_trig: Lfo mode opens Attack on the initial key press from Idle", "[env_trig]") {
    Envelope e;
    e.prepare(kSr, kDiv);
    e.setParams(makeParams(EnvTrigMode::Lfo));
    REQUIRE(e.stage() == EnvStage::Idle);

    e.noteOn(/*legato=*/false);                // first press from Idle
    REQUIRE(e.stage() == EnvStage::Attack);    // arms the envelope
}

// =====================================================================================
// RT-safety: the trigger entry points + tick() are noexcept and allocate/lock nothing
// under the armed AudioThreadGuard sentinel. §2.1; docs/design/00 §9.1 RT-1/RT-2/RT-4.
// =====================================================================================

TEST_CASE("env_trig: trigger entry points and tick are noexcept", "[env_trig]") {
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().noteOn(true)));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().noteOff()));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().clockTrigger()));
    STATIC_REQUIRE(noexcept(std::declval<Envelope&>().tick()));
}

TEST_CASE("env_trig: trigger entry points allocate and lock nothing under the sentinel",
          "[env_trig][rt]") {
    Envelope e;
    e.prepare(kSr, kDiv);                       // prepare() is the only sizing site
    e.setParams(makeParams(EnvTrigMode::GateTrig));
    e.noteOn(false);                            // warm any first-call paths before arming

    mw::test::AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        // Cycle all three modes' trigger surface on the audio thread.
        if ((i & 0x3F) == 0) e.noteOn(true);    // legato retrigger (GateTrig path)
        if ((i & 0x7F) == 0) e.noteOff();        // release
        if ((i & 0xFF) == 0) e.clockTrigger();   // clock edge (no-op outside Lfo)
        acc += e.tick();
    }
    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
    REQUIRE(acc >= 0.0f);                        // touch acc so the loop is not elided
}
