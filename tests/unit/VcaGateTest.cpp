// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/VcaGateTest.cpp — Layer-1 unit tests for the VCA anti-thump gate fade +
// ENV/GATE mode handling (task 060). Realizes docs/design/03 §4.6 (anti-thump
// low-offset selection / gate-edge one-pole fade / DC-offset null), §4.4 (ENV vs GATE
// amplitude source), and §6.5 (click-safe ENV<->GATE switch).
//
// Test-case names begin with "vca_thump" so `ctest -R vca_thump --no-tests=error`
// selects them (silent-pass rule). The display text avoids '[' so Catch2 does not
// mis-parse a tag.
//
// Covers every acceptance criterion in plan/backlog/060:
//   - prepare computes gateFadeCoeff_ from kVcaAntiThumpMs (§4.6): the fade reaches
//     the target within ~kVcaAntiThumpMs and is a true one-pole (no step);
//   - one-pole gateFade_ on the control at gate open/close edges -> the faded control
//     is continuous, so the FIRST kVcaAntiThumpMs of a 0->1 gate edge has bounded
//     energy (no audible thump) vs the un-faded step reference (§4.5/§4.6 hook);
//   - DC-offset null with kVcaOffsetNull at the gate transition (§4.6): with the
//     default kVcaOffsetNull == 0 the gate floor is exactly clean (no residual DC);
//   - setMode: ENV follows the ADSR-shaped control; GATE holds a flat full level for
//     the gate duration (§4.4 acceptance hook);
//   - the fade shares the canonical mw::params::OnePoleSmoother kind (ADR-020 S10);
//   - ENV<->GATE switch is click-safe (§6.5): no discontinuity on the mode flip;
//   - kVcaAntiThumpMs / kVcaOffsetNull resolve from the calibration header (ADR-020 S13);
//   - prepare/reset/setMode/process(Block) are noexcept and allocate/lock nothing.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp/VcaGate.h"
#include "calibration/EnvLfoVcaConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::VcaGate;
using mw101::dsp::VcaMode;

namespace {

// Sample-accurate gate-fade control reference, computed straight from the (PI)
// calibration constants (NOT from the production source). The faded control is a
// one-pole approach from its current value toward the per-tick mode target:
//   target = (mode==Gate) ? 1.0 : envControl, while gateOpen; 0 while gateClosed.
//   faded += (1 - coeff) * (target + offsetNull - faded)
// where coeff = exp(-1 / (kVcaAntiThumpMs/1000 * tickRate)).
double fadeCoeffOracle(double tickRateHz) {
    const double tauSec = static_cast<double>(mw::cal::vca::kVcaAntiThumpMs) / 1000.0;
    return std::exp(-1.0 / (tauSec * tickRateHz));
}

} // namespace

// --- shape / coefficient -----------------------------------------------------------

TEST_CASE("vca_thump: prepare derives the gate-fade coefficient from kVcaAntiThumpMs", "[vca_thump]") {
    // §4.6: the gate fade time constant is kVcaAntiThumpMs. With a per-sample control
    // rate (controlRateDivider == 1) at 48 kHz, after kVcaAntiThumpMs of a held-open
    // gate the faded control must be within a one-time-constant band of the target.
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, /*controlRateDivider=*/1);
    g.reset();

    g.setMode(VcaMode::Gate);
    g.gateOn();   // 0 -> 1 edge

    const int ticksPerTau = static_cast<int>(std::lround(
        kSr * static_cast<double>(mw::cal::vca::kVcaAntiThumpMs) / 1000.0));
    REQUIRE(ticksPerTau > 1);   // a real, multi-tick fade (not an instant step)

    // Drive one time constant of faded control with a flat-full ENV input (Gate mode
    // ignores it and targets 1.0). After 1*tau a one-pole reaches ~1-1/e ~= 0.632.
    float last = 0.0f;
    for (int i = 0; i < ticksPerTau; ++i) last = g.tickControl(/*envControl=*/0.0f);

    const double oneMinusInvE = 1.0 - std::exp(-1.0);
    REQUIRE(static_cast<double>(last) > oneMinusInvE - 0.10);
    REQUIRE(static_cast<double>(last) < 1.0);

    // And the per-tick coefficient matches the (PI) one-pole oracle: from rest, one
    // tick toward target 1.0 advances by exactly (1 - coeff).
    VcaGate g2;
    g2.prepare(kSr, 1);
    g2.reset();
    g2.setMode(VcaMode::Gate);
    g2.gateOn();
    const float afterOne = g2.tickControl(0.0f);
    const double wantOne = 1.0 - fadeCoeffOracle(kSr);
    REQUIRE(std::abs(static_cast<double>(afterOne) - wantOne) < 1.0e-4);
}

TEST_CASE("vca_thump: a 0 to 1 gate edge produces a continuous monotone fade, not a step", "[vca_thump]") {
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);

    // Before gateOn the faded control is rest (0).
    REQUIRE(g.tickControl(0.0f) == 0.0f);

    g.gateOn();
    float prev = 0.0f;
    const int n = static_cast<int>(std::lround(kSr * 0.020));  // 20 ms window
    float maxStep = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float c = g.tickControl(0.0f);
        REQUIRE(c >= prev);                 // monotone rise on open
        REQUIRE(c <= 1.0f + 1.0e-6f);        // bounded by full level
        maxStep = std::max(maxStep, c - prev);
        prev = c;
    }
    // The single biggest per-sample jump of the FADED control is the first step,
    // 1*(1-coeff). It must be far below a hard 0->1 unit step (no thump).
    const double firstStep = 1.0 - fadeCoeffOracle(kSr);
    REQUIRE(static_cast<double>(maxStep) <= firstStep + 1.0e-5);
    REQUIRE(static_cast<double>(maxStep) < 0.05);   // small per-sample slew, not a click
    REQUIRE(prev > 0.95f);                          // converged near full by 20 ms
}

// --- the anti-thump energy bound (the headline acceptance criterion) ----------------

TEST_CASE("vca_thump: energy in the first kVcaAntiThumpMs of a gate edge is bounded below the un-faded step", "[vca_thump]") {
    // §4.5/§4.6 acceptance hook: no audible thump on a 0->1 gate edge — the energy in
    // the first kVcaAntiThumpMs is bounded. We make this objective by comparing the
    // faded control's onset energy against the energy a HARD unit step would inject
    // over the same window. The fade must inject strictly, substantially less.
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);
    g.gateOn();

    const int window = static_cast<int>(std::lround(
        kSr * static_cast<double>(mw::cal::vca::kVcaAntiThumpMs) / 1000.0));
    REQUIRE(window > 1);

    // "Excess energy" = sum of (target - faded)^2 each tick = the deviation a fade
    // carries vs the step that would already be at full level. Equivalently the faded
    // onset energy is well below the step energy (== window, since a step is 1.0 every
    // sample of the window).
    double fadedEnergy = 0.0;
    for (int i = 0; i < window; ++i) {
        const double c = static_cast<double>(g.tickControl(0.0f));
        fadedEnergy += c * c;
    }
    const double stepEnergy = static_cast<double>(window);  // hard step: 1.0 each tick

    // A one-pole that reaches ~0.632 at the end of one tau accumulates well under half
    // the step energy over the window — the concrete "bounded onset" guarantee.
    REQUIRE(fadedEnergy < stepEnergy);
    REQUIRE(fadedEnergy < 0.5 * stepEnergy);
    REQUIRE(fadedEnergy > 0.0);   // it does open (not stuck silent)
}

// --- DC-offset null at the gate transition -----------------------------------------

TEST_CASE("vca_thump: the default kVcaOffsetNull leaves the closed-gate floor exactly clean", "[vca_thump]") {
    // §4.6: residual DC offset is nulled with kVcaOffsetNull (default 0 == clean). With
    // the default, a fully-closed, fully-settled gate produces exactly 0 control (no
    // residual DC pedestal that would thump on the next open).
    REQUIRE(mw::cal::vca::kVcaOffsetNull == 0.0f);

    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);

    // Open, settle to full, then close and let the release fade settle.
    g.gateOn();
    for (int i = 0; i < 4096; ++i) g.tickControl(0.0f);
    g.gateOff();
    float c = 1.0f;
    for (int i = 0; i < 8192; ++i) c = g.tickControl(0.0f);

    // Fully settled closed gate: exactly the null (0 by default), no DC pedestal.
    REQUIRE(c == static_cast<float>(mw::cal::vca::kVcaOffsetNull));
    REQUIRE(c == 0.0f);
}

// --- ENV vs GATE mode (the §4.4 acceptance hook) ------------------------------------

TEST_CASE("vca_thump: ENV mode follows the ADSR-shaped control once the fade has settled", "[vca_thump]") {
    // §4.4: in ENV mode the control follows the ADSR contour passed in (the gate fade
    // only guards the edges, not the body). After the onset fade settles, the faded
    // control tracks a slowly-varying envInput closely.
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Env);
    g.gateOn();

    // Let the onset fade settle on a flat envelope so we isolate ENV-following.
    for (int i = 0; i < 4096; ++i) g.tickControl(0.5f);

    // A slowly-varying ADSR-like contour: the faded control should track it tightly
    // because the fade time constant (~2 ms) is much faster than this ramp. The signal
    // angular frequency (~3e-4 rad/sample) is ~30x below the fade corner (~1-coeff),
    // so the tracking error is small.
    float maxErr = 0.0f;
    for (int i = 0; i < 6000; ++i) {
        const float env = 0.5f + 0.4f * std::sin(0.0003f * static_cast<float>(i));
        const float c = g.tickControl(env);
        // Skip the first few ticks: even a tiny settle on entry is not "tracking".
        if (i > 200) maxErr = std::max(maxErr, std::abs(c - env));
    }
    // ENV mode is NOT a flat hold: it tracks the contour. A GATE-mode (flat 1.0) bug
    // would diverge by ~0.5; the fade-lag for this slow ramp is well under 0.02.
    REQUIRE(maxErr < 0.02f);
}

TEST_CASE("vca_thump: GATE mode holds a flat full level for the gate duration regardless of env input", "[vca_thump]") {
    // §4.4: in GATE mode the control is a flat full level (1.0) for the gate duration —
    // the ADSR shape is bypassed. Feed a wildly-varying envInput; once settled the
    // faded control stays pinned near 1.0.
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);
    g.gateOn();

    for (int i = 0; i < 4096; ++i) g.tickControl(0.0f);   // settle the onset fade

    float minLevel = 2.0f;
    for (int i = 0; i < 2000; ++i) {
        // Deliberately hostile env input that GATE mode must IGNORE.
        const float env = 0.5f + 0.5f * std::sin(0.05f * static_cast<float>(i));
        const float c = g.tickControl(env);
        minLevel = std::min(minLevel, c);
        REQUIRE(c <= 1.0f + 1.0e-6f);
    }
    // Flat full hold: never sags away from 1.0 (would happen if env leaked through).
    REQUIRE(minLevel > 0.99f);
}

// --- click-safe ENV<->GATE switch (§6.5) --------------------------------------------

TEST_CASE("vca_thump: the ENV to GATE switch is click-safe with no control discontinuity", "[vca_thump]") {
    // §6.5: the ENV<->GATE transition is click-safe — the faded control has no jump on
    // the mode flip; it ramps from the old level to the new target over the fade.
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Env);
    g.gateOn();

    // Settle ENV mode at a low contour level (0.2).
    float c = 0.0f;
    for (int i = 0; i < 4096; ++i) c = g.tickControl(0.2f);
    const float beforeSwitch = c;
    REQUIRE(std::abs(beforeSwitch - 0.2f) < 0.02f);

    // Flip to GATE (target jumps 0.2 -> 1.0). The very next tick must NOT jump to 1.0;
    // it moves by at most one fade step.
    g.setMode(VcaMode::Gate);
    const float justAfter = g.tickControl(0.2f);   // env input now ignored
    const double maxStep = 1.0 - fadeCoeffOracle(kSr);
    REQUIRE(std::abs(static_cast<double>(justAfter - beforeSwitch)) <= maxStep + 1.0e-5);

    // And it converges up to the new full target over the fade (no stall).
    for (int i = 0; i < 4096; ++i) c = g.tickControl(0.2f);
    REQUIRE(c > 0.99f);
}

TEST_CASE("vca_thump: the GATE to ENV switch is click-safe with no control discontinuity", "[vca_thump]") {
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);
    g.gateOn();

    float c = 0.0f;
    for (int i = 0; i < 4096; ++i) c = g.tickControl(0.0f);   // GATE settles at 1.0
    REQUIRE(c > 0.99f);
    const float beforeSwitch = c;

    // Flip to ENV with a low contour (0.1): target drops 1.0 -> 0.1. No instant jump.
    g.setMode(VcaMode::Env);
    const float justAfter = g.tickControl(0.1f);
    const double maxStep = 1.0 - fadeCoeffOracle(kSr);
    REQUIRE(std::abs(static_cast<double>(justAfter - beforeSwitch)) <= maxStep + 1.0e-5);

    for (int i = 0; i < 4096; ++i) c = g.tickControl(0.1f);
    REQUIRE(std::abs(c - 0.1f) < 0.02f);
}

// --- shared smoother kind (ADR-020 S10) ---------------------------------------------

TEST_CASE("vca_thump: the gate fade shares the canonical OnePoleSmoother kind", "[vca_thump]") {
    // §4.6 / §6.4 / ADR-020 S10: the fade uses the single canonical one-pole smoother
    // kind, not a second smoother flavor. VcaGate exposes the design-doc alias; assert
    // it resolves to mw::params::OnePoleSmoother (the realized canonical type, task 008).
    static_assert(std::is_same_v<mw101::dsp::VcaGate::Smoother, mw::params::OnePoleSmoother>,
                  "the gate fade must share mw::params::OnePoleSmoother (ADR-020 S10)");
    SUCCEED("VcaGate::Smoother == mw::params::OnePoleSmoother");
}

// --- gate close fade (offset edge) --------------------------------------------------

TEST_CASE("vca_thump: a 1 to 0 gate close fades down continuously, not a step", "[vca_thump]") {
    // §4.6: the fade guards BOTH edges. A note-off (gate close) ramps the control down
    // to the null over the fade rather than dropping instantly (no release thump).
    constexpr double kSr = 48000.0;
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);
    g.gateOn();
    for (int i = 0; i < 4096; ++i) g.tickControl(0.0f);   // settle open at 1.0

    g.gateOff();
    float prev = 1.0f;
    float maxStep = 0.0f;
    const int n = static_cast<int>(std::lround(kSr * 0.020));
    for (int i = 0; i < n; ++i) {
        const float c = g.tickControl(0.0f);
        REQUIRE(c <= prev + 1.0e-6f);   // monotone fall on close
        maxStep = std::max(maxStep, prev - c);
        prev = c;
    }
    const double firstStep = 1.0 - fadeCoeffOracle(kSr);
    REQUIRE(static_cast<double>(maxStep) <= firstStep + 1.0e-5);
    REQUIRE(prev < 0.05f);   // converged near the null floor by 20 ms
}

// --- calibration sourcing (ADR-020 S13) ---------------------------------------------

TEST_CASE("vca_thump: kVcaAntiThumpMs and kVcaOffsetNull resolve from the calibration header", "[vca_thump]") {
    // Acceptance: the (PI) anti-thump fade time and offset null live in the calibration
    // header; the DSP consumes them, not inlined literals (ADR-020 S13). Pin the
    // centralized defaults so an inlined-literal regression fails, and prove the source
    // actually consumes kVcaAntiThumpMs via the realized fade rate.
    REQUIRE(mw::cal::vca::kVcaAntiThumpMs == 2.0f);
    REQUIRE(mw::cal::vca::kVcaOffsetNull == 0.0f);

    constexpr double kSr = 96000.0;   // different SR => different tick count per tau
    VcaGate g;
    g.prepare(kSr, 1);
    g.reset();
    g.setMode(VcaMode::Gate);
    g.gateOn();
    const float afterOne = g.tickControl(0.0f);
    const double wantOne = 1.0 - fadeCoeffOracle(kSr);   // uses kVcaAntiThumpMs
    REQUIRE(std::abs(static_cast<double>(afterOne) - wantOne) < 1.0e-4);
}

// --- control-rate divider honored ---------------------------------------------------

TEST_CASE("vca_thump: the fade rate scales with the control-rate divider", "[vca_thump]") {
    // §6.2: the fade advances on the control-rate tick (controlRateDivider passed to
    // prepare). A larger divider => fewer fade ticks per second => a bigger per-tick
    // step for the same wall-clock fade time.
    constexpr double kSr = 48000.0;

    VcaGate g1;
    g1.prepare(kSr, /*controlRateDivider=*/1);
    g1.reset();
    g1.setMode(VcaMode::Gate);
    g1.gateOn();
    const float step1 = g1.tickControl(0.0f);

    VcaGate g8;
    g8.prepare(kSr, /*controlRateDivider=*/8);
    g8.reset();
    g8.setMode(VcaMode::Gate);
    g8.gateOn();
    const float step8 = g8.tickControl(0.0f);

    // Coarser control rate => bigger first step toward the target.
    REQUIRE(step8 > step1);
    const double want1 = 1.0 - fadeCoeffOracle(kSr / 1.0);
    const double want8 = 1.0 - fadeCoeffOracle(kSr / 8.0);
    REQUIRE(std::abs(static_cast<double>(step1) - want1) < 1.0e-4);
    REQUIRE(std::abs(static_cast<double>(step8) - want8) < 1.0e-4);
}

// --- real-time safety ----------------------------------------------------------------

TEST_CASE("vca_thump: tickControl and processBlock are noexcept and allocate/lock nothing", "[vca_thump]") {
    static_assert(noexcept(std::declval<VcaGate&>().tickControl(0.0f)),
                  "tickControl() must be noexcept [docs/design/03 §4.1; ADR-001 C5].");
    static_assert(noexcept(std::declval<VcaGate&>().processControlBlock(nullptr, nullptr, 0)),
                  "processControlBlock() must be noexcept [ADR-001 C5].");
    static_assert(noexcept(std::declval<VcaGate&>().prepare(48000.0, 1)),
                  "prepare() must be noexcept.");
    static_assert(noexcept(std::declval<VcaGate&>().reset()), "reset() must be noexcept.");
    static_assert(noexcept(std::declval<VcaGate&>().setMode(VcaMode::Env)),
                  "setMode() must be noexcept.");
    static_assert(noexcept(std::declval<VcaGate&>().gateOn()), "gateOn() must be noexcept.");
    static_assert(noexcept(std::declval<VcaGate&>().gateOff()), "gateOff() must be noexcept.");

    // POD / RT discipline (ADR-020 S14): trivially copyable, standard-layout.
    static_assert(std::is_trivially_copyable_v<VcaGate>,
                  "VcaGate state must be a trivially-copyable POD (ADR-020 S14)");
    static_assert(std::is_standard_layout_v<VcaGate>,
                  "VcaGate must be standard-layout (POD seam state)");

    VcaGate g;
    g.prepare(48000.0, 1);   // prepare may allocate; happens BEFORE arming
    g.reset();
    g.setMode(VcaMode::Env);
    g.gateOn();

    constexpr int kN = 4096;
    std::vector<float> env(kN), out(kN);
    for (int i = 0; i < kN; ++i) env[i] = 0.5f + 0.4f * std::sin(0.01f * static_cast<float>(i));

    mw::test::AudioThreadGuard guard;
    guard.arm();
    float acc = 0.0f;
    for (int i = 0; i < kN; ++i) acc += g.tickControl(env[i]);       // hot path, armed
    g.processControlBlock(out.data(), env.data(), kN);                // hot path, armed
    g.gateOff();
    for (int i = 0; i < kN; ++i) acc += g.tickControl(env[i]);       // close fade, armed
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
    REQUIRE(std::isfinite(acc));
}

TEST_CASE("vca_thump: processControlBlock matches the per-tick tickControl over n", "[vca_thump]") {
    constexpr double kSr = 48000.0;
    constexpr int kN = 311;

    VcaGate ref;
    ref.prepare(kSr, 1);
    ref.reset();
    ref.setMode(VcaMode::Env);
    ref.gateOn();

    VcaGate blk;
    blk.prepare(kSr, 1);
    blk.reset();
    blk.setMode(VcaMode::Env);
    blk.gateOn();

    std::vector<float> env(kN), expected(kN), got(kN);
    for (int i = 0; i < kN; ++i) {
        env[i] = 0.3f + 0.2f * std::sin(0.02f * static_cast<float>(i));
        expected[i] = ref.tickControl(env[i]);
    }
    blk.processControlBlock(got.data(), env.data(), kN);

    for (int i = 0; i < kN; ++i)
        REQUIRE(std::abs(got[i] - expected[i]) < 1.0e-6f);
}
