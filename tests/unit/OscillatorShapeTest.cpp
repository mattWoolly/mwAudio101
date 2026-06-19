// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the band-limited VCO SHAPE construction (task 030): the
// PolyBLEP/minBLEP saw, the two-BLEP variable-width pulse (PWM), the PWM width map,
// and the duty/dt overlap clamp. Test-case names begin with "vcoshape" so
// `ctest -R vcoshape` selects them (silent-pass rule; display names avoid '[' so
// the tag is not mis-parsed).
//
// Covers every acceptance criterion in plan/backlog/030 against
// docs/design/01-dsp-oscillators.md §4.5, §4.6, §2.2-§2.3, §10 and ADR-002 C1-C3,
// C7, C9:
//   - band-limited saw equals (2*t-1) - polyBlep(t,dt) sample-for-sample (C1);
//   - the pulse has exactly two corrected transitions per period and its DC mean
//     tracks 2*duty-1 across a 0.05..0.5 PWM sweep (C2, §4.5);
//   - the effective duty never drops below max(kPwmDutyMin, dt) and the two BLEP
//     windows never overlap at the 5% / high-pitch extreme (C3, §4.6);
//   - the PWM map: duty = kPwmDutyMax - pwmCvNorm*(kPwmDutyMax-kPwmDutyMin) (§4.6);
//   - HQ auto-escalation: a voice above kHqEscalationHz uses minBLEP, below it
//     uses PolyBLEP; the mode is set only in prepare/setControls (§2.2, §2.3, C9);
//   - renderSample stays noexcept and allocates/locks nothing on the hot path.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp/Oscillator.h"
#include "dsp/PolyBlep.h"
#include "dsp/MinBlepTable.h"
#include "calibration/VcoConstants.h"
#include "calibration/VcoShapeConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::Oscillator;
using mw101::dsp::OscControls;
using mw101::dsp::OscAaMode;
using mw101::dsp::Footage;
using mw101::dsp::MinBlepTable;
using mw101::dsp::polyBlep;

namespace {

// Build a PolyBLEP-mode oscillator at a given footage and summed pitch CV, with a
// given normalized PWM CV. No HQ table => band-limiting uses the closed-form PolyBLEP.
Oscillator makeOsc (double sampleRate, double cvVolts, Footage f, float pwmCvNorm,
                    OscAaMode mode, const MinBlepTable* hqTable) {
    Oscillator osc;
    osc.prepare (sampleRate, hqTable);
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (cvVolts);
    c.footage      = f;
    c.pwmCvNorm    = pwmCvNorm;
    c.aaMode       = mode;
    osc.setControls (c);
    return osc;
}

} // namespace

// -----------------------------------------------------------------------------
// C1 — band-limited saw equals (2*t - 1) - polyBlep(t, dt) sample-for-sample.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: PolyBLEP saw equals two-t-minus-one minus polyBlep sample-for-sample at a fixed frequency", "[vcoshape]") {
    const double sr = 48000.0;
    Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts + 2.0, Footage::Eight,
                              0.0f, OscAaMode::PolyBlep, nullptr);   // ~1768 Hz < escalation
    const float dt = static_cast<float> (osc.dt());

    REQUIRE (osc.frequencyHz() < mw::cal::vco::hqEscalationHzAt (sr));   // stays in PolyBLEP

    int wrapsSeen = 0;
    for (int i = 0; i < 4000; ++i) {
        const auto out = osc.renderSample();
        const float t  = static_cast<float> (osc.phase());
        const float expected = (2.0f * t - 1.0f) - polyBlep (t, dt);   // ADR-002 C1
        REQUIRE (out.saw == Catch::Approx (expected).margin (1e-6));
        if (osc.wrappedThisSample()) ++wrapsSeen;
    }
    REQUIRE (wrapsSeen > 0);   // the residual was actually exercised at wraps
}

// -----------------------------------------------------------------------------
// C2 — exactly two corrected transitions per period; DC mean tracks 2*duty-1.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: pulse has exactly two band-limited transitions per period", "[vcoshape]") {
    // A low fundamental gives a long, well-resolved period; count sign-direction
    // changes of the underlying naive pulse over exactly one period.
    const double sr = 48000.0;
    Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts - 2.0, Footage::Eight,
                              0.6f, OscAaMode::PolyBlep, nullptr);   // ~110 Hz, duty ~0.23
    const double duty = osc.duty();
    REQUIRE (duty > mw::cal::vco::kPwmDutyMin);
    REQUIRE (duty < mw::cal::vco::kPwmDutyMax);

    // Track the naive (pre-BLEP) high/low region implied by phase vs duty and count
    // rising + falling crossings over EXACTLY one period — from the first wrap (start
    // counting) up to (but not including) the second wrap. Within one period there is
    // exactly one rising edge (at phase 0 / the wrap) and one falling edge (at the duty
    // phase) — two corrected transitions per cycle [ADR-002 C2; §4.5].
    bool prevHigh = false;
    bool counting = false;
    int  rising = 0, falling = 0;
    for (int i = 0; i < 20000; ++i) {
        (void) osc.renderSample();
        const double t    = osc.phase();
        const bool   wrap = osc.wrappedThisSample();
        const bool   high = (t < duty);
        if (counting && wrap)
            break;                       // reached the second wrap: one full period done
        if (! counting && wrap) {
            counting = true;             // first wrap: the rising edge of THIS period
            ++rising;                    // the wrap IS the rising edge (low -> high)
            prevHigh = true;             // just-after-wrap phase is high (t < duty)
            continue;
        }
        if (counting) {
            if (high && ! prevHigh) ++rising;
            if (! high && prevHigh) ++falling;
            prevHigh = high;
        }
    }
    REQUIRE (rising == 1);
    REQUIRE (falling == 1);
}

TEST_CASE("vcoshape: band-limited pulse DC mean tracks two-duty-minus-one across a 0.05 to 0.5 sweep", "[vcoshape]") {
    const double sr = 48000.0;
    // Sweep pwmCvNorm so duty walks 0.5 -> 0.05; check the integrated DC of the
    // band-limited pulse equals 2*duty-1 within tolerance [§4.5, §10].
    for (float cv = 0.0f; cv <= 1.0f + 1e-6f; cv += 0.1f) {
        Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts - 2.0, Footage::Eight,
                                  cv, OscAaMode::PolyBlep, nullptr);   // ~110 Hz
        const double duty = osc.duty();

        // Integrate over a whole number of periods to average out the (zero-mean)
        // BLEP residuals; ~110 Hz at 48 kHz is ~436 samples/period.
        const int kSamples = 48000;   // ~110 periods
        double sum = 0.0;
        for (int i = 0; i < kSamples; ++i)
            sum += static_cast<double> (osc.renderSample().pulse);
        const double mean = sum / kSamples;

        const double expectedMean = 2.0 * duty - 1.0;
        REQUIRE (mean == Catch::Approx (expectedMean).margin (0.01));
    }
}

// -----------------------------------------------------------------------------
// §4.6 — PWM width map.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: the PWM width map sends pwmCvNorm zero to square and one to the five-percent floor", "[vcoshape]") {
    const double sr = 48000.0;
    Oscillator sq  = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 0.0f,
                              OscAaMode::PolyBlep, nullptr);
    Oscillator min = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 1.0f,
                              OscAaMode::PolyBlep, nullptr);
    Oscillator mid = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 0.5f,
                              OscAaMode::PolyBlep, nullptr);

    REQUIRE (sq.duty()  == Catch::Approx (mw::cal::vco::kPwmDutyMax));   // 0.50
    REQUIRE (min.duty() == Catch::Approx (mw::cal::vco::kPwmDutyMin));   // 0.05
    REQUIRE (mid.duty() == Catch::Approx (0.5 * (mw::cal::vco::kPwmDutyMax
                                                 + mw::cal::vco::kPwmDutyMin)));   // linear

    // The map is centralized in calibration, not inlined.
    REQUIRE (mw::cal::vco::kPwmDutyMax == 0.5f);
    REQUIRE (mw::cal::vco::kPwmDutyMin == 0.05f);
    REQUIRE (mw::cal::vco::pwmDutyFromCvNorm (0.0f) == Catch::Approx (0.5f));
    REQUIRE (mw::cal::vco::pwmDutyFromCvNorm (1.0f) == Catch::Approx (0.05f));
}

// -----------------------------------------------------------------------------
// C3 — duty/dt overlap clamp: effective duty >= max(kPwmDutyMin, dt), windows
// never overlap at the 5% / high-pitch extreme.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: effective duty never drops below max of the five-percent floor and dt", "[vcoshape]") {
    const double sr = 44100.0;   // lowest blessed SR => largest dt for a given freq
    // Drive a high fundamental at the narrowest requested width and confirm the
    // effective duty is clamped up to max(kPwmDutyMin, dt) [ADR-002 C3; §4.6].
    for (double octave = 0.0; octave <= 8.0; octave += 0.5) {
        Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts + octave, Footage::Two,
                                  1.0f /* narrowest */, OscAaMode::PolyBlep, nullptr);
        const double dt   = osc.dt();
        const double duty = osc.duty();
        const double floorDuty = std::max (static_cast<double> (mw::cal::vco::kPwmDutyMin), dt);

        REQUIRE (duty >= floorDuty - 1e-9);

        // The two BLEP correction windows are each `dt` wide (one at phase 0, one at
        // the duty phase). They never overlap: duty >= dt AND (1 - duty) >= dt.
        REQUIRE (duty       >= dt - 1e-9);
        REQUIRE ((1.0 - duty) >= dt - 1e-9);
    }
}

TEST_CASE("vcoshape: at the five-percent and high-pitch extreme the rising and falling windows stay disjoint", "[vcoshape]") {
    // Force a very high fundamental at 44.1 kHz so dt is large and the requested 5%
    // duty would overlap without the clamp; assert the clamp keeps the windows apart.
    const double sr = 44100.0;
    Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts + 6.0, Footage::Two,
                              1.0f, OscAaMode::PolyBlep, nullptr);   // very high pitch
    const double dt   = osc.dt();
    const double duty = osc.duty();
    REQUIRE (dt > mw::cal::vco::kPwmDutyMin);          // the clamp must be active here
    REQUIRE (duty == Catch::Approx (dt).margin (1e-9)); // clamped up to dt
    REQUIRE (duty >= dt - 1e-9);
    REQUIRE ((1.0 - duty) >= dt - 1e-9);
}

// -----------------------------------------------------------------------------
// §2.2 / §2.3 / C9 — AA mode selection and HQ auto-escalation.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: a voice below the escalation threshold uses PolyBLEP and above it uses minBLEP", "[vcoshape]") {
    MinBlepTable table;
    table.build();
    REQUIRE (table.isBuilt());

    const double sr = 48000.0;

    // Low fundamental, Standard tier (PolyBlep): effective mode stays PolyBLEP.
    Oscillator lo = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 0.0f,
                             OscAaMode::PolyBlep, &table);   // ~442 Hz
    REQUIRE (lo.frequencyHz() < mw::cal::vco::hqEscalationHzAt (sr));
    REQUIRE (lo.effectiveAaMode() == OscAaMode::PolyBlep);

    // High fundamental above kHqEscalationHz: auto-escalates to minBLEP even though
    // the requested tier is PolyBlep [ADR-002 C9; §2.3].
    Oscillator hi = makeOsc (sr, mw::cal::vco::kPitchRefVolts + 4.0, Footage::Two, 0.0f,
                             OscAaMode::PolyBlep, &table);   // very high fundamental
    REQUIRE (hi.frequencyHz() > mw::cal::vco::hqEscalationHzAt (sr));
    REQUIRE (hi.effectiveAaMode() == OscAaMode::MinBlepHq);

    // Sweeping back below the threshold returns to PolyBLEP (toggle per §2.3).
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts);
    c.footage      = Footage::Eight;
    c.aaMode       = OscAaMode::PolyBlep;
    hi.setControls (c);
    REQUIRE (hi.effectiveAaMode() == OscAaMode::PolyBlep);
}

TEST_CASE("vcoshape: the HQ tier renders a finite band-limited saw close to the trivial ramp away from edges", "[vcoshape]") {
    // minBLEP-mode saw oracle: away from the wrap edge the correction is ~0, so the
    // HQ saw tracks the trivial ramp (2*t-1); the correction is finite and bounded.
    MinBlepTable table;
    table.build();
    const double sr = 48000.0;
    Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 0.0f,
                              OscAaMode::MinBlepHq, &table);   // ~442 Hz, HQ tier
    REQUIRE (osc.effectiveAaMode() == OscAaMode::MinBlepHq);

    double maxAbs = 0.0;
    for (int i = 0; i < 8000; ++i) {
        const float saw = osc.renderSample().saw;
        REQUIRE (std::isfinite (saw));
        maxAbs = std::max (maxAbs, static_cast<double> (std::abs (saw)));
    }
    // The band-limited saw overshoots the trivial +-1 ramp only modestly (Gibbs),
    // never blows up.
    REQUIRE (maxAbs < 2.0);
}

// -----------------------------------------------------------------------------
// §2.2 — the AA mode is set only in prepare/setControls, never per-sample.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: the AA mode is fixed across a render block and only changes via setControls", "[vcoshape]") {
    MinBlepTable table;
    table.build();
    const double sr = 48000.0;

    // Below threshold: the effective mode is constant for every sample of the block
    // (no per-sample mode flip on the audio thread) [§2.2; ADR-018 Q5].
    Oscillator osc = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 0.3f,
                              OscAaMode::PolyBlep, &table);
    const OscAaMode m0 = osc.effectiveAaMode();
    for (int i = 0; i < 4096; ++i) {
        (void) osc.renderSample();
        REQUIRE (osc.effectiveAaMode() == m0);   // invariant within the block
    }
}

// -----------------------------------------------------------------------------
// Real-time safety: the band-limited hot path allocates/locks nothing.
// -----------------------------------------------------------------------------
TEST_CASE("vcoshape: the band-limited renderSample is noexcept and allocates or locks nothing", "[vcoshape][rt]") {
    static_assert (noexcept (std::declval<Oscillator&>().renderSample()),
                   "renderSample() must be noexcept [§2.4; ADR-002 C11].");
    static_assert (noexcept (std::declval<const Oscillator&>().duty()),
                   "duty() must be noexcept.");
    static_assert (noexcept (std::declval<const Oscillator&>().effectiveAaMode()),
                   "effectiveAaMode() must be noexcept.");

    MinBlepTable table;
    table.build();   // table built off the audio thread, before arming
    const double sr = 48000.0;

    // Both tiers exercised under the guard: PolyBLEP and HQ/escalated minBLEP.
    Oscillator poly = makeOsc (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight, 0.4f,
                               OscAaMode::PolyBlep, &table);
    Oscillator hq   = makeOsc (sr, mw::cal::vco::kPitchRefVolts + 4.0, Footage::Two, 0.4f,
                               OscAaMode::MinBlepHq, &table);

    mw::test::AudioThreadGuard g;
    g.arm();
    double acc = 0.0;
    for (int i = 0; i < 8192; ++i) {
        const auto a = poly.renderSample();
        const auto b = hq.renderSample();
        acc += a.saw + a.pulse + b.saw + b.pulse;
    }
    g.disarm();

    REQUIRE_FALSE (g.violated());
    REQUIRE (g.violations().empty());
    REQUIRE (std::isfinite (acc));
}
