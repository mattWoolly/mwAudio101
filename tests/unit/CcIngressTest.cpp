// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/CcIngressTest.cpp — the AUDIO-EFFECT acceptance suite for the continuous-
// controller INGRESS leg of the control-dispatch seam (task 162c, extending the ADR-028
// keystone built by tasks 160 / 161 / 162 / 163).
//
// Test-case display names ALL begin with "cc_ingress" so
// `ctest -R cc_ingress --no-tests=error` selects exactly this suite under the silent-pass
// rule (AGENTS.md "Tests"). The tag is "[cc_ingress]". No literal '[' appears in any
// display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (the assertion class the audit found MISSING — real audible effect,
// measured on RENDERED OUTPUT via a Goertzel fundamental estimator + an RMS-envelope AC
// energy estimator, NOT "non-silent/deterministic"):
//   * A pitch-bend event (BlockContext::controllers.pitchBend / a PitchBend MidiEvent)
//     bends the VCO fundamental UP and DOWN by the mod.bend_range_vco amount — asserted
//     via the rendered fundamental shift (Goertzel), in V/oct (1200 cents == 1 octave);
//   * the same bend, with mod.bend_dest=VCF, shifts the VCF cutoff (the brightness /
//     above-cutoff energy rises) and does NOT move the VCO fundamental — asserted on a
//     resonant, partly-closed filter;
//   * a mod-wheel (CC1) move RAISES the LFO modulation depth per mod.lfo_mod_wheel —
//     asserted audibly (the LFO->filter wobble AC energy rises as the wheel rises);
//   * a CENTERED bend (0) / wheel DOWN (0) is the NEUTRAL identity (no shift / no boost);
//   * the dispatch+render path is deterministic with live controllers active;
//   * the ingress+dispatch+render path stays RT-safe (AudioThreadGuard, [rt]).
//
// The Engine consumes the seam's immutable mw::ParamSnapshot once per control tick AND
// (this task) the live continuous-controller position from BlockContext::controllers /
// the per-sub-block PitchBend + CC1 MidiEvent stream; this file builds those PODs directly
// (the off-thread bridge's job in the real shell) and reads the audible result, so it links
// mwcore ONLY (no JUCE) [docs/design/00 §5.4; ADR-001].

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "params/ParamDefs.h"
#include "params/ParamIDs.h"
#include "params/ParamSnapshot.h"
#include "voice/VoiceTypes.h"
#include "calibration/EngineConstants.h"
#include "calibration/ControlDispatchCcIngressConstants.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- registry-index lookup (same keying the ParamSnapshot uses) --------------------
int slotOf(const char* id) noexcept {
    for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
        const char* a = mw::params::kParamDefs[static_cast<std::size_t>(i)].id;
        const char* b = id;
        int k = 0;
        while (a[k] != '\0' && b[k] != '\0' && a[k] == b[k]) ++k;
        if (a[k] == '\0' && b[k] == '\0') return i;
    }
    return -1;
}

// ParamSnapshot pre-loaded with every live param's DEFAULT in normalized [0,1] form
// (exactly what the bridge emits). Mirrors the DispatchLfoModTest Snap helper.
struct Snap {
    mw::ParamSnapshot s{};

    Snap() noexcept {
        for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
            const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
            if (d.type == mw::params::ParamType::Continuous) {
                s.normalizedValues[static_cast<std::size_t>(i)] = normFor(d, d.defaultValue);
            } else {
                const int idx = static_cast<int>(d.defaultValue);
                s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
                const float denom = (d.choiceCount > 1)
                                        ? static_cast<float>(d.choiceCount - 1) : 1.0f;
                s.normalizedValues[static_cast<std::size_t>(i)] =
                    static_cast<float>(idx) / denom;
            }
        }
    }

    // convertTo0to1(value) for a continuous def: ((value-min)/span)^skew (the JUCE
    // NormalisableRange the bridge uses). The engine's contValueSkewed inverts this.
    static float normFor(const mw::params::ParamDef& d, float value) noexcept {
        const float span = d.maxValue - d.minValue;
        if (span <= 0.0f) return 0.0f;
        const float lin = std::clamp((value - d.minValue) / span, 0.0f, 1.0f);
        return (d.skew == 1.0f) ? lin : std::pow(lin, d.skew);
    }

    void setCont(const char* id, float value) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        const float v = std::clamp(value, d.minValue, d.maxValue);
        s.normalizedValues[static_cast<std::size_t>(i)] = normFor(d, v);
    }

    void setChoice(const char* id, int idx) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
        const float denom = (d.choiceCount > 1) ? static_cast<float>(d.choiceCount - 1) : 1.0f;
        s.normalizedValues[static_cast<std::size_t>(i)] = static_cast<float>(idx) / denom;
    }
};

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOn;
    e.noteId = static_cast<std::int16_t>(note);
    e.data0 = static_cast<float>(note);   // task 118e: note number = pitch (read from data0)
    e.value = vel;
    e.sampleOffset = offset;
    return e;
}

// A PitchBend MidiEvent carrying a CENTERED signed unit value [-1,+1] (the controller
// position the seam consumes — see BlockContext::ContinuousControllers / docs/design/09).
mw::MidiEvent pitchBend(float unit, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::PitchBend;
    e.noteId = -1;
    e.value = unit;     // [-1,+1]; 0 == centered
    e.sampleOffset = offset;
    return e;
}

// A ControlChange MidiEvent for CC1 (mod wheel) carrying a 7-bit value 0..127 (the raw MIDI
// controller value the ingress normalizes to [0,1]).
mw::MidiEvent modWheelCc(int value7bit, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::ControlChange;
    e.noteId = -1;
    e.data0 = static_cast<float>(mw::cal::ccingress::kModWheelCcNumber);   // CC1
    e.value = static_cast<float>(value7bit);                               // 0..127
    e.sampleOffset = offset;
    return e;
}

struct Block {
    std::vector<float> L, R;
    float* ch[2];
    explicit Block(int n)
        : L(static_cast<std::size_t>(n), 0.0f), R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data(); ch[1] = R.data();
    }
    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& ev, int n,
                         const mw::ParamSnapshot* p,
                         mw::ContinuousControllers cc = {}) noexcept {
        mw::BlockContext c{};
        c.audio.channels = ch; c.audio.numChannels = 2; c.audio.numFrames = n;
        c.params = p;
        c.transport = mw::TransportInfo{ 120.0, 0.0, false, kSr };
        c.midi.events = ev.empty() ? nullptr : ev.data();
        c.midi.numEvents = static_cast<int>(ev.size());
        c.controllers = cc;
        return c;
    }
};

// Render `seconds` of sustained audio for a held note, with an OPTIONAL controller event
// applied on the keypress block AND a persistent controller snapshot set on every block
// (so the engine's running controller state stays at the requested position even after the
// keypress block's event). Returns the mono buffer.
//   bendUnit : pitch-bend position [-1,+1] held for the whole render (0 == centered).
//   wheel7   : mod-wheel CC1 raw value 0..127 held for the whole render (0 == down). <0 == none.
std::vector<float> renderHeldWithControllers(mw::Engine& eng, const mw::ParamSnapshot* snap,
                                             int midiNote, double seconds,
                                             float bendUnit, int wheel7,
                                             int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    mw::ContinuousControllers cc{};
    cc.pitchBend = bendUnit;
    cc.modWheel  = (wheel7 >= 0)
                       ? static_cast<float>(wheel7) / mw::cal::ccingress::kSevenBitMax
                       : 0.0f;

    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, 1.0f, 0) };
        // Also drive the live controller events on the keypress block so the engine's
        // event-driven ingress path is exercised (not only the snapshot seed).
        ev.push_back(pitchBend(bendUnit, 0));
        if (wheel7 >= 0)
            ev.push_back(modWheelCc(wheel7, 0));
        auto c = on.ctx(ev, kBlk, snap, cc);
        eng.process(c);
    }
    for (int b = 1; b < warmupBlocks; ++b) {
        Block w(kBlk);
        std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap, cc);
        eng.process(c);
    }
    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total) + kBlk);
    int rendered = 0;
    while (rendered < total) {
        Block w(kBlk);
        std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap, cc);
        eng.process(c);
        for (int i = 0; i < kBlk && rendered < total; ++i, ++rendered)
            out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    return out;
}

double goertzelPower(const std::vector<float>& x, double f, double sr) noexcept {
    const int N = static_cast<int>(x.size());
    if (N == 0) return 0.0;
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int n = 0; n < N; ++n) {
        const double s0 = static_cast<double>(x[static_cast<std::size_t>(n)]) + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

double estimateFundamental(const std::vector<float>& x, double sr,
                           double fLo, double fHi) noexcept {
    double bestF = fLo, bestP = -1.0;
    const int steps = 600;
    for (int i = 0; i <= steps; ++i) {
        const double f = fLo * std::pow(fHi / fLo, static_cast<double>(i) / steps);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    const double lo = bestF * 0.97, hi = bestF * 1.03;
    for (int i = 0; i <= 200; ++i) {
        const double f = lo + (hi - lo) * (static_cast<double>(i) / 200.0);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    return bestF;
}

double midiHz(int n) noexcept { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}

std::vector<float> rmsEnvelope(const std::vector<float>& x, int hop) noexcept {
    std::vector<float> env;
    const int N = static_cast<int>(x.size());
    for (int start = 0; start + hop <= N; start += hop) {
        double acc = 0.0;
        for (int i = 0; i < hop; ++i) {
            const double v = x[static_cast<std::size_t>(start + i)];
            acc += v * v;
        }
        env.push_back(static_cast<float>(std::sqrt(acc / hop)));
    }
    return env;
}

double envAcEnergy(const std::vector<float>& env) noexcept {
    double mean = 0.0; for (float v : env) mean += v;
    mean /= std::max<std::size_t>(1, env.size());
    double acc = 0.0;
    for (float v : env) { const double d = v - mean; acc += d * d; }
    return acc / std::max<std::size_t>(1, env.size());
}

// Energy strictly ABOVE a frequency (a brightness proxy): sum the Goertzel power on a set of
// harmonics above the cutoff band. Rises when the VCF cutoff opens (more harmonics pass).
double energyAboveBand(const std::vector<float>& x, double f0, double sr,
                       int firstHarmonic, int lastHarmonic) noexcept {
    double acc = 0.0;
    for (int h = firstHarmonic; h <= lastHarmonic; ++h)
        acc += goertzelPower(x, f0 * h, sr);
    return acc;
}

constexpr float kSaneBound = 64.0f;

void openFilter(Snap& snap) noexcept {
    snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);   // fully open
    snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
    snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
}

} // namespace

// ===========================================================================
// PITCH-BEND -> VCO (the headline): a live pitch-bend with mod.bend_dest=VCO and a non-zero
// mod.bend_range_vco shifts the rendered VCO fundamental UP (bend up) and DOWN (bend down) by
// the range amount, in V/oct (1200 cents == 1 octave). A centered bend leaves it neutral.
// ===========================================================================
TEST_CASE("cc_ingress: a pitch-bend event bends the VCO fundamental by the bend range",
          "[cc_ingress]") {
    const double f0 = midiHz(60);

    auto fundamentalAtBend = [&](float bendUnit) {
        Snap snap; openFilter(snap);
        snap.setChoice(mw::params::ids::kModBendDest, 0);                  // VCO
        snap.setCont(mw::params::ids::kModBendRangeVco, 1200.0f);          // 1 octave at full bend
        snap.setCont(mw::params::ids::kLfoDepthPitch, 0.0f);              // isolate the bend (no vibrato)
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeldWithControllers(eng, &snap.s, 60, 0.40, bendUnit, /*wheel7=*/-1);
        return estimateFundamental(buf, kSr, f0 * 0.25, f0 * 4.0);
    };

    const double neutral = fundamentalAtBend(0.0f);   // centered: no shift
    const double bentUp  = fundamentalAtBend(1.0f);   // +1 octave
    const double bentDn  = fundamentalAtBend(-1.0f);  // -1 octave

    REQUIRE(neutral == Catch::Approx(f0).epsilon(0.03));    // centered bend == no shift
    REQUIRE(bentUp  == Catch::Approx(f0 * 2.0).epsilon(0.06));   // full bend up == +1 octave
    REQUIRE(bentDn  == Catch::Approx(f0 * 0.5).epsilon(0.06));   // full bend down == -1 octave
}

// ===========================================================================
// PITCH-BEND range scales the shift: half the range param -> half the octave shift (a
// semitone-accurate V/oct law). A 600-cent range at full bend is +half an octave (x sqrt2).
// ===========================================================================
TEST_CASE("cc_ingress: the pitch-bend shift scales with the mod.bend_range_vco amount",
          "[cc_ingress]") {
    const double f0 = midiHz(57);

    auto fundamentalAtRange = [&](float rangeCents) {
        Snap snap; openFilter(snap);
        snap.setChoice(mw::params::ids::kModBendDest, 0);                  // VCO
        snap.setCont(mw::params::ids::kModBendRangeVco, rangeCents);
        snap.setCont(mw::params::ids::kLfoDepthPitch, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeldWithControllers(eng, &snap.s, 57, 0.40, /*bend=*/1.0f, /*wheel7=*/-1);
        return estimateFundamental(buf, kSr, f0 * 0.5, f0 * 4.0);
    };

    const double full = fundamentalAtRange(1200.0f);   // +1 octave  -> x2
    const double half = fundamentalAtRange(600.0f);    // +half oct  -> x sqrt(2) ~ 1.414
    REQUIRE(full == Catch::Approx(f0 * 2.0).epsilon(0.06));
    REQUIRE(half == Catch::Approx(f0 * std::pow(2.0, 0.5)).epsilon(0.06));
}

// ===========================================================================
// PITCH-BEND -> VCF (mod.bend_dest=VCF): the same live bend shifts the VCF cutoff (brightness
// rises: more harmonics pass a partly-closed resonant filter) WITHOUT moving the VCO
// fundamental. Asserts the dest switch routes the bend to the filter, not the oscillator.
// ===========================================================================
TEST_CASE("cc_ingress: a pitch-bend with bend_dest VCF opens the filter not the VCO pitch",
          "[cc_ingress]") {
    const double f0 = midiHz(45);   // low note: many harmonics live below/around the cutoff

    auto measure = [&](float bendUnit, double& fundOut, double& brightOut) {
        Snap snap;
        snap.setChoice(mw::params::ids::kModBendDest, 1);                  // VCF
        snap.setCont(mw::params::ids::kModBendRangeVcf, 1200.0f);          // 1 octave of cutoff at full bend
        snap.setCont(mw::params::ids::kModBendRangeVco, 0.0f);            // VCO range irrelevant (dest=VCF)
        snap.setCont(mw::params::ids::kVcfCutoff, 0.4f);                  // partly closed so opening shows
        snap.setCont(mw::params::ids::kVcfResonance, 0.3f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        snap.setCont(mw::params::ids::kLfoDepthPitch, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeldWithControllers(eng, &snap.s, 45, 0.40, bendUnit, /*wheel7=*/-1);
        fundOut   = estimateFundamental(buf, kSr, f0 * 0.5, f0 * 2.0);
        brightOut = energyAboveBand(buf, f0, kSr, 4, 16);   // upper-harmonic energy
    };

    double fundNeutral = 0.0, brightNeutral = 0.0;
    double fundUp = 0.0, brightUp = 0.0;
    measure(0.0f, fundNeutral, brightNeutral);
    measure(1.0f, fundUp, brightUp);

    // The VCO fundamental does NOT move with a VCF-routed bend (within estimator tolerance).
    REQUIRE(fundUp == Catch::Approx(fundNeutral).epsilon(0.03));
    // The bend opens the filter: the upper-harmonic (brightness) energy rises measurably.
    REQUIRE(brightNeutral > 0.0);
    REQUIRE(brightUp > brightNeutral * 1.5);
}

// ===========================================================================
// MOD-WHEEL (CC1) -> LFO DEPTH: with mod.lfo_mod_wheel routed and dest=Filter, raising the
// live mod wheel RAISES the LFO->filter wobble depth (the amplitude-envelope AC energy rises
// with the wheel). Wheel down (0) is neutral (the routed-but-no-wheel baseline). This is the
// audible mod-wheel sweep the 162 leg was inert for.
// ===========================================================================
TEST_CASE("cc_ingress: a mod-wheel move raises the LFO modulation depth per mod.lfo_mod_wheel",
          "[cc_ingress]") {
    auto wobbleAtWheel = [&](int wheel7) {
        Snap snap;
        snap.setChoice(mw::params::ids::kLfoDest, 1);                     // Filter
        snap.setCont(mw::params::ids::kLfoRate, 5.0f);
        snap.setCont(mw::params::ids::kLfoDepthCutoff, 0.25f);           // modest base depth
        snap.setCont(mw::params::ids::kLfoDelay, 0.0f);
        snap.setCont(mw::params::ids::kModLfoModWheel, 1.0f);            // full wheel->LFO routing
        snap.setCont(mw::params::ids::kVcfCutoff, 0.45f);
        snap.setCont(mw::params::ids::kVcfResonance, 0.6f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeldWithControllers(eng, &snap.s, 60, 0.8, /*bend=*/0.0f, wheel7);
        return envAcEnergy(rmsEnvelope(buf, 128));
    };

    const double wheelDown = wobbleAtWheel(0);     // wheel down: the routed-but-neutral baseline
    const double wheelUp   = wobbleAtWheel(127);   // wheel up: deeper LFO wobble
    REQUIRE(wheelDown > 0.0);                       // base LFO depth still wobbles
    REQUIRE(wheelUp > wheelDown * 1.5);             // the wheel deepens the modulation audibly
}

// ===========================================================================
// MOD-WHEEL routing OFF: with mod.lfo_mod_wheel == 0, the wheel has NO effect on the LFO
// depth (the routing knob, not the wheel alone, enables the boost). Guards that the wheel is
// inert without the routing — the wheel and the routing param BOTH gate the boost.
// ===========================================================================
TEST_CASE("cc_ingress: the mod-wheel is inert when mod.lfo_mod_wheel routing is zero",
          "[cc_ingress]") {
    auto wobbleAtWheel = [&](int wheel7) {
        Snap snap;
        snap.setChoice(mw::params::ids::kLfoDest, 1);                     // Filter
        snap.setCont(mw::params::ids::kLfoRate, 5.0f);
        snap.setCont(mw::params::ids::kLfoDepthCutoff, 0.4f);
        snap.setCont(mw::params::ids::kLfoDelay, 0.0f);
        snap.setCont(mw::params::ids::kModLfoModWheel, 0.0f);            // NO wheel->LFO routing
        snap.setCont(mw::params::ids::kVcfCutoff, 0.45f);
        snap.setCont(mw::params::ids::kVcfResonance, 0.6f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeldWithControllers(eng, &snap.s, 60, 0.5, /*bend=*/0.0f, wheel7);
        return envAcEnergy(rmsEnvelope(buf, 128));
    };

    const double wheelDown = wobbleAtWheel(0);
    const double wheelUp   = wobbleAtWheel(127);
    // No routing: the wheel does not change the wobble depth (identical AC energy).
    REQUIRE(wheelDown == Catch::Approx(wheelUp).epsilon(0.02));
}

// ===========================================================================
// NEUTRAL identity: a centered bend (0) AND a wheel-down (0) render BIT-IDENTICALLY to no
// controller input at all (no controllers field set). The neutral controllers are the exact
// no-controller identity — the seam adds nothing when the controllers are at rest.
// ===========================================================================
TEST_CASE("cc_ingress: centered bend and zero wheel are the no-controller identity",
          "[cc_ingress]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kModBendDest, 2);                     // Both (VCO + VCF)
    snap.setCont(mw::params::ids::kModBendRangeVco, 1200.0f);
    snap.setCont(mw::params::ids::kModBendRangeVcf, 1200.0f);
    snap.setCont(mw::params::ids::kModLfoModWheel, 1.0f);                // full routing
    snap.setChoice(mw::params::ids::kLfoDest, 1);
    snap.setCont(mw::params::ids::kLfoDepthCutoff, 0.5f);

    // Render with the controllers explicitly at rest (centered bend, wheel down) — via the
    // event path AND the snapshot seed.
    mw::Engine engA; engA.prepare(kSr, kMaxBlock, kMaxVoices);
    auto withRestControllers = renderHeldWithControllers(engA, &snap.s, 62, 0.20,
                                                         /*bend=*/0.0f, /*wheel7=*/0);

    // Render with NO controller input at all (default ContinuousControllers, no events).
    auto renderNoControllers = [&]() {
        constexpr int kBlk = 256;
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        {
            Block on(kBlk);
            std::vector<mw::MidiEvent> ev{ noteOn(62, 1.0f, 0) };
            auto c = on.ctx(ev, kBlk, &snap.s);
            eng.process(c);
        }
        for (int b = 1; b < 8; ++b) {
            Block w(kBlk); std::vector<mw::MidiEvent> none;
            auto c = w.ctx(none, kBlk, &snap.s); eng.process(c);
        }
        const int total = static_cast<int>(0.20 * kSr);
        std::vector<float> out; out.reserve(static_cast<std::size_t>(total) + kBlk);
        int rendered = 0;
        while (rendered < total) {
            Block w(kBlk); std::vector<mw::MidiEvent> none;
            auto c = w.ctx(none, kBlk, &snap.s); eng.process(c);
            for (int i = 0; i < kBlk && rendered < total; ++i, ++rendered)
                out.push_back(w.L[static_cast<std::size_t>(i)]);
        }
        return out;
    };
    auto noControllers = renderNoControllers();

    REQUIRE(withRestControllers.size() == noControllers.size());
    for (std::size_t i = 0; i < withRestControllers.size(); ++i)
        REQUIRE(withRestControllers[i] == noControllers[i]);
}

// ===========================================================================
// DETERMINISM: two independently-prepared engines fed identical notes + live controllers
// produce bit-identical output through the ingress+dispatch+render path.
// ===========================================================================
TEST_CASE("cc_ingress: the controller ingress and dispatch path is deterministic",
          "[cc_ingress]") {
    Snap snap; openFilter(snap);
    snap.setChoice(mw::params::ids::kModBendDest, 2);                     // Both
    snap.setCont(mw::params::ids::kModBendRangeVco, 400.0f);
    snap.setCont(mw::params::ids::kModBendRangeVcf, 700.0f);
    snap.setCont(mw::params::ids::kModLfoModWheel, 0.8f);
    snap.setChoice(mw::params::ids::kLfoDest, 1);
    snap.setCont(mw::params::ids::kLfoDepthCutoff, 0.5f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeldWithControllers(eng, &snap.s, 55, 0.15, /*bend=*/0.5f, /*wheel7=*/96);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);
}

// ===========================================================================
// RT-SAFETY: the controller ingress (event consumption into running state) + the dispatch
// + render allocate nothing and take no lock on the audio thread.
// ===========================================================================
TEST_CASE("cc_ingress: the controller ingress and dispatch is allocation and lock free under the guard",
          "[cc_ingress][rt]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kModBendDest, 2);
    snap.setCont(mw::params::ids::kModBendRangeVco, 200.0f);
    snap.setCont(mw::params::ids::kModBendRangeVcf, 300.0f);
    snap.setCont(mw::params::ids::kModLfoModWheel, 0.6f);
    snap.setChoice(mw::params::ids::kLfoDest, 1);
    snap.setCont(mw::params::ids::kLfoDepthCutoff, 0.7f);
    snap.setCont(mw::params::ids::kLfoRate, 6.0f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    mw::ContinuousControllers cc{};
    cc.pitchBend = 0.5f;
    cc.modWheel  = 0.75f;

    Block warm(kBlk);
    std::vector<mw::MidiEvent> warmEv{ noteOn(64, 0.9f, 0),
                                       pitchBend(0.5f, 0),
                                       modWheelCc(96, 0) };
    auto warmCtx = warm.ctx(warmEv, kBlk, &snap.s, cc);
    eng.process(warmCtx);
    REQUIRE(eng.voiceManager().activeCount() >= 1);

    std::vector<Block> blocks; blocks.reserve(8);
    for (int b = 0; b < 8; ++b) blocks.emplace_back(kBlk);
    // Each block carries a fresh bend + CC event so the ingress event path runs while armed.
    std::vector<std::vector<mw::MidiEvent>> evs; evs.reserve(8);
    for (int b = 0; b < 8; ++b)
        evs.push_back({ pitchBend(0.5f - 0.1f * static_cast<float>(b % 3), 0),
                        modWheelCc(64 + 8 * (b % 4), 0) });
    std::vector<mw::BlockContext> ctxs; ctxs.reserve(8);
    for (int b = 0; b < 8; ++b)
        ctxs.push_back(blocks[static_cast<std::size_t>(b)].ctx(
            evs[static_cast<std::size_t>(b)], kBlk, &snap.s, cc));

    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (int b = 0; b < 8; ++b) eng.process(ctxs[static_cast<std::size_t>(b)]);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    float peak = 0.0f;
    for (int b = 0; b < 8; ++b)
        for (int i = 0; i < kBlk; ++i)
            peak = std::max(peak,
                            std::fabs(blocks[static_cast<std::size_t>(b)].L[static_cast<std::size_t>(i)]));
    REQUIRE(peak > 0.0f);
    REQUIRE(peak < kSaneBound);
}
