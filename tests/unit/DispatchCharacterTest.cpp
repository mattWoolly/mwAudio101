// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchCharacterTest.cpp — the AUDIO-EFFECT / model-observable acceptance
// suite for the analog-character + tuning + expression leg of the control-dispatch seam
// (task 164; ADR-028). This EXTENDS the keystone (160) seam that 161/162/163 also extend:
// the same once-per-control-tick applyParamSnapshot path now also reads the analog
// character group (drift / vintage / variance / tune.a4 / tune.slop / warmup), the
// expression scaler (amp.expression), velocity (vel.*), and the MPE routing (mpe.*).
//
// Test-case display names ALL begin with "dispatch_character" so
// `ctest -R dispatch_character --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[dispatch_character]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (real audible / model-observable effect — the assertion class the
// ADR-028 audit found MISSING — measured on RENDERED OUTPUT via a Goertzel fundamental
// estimator + amplitude probes, or via the drift/pitch model):
//   * tune.a4 = 442 is the hardware reference (== 440 default to a small, exact shift),
//     a4 = 460 renders SHARP and a4 = 400 renders FLAT vs the 442 home (pitch ratio);
//   * vintage.enable ON with drift.depth + drift.rate PERTURBS the pitch over time, and
//     disabled is perfectly STABLE (and bit-identical to the no-character default);
//   * vintage.age (the host macro folded into the drift group) similarly perturbs pitch;
//   * var.cutoff / var.pw add per-voice spread that is bit-stable for a fixed seed but
//     differs from the un-perturbed render (model-observable on the rendered spectrum);
//   * amp.expression scales the VCA output amplitude (0 => silent, 0.5 => ~half, 1 => unity);
//   * velocity routing (vel.enable/depth) is wired correct-but-inert at the seam — the
//     per-note velocity INGRESS is hardcoded 1.0 upstream (task 162b), so a velocity sweep
//     is currently inert; this suite asserts the INERT-but-correct contract + flags it;
//   * MPE routing (mpe.*) decodes without perturbing the default render — the live per-note
//     MPE position/pressure ingress is a separate controller seam (flagged), so the decode
//     is correct-but-inert today;
//   * determinism across two identical runs (incl. with vintage ON);
//   * the dispatch+render path stays RT-safe (AudioThreadGuard-clean) — the [rt] check.
//
// The Engine consumes the seam's immutable mw::ParamSnapshot once per control tick; this
// file builds that POD directly (the off-thread bridge's job in the real shell) and reads
// the audible result, so it links mwcore ONLY (no JUCE) [docs/design/00 §5.4; ADR-001].

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
#include "calibration/VcoConstants.h"   // kPitchRefHz (the 442 Hz home)

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- registry-index lookup (mirrors the 160/161/162 dispatch test helper). --------
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

// Build a ParamSnapshot pre-loaded with every live param's DEFAULT in normalized [0,1]
// form (exactly what the bridge emits). Setters override by engineering value / index.
struct Snap {
    mw::ParamSnapshot s{};

    Snap() noexcept {
        for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
            const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
            if (d.type == mw::params::ParamType::Continuous) {
                const float span = d.maxValue - d.minValue;
                const float norm  = span > 0.0f ? (d.defaultValue - d.minValue) / span : 0.0f;
                s.normalizedValues[static_cast<std::size_t>(i)] = norm;
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

    // Set a CONTINUOUS param by its engineering value. For a SKEWED registry slot the
    // bridge stores convertTo0to1(value) = ((value-min)/span)^skew; the dispatch inverts
    // it with norm^(1/skew). We mirror that here so a skewed param (e.g. drift.rate) is
    // round-tripped exactly: store ((v-min)/span)^skew.
    void setCont(const char* id, float value) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        const float span = d.maxValue - d.minValue;
        const float v = std::clamp(value, d.minValue, d.maxValue);
        float lin = span > 0.0f ? (v - d.minValue) / span : 0.0f;
        const float norm = (d.skew == 1.0f) ? lin : std::pow(lin, d.skew);
        s.normalizedValues[static_cast<std::size_t>(i)] = norm;
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
    e.value = vel;
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
                         const mw::ParamSnapshot* p, bool playing = false) noexcept {
        mw::BlockContext c{};
        c.audio.channels = ch; c.audio.numChannels = 2; c.audio.numFrames = n;
        c.params = p;
        c.transport = mw::TransportInfo{ 120.0, 0.0, playing, kSr };
        c.midi.events = ev.empty() ? nullptr : ev.data();
        c.midi.numEvents = static_cast<int>(ev.size());
        return c;
    }
};

// Render `seconds` of sustained audio for a held note (default velocity 1.0), returning
// the mono (L) buffer. Drives the snapshot every block so the dispatch applies.
std::vector<float> renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap,
                              int midiNote, double seconds,
                              float vel = 1.0f, int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, vel, 0) };
        auto c = on.ctx(ev, kBlk, snap);
        eng.process(c);
    }
    for (int b = 1; b < warmupBlocks; ++b) {
        Block w(kBlk);
        std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap);
        eng.process(c);
    }
    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total) + kBlk);
    int rendered = 0;
    while (rendered < total) {
        Block w(kBlk);
        std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap);
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
    const int steps = 800;
    for (int i = 0; i <= steps; ++i) {
        const double f = fLo * std::pow(fHi / fLo, static_cast<double>(i) / steps);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    const double lo = bestF * 0.985, hi = bestF * 1.015;
    for (int i = 0; i <= 400; ++i) {
        const double f = lo + (hi - lo) * (static_cast<double>(i) / 400.0);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    return bestF;
}

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}
double peakAbs(const std::vector<float>& x) noexcept {
    double p = 0.0; for (float v : x) p = std::max(p, std::fabs((double) v)); return p;
}

// note 69 (A4) renders at the tune.a4 reference (hardware home 442). midiHz442 maps a
// note to its rendered Hz under the hardware-reference tuning, so the a4-shift tests can
// predicate the rendered fundamental against the configured A4 reference.
double midiHz442(int n) noexcept {
    return mw::cal::vco::kPitchRefHz * std::pow(2.0, (n - 69) / 12.0);
}

constexpr float kSaneBound = 64.0f;

} // namespace

// ===========================================================================
// tune.a4: the A4 reference shifts the GLOBAL pitch. a4 == 442 is the exact hardware home;
// a4 == 460 renders SHARP and a4 == 400 renders FLAT, by the log2(a4/442) ratio. Measured
// on the rendered fundamental of note 69 (A4). [docs/design/01 §4.3; ADR-028]
// ===========================================================================
TEST_CASE("dispatch_character: tune a4 reference shifts the global pitch",
          "[dispatch_character]") {
    auto fundForA4 = [](float a4Hz) {
        Snap snap;
        snap.setCont(mw::params::ids::kTuneA4, a4Hz);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 69, 0.35);   // A4
        return estimateFundamental(buf, kSr, 360.0, 520.0);
    };

    const double at442 = fundForA4(442.0f);   // hardware home
    const double at440 = fundForA4(440.0f);   // schema default
    const double at460 = fundForA4(460.0f);   // sharp
    const double at400 = fundForA4(400.0f);   // flat

    // a4 = 442 lands on the hardware reference (the VCO home) for note 69.
    REQUIRE(at442 == Catch::Approx(442.0).epsilon(0.01));
    // a4 = 460 is sharper than 442 by exactly the 460/442 ratio; 400 is flatter by 400/442.
    REQUIRE((at460 / at442) == Catch::Approx(460.0 / 442.0).epsilon(0.01));
    REQUIRE((at400 / at442) == Catch::Approx(400.0 / 442.0).epsilon(0.01));
    // The 440 default is just below 442 (a small, real, downward shift — not identical).
    REQUIRE(at440 < at442);
    REQUIRE((at440 / at442) == Catch::Approx(440.0 / 442.0).epsilon(0.01));
}

// ===========================================================================
// vintage/drift ENABLED perturbs the pitch over time; DISABLED is perfectly stable AND
// bit-identical to the no-character default render. Measured: split the held render into
// an early and a late window and compare the per-window fundamental — with drift ON the
// pitch wanders between windows; with drift OFF the two windows are identical.
// ===========================================================================
TEST_CASE("dispatch_character: vintage drift enabled perturbs pitch over time disabled is stable",
          "[dispatch_character]") {
    const int note = 57;   // A3 (a clean low-ish tone for a stable fundamental estimate)
    const double fHome = midiHz442(note);

    auto windowedFunds = [&](bool vintageOn, float depthCents, float rate01) {
        Snap snap;
        snap.setChoice(mw::params::ids::kVintageEnable, vintageOn ? 1 : 0);
        snap.setCont(mw::params::ids::kDriftDepth, depthCents);
        snap.setCont(mw::params::ids::kDriftRate, rate01);
        // A wide drift smoother + a long render so the OU walk visibly moves the pitch.
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, note, 1.6, /*vel=*/1.0f, /*warmup=*/4);
        const std::size_t half = buf.size() / 2;
        std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(half));
        std::vector<float> late (buf.begin() + static_cast<long>(half), buf.end());
        const double fe = estimateFundamental(early, kSr, fHome * 0.85, fHome * 1.15);
        const double fl = estimateFundamental(late,  kSr, fHome * 0.85, fHome * 1.15);
        return std::pair<double,double>{ fe, fl };
    };

    // (a) DISABLED: rock-stable — the two windows are the SAME pitch (drift off).
    {
        auto [fe, fl] = windowedFunds(/*on=*/false, 50.0f, 1.0f);
        REQUIRE(fe == Catch::Approx(fHome).epsilon(0.01));
        REQUIRE(fl == Catch::Approx(fe).epsilon(0.002));   // no wander when disabled
    }

    // (b) ENABLED with the MAX drift depth + a fast rate: the late window's pitch has
    //     drifted away from the early window (an audible, model-observable wander).
    {
        auto [fe, fl] = windowedFunds(/*on=*/true, 50.0f, 1.0f);
        const double relShift = std::fabs(fl - fe) / fHome;
        REQUIRE(relShift > 0.0015);   // the pitch demonstrably moved between windows
    }
}

// ===========================================================================
// DISABLED character is BIT-IDENTICAL to the no-character baseline: turning vintage.enable
// off (the default) with arbitrary drift/var/age settings must not change a single sample
// vs a snapshot that leaves the whole character group at its default. This is the
// regression guard that the analog-character leg is inert when its master switch is off.
// ===========================================================================
TEST_CASE("dispatch_character: disabled character is bit identical to the baseline render",
          "[dispatch_character]") {
    auto render = [](bool loadCharacter) {
        Snap snap;
        if (loadCharacter) {
            // vintage.enable stays OFF (default), but load every other character knob hot.
            snap.setCont(mw::params::ids::kDriftDepth, 50.0f);
            snap.setCont(mw::params::ids::kDriftRate, 1.0f);
            snap.setCont(mw::params::ids::kVintageAge, 1.0f);
            snap.setCont(mw::params::ids::kVintageCalSpread, 1.0f);
            snap.setCont(mw::params::ids::kVintageDetuneAmt, 1.0f);
            snap.setCont(mw::params::ids::kTuneSlop, 20.0f);
            snap.setCont(mw::params::ids::kVarCutoff, 1.0f);
            snap.setCont(mw::params::ids::kVarEnvTime, 1.0f);
            snap.setCont(mw::params::ids::kVarPw, 1.0f);
            snap.setCont(mw::params::ids::kVarGlide, 1.0f);
        }
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 60, 0.20);
    };
    const auto baseline  = render(false);
    const auto character = render(true);
    REQUIRE(baseline.size() == character.size());
    for (std::size_t i = 0; i < baseline.size(); ++i)
        REQUIRE(baseline[i] == character[i]);   // vintage off => character group is inert
}

// ===========================================================================
// vintage.age (the host Age macro folded into the drift group) perturbs the pitch the same
// way a manual drift.depth/rate does. With vintage ENABLED, a high Age must move the pitch
// between an early and a late window (the macro opens drift depth + rate together).
// ===========================================================================
TEST_CASE("dispatch_character: vintage age macro perturbs pitch when enabled",
          "[dispatch_character]") {
    const int note = 57;
    const double fHome = midiHz442(note);

    Snap snap;
    snap.setChoice(mw::params::ids::kVintageEnable, 1);
    snap.setCont(mw::params::ids::kVintageAge, 1.0f);     // full Age
    // Leave drift.depth/rate at their schema defaults; the Age macro should open them.
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto buf = renderHeld(eng, &snap.s, note, 1.6, 1.0f, 4);
    const std::size_t half = buf.size() / 2;
    std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(half));
    std::vector<float> late (buf.begin() + static_cast<long>(half), buf.end());
    const double fe = estimateFundamental(early, kSr, fHome * 0.85, fHome * 1.15);
    const double fl = estimateFundamental(late,  kSr, fHome * 0.85, fHome * 1.15);
    REQUIRE(std::fabs(fl - fe) / fHome > 0.0015);   // Age opened the drift -> pitch wandered
}

// ===========================================================================
// var.* per-voice variance adds spread frozen at note-on: a fixed-seed render with
// var.cutoff / var.pw up is bit-stable run-to-run BUT differs from the un-perturbed
// render (the variance shifts the cutoff/PW personality of the note). Model-observable on
// the rendered spectrum.
// ===========================================================================
TEST_CASE("dispatch_character: var spread changes the note vs the unperturbed render",
          "[dispatch_character]") {
    auto renderVar = [](bool varOn) {
        Snap snap;
        snap.setChoice(mw::params::ids::kVintageEnable, varOn ? 1 : 0);
        // A pulse-rich mix so a PW/cutoff variance is clearly visible in the spectrum.
        snap.setCont(mw::params::ids::kSawLevel, 0.5f);
        snap.setCont(mw::params::ids::kPulseLevel, 0.6f);
        snap.setCont(mw::params::ids::kVcfCutoff, 0.6f);
        snap.setCont(mw::params::ids::kVcfResonance, 0.4f);
        if (varOn) {
            snap.setCont(mw::params::ids::kVarCutoff, 1.0f);
            snap.setCont(mw::params::ids::kVarPw, 1.0f);
            snap.setCont(mw::params::ids::kVintageCalSpread, 1.0f);
        }
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 60, 0.30);
    };

    const auto off = renderVar(false);
    const auto on  = renderVar(true);

    // Bit-stable for a fixed seed: two on-renders are identical (determinism with variance).
    const auto on2 = renderVar(true);
    REQUIRE(on.size() == on2.size());
    for (std::size_t i = 0; i < on.size(); ++i) REQUIRE(on[i] == on2[i]);

    // But the variance personality differs from the un-perturbed note (not bit-identical):
    // compare total energy + a spectral probe; require a measurable difference somewhere.
    bool differs = (on.size() != off.size());
    if (!differs) {
        for (std::size_t i = 0; i < on.size(); ++i)
            if (on[i] != off[i]) { differs = true; break; }
    }
    REQUIRE(differs);
    // And both still SOUND (the variance is a perturbation, not a mute).
    REQUIRE(rms(on)  > 1.0e-4);
    REQUIRE(rms(off) > 1.0e-4);
}

// ===========================================================================
// amp.expression (CC11) scales the VCA OUTPUT amplitude. The param reaches the seam
// directly (schema default 1.0 == unity), so this is an immediately-audible scaler: 1.0 is
// unity, ~0.5 halves the output amplitude, 0.0 silences it. Measured on the rendered RMS.
// ===========================================================================
TEST_CASE("dispatch_character: amp expression scales the vca output amplitude",
          "[dispatch_character]") {
    auto rmsForExpr = [](float expr) {
        Snap snap;
        snap.setCont(mw::params::ids::kAmpExpression, expr);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.25);
        return rms(buf);
    };

    const double full = rmsForExpr(1.0f);   // unity
    const double half = rmsForExpr(0.5f);   // ~half amplitude
    const double zero = rmsForExpr(0.0f);   // silent

    REQUIRE(full > 1.0e-4);                          // the voice sounds at unity
    REQUIRE((half / full) == Catch::Approx(0.5).epsilon(0.06));   // expression scales linearly
    REQUIRE(zero < full * 1.0e-3);                   // expression 0 => effectively silent
}

// ===========================================================================
// VELOCITY routing (vel.enable / vel.depth) is WIRED (task 162's leg) but CORRECT-BUT-INERT
// for a per-note velocity SWEEP today: the per-note velocity INGRESS is hardcoded 1.0
// UPSTREAM — VoiceManager::controlTick fires v.noteOn(note, /*velocity=*/1.0f, ...) from the
// KeyAssigner decision, which carries the resolved note but NOT the key velocity (the
// KeyAssigner is a held-key priority model with no velocity field). So EVERY voice the
// engine triggers records velocity == 1.0 regardless of the MIDI note-on value; wiring a
// genuine per-note velocity ingress through to the voice is task 162b.
//
// This asserts the correct-but-inert contract WITHOUT faking an audible velocity sweep:
//   (1) the recorded per-voice velocity IS pinned at 1.0 even for a soft (0.2) MIDI note-on
//       — the SEAM GAP, surfaced via the Engine's voice accessor (proves it, not assumes it);
//   (2) at that pinned full velocity the velocity->VCA AMPLITUDE leg is the identity
//       ((1-depth)+depth*1 == 1), so vel.enable / vel.depth do not scale the output AMPLITUDE
//       — sensing-off RMS == sensing-on full-depth RMS;
//   (3) the velocity legs stay deterministic.
// (The velocity->cutoff leg, by contrast, is a CONSTANT offset at the pinned full velocity —
// it is not a per-note sweep, so we do not assert a tone change here; that awaits 162b.)
// ===========================================================================
TEST_CASE("dispatch_character: velocity routing is wired correct but inert without per note ingress",
          "[dispatch_character]") {
    // (1) The per-note velocity ingress is pinned at 1.0 upstream — even a soft MIDI note-on
    //     (0.2) records velocity 1.0 on the triggered voice. This proves the 162b gap.
    {
        Snap snap;
        snap.setChoice(mw::params::ids::kVelEnable, 1);
        snap.setCont(mw::params::ids::kVelDepth, 1.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        Block on(256);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 0.2f, 0) };   // a SOFT key
        auto c = on.ctx(ev, 256, &snap.s);
        eng.process(c);
        REQUIRE(eng.voiceManager().activeCount() >= 1);
        const int vi = static_cast<int>(eng.voiceManager().activeIndex(0));
        // Despite the 0.2 note-on, the recorded velocity is the pinned 1.0 (the seam gap).
        REQUIRE(eng.voiceManager().voice(vi).currentVelocity() == Catch::Approx(1.0f));
    }

    // (2) At the pinned full velocity the velocity->VCA AMPLITUDE scale is the identity, so
    //     sensing on at full depth does not change the output AMPLITUDE vs sensing off. We
    //     mute the velocity->cutoff influence by holding the cutoff fully open (its default),
    //     where a further cutoff offset cannot raise the already-passed energy materially, and
    //     compare the broadband RMS. (Use a saw-only mix at the open default cutoff.)
    auto rmsForVel = [](bool enable, float depth) {
        Snap snap;
        snap.setChoice(mw::params::ids::kVelEnable, enable ? 1 : 0);
        snap.setCont(mw::params::ids::kVelDepth, depth);
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);   // fully open: cutoff leg saturated
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.25, /*vel=*/1.0f);
        return rms(buf);
    };
    const double sensingOff  = rmsForVel(false, 0.0f);
    const double sensingFull = rmsForVel(true,  1.0f);
    REQUIRE(sensingOff > 1.0e-4);
    // The velocity->VCA AMPLITUDE leg is inert at the pinned full velocity (identity scale).
    REQUIRE((sensingFull / sensingOff) == Catch::Approx(1.0).epsilon(0.02));

    // (3) Determinism through the velocity legs.
    const double a = rmsForVel(true, 0.7f);
    const double b = rmsForVel(true, 0.7f);
    REQUIRE(a == b);
}

// ===========================================================================
// MPE routing (mpe.enable / mpe.bend_range / mpe.pressure_dest) DECODES at the seam but is
// CORRECT-BUT-INERT today: the live per-note MPE pitch-bend / pressure POSITION ingress is
// a separate controller seam (BlockContext carries no continuous per-note MPE state — see
// Engine.cpp / the 162 bend leg, which is also inert for the same reason). So enabling MPE
// + setting a bend range / pressure dest does NOT alter the default render. This asserts
// the inert-but-correct contract and flags the ingress gap (it does NOT fake an MPE effect).
// ===========================================================================
TEST_CASE("dispatch_character: mpe routing decodes correct but inert without live ingress",
          "[dispatch_character]") {
    auto render = [](bool mpeOn) {
        Snap snap;
        if (mpeOn) {
            snap.setChoice(mw::params::ids::kMpeEnable, 1);
            snap.setCont(mw::params::ids::kMpeBendRange, 96.0f);   // max bend range
            snap.setChoice(mw::params::ids::kMpePressureDest, 1);  // VCA Level
        }
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 60, 0.20);
    };
    const auto off = render(false);
    const auto on  = render(true);
    REQUIRE(off.size() == on.size());
    // With no live MPE per-note position/pressure reaching the core seam, the MPE decode is
    // inert — the render is bit-identical. (The live ingress is a separate seam; flagged.)
    for (std::size_t i = 0; i < off.size(); ++i)
        REQUIRE(off[i] == on[i]);
}

// ===========================================================================
// DETERMINISM through the analog-character leg: two independently-prepared engines fed the
// identical note + character snapshot (vintage ON, drift + variance live) produce
// bit-identical output — the drift PRNG is seeded deterministically off the instance seed.
// ===========================================================================
TEST_CASE("dispatch_character: the character dispatch path is deterministic",
          "[dispatch_character]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kVintageEnable, 1);
    snap.setCont(mw::params::ids::kDriftDepth, 30.0f);
    snap.setCont(mw::params::ids::kDriftRate, 0.8f);
    snap.setCont(mw::params::ids::kVarCutoff, 0.7f);
    snap.setCont(mw::params::ids::kVarPw, 0.5f);
    snap.setCont(mw::params::ids::kTuneSlop, 10.0f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 55, 0.20);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);
}

// ===========================================================================
// RT-SAFETY: the analog-character dispatch (drift processBlock + the per-tick character
// decode + render) allocates nothing and takes no lock on the audio thread, with the full
// character group live (vintage ON, drift + variance + a4 shift + expression).
// ===========================================================================
TEST_CASE("dispatch_character: dispatch and render are allocation and lock free under the guard",
          "[dispatch_character][rt]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kVintageEnable, 1);
    snap.setCont(mw::params::ids::kDriftDepth, 40.0f);
    snap.setCont(mw::params::ids::kDriftRate, 0.9f);
    snap.setCont(mw::params::ids::kVintageAge, 0.6f);
    snap.setCont(mw::params::ids::kVintageCalSpread, 0.8f);
    snap.setCont(mw::params::ids::kVarCutoff, 0.6f);
    snap.setCont(mw::params::ids::kVarPw, 0.5f);
    snap.setCont(mw::params::ids::kVarEnvTime, 0.5f);
    snap.setCont(mw::params::ids::kVarGlide, 0.5f);
    snap.setCont(mw::params::ids::kTuneA4, 444.0f);
    snap.setCont(mw::params::ids::kTuneSlop, 8.0f);
    snap.setCont(mw::params::ids::kAmpExpression, 0.8f);
    snap.setChoice(mw::params::ids::kMpeEnable, 1);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    Block warm(kBlk);
    std::vector<mw::MidiEvent> warmEv{ noteOn(64, 0.9f, 0) };
    auto warmCtx = warm.ctx(warmEv, kBlk, &snap.s);
    eng.process(warmCtx);   // first-touch realized while allocation is still permitted
    REQUIRE(eng.voiceManager().activeCount() >= 1);

    std::vector<Block> blocks; blocks.reserve(8);
    for (int b = 0; b < 8; ++b) blocks.emplace_back(kBlk);
    const std::vector<mw::MidiEvent> none;
    std::vector<mw::BlockContext> ctxs; ctxs.reserve(8);
    for (int b = 0; b < 8; ++b)
        ctxs.push_back(blocks[static_cast<std::size_t>(b)].ctx(none, kBlk, &snap.s));

    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (int b = 0; b < 8; ++b) eng.process(ctxs[static_cast<std::size_t>(b)]);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    const double peak = [&]{
        double p = 0.0;
        for (int b = 0; b < 8; ++b)
            for (int i = 0; i < kBlk; ++i)
                p = std::max(p, std::fabs((double) blocks[static_cast<std::size_t>(b)].L[static_cast<std::size_t>(i)]));
        return p;
    }();
    REQUIRE(peak > 0.0);
    REQUIRE(peak < kSaneBound);
}
