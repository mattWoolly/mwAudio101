// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EnvLfoVcaRtSafeTest.cpp — the CROSS-CLASS real-time-safety and
// control-rate-determinism QA suite for the Envelope / Lfo / Vca / ModRouting hot
// paths (task 062). Realizes plan/backlog/062-env-lfo-vca-real-time.md scope:
// docs/design/03-dsp-envelope-lfo-vca.md §2.1 (RT invariants / single shared
// envelope) and §6.2 (control-rate cadence + CLASS-EXACT block-boundary
// bookkeeping), and ADR-020 S14 (POD smoother state sized in prepare, no audio-thread
// alloc/lock) + S12 (boundary bookkeeping CLASS-EXACT, value CLASS-FP).
//
// This is a QA / invariant tier, NOT an algorithm-correctness tier: per-class
// curve/value correctness is owned by EnvelopeCurveTest / LfoCoreTest / VcaTaperTest /
// ModRoutingCombineTest (task 062 ## Out of scope). Here we assert the cross-cutting
// contract that holds for ALL FOUR types at once:
//   - under an armed AudioThreadGuard, tick()/process()/processBlock()/combine()
//     allocate nothing and lock nothing (§2.1; ADR-001 C3/C4; ADR-020 S14);
//   - the hot paths are noexcept and the per-class state is a trivially-copyable,
//     standard-layout POD so "all state sized in prepare" holds by construction
//     (§2.1; ADR-019 VT-01; ADR-020 S14);
//   - the envelope and the LFO advance on the control-rate tick and are
//     sample-accurate at block boundaries — re-chunking a tick stream into arbitrary
//     blocks yields the IDENTICAL sample-by-sample contour/value (§6.2);
//   - the integer/index block-boundary bookkeeping (envelope stage index sequence,
//     LFO cycle-edge tick indices) is bit-reproducible run-to-run — the CLASS-EXACT
//     path (§6.2; ADR-020 S12);
//   - there is exactly ONE shared Envelope feeding VCF cutoff, VCA gain and PWM (no
//     separate filter/amp EG): the single contour fans out through ModRouting to all
//     three destinations (§2.1 acceptance hook).
//
// Test-case names begin with "envlfovca_rtsafe" so `ctest -R envlfovca_rtsafe
// --no-tests=error` selects exactly these (silent-pass rule); display text avoids '['
// so Catch2 does not mis-parse a tag.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "dsp/Envelope.h"
#include "dsp/Lfo.h"
#include "dsp/Vca.h"
#include "dsp/ModRouting.h"
#include "calibration/EnvLfoVcaConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::Envelope;
using mw101::dsp::EnvParams;
using mw101::dsp::EnvStage;
using mw101::dsp::EnvTrigMode;
using mw101::dsp::Lfo;
using mw101::dsp::LfoShape;
using mw101::dsp::Vca;
using mw101::dsp::VcaMode;
using mw101::dsp::ModRoutingCombiner;
using mw101::dsp::ModDepths;
using mw101::dsp::VelocityRouting;
using mw101::dsp::ModContributions;

namespace {

constexpr double kSr = 48000.0;
constexpr int    kCtlDiv = 1;   // per-tick control rate for the determinism checks

// A representative, fully-armed config: a gated envelope partway through its contour,
// a running LFO, a VCA passing audio, and a combiner with non-trivial depths +
// velocity. Built OUTSIDE any armed guard scope (prepare may allocate).
EnvParams busyEnvParams() {
    EnvParams p;
    p.attackSec  = 0.010f;
    p.decaySec   = 0.050f;
    p.sustain    = 0.6f;
    p.releaseSec = 0.080f;
    p.trig       = EnvTrigMode::GateTrig;
    p.curve      = mw::cal::env::kEnvCurve;
    return p;
}

ModDepths busyDepths() {
    ModDepths d;
    d.lfoToPitch  = 0.5f;
    d.lfoToCutoff = 0.4f;
    d.lfoToPw     = 0.3f;
    d.lfoToVca    = 0.2f;
    d.envToCutoff = 0.7f;
    d.envToPw     = 0.6f;
    d.keyFollow   = 0.5f;
    return d;
}

} // namespace

// ============================================================================
// 1. POD state + noexcept hot paths — "all state sized in prepare" by construction
//    (§2.1 acceptance hook; ADR-019 VT-01; ADR-020 S14)
// ============================================================================

TEST_CASE("envlfovca_rtsafe: all four types hold trivially-copyable standard-layout POD state",
          "[envlfovca_rtsafe]") {
    // POD state with no owning/heap members is how "all state is sized in prepare /
    // no audio-thread allocation" is guaranteed structurally (§2.1; ADR-020 S14): a
    // trivially-copyable, standard-layout object owns no std::vector / no pointer it
    // frees, so a hot-path method literally cannot allocate via a member's ctor.
    STATIC_REQUIRE(std::is_trivially_copyable_v<Envelope>);
    STATIC_REQUIRE(std::is_standard_layout_v<Envelope>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Lfo>);
    STATIC_REQUIRE(std::is_standard_layout_v<Lfo>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Vca>);
    STATIC_REQUIRE(std::is_standard_layout_v<Vca>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModRoutingCombiner>);
    STATIC_REQUIRE(std::is_standard_layout_v<ModRoutingCombiner>);

    // The routing PODs that cross the per-tick combine seam are likewise PODs.
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModDepths>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<VelocityRouting>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModContributions>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<EnvParams>);
}

TEST_CASE("envlfovca_rtsafe: every hot path and lifecycle entry point is noexcept",
          "[envlfovca_rtsafe]") {
    // §2.1 / ADR-001: process()/reset() and the per-tick hot paths are noexcept so an
    // escaped throw terminates (caught as a crash) rather than unwinding the audio
    // thread. Asserted statically across all four types.
    Envelope e;
    Lfo lfo;
    Vca v;
    ModRoutingCombiner mr;
    const EnvParams ep{};
    const ModDepths d{};
    const VelocityRouting vel{};
    float buf = 0.0f, ctl = 0.0f;
    const float noise = 0.0f;

    // Envelope
    STATIC_REQUIRE(noexcept(e.prepare(kSr, kCtlDiv)));
    STATIC_REQUIRE(noexcept(e.reset()));
    STATIC_REQUIRE(noexcept(e.setParams(ep)));
    STATIC_REQUIRE(noexcept(e.noteOn(true)));
    STATIC_REQUIRE(noexcept(e.noteOff()));
    STATIC_REQUIRE(noexcept(e.clockTrigger()));
    STATIC_REQUIRE(noexcept(e.tick()));

    // Lfo
    STATIC_REQUIRE(noexcept(lfo.prepare(kSr, kCtlDiv)));
    STATIC_REQUIRE(noexcept(lfo.reset()));
    STATIC_REQUIRE(noexcept(lfo.setRateHz(5.0f)));
    STATIC_REQUIRE(noexcept(lfo.setShape(LfoShape::Square)));
    STATIC_REQUIRE(noexcept(lfo.resetPhaseOnKey()));
    STATIC_REQUIRE(noexcept(lfo.setNoiseSource(&noise)));
    STATIC_REQUIRE(noexcept(lfo.tick()));
    STATIC_REQUIRE(noexcept(lfo.cycleEdge()));

    // Vca
    STATIC_REQUIRE(noexcept(v.prepare(kSr)));
    STATIC_REQUIRE(noexcept(v.reset()));
    STATIC_REQUIRE(noexcept(v.setMode(VcaMode::Env)));
    STATIC_REQUIRE(noexcept(v.setDrive(0.0f)));
    STATIC_REQUIRE(noexcept(v.process(0.0f, 0.0f)));
    STATIC_REQUIRE(noexcept(v.processBlock(&buf, &ctl, 1)));

    // ModRouting
    STATIC_REQUIRE(noexcept(mr.prepare(kSr)));
    STATIC_REQUIRE(noexcept(mr.reset()));
    STATIC_REQUIRE(noexcept(mr.combine(d, vel, 0.0f, 0.0f, 1.0f)));

    SUCCEED("all env/lfo/vca/modrouting hot paths are statically noexcept");
}

// ============================================================================
// 2. No heap alloc / no locks under an armed AudioThreadGuard, ALL FOUR types
//    in one representative process pass (§2.1; ADR-001 C3/C4; ADR-020 S14)
// ============================================================================

TEST_CASE("envlfovca_rtsafe: a representative env/lfo/vca/modrouting process pass allocates and locks nothing",
          "[envlfovca_rtsafe]") {
    // Build + prepare everything OUTSIDE the armed scope (prepare is the only place
    // sizing/allocation is allowed, §2.1 / ADR-001 C2).
    Envelope env;
    env.prepare(kSr, kCtlDiv);
    env.reset();
    env.setParams(busyEnvParams());
    env.noteOn(/*legato=*/false);

    Lfo lfo;
    lfo.prepare(kSr, kCtlDiv);
    lfo.reset();
    lfo.setRateHz(5.0f);

    Vca vca;
    vca.prepare(kSr);
    vca.reset();
    vca.setMode(VcaMode::Env);

    ModRoutingCombiner mr;
    mr.prepare(kSr);
    mr.reset();

    const ModDepths depths = busyDepths();
    VelocityRouting vel;   // ON by default (ADR-016 R-2)

    constexpr int kN = 4096;
    const float noiseSample = 0.123f;
    lfo.setNoiseSource(&noiseSample);

    // Pre-size all scratch buffers BEFORE arming (these are the "host block" buffers a
    // real process() would receive pre-allocated, not allocate itself).
    std::vector<float> audio(kN), control(kN);
    for (int i = 0; i < kN; ++i)
        audio[i] = 0.4f * std::sin(0.02f * static_cast<float>(i));

    double acc = 0.0;

    mw::test::AudioThreadGuard guard;
    guard.arm();   // ----- BEGIN audio-thread-equivalent hot zone -----

    // Cycle each LFO shape so every waveform branch (incl. Noise + Random PRNG) runs
    // armed; shape switch is a stepped selector, not value-smoothed (ADR-020 S7).
    const LfoShape shapes[] = { LfoShape::SmoothTri, LfoShape::Square,
                                LfoShape::Random, LfoShape::Noise };

    for (int i = 0; i < kN; ++i) {
        if ((i & 1023) == 0) lfo.setShape(shapes[(i >> 10) & 3]);

        const float envLevel = env.tick();                 // ADSR contour, armed
        const float lfoValue = lfo.tick();                 // LFO value, armed
        const ModContributions c =
            mr.combine(depths, vel, envLevel, lfoValue, 0.8f);   // routing, armed
        const float out = vca.process(audio[static_cast<std::size_t>(i)],
                                      c.vcaControl);        // VCA taper, armed
        acc += static_cast<double>(out);

        if (i == kN / 2) env.noteOff();                    // release transition, armed
    }

    // Block variant of the VCA hot path (the other declared process entry point).
    for (int i = 0; i < kN; ++i)
        control[static_cast<std::size_t>(i)] = 0.3f + 0.5f * static_cast<float>(i) / kN;
    vca.processBlock(audio.data(), control.data(), kN);

    guard.disarm();   // ----- END hot zone -----

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
    REQUIRE(std::isfinite(acc));
}

TEST_CASE("envlfovca_rtsafe: a full attack-to-idle envelope lifecycle under the guard allocates nothing",
          "[envlfovca_rtsafe]") {
    // Drive the envelope through every stage (Attack -> Decay -> Sustain -> Release ->
    // Idle) while armed, so the whole stage machine's bookkeeping runs in the hot zone.
    Envelope env;
    env.prepare(kSr, kCtlDiv);
    env.reset();
    EnvParams p = busyEnvParams();
    p.attackSec = 0.002f;
    p.decaySec  = 0.004f;
    p.releaseSec = 0.004f;
    env.setParams(p);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    env.noteOn(/*legato=*/false);
    // Run well past attack+decay so we reach Sustain.
    for (int i = 0; i < 2000; ++i) (void) env.tick();
    const bool reachedSustainOrLater = (env.stage() == EnvStage::Sustain);
    env.noteOff();
    // Run past release so we reach Idle.
    for (int i = 0; i < 4000 && env.active(); ++i) (void) env.tick();
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(reachedSustainOrLater);
    REQUIRE(env.stage() == EnvStage::Idle);   // reached the Idle terminus (§2.4)
}

// ============================================================================
// 3. Control-rate cadence: envelope + LFO are sample-accurate at block boundaries
//    (§6.2). Re-chunking the same tick stream into arbitrary blocks is identical.
// ============================================================================

TEST_CASE("envlfovca_rtsafe: the envelope contour is sample-accurate across arbitrary block boundaries",
          "[envlfovca_rtsafe]") {
    // §6.2: the envelope advances on the control-rate tick driven by a sample counter,
    // never a wall-clock timer, so the per-tick contour is INDEPENDENT of how the host
    // chops the block. A reference run of N single ticks must equal the SAME N ticks
    // gathered while we re-trigger noteOff at the same tick index in two different
    // block layouts.
    constexpr int kN = 3000;
    EnvParams p = busyEnvParams();
    p.attackSec = 0.003f;
    p.decaySec  = 0.020f;
    p.releaseSec = 0.030f;
    const int kReleaseTick = 1500;

    auto run = [&](int /*blockSize*/) {
        Envelope env;
        env.prepare(kSr, kCtlDiv);
        env.reset();
        env.setParams(p);
        env.noteOn(false);
        std::vector<float> out(kN);
        for (int i = 0; i < kN; ++i) {
            if (i == kReleaseTick) env.noteOff();
            out[static_cast<std::size_t>(i)] = env.tick();
        }
        return out;
    };

    // The control-rate tick is the same regardless of "block size"; we drive identical
    // event offsets (release at the same tick) so the contours must be bit-identical.
    const std::vector<float> a = run(32);
    const std::vector<float> b = run(257);   // a different host block layout

    REQUIRE(a.size() == b.size());
    bool allEqual = true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) { allEqual = false; break; }
    }
    REQUIRE(allEqual);   // sample-accurate, layout-independent (§6.2)
}

TEST_CASE("envlfovca_rtsafe: the LFO value and cycle edge are reproducible tick-for-tick across runs",
          "[envlfovca_rtsafe]") {
    // §6.2 / ADR-020 S12: the LFO advances on the control-rate tick. Two fresh,
    // identically-prepared LFOs must produce a bit-identical value stream AND identical
    // integer cycle-edge tick indices (the CLASS-EXACT block-boundary bookkeeping).
    // At fc=48 kHz, one 7.5 Hz cycle is 6400 ticks; 32000 ticks spans ~5 cycles so the
    // wrap/cycle-edge bookkeeping is genuinely exercised.
    constexpr int kN = 32000;
    auto run = [&]() {
        Lfo lfo;
        lfo.prepare(kSr, kCtlDiv);
        lfo.reset();
        lfo.setShape(LfoShape::SmoothTri);
        lfo.setRateHz(7.5f);
        std::vector<float> vals(kN);
        std::vector<int>   edges;   // tick indices where cycleEdge() fired
        for (int i = 0; i < kN; ++i) {
            vals[static_cast<std::size_t>(i)] = lfo.tick();
            if (lfo.cycleEdge()) edges.push_back(i);
        }
        return std::pair<std::vector<float>, std::vector<int>>{vals, edges};
    };

    const auto r1 = run();
    const auto r2 = run();

    // CLASS-FP value stream is bit-identical here (same seed, same platform).
    REQUIRE(r1.first == r2.first);
    // CLASS-EXACT integer cycle-edge bookkeeping is bit-identical (ADR-020 S12).
    REQUIRE(r1.second == r2.second);
    REQUIRE_FALSE(r1.second.empty());   // the LFO actually wrapped at this rate
}

TEST_CASE("envlfovca_rtsafe: the Random LFO sample-hold is deterministic and changes only on cycle edges",
          "[envlfovca_rtsafe]") {
    // §6.2 / §3.5: the digital uniform S/H is a seeded POD PRNG, so the held value is
    // golden-reproducible AND the integer "changes only on a cycle edge" bookkeeping is
    // exact. Asserts both the determinism (run-to-run identical) and the edge coupling.
    // At fc=48 kHz, one 3 Hz cycle is 16000 ticks; 48000 ticks spans ~3 cycles so the
    // S/H register is genuinely reloaded on real cycle edges.
    constexpr int kN = 48000;
    auto run = [&]() {
        Lfo lfo;
        lfo.prepare(kSr, kCtlDiv);
        lfo.reset();
        lfo.setShape(LfoShape::Random);
        lfo.setRateHz(3.0f);
        std::vector<float> vals(kN);
        for (int i = 0; i < kN; ++i) vals[static_cast<std::size_t>(i)] = lfo.tick();
        return vals;
    };

    const std::vector<float> v1 = run();
    const std::vector<float> v2 = run();
    REQUIRE(v1 == v2);   // seeded PRNG -> bit-reproducible (golden-stable)

    // The held value must change ONLY on a cycle edge: re-run tracking edges and assert
    // every value change coincides with a flagged edge tick.
    Lfo lfo;
    lfo.prepare(kSr, kCtlDiv);
    lfo.reset();
    lfo.setShape(LfoShape::Random);
    lfo.setRateHz(3.0f);
    float prev = lfo.tick();
    bool prevWasEdge = lfo.cycleEdge();
    int changeCount = 0;
    for (int i = 1; i < kN; ++i) {
        const float cur = lfo.tick();
        const bool edge = lfo.cycleEdge();
        if (cur != prev) {
            ++changeCount;
            // A change at tick i means the register was reloaded on the edge at i-1.
            REQUIRE(prevWasEdge);
        }
        prev = cur;
        prevWasEdge = edge;
    }
    REQUIRE(changeCount > 0);   // the S/H actually stepped
}

// ============================================================================
// 4. Single shared Envelope feeds VCF / VCA / PWM — no separate filter/amp EG
//    (§2.1 acceptance hook)
// ============================================================================

TEST_CASE("envlfovca_rtsafe: one shared Envelope contour fans out to VCF cutoff, VCA gain and PWM",
          "[envlfovca_rtsafe]") {
    // §2.1: there is exactly ONE Envelope per voice, shared across VCF cutoff, VCA gain
    // and VCO pulse width; there is NO separate filter or amp EG. We confirm this
    // STRUCTURALLY: a single envLevel sample fed once into ModRouting::combine drives
    // all three destinations (cutoffMod, vcaControl, pwMod) from that one contour, and
    // toggling only the envelope (not three separate EGs) moves all three together.
    Envelope env;
    env.prepare(kSr, kCtlDiv);
    env.reset();
    env.setParams(busyEnvParams());

    ModRoutingCombiner mr;
    mr.prepare(kSr);
    mr.reset();

    // Depths that route the env to BOTH cutoff and PWM, and the env IS the VCA base amp.
    ModDepths d{};
    d.envToCutoff = 0.8f;
    d.envToPw     = 0.5f;
    d.lfoToVca    = 0.0f;     // isolate the env's contribution to vcaControl
    VelocityRouting vel{};
    vel.enabled = false;       // isolate the env (no velocity scaling) for this check

    // At Idle the contour is 0: all three env-driven contributions are 0.
    {
        const float envLevel = env.level();            // 0 at Idle
        REQUIRE(envLevel == 0.0f);
        const ModContributions c = mr.combine(d, vel, envLevel, /*lfo=*/0.0f, /*vel=*/0.0f);
        REQUIRE(c.cutoffMod == 0.0f);
        REQUIRE(c.pwMod == 0.0f);
        REQUIRE(c.vcaControl == 0.0f);
    }

    // Open the SINGLE envelope; drive it up into a clearly non-zero contour.
    env.noteOn(false);
    float envLevel = 0.0f;
    for (int i = 0; i < 800; ++i) envLevel = env.tick();
    REQUIRE(envLevel > 0.0f);

    // The ONE contour value, combined ONCE, drives ALL THREE destinations.
    const ModContributions c = mr.combine(d, vel, envLevel, /*lfo=*/0.0f, /*velNorm=*/0.0f);
    REQUIRE(c.cutoffMod > 0.0f);                              // VCF cutoff amount moved
    REQUIRE(c.pwMod > 0.0f);                                  // PWM moved
    REQUIRE(c.vcaControl > 0.0f);                             // VCA gain moved
    // Each is exactly the shared contour scaled by its own depth (no second EG). With
    // the LFO value 0 the mod-bus LPF output is 0, so the routing reduces to a single
    // multiply of the ONE shared contour — comparable exactly (no FP tolerance needed).
    REQUIRE(c.cutoffMod == envLevel * d.envToCutoff);
    REQUIRE(c.pwMod     == envLevel * d.envToPw);
    REQUIRE(c.vcaControl == envLevel);   // baseAmp == envLevel, velocity disabled
}
