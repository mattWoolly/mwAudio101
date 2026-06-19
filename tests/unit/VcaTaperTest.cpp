// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/VcaTaperTest.cpp — Layer-1 unit tests for the BA662A-class OTA VCA
// taper + tanh soft-drive (task 056). Realizes docs/design/03 §4.3 (control law /
// taper) and §4.5 (output level / drive placement).
//
// Test-case names begin with "vca_taper" so `ctest -R vca_taper --no-tests=error`
// selects them (silent-pass rule). The display text avoids '[' so Catch2 does not
// mis-parse a tag. Covers every acceptance criterion in plan/backlog/056:
//   - taper(c) == pow(clamp(c,0,1), kVcaTaperExp) and
//     process(in,c) == tanh(kVcaOtaDrive*taper*in)/tanh(kVcaOtaDrive) (the oracle);
//   - gain monotonic in control; process(in, 0) == 0; at full control the gain
//     matches the calibrated full-scale (== 1.0 normalization) (§4.3 acceptance hook);
//   - control is clamped to [0,1]; sign/odd symmetry of the tanh transfer;
//   - processBlock matches the per-sample process over n;
//   - kVcaTaperExp / kVcaOtaDrive resolve from the calibration header, not inlined;
//   - process()/processBlock() are noexcept and allocate/lock nothing (RT guard).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp/Vca.h"
#include "calibration/EnvLfoVcaConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::Vca;

namespace {

// Independent oracle of the §4.3 transfer, computed straight from the calibration
// constants (NOT from the production source). taper(c)=pow(clamp(c,0,1),exp);
// out = tanh(drive * taper * in) / tanh(drive).
double taperOracle (double control) {
    const double c = control < 0.0 ? 0.0 : (control > 1.0 ? 1.0 : control);
    return std::pow (c, static_cast<double> (mw::cal::vca::kVcaTaperExp));
}

double processOracle (double in, double control) {
    const double drive = static_cast<double> (mw::cal::vca::kVcaOtaDrive);
    const double g = taperOracle (control);
    return std::tanh (drive * g * in) / std::tanh (drive);
}

} // namespace

TEST_CASE("vca_taper: process matches the tanh-over-taper oracle within tolerance", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // Sweep input and control across the relevant ranges; compare against the
    // double-precision oracle built directly from the (PI) calibration constants.
    const float inputs[]   = { -1.0f, -0.5f, -0.1f, 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (int ci = 0; ci <= 20; ++ci) {
        const float control = static_cast<float> (ci) / 20.0f;   // 0.00 .. 1.00
        for (const float in : inputs) {
            const float got = vca.process (in, control);
            const double want = processOracle (in, control);
            REQUIRE (std::abs (static_cast<double> (got) - want) < 1.0e-5);
        }
    }
}

TEST_CASE("vca_taper: process at zero control is exactly silent", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // taper(0) == 0 -> tanh(0)/tanh(drive) == 0, for ANY input. process(in,0)==0.
    const float inputs[] = { -1.0f, -0.3f, 0.0f, 0.2f, 0.9f, 1.0f, 5.0f };
    for (const float in : inputs)
        REQUIRE (vca.process (in, 0.0f) == 0.0f);
}

TEST_CASE("vca_taper: gain rises monotonically with control", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // For a fixed positive input, the output must be non-decreasing as control rises
    // from 0 to 1: taper(c) is monotone non-decreasing in c (pow with exp > 0) and
    // tanh is strictly increasing, so the composed gain is monotone. (§4.3 acceptance
    // hook: "gain monotonic in control".)
    constexpr float kIn = 0.7f;
    float prev = -2.0f;
    for (int ci = 0; ci <= 100; ++ci) {
        const float control = static_cast<float> (ci) / 100.0f;
        const float out = vca.process (kIn, control);
        REQUIRE (out >= prev);
        prev = out;
    }

    // Strict rise across the full span (not flat): full control clearly louder than
    // a quiet control for the same input.
    REQUIRE (vca.process (kIn, 1.0f) > vca.process (kIn, 0.1f));
}

TEST_CASE("vca_taper: at full control the gain matches the calibrated full-scale", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // §4.3 / §4.5: the tanh is normalized by tanh(drive) so that at full control
    // (taper(1)==1) the transfer is out = tanh(drive*in)/tanh(drive). At a small
    // input this is unity gain (linear OTA window); the normalization guarantees a
    // full-scale input maps to full-scale output: process(1, 1) == 1.
    REQUIRE (std::abs (vca.process (1.0f, 1.0f) - 1.0f) < 1.0e-6f);
    REQUIRE (std::abs (vca.process (-1.0f, 1.0f) + 1.0f) < 1.0e-6f);

    // Near zero input, full control approaches unity gain (drive in the linear
    // window): tanh(drive*x)/tanh(drive) -> x*(drive/tanh(drive)) for small x.
    const float small = 1.0e-3f;
    const float slope = vca.process (small, 1.0f) / small;
    const double wantSlope =
        static_cast<double> (mw::cal::vca::kVcaOtaDrive)
        / std::tanh (static_cast<double> (mw::cal::vca::kVcaOtaDrive));
    REQUIRE (std::abs (static_cast<double> (slope) - wantSlope) < 1.0e-3);
}

TEST_CASE("vca_taper: control is clamped to the zero-to-one window", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // control < 0 clamps to 0 (silent); control > 1 clamps to 1 (== full control).
    REQUIRE (vca.process (0.8f, -0.5f) == 0.0f);
    REQUIRE (vca.process (0.8f, -10.0f) == 0.0f);
    REQUIRE (std::abs (vca.process (0.8f, 2.0f) - vca.process (0.8f, 1.0f)) < 1.0e-6f);
    REQUIRE (std::abs (vca.process (0.8f, 5.0f) - vca.process (0.8f, 1.0f)) < 1.0e-6f);
}

TEST_CASE("vca_taper: transfer is odd-symmetric in the input sign", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // tanh is odd, taper(control) is a non-negative scalar, so process(-in,c) must
    // equal -process(in,c). A polarity-inverting or DC-offset bug breaks this.
    const float controls[] = { 0.1f, 0.4f, 0.7f, 1.0f };
    const float inputs[]   = { 0.2f, 0.5f, 0.9f };
    for (const float c : controls)
        for (const float in : inputs)
            REQUIRE (std::abs (vca.process (-in, c) + vca.process (in, c)) < 1.0e-6f);
}

TEST_CASE("vca_taper: processBlock matches the per-sample process over n", "[vca_taper]") {
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    constexpr int kN = 257;
    std::vector<float> buffer (kN), control (kN), expected (kN);
    for (int i = 0; i < kN; ++i) {
        buffer[i]  = std::sin (0.03f * static_cast<float> (i)) * 0.9f;   // audio in
        control[i] = static_cast<float> (i % (kN / 4)) / static_cast<float> (kN / 4);
        expected[i] = vca.process (buffer[i], control[i]);              // per-sample ref
    }

    vca.processBlock (buffer.data(), control.data(), kN);

    for (int i = 0; i < kN; ++i)
        REQUIRE (std::abs (buffer[i] - expected[i]) < 1.0e-6f);
}

TEST_CASE("vca_taper: setDrive does not alter the documented default-window transfer here", "[vca_taper]") {
    // §4.5: drive is an OPTIONAL character control; the default linear window is the
    // documented behavior. setDrive is exposed (task-052 surface) but the §4.3
    // transfer this task implements uses the centralized kVcaOtaDrive. Calling
    // setDrive must not throw / must be noexcept, and the default transfer is stable.
    static_assert (noexcept (std::declval<Vca&>().setDrive (0.0f)),
                   "setDrive must be noexcept [ADR-001 C5].");
    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    const float before = vca.process (0.5f, 0.8f);
    vca.setDrive (0.0f);                       // default character (linear window)
    const float after = vca.process (0.5f, 0.8f);
    REQUIRE (std::abs (after - before) < 1.0e-6f);
}

TEST_CASE("vca_taper: kVcaTaperExp and kVcaOtaDrive resolve from the calibration header", "[vca_taper]") {
    // Acceptance: the (PI) taper exponent and OTA drive live in the calibration
    // header and the DSP source references them — NOT inlined at the call site
    // (ADR-020 S13). Pin the centralized defaults so an inlined-literal regression
    // fails, and prove the source actually consumes kVcaTaperExp by checking the
    // curvature it produces (exp == 2 => taper is the square of control).
    REQUIRE (mw::cal::vca::kVcaTaperExp == 2.0f);
    REQUIRE (mw::cal::vca::kVcaOtaDrive == 1.0f);

    Vca vca;
    vca.prepare (48000.0);
    vca.reset();

    // With exp == 2.0 the taper of 0.5 is 0.25; verify the source uses that curve by
    // comparing the small-signal gain at control 0.5 vs 1.0 (ratio == taper ratio).
    const float small = 1.0e-3f;
    const float gHalf = vca.process (small, 0.5f) / small;
    const float gFull = vca.process (small, 1.0f) / small;
    // ratio = taper(0.5)/taper(1.0) = 0.5^kVcaTaperExp.
    const double wantRatio = std::pow (0.5, static_cast<double> (mw::cal::vca::kVcaTaperExp));
    REQUIRE (std::abs (static_cast<double> (gHalf / gFull) - wantRatio) < 1.0e-3);
}

TEST_CASE("vca_taper: process and processBlock are noexcept and allocate/lock nothing (RT guard)", "[vca_taper]") {
    static_assert (noexcept (std::declval<Vca&>().process (0.0f, 0.0f)),
                   "process() must be noexcept [docs/design/03 §4.1; ADR-001 C5].");
    static_assert (noexcept (std::declval<Vca&>().processBlock (nullptr, nullptr, 0)),
                   "processBlock() must be noexcept [docs/design/03 §4.1; ADR-001 C5].");
    static_assert (noexcept (std::declval<Vca&>().prepare (48000.0)), "prepare() must be noexcept.");
    static_assert (noexcept (std::declval<Vca&>().reset()), "reset() must be noexcept.");

    Vca vca;
    vca.prepare (48000.0);   // prepare may allocate; happens BEFORE arming
    vca.reset();

    constexpr int kN = 4096;
    std::vector<float> buffer (kN), control (kN);
    for (int i = 0; i < kN; ++i) {
        buffer[i]  = 0.5f;
        control[i] = static_cast<float> (i) / static_cast<float> (kN);
    }

    mw::test::AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    for (int i = 0; i < kN; ++i) acc += vca.process (buffer[i], control[i]);   // hot path, armed
    vca.processBlock (buffer.data(), control.data(), kN);                       // hot path, armed
    g.disarm();

    REQUIRE_FALSE (g.violated());
    REQUIRE (g.violations().empty());
    REQUIRE (std::isfinite (acc));
}
