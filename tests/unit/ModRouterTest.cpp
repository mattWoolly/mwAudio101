// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for ModRouter — the fixed LFO/ADSR modulation router (task 082).
// Realizes docs/design/05 §3.1 (routing model), §3.2 (class signature + the fixed
// resolve() expression) and §3.3 / §11 (routing table + acceptance hooks).
//
// Test-case names begin with "modrouter" so `-R modrouter` selects exactly this
// suite under the silent-pass rule (the discovery registers names, not tags). The
// display text avoids '[' so ctest -R selection is not broken by a stray tag parse.
// Each TEST_CASE maps to an 082 acceptance criterion.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <type_traits>

#include "control/ModRouter.h"

#include "../invariants/AudioThreadGuard.h"

using namespace mw::control;

namespace {
// A hand-rolled oracle of the §3.2 fixed expression, written independently of the
// implementation so the test pins the documented math, not the code under test.
ModOutputs oracle(const ModDepths& d, PwmSource pwm, const ModInputs& in) noexcept {
    ModOutputs o{};
    o.pitchMod = in.lfoValue * d.lfoToPitch;
    o.cutoffMod = in.lfoValue * d.lfoToCutoff + in.envValue * d.envToCutoff;
    o.pwmMod = (pwm == PwmSource::Lfo) ? in.lfoValue * d.lfoToPwm
             : (pwm == PwmSource::Env) ? in.envValue * d.envToPwm
                                       : in.pwmManual;
    o.vcaTremolo = in.lfoValue * d.lfoToVca;
    return o;
}
} // namespace

// --- §3.2 seam shape: prepare / setters / resolve are noexcept, accessors -------

TEST_CASE("modrouter: the hot path and setters are all noexcept", "[modrouter]") {
    STATIC_REQUIRE(noexcept(std::declval<ModRouter&>().prepare(48000.0)));
    STATIC_REQUIRE(noexcept(std::declval<ModRouter&>().setPwmSource(PwmSource::Lfo)));
    STATIC_REQUIRE(noexcept(std::declval<ModRouter&>().setVcaSource(VcaSource::Env)));
    STATIC_REQUIRE(noexcept(std::declval<ModRouter&>().setDepths(ModDepths{})));
    STATIC_REQUIRE(noexcept(std::declval<const ModRouter&>().resolve(ModInputs{})));
    STATIC_REQUIRE(noexcept(std::declval<const ModRouter&>().pwmSource()));
    STATIC_REQUIRE(noexcept(std::declval<const ModRouter&>().vcaSource()));
}

TEST_CASE("modrouter: default source switches match docs/design/05 sec 3.2", "[modrouter]") {
    ModRouter r;
    REQUIRE(r.pwmSource() == PwmSource::Lfo);   // signature default
    REQUIRE(r.vcaSource() == VcaSource::Env);   // signature default
}

// --- C1 (§3.1, §3.3): same LFO value reaches pitch/PWM/cutoff via INDEPENDENT ---
//     depths; there is no second envelope.

TEST_CASE("modrouter: one LFO value reaches pitch PWM and cutoff scaled by independent depths",
          "[modrouter]") {
    ModRouter r;
    r.prepare(48000.0);
    r.setPwmSource(PwmSource::Lfo);   // route the LFO into PWM for this check

    ModDepths d{};
    d.lfoToPitch = 0.25f;
    d.lfoToPwm = 0.50f;
    d.lfoToCutoff = 0.75f;
    d.lfoToVca = 0.10f;
    d.envToCutoff = 0.0f;             // isolate the LFO contribution to cutoff
    r.setDepths(d);

    const float lfo = 0.8f;
    ModInputs in{};
    in.lfoValue = lfo;
    in.envValue = 0.0f;

    const ModOutputs o = r.resolve(in);

    // The SAME instantaneous lfoValue scaled by each independent depth gain.
    REQUIRE(o.pitchMod == Catch::Approx(lfo * 0.25f));
    REQUIRE(o.pwmMod == Catch::Approx(lfo * 0.50f));
    REQUIRE(o.cutoffMod == Catch::Approx(lfo * 0.75f));
    REQUIRE(o.vcaTremolo == Catch::Approx(lfo * 0.10f));

    // Depths are genuinely independent: changing one destination's gain must not
    // change another destination's output for the same input.
    ModDepths d2 = d;
    d2.lfoToCutoff = 0.10f;           // change ONLY the cutoff gain
    r.setDepths(d2);
    const ModOutputs o2 = r.resolve(in);
    REQUIRE(o2.cutoffMod == Catch::Approx(lfo * 0.10f));   // cutoff changed
    REQUIRE(o2.pitchMod == Catch::Approx(o.pitchMod));     // pitch unchanged
    REQUIRE(o2.pwmMod == Catch::Approx(o.pwmMod));         // pwm unchanged
    REQUIRE(o2.vcaTremolo == Catch::Approx(o.vcaTremolo)); // tremolo unchanged
}

TEST_CASE("modrouter: there is exactly one shared envelope input, no second envelope",
          "[modrouter]") {
    // §3.1: one shared ADSR routes to cutoff/VCA/PWM. The ModInputs POD exposes a
    // SINGLE envelope scalar; the same envValue feeds every ENV destination.
    STATIC_REQUIRE(std::is_same_v<decltype(ModInputs::envValue), float>);

    ModRouter r;
    r.prepare(48000.0);
    r.setPwmSource(PwmSource::Env);

    ModDepths d{};
    d.envToCutoff = 0.6f;
    d.envToPwm = 0.4f;
    r.setDepths(d);

    ModInputs in{};
    in.lfoValue = 0.0f;       // silence the LFO so only the shared ENV is visible
    in.envValue = 0.5f;

    const ModOutputs o = r.resolve(in);
    // The ONE envelope value drives both ENV destinations (cutoff + PWM-when-ENV).
    REQUIRE(o.cutoffMod == Catch::Approx(0.5f * 0.6f));
    REQUIRE(o.pwmMod == Catch::Approx(0.5f * 0.4f));
}

// --- C2 (§3.2): PWM uses ENV only when source=ENV, LFO only when=LFO, manual=MANUAL

TEST_CASE("modrouter: PWM source switch selects ENV LFO or MANUAL per docs/design/05 sec 3.2",
          "[modrouter]") {
    ModRouter r;
    r.prepare(48000.0);

    ModDepths d{};
    d.lfoToPwm = 0.7f;
    d.envToPwm = 0.3f;
    r.setDepths(d);

    ModInputs in{};
    in.lfoValue = 0.9f;
    in.envValue = 0.4f;
    in.pwmManual = 0.55f;

    // LFO source: PWM == lfoValue * lfoToPwm; ignores env and manual.
    r.setPwmSource(PwmSource::Lfo);
    REQUIRE(r.pwmSource() == PwmSource::Lfo);
    REQUIRE(r.resolve(in).pwmMod == Catch::Approx(0.9f * 0.7f));

    // ENV source: PWM == envValue * envToPwm; ignores lfo and manual.
    r.setPwmSource(PwmSource::Env);
    REQUIRE(r.pwmSource() == PwmSource::Env);
    REQUIRE(r.resolve(in).pwmMod == Catch::Approx(0.4f * 0.3f));

    // MANUAL source: PWM == pwmManual static value; ignores lfo, env, and depths.
    r.setPwmSource(PwmSource::Manual);
    REQUIRE(r.pwmSource() == PwmSource::Manual);
    REQUIRE(r.resolve(in).pwmMod == Catch::Approx(0.55f));
}

TEST_CASE("modrouter: only the selected PWM source affects pwmMod, the others are inert",
          "[modrouter]") {
    ModRouter r;
    r.prepare(48000.0);

    ModDepths d{};
    d.lfoToPwm = 0.7f;
    d.envToPwm = 0.3f;
    r.setDepths(d);

    // In MANUAL mode, varying lfo/env must NOT move pwmMod (it tracks pwmManual).
    r.setPwmSource(PwmSource::Manual);
    ModInputs a{};
    a.lfoValue = 0.1f; a.envValue = 0.2f; a.pwmManual = 0.42f;
    ModInputs b{};
    b.lfoValue = -0.9f; b.envValue = 0.95f; b.pwmManual = 0.42f;
    REQUIRE(r.resolve(a).pwmMod == Catch::Approx(0.42f));
    REQUIRE(r.resolve(b).pwmMod == Catch::Approx(0.42f));

    // In LFO mode, varying env/manual must NOT move pwmMod.
    r.setPwmSource(PwmSource::Lfo);
    ModInputs c{};
    c.lfoValue = 0.5f; c.envValue = 0.0f; c.pwmManual = 0.0f;
    ModInputs e{};
    e.lfoValue = 0.5f; e.envValue = 1.0f; e.pwmManual = 1.0f;
    REQUIRE(r.resolve(c).pwmMod == Catch::Approx(r.resolve(e).pwmMod));
}

// --- C3 (§3.1): cutoffMod sums LFO depth + ADSR env depth; vcaTremolo = lfo*lfoToVca

TEST_CASE("modrouter: cutoffMod sums the LFO depth and the ADSR env depth", "[modrouter]") {
    ModRouter r;
    r.prepare(48000.0);

    ModDepths d{};
    d.lfoToCutoff = 0.5f;
    d.envToCutoff = 0.25f;
    r.setDepths(d);

    ModInputs in{};
    in.lfoValue = -0.4f;     // bipolar LFO
    in.envValue = 0.8f;      // unipolar ENV

    const ModOutputs o = r.resolve(in);
    // §3.2: cutoffMod = lfo*lfoToCutoff + env*envToCutoff (a SUM of both paths).
    const float expected = (-0.4f * 0.5f) + (0.8f * 0.25f);
    REQUIRE(o.cutoffMod == Catch::Approx(expected));
}

TEST_CASE("modrouter: vcaTremolo equals lfoValue times lfoToVca and ignores the envelope",
          "[modrouter]") {
    ModRouter r;
    r.prepare(48000.0);

    ModDepths d{};
    d.lfoToVca = 0.65f;
    d.envToCutoff = 0.99f;   // an ENV path that must NOT leak into the tremolo
    r.setDepths(d);

    ModInputs in{};
    in.lfoValue = 0.3f;
    in.envValue = 1.0f;
    REQUIRE(r.resolve(in).vcaTremolo == Catch::Approx(0.3f * 0.65f));

    // Tremolo tracks the LFO only — flipping envValue leaves vcaTremolo unchanged.
    ModInputs in2 = in;
    in2.envValue = 0.0f;
    REQUIRE(r.resolve(in2).vcaTremolo == Catch::Approx(r.resolve(in).vcaTremolo));
}

// --- Oracle cross-check across the whole output: implementation == §3.2 math ----

TEST_CASE("modrouter: resolve matches the docs/design/05 sec 3.2 fixed expression oracle",
          "[modrouter]") {
    ModRouter r;
    r.prepare(44100.0);

    ModDepths d{};
    d.lfoToPitch = 0.12f;
    d.lfoToPwm = 0.34f;
    d.lfoToCutoff = 0.56f;
    d.envToCutoff = 0.78f;
    d.envToPwm = 0.21f;
    d.lfoToVca = 0.43f;
    r.setDepths(d);

    const ModInputs ins[] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
        {-1.0f, 0.0f, 0.5f},
        {0.37f, 0.91f, 0.66f},
        {-0.62f, 0.25f, 0.10f},
    };

    for (const PwmSource pwm : {PwmSource::Env, PwmSource::Manual, PwmSource::Lfo}) {
        r.setPwmSource(pwm);
        for (const ModInputs& in : ins) {
            const ModOutputs got = r.resolve(in);
            const ModOutputs exp = oracle(d, pwm, in);
            REQUIRE(got.pitchMod == Catch::Approx(exp.pitchMod));
            REQUIRE(got.pwmMod == Catch::Approx(exp.pwmMod));
            REQUIRE(got.cutoffMod == Catch::Approx(exp.cutoffMod));
            REQUIRE(got.vcaTremolo == Catch::Approx(exp.vcaTremolo));
        }
    }
}

// --- §3.2: setVcaSource is honored by the accessor (the VCA ENV/GATE switch) ----

TEST_CASE("modrouter: setVcaSource is reflected by the vcaSource accessor", "[modrouter]") {
    ModRouter r;
    REQUIRE(r.vcaSource() == VcaSource::Env);
    r.setVcaSource(VcaSource::Gate);
    REQUIRE(r.vcaSource() == VcaSource::Gate);
    r.setVcaSource(VcaSource::Env);
    REQUIRE(r.vcaSource() == VcaSource::Env);
}

// --- C4 (§10): resolve() performs no heap allocation under the RT sentinel ------

TEST_CASE("modrouter: resolve performs no heap allocation under the alloc sentinel",
          "[modrouter]") {
    ModRouter r;
    r.prepare(48000.0);          // prepare runs BEFORE arming (no alloc expected anyway)

    ModDepths d{};
    d.lfoToPitch = 0.2f; d.lfoToPwm = 0.3f; d.lfoToCutoff = 0.4f;
    d.envToCutoff = 0.5f; d.envToPwm = 0.6f; d.lfoToVca = 0.7f;
    r.setDepths(d);
    r.setPwmSource(PwmSource::Lfo);
    r.setVcaSource(VcaSource::Env);

    mw::test::AudioThreadGuard g;
    g.arm();
    float acc = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        ModInputs in{};
        in.lfoValue = std::sin(static_cast<float>(i) * 0.01f);
        in.envValue = 0.5f;
        in.pwmManual = 0.25f;
        const ModOutputs o = r.resolve(in);
        acc += o.pitchMod + o.pwmMod + o.cutoffMod + o.vcaTremolo;
    }
    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
    REQUIRE(std::isfinite(acc));   // keep the loop from being optimized away
}
