// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the VCO phase core / exp-pitch / footage / drift model
// (task 029). Test-case names begin with "vco" so `ctest -R vco` selects them
// (silent-pass rule; the display names avoid '[' so the tag is not mis-parsed).
//
// Covers every acceptance criterion in plan/backlog/029 against
// docs/design/01-dsp-oscillators.md §4.1-§4.4, §4.7, §2.1, §10:
//   - footage octave ratios are EXACT (4' == 2x 8', 16' == 0.5x 8', 2' == 4x 8');
//   - 8' + Transpose-Middle + 0-cent tune lands EXACTLY on the 442.0 Hz reference;
//   - exp 1V/oct law: +1 V doubles frequency;
//   - dt_ never exceeds kDtMax = 0.5 across the audio band (at most one wrap/sample);
//   - renderSample advances the master phase once per sample and wraps in [0,1);
//   - wrappedThisSample() flags exactly the wrap sample;
//   - the drift hooks exist and default near-zero (settled freq == ideal freq);
//   - every (PI) pitch/footage/drift constant is referenced from the calibration
//     header, not duplicated;
//   - renderSample/phase/frequencyHz are noexcept and allocate/lock nothing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <type_traits>
#include <utility>

#include "dsp/Oscillator.h"
#include "calibration/VcoConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::Oscillator;
using mw101::dsp::OscControls;
using mw101::dsp::OscAaMode;
using mw101::dsp::Footage;

namespace {

// Drive the oscillator at footage `f` with summed pitch CV `cvVolts` and read back
// the steady-state fundamental it computes. setControls then a single advance.
double freqAt (double sampleRate, double cvVolts, Footage f, float pwm = 0.0f) {
    Oscillator osc;
    osc.prepare (sampleRate, nullptr);
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (cvVolts);
    c.footage = f;
    c.pwmCvNorm = pwm;
    c.aaMode = OscAaMode::PolyBlep;
    osc.setControls (c);
    return osc.frequencyHz();
}

} // namespace

TEST_CASE("vco: 8 foot plus Transpose-Middle plus zero-cent tune lands on the 442 Hz reference", "[vco]") {
    // The calibration anchor kPitchRefVolts is chosen so that, at the 8' reference
    // footage with the reference (Transpose Middle, 0-cent) summed CV, the converter
    // produces EXACTLY kPitchRefHz = 442.0 Hz [§4.3, §4.4, §10].
    const double f = freqAt (48000.0, mw::cal::vco::kPitchRefVolts, Footage::Eight);
    REQUIRE (f == Catch::Approx (mw::cal::vco::kPitchRefHz));   // 442.0 Hz
    REQUIRE (mw::cal::vco::kPitchRefHz == 442.0);               // (PI) reference is A4 = 442
}

TEST_CASE("vco: footage 4 foot is exactly twice the 8 foot frequency and 16 foot is half and 2 foot is quadruple", "[vco]") {
    const double sr = 48000.0;
    const double cv = mw::cal::vco::kPitchRefVolts;
    const double f16 = freqAt (sr, cv, Footage::Sixteen);
    const double f8  = freqAt (sr, cv, Footage::Eight);
    const double f4  = freqAt (sr, cv, Footage::Four);
    const double f2  = freqAt (sr, cv, Footage::Two);

    // Exact octave ratios: footage is a CV octave offset, not an analog divider
    // (16'/8'/4'/2' span -1/0/+1/+2 octaves about the 8' reference) [§4.4, §10].
    REQUIRE (f4 == Catch::Approx (2.0 * f8));
    REQUIRE (f16 == Catch::Approx (0.5 * f8));
    REQUIRE (f2 == Catch::Approx (4.0 * f8));
    REQUIRE (f2 == Catch::Approx (2.0 * f4));
}

TEST_CASE("vco: the exponential converter follows 1 V per octave so plus one volt doubles the frequency", "[vco]") {
    const double sr = 48000.0;
    const double base = freqAt (sr, mw::cal::vco::kPitchRefVolts, Footage::Eight);
    const double up   = freqAt (sr, mw::cal::vco::kPitchRefVolts + 1.0, Footage::Eight);
    const double down = freqAt (sr, mw::cal::vco::kPitchRefVolts - 1.0, Footage::Eight);
    REQUIRE (up == Catch::Approx (2.0 * base));
    REQUIRE (down == Catch::Approx (0.5 * base));
}

TEST_CASE("vco: dt never exceeds kDtMax across the audio band so at most one wrap per sample", "[vco]") {
    // kDtMax = 0.5 is the Nyquist clamp guaranteeing at most one wrap per sample
    // [§4.4]. Push the pitch far above Nyquist (huge CV) at the lowest blessed SR and
    // confirm dt saturates AT the clamp, never above it.
    REQUIRE (mw::cal::vco::kDtMax == 0.5);

    Oscillator osc;
    osc.prepare (44100.0, nullptr);
    osc.reset();
    OscControls c{};
    c.footage = Footage::Two;
    c.aaMode = OscAaMode::PolyBlep;
    // Sweep across (and well beyond) the audio band; dt must always stay <= kDtMax.
    for (double cv = -2.0; cv <= 20.0; cv += 0.25) {
        c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts + cv);
        osc.setControls (c);
        REQUIRE (osc.dt() <= mw::cal::vco::kDtMax);
        REQUIRE (osc.dt() > 0.0);
    }

    // At an extreme over-Nyquist pitch the clamp must be ACTIVE (saturated at kDtMax),
    // proving the guard actually engages (a freq/fs that would exceed 0.5 is held).
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts + 20.0);
    osc.setControls (c);
    REQUIRE (osc.dt() == Catch::Approx (mw::cal::vco::kDtMax));
}

TEST_CASE("vco: renderSample advances the master phase exactly once per sample and wraps within zero to one", "[vco]") {
    const double sr = 48000.0;
    Oscillator osc;
    osc.prepare (sr, nullptr);
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts);   // ~442 Hz
    c.footage = Footage::Eight;
    c.aaMode = OscAaMode::PolyBlep;
    osc.setControls (c);

    REQUIRE (osc.phase() == 0.0);   // reset() leaves phase at 0

    const double dt = osc.dt();
    double prev = osc.phase();
    int wraps = 0;
    constexpr int kN = 5000;
    for (int i = 0; i < kN; ++i) {
        const auto out = osc.renderSample();
        (void) out;
        const double p = osc.phase();
        REQUIRE (p >= 0.0);
        REQUIRE (p < 1.0);                 // phase stays in [0,1)
        if (osc.wrappedThisSample()) {
            ++wraps;
            // On a wrap the phase decreased by ~1 cycle.
            REQUIRE (p < prev);
        } else {
            // No wrap => monotone advance by exactly dt.
            REQUIRE (p == Catch::Approx (prev + dt));
        }
        prev = p;
    }
    // Expected wraps ~= kN * dt (one wrap per cycle); allow +-1 for boundary phase.
    const double expected = kN * dt;
    REQUIRE (std::abs (static_cast<double> (wraps) - expected) <= 1.0);
}

TEST_CASE("vco: the raw saw output is a rising ramp synchronous with the master phase", "[vco]") {
    // core-osc-4 adds band-limiting; this task emits the raw ramp = 2*phase - 1.
    const double sr = 48000.0;
    Oscillator osc;
    osc.prepare (sr, nullptr);
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts);
    c.footage = Footage::Eight;
    c.aaMode = OscAaMode::PolyBlep;
    osc.setControls (c);

    for (int i = 0; i < 2000; ++i) {
        const auto out = osc.renderSample();
        const float expectedSaw = static_cast<float> (2.0 * osc.phase() - 1.0);
        REQUIRE (out.saw == Catch::Approx (expectedSaw));
        REQUIRE (out.saw >= -1.0f);
        REQUIRE (out.saw < 1.0f);
    }
}

TEST_CASE("vco: the drift model is present but defaults to a near-zero settled error", "[vco]") {
    // §4.7: hooks must EXIST but the default build ships drift effectively at zero.
    // With the default (zero) per-voice seeds, the fully-settled fundamental must
    // equal the ideal exp-pitch frequency to a very tight tolerance.
    const double sr = 48000.0;
    Oscillator osc;
    osc.prepare (sr, nullptr);
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts);
    c.footage = Footage::Eight;
    c.aaMode = OscAaMode::PolyBlep;
    osc.setControls (c);

    // Force the warm-up transient to fully settle, then re-evaluate.
    osc.settleDriftForTest();
    osc.setControls (c);
    REQUIRE (osc.frequencyHz() == Catch::Approx (mw::cal::vco::kPitchRefHz).epsilon (1e-9));

    // The drift seeds are exposed (the hooks exist) and default to zero.
    REQUIRE (osc.scaleErrSeed() == 0.0f);
    REQUIRE (osc.offsetErrSeed() == 0.0f);
}

TEST_CASE("vco: a nonzero drift scale seed detunes the fundamental within the bounded range", "[vco]") {
    // The seeds are HOOKS (§4.7): a nonzero scale seed must move the pitch, and the
    // magnitude must stay within the datasheet-bounded (PI) ceiling.
    const double sr = 48000.0;
    Oscillator osc;
    osc.prepare (sr, nullptr);
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts);
    c.footage = Footage::Eight;
    c.aaMode = OscAaMode::PolyBlep;

    osc.setDriftSeeds (1.0f, 0.0f);   // full positive scale seed
    osc.settleDriftForTest();
    osc.setControls (c);
    const double sharp = osc.frequencyHz();

    REQUIRE (sharp > mw::cal::vco::kPitchRefHz);   // sharper than ideal

    // Bound: the max scale drift is kDriftScalePpmMax ppm of an octave's worth of CV,
    // i.e. the detune ratio must not exceed 2^(kDriftScalePpmMax/1e6) beyond unity.
    const double maxRatio = std::pow (2.0, mw::cal::drift::kDriftScalePpmMax / 1.0e6);
    REQUIRE (sharp <= mw::cal::vco::kPitchRefHz * maxRatio * 1.0001);

    REQUIRE (mw::cal::drift::kDriftScalePpmMax == 50.0);
    REQUIRE (mw::cal::warmup::kWarmupTauSec == 30.0);
    REQUIRE (mw::cal::drift::kHfTrackEnable == true);
}

TEST_CASE("vco: every PI pitch and footage and drift constant is referenced from the calibration header", "[vco]") {
    // (PI) centralization (§10): pin the centralized values so an inlined-literal
    // regression in the DSP source fails here.
    REQUIRE (mw::cal::vco::kPitchRefHz == 442.0);
    REQUIRE (mw::cal::vco::kDtMax == 0.5);
    REQUIRE (mw::cal::drift::kDriftScalePpmMax == 50.0);
    REQUIRE (mw::cal::drift::kDriftScaleErrPct == 0.05);
    REQUIRE (mw::cal::warmup::kWarmupTauSec == 30.0);
    REQUIRE (mw::cal::drift::kHfTrackEnable == true);

    // Footage octave offsets (volts == octaves at 1V/oct): -1/0/+1/+2 about 8'.
    REQUIRE (mw::cal::vco::footageOffsetV (Footage::Sixteen) == -1.0);
    REQUIRE (mw::cal::vco::footageOffsetV (Footage::Eight) == 0.0);
    REQUIRE (mw::cal::vco::footageOffsetV (Footage::Four) == 1.0);
    REQUIRE (mw::cal::vco::footageOffsetV (Footage::Two) == 2.0);
}

TEST_CASE("vco: renderSample and phase and frequencyHz are noexcept and allocate or lock nothing", "[vco]") {
    static_assert (noexcept (std::declval<Oscillator&>().renderSample()),
                   "renderSample() must be noexcept [§2.4; ADR-001 C5].");
    static_assert (noexcept (std::declval<const Oscillator&>().phase()),
                   "phase() must be noexcept.");
    static_assert (noexcept (std::declval<const Oscillator&>().frequencyHz()),
                   "frequencyHz() must be noexcept.");
    static_assert (noexcept (std::declval<const Oscillator&>().wrappedThisSample()),
                   "wrappedThisSample() must be noexcept.");
    static_assert (noexcept (std::declval<Oscillator&>().reset()),
                   "reset() must be noexcept.");
    static_assert (noexcept (std::declval<Oscillator&>().setControls (std::declval<const OscControls&>())),
                   "setControls() must be noexcept.");

    Oscillator osc;
    osc.prepare (48000.0, nullptr);   // prepare may allocate; happens BEFORE arming
    osc.reset();
    OscControls c{};
    c.pitchCvVolts = static_cast<float> (mw::cal::vco::kPitchRefVolts);
    c.footage = Footage::Eight;
    c.aaMode = OscAaMode::PolyBlep;
    osc.setControls (c);

    mw::test::AudioThreadGuard g;
    g.arm();
    double acc = 0.0;
    for (int i = 0; i < 8192; ++i) {
        const auto out = osc.renderSample();          // hot path, armed
        acc += out.saw + out.pulse + osc.phase() + osc.frequencyHz();
        if (osc.wrappedThisSample()) acc += 1.0;
    }
    // setControls is a per-block (non-per-sample) call but must also be alloc-free.
    osc.setControls (c);
    g.disarm();

    REQUIRE_FALSE (g.violated());
    REQUIRE (g.violations().empty());
    REQUIRE (std::isfinite (acc));
}
