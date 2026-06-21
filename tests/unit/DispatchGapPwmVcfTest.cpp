// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchGapPwmVcfTest.cpp — the AUDIO-EFFECT acceptance suite for the TWO
// audit-found unwired params wired into the ADR-028 control-dispatch seam (task 162e):
//   * mw101.vco.pwm_depth — the MANUAL pulse-width-modulation depth (static, LFO-INDEPENDENT;
//                           distinct from the LFO->PWM amount mw101.lfo.depth_pwm).
//   * mw101.vcf.lfo_mod   — the VCF module's OWN LFO->cutoff amount (distinct from the LFO
//                           panel's mw101.lfo.depth_cutoff; both SUM into the cutoff CV).
//
// The 165 completeness audit (DispatchCompleteTest.cpp) flagged both as FindingUnwired and
// asserted them INERT (bit-identical low-vs-high). This task wires them; this suite proves the
// REAL rendered effect (knob -> observable spectrum/wobble), and the 165 manifest entries flip
// from FindingUnwired -> asserted-audio in the same PR.
//
// Test-case display names ALL begin with "dispatch_gap" so `ctest -R dispatch_gap
// --no-tests=error` selects exactly this suite under the silent-pass rule (AGENTS.md "Tests").
// The tag is "[dispatch_gap]". No literal '[' appears in any display name so Catch2 never
// mis-parses a tag out of the name. Links mwcore ONLY (no JUCE) [docs/design/00 §5.4; ADR-001].

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

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

namespace P = mw::params::ids;

// --- registry-index lookup (the SAME registry-index keying the ParamSnapshot uses) --------
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

// ParamSnapshot pre-loaded with every live param's DEFAULT in the normalized [0,1] form the
// bridge emits (convertTo0to1(default)); setCont/setChoice mirror the 160-164 dispatch helpers.
struct Snap {
    mw::ParamSnapshot s{};

    static float normFor(const mw::params::ParamDef& d, float value) noexcept {
        const float span = d.maxValue - d.minValue;
        if (span <= 0.0f) return 0.0f;
        const float lin = std::clamp((value - d.minValue) / span, 0.0f, 1.0f);
        return (d.skew == 1.0f) ? lin : std::pow(lin, d.skew);
    }

    Snap() noexcept {
        for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
            const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
            if (d.type == mw::params::ParamType::Continuous) {
                s.normalizedValues[static_cast<std::size_t>(i)] = normFor(d, d.defaultValue);
            } else {
                const int idx = static_cast<int>(d.defaultValue);
                s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
                const float denom =
                    (d.choiceCount > 1) ? static_cast<float>(d.choiceCount - 1) : 1.0f;
                s.normalizedValues[static_cast<std::size_t>(i)] =
                    static_cast<float>(idx) / denom;
            }
        }
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
                         const mw::ParamSnapshot* p) noexcept {
        mw::BlockContext bc{};
        bc.audio.channels = ch; bc.audio.numChannels = 2; bc.audio.numFrames = n;
        bc.params = p;
        bc.transport = mw::TransportInfo{ 120.0, 0.0, false, kSr };
        bc.midi.events = ev.empty() ? nullptr : ev.data();
        bc.midi.numEvents = static_cast<int>(ev.size());
        bc.controllers = mw::ContinuousControllers{};
        return bc;
    }
};

// Render `seconds` of a held note (mono L), driving the snapshot every block so the per-tick
// dispatch applies (the 160-164 dispatch-test render pattern).
std::vector<float> renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap, int midiNote,
                              double seconds, int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, 1.0f, 0) };
        auto c = on.ctx(ev, kBlk, snap);
        eng.process(c);
    }
    for (int b = 1; b < warmupBlocks; ++b) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap); eng.process(c);
    }
    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total) + kBlk);
    int rendered = 0;
    while (rendered < total) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap); eng.process(c);
        for (int i = 0; i < kBlk && rendered < total; ++i, ++rendered)
            out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    return out;
}

std::vector<float> freshHeld(const mw::ParamSnapshot* snap, int midiNote, double seconds,
                             int warmupBlocks = 8) {
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    return renderHeld(eng, snap, midiNote, seconds, warmupBlocks);
}

// --- measurement primitives (Goertzel + RMS-envelope wobble, the 165/162 patterns) --------
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
double midiHz442(int n) noexcept {
    // The CEM3340 VCO homes on 442 Hz at the 8' reference (the rendered fundamental frame).
    return 442.0 * std::pow(2.0, (n - 69) / 12.0);
}
double secondOverFirst(const std::vector<float>& x, double f0, double sr) noexcept {
    const double h1 = goertzelPower(x, f0, sr);
    const double h2 = goertzelPower(x, 2.0 * f0, sr);
    return h2 / std::max(1e-12, h1);
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
bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}

// A pulse-only, env-defeated, open-filter base: a flat steady pulse whose DUTY is governed by
// the param under test, so the 2nd-harmonic (even) content tracks the manual PWM directly.
void flatPulse(Snap& sn) noexcept {
    sn.setCont(P::kSawLevel, 0.0f);
    sn.setCont(P::kPulseLevel, 1.0f);
    sn.setCont(P::kSubLevel, 0.0f);
    sn.setCont(P::kNoiseLevel, 0.0f);
    sn.setCont(P::kVcoPw, 0.0f);          // base square (weak 2nd harmonic) before manual depth
    sn.setCont(P::kEnvAttack, 0.0f);
    sn.setCont(P::kEnvDecay, 0.0f);
    sn.setCont(P::kEnvSustain, 1.0f);
    sn.setCont(P::kVcfCutoff, 1.0f);       // fully open: the duty spectrum reaches the output
    sn.setCont(P::kVcfEnvMod, 0.0f);
    sn.setCont(P::kVcfKbdTrack, 0.0f);
    sn.setCont(P::kVcfResonance, 0.0f);
    sn.setCont(P::kVcaLevel, 0.8f);
}

} // namespace

// =====================================================================================
// (1) mw101.vco.pwm_depth — MANUAL PWM. With the LFO OFF (no lfo.depth_pwm, dest not PWM), a
// pulse-only square (base vco.pw == 0, ~zero 2nd harmonic) has its duty narrowed by the manual
// depth alone: the 2nd (even) harmonic jumps. This proves the STATIC manual path is wired and
// is DISTINCT from the LFO->PWM amount (which is held at zero here). [docs/design/01 §4.6;
// docs/design/06 §3.0]
// =====================================================================================
TEST_CASE("dispatch_gap: vco pwm_depth narrows the pulse duty with the LFO off (manual PWM)",
          "[dispatch_gap]") {
    const int    note = 60;
    const double f0   = midiHz442(note);

    auto secondHarmonicRatio = [&](float depth) {
        Snap sn; flatPulse(sn);
        // LFO fully OFF for the manual path: no LFO->PWM routing of any kind.
        sn.setChoice(P::kLfoDest, 0);             // Pitch (NOT PWM)
        sn.setCont(P::kLfoDepthPwm, 0.0f);        // the LFO->PWM amount stays zero
        sn.setCont(P::kLfoDepthPitch, 0.0f);
        sn.setCont(P::kLfoDepthCutoff, 0.0f);
        sn.setCont(P::kVcoPwmDepth, depth);       // the MANUAL depth under test
        return secondOverFirst(freshHeld(&sn.s, note, 0.30), f0, kSr);
    };

    // depth 0 => the base square (50% duty, weak even harmonics); depth 1 => the manual depth
    // narrows the duty so the 2nd harmonic is far stronger — the same dimension vco.pw moves,
    // here driven by the MANUAL depth with the LFO disengaged.
    const double square = secondHarmonicRatio(0.0f);
    const double narrow = secondHarmonicRatio(1.0f);
    REQUIRE(narrow > square * 3.0);
}

// The manual depth is DISTINCT from the LFO->PWM path: with the LFO still OFF, lfo.depth_pwm
// moved low-vs-high does NOTHING (no LFO routed to PWM), while vco.pwm_depth low-vs-high
// changes the duty. This is the discriminator the 165 finding asked for.
TEST_CASE("dispatch_gap: vco pwm_depth is the manual path distinct from lfo depth_pwm",
          "[dispatch_gap]") {
    const int    note = 60;
    const double f0   = midiHz442(note);

    auto secondHarmonic = [&](float manualDepth, float lfoPwmDepth) {
        Snap sn; flatPulse(sn);
        sn.setChoice(P::kLfoDest, 0);             // LFO NOT routed to PWM
        sn.setCont(P::kLfoDepthPwm, lfoPwmDepth);
        sn.setCont(P::kVcoPwmDepth, manualDepth);
        return secondOverFirst(freshHeld(&sn.s, note, 0.30), f0, kSr);
    };

    // With the LFO dest != PWM, moving lfo.depth_pwm has no effect (LFO not routed to PWM).
    const double lfoOnly0 = secondHarmonic(0.0f, 0.0f);
    const double lfoOnly1 = secondHarmonic(0.0f, 1.0f);
    REQUIRE(lfoOnly1 == Catch::Approx(lfoOnly0).epsilon(0.05));   // lfo.depth_pwm inert here

    // The MANUAL depth, in contrast, changes the duty on its own.
    const double manual1 = secondHarmonic(1.0f, 0.0f);
    REQUIRE(manual1 > lfoOnly0 * 3.0);
}

// =====================================================================================
// (2) mw101.vcf.lfo_mod — the VCF's OWN LFO->cutoff amount. With the LFO panel's depth_cutoff
// at ZERO and the dest switch NOT on Filter, the VCF-panel lfo_mod alone routes the per-voice
// LFO to the cutoff: the output AMPLITUDE envelope wobbles (AC energy rises sharply). This
// proves the VCF-panel path is wired and DISTINCT from lfo.depth_cutoff. [docs/design/02 §1.2;
// docs/design/05 §3.1; docs/design/06 §3.0]
// =====================================================================================
TEST_CASE("dispatch_gap: vcf lfo_mod makes the LFO modulate the filter cutoff",
          "[dispatch_gap]") {
    auto envWobble = [&](float vcfLfoMod) {
        Snap sn;
        sn.setCont(P::kSawLevel, 0.9f);
        sn.setCont(P::kPulseLevel, 0.0f);
        sn.setCont(P::kSubLevel, 0.0f);
        sn.setCont(P::kNoiseLevel, 0.0f);
        sn.setCont(P::kEnvAttack, 0.0f);
        sn.setCont(P::kEnvDecay, 0.0f);
        sn.setCont(P::kEnvSustain, 1.0f);
        sn.setCont(P::kVcfCutoff, 0.45f);          // partly closed so the cutoff wobble bites
        sn.setCont(P::kVcfResonance, 0.6f);
        sn.setCont(P::kVcfEnvMod, 0.0f);
        sn.setCont(P::kVcfKbdTrack, 0.0f);
        // The LFO PANEL is NOT routed to cutoff: dest != Filter AND lfo.depth_cutoff == 0.
        sn.setChoice(P::kLfoDest, 0);              // Pitch (NOT Filter)
        sn.setCont(P::kLfoDepthCutoff, 0.0f);      // the lfo.depth_cutoff term stays zero
        sn.setCont(P::kLfoRate, 5.0f);
        sn.setCont(P::kLfoDelay, 0.0f);
        sn.setCont(P::kVcfLfoMod, vcfLfoMod);      // the VCF-panel LFO->cutoff amount under test
        auto buf = freshHeld(&sn.s, 60, 0.8);
        return envAcEnergy(rmsEnvelope(buf, 128));
    };

    const double flat   = envWobble(0.0f);   // no VCF LFO mod: a steady envelope
    const double wobbly = envWobble(1.0f);   // full VCF LFO mod: a wobbling envelope
    REQUIRE(wobbly > flat * 5.0);            // the wobble is unmistakable in the envelope
}

// The VCF-panel lfo_mod is DISTINCT from lfo.depth_cutoff and SUMS WITH it: it wobbles the
// cutoff even when the LFO dest switch is on a DIFFERENT destination (Pitch), where
// lfo.depth_cutoff would contribute nothing. (lfo.depth_cutoff only fires at dest==Filter; the
// VCF panel routes the LFO to cutoff regardless of the panel switch.)
TEST_CASE("dispatch_gap: vcf lfo_mod routes the LFO to cutoff independent of the lfo dest switch",
          "[dispatch_gap]") {
    auto wobbleAt = [&](int lfoDest, float lfoDepthCutoff, float vcfLfoMod) {
        Snap sn;
        sn.setCont(P::kSawLevel, 0.9f);
        sn.setCont(P::kPulseLevel, 0.0f);
        sn.setCont(P::kSubLevel, 0.0f);
        sn.setCont(P::kNoiseLevel, 0.0f);
        sn.setCont(P::kEnvAttack, 0.0f);
        sn.setCont(P::kEnvDecay, 0.0f);
        sn.setCont(P::kEnvSustain, 1.0f);
        sn.setCont(P::kVcfCutoff, 0.45f);
        sn.setCont(P::kVcfResonance, 0.6f);
        sn.setCont(P::kVcfEnvMod, 0.0f);
        sn.setCont(P::kVcfKbdTrack, 0.0f);
        sn.setChoice(P::kLfoDest, lfoDest);
        sn.setCont(P::kLfoDepthCutoff, lfoDepthCutoff);
        sn.setCont(P::kLfoRate, 5.0f);
        sn.setCont(P::kLfoDelay, 0.0f);
        sn.setCont(P::kVcfLfoMod, vcfLfoMod);
        return envAcEnergy(rmsEnvelope(freshHeld(&sn.s, 60, 0.8), 128));
    };

    // dest = Pitch so the LFO panel's depth_cutoff contributes NOTHING to cutoff. With both
    // lfo.depth_cutoff and vcf.lfo_mod at zero the envelope is steady; turning vcf.lfo_mod up
    // wobbles the cutoff anyway — proving the VCF-panel path is independent of the dest switch.
    const double quiet  = wobbleAt(0, 0.0f, 0.0f);   // dest=Pitch, both cutoff-LFO terms off
    const double vcfMod = wobbleAt(0, 0.0f, 1.0f);   // dest=Pitch, VCF-panel lfo_mod up
    REQUIRE(vcfMod > quiet * 5.0);
}

// =====================================================================================
// DETERMINISM over the wired pwm_depth + vcf.lfo_mod dispatch+render path.
// =====================================================================================
TEST_CASE("dispatch_gap: the pwm_depth and vcf lfo_mod dispatch path is deterministic",
          "[dispatch_gap]") {
    Snap sn;
    sn.setCont(P::kSawLevel, 0.5f); sn.setCont(P::kPulseLevel, 0.5f);
    sn.setCont(P::kVcfCutoff, 0.5f); sn.setCont(P::kVcfResonance, 0.5f);
    sn.setCont(P::kVcoPwmDepth, 0.6f);
    sn.setChoice(P::kLfoDest, 0); sn.setCont(P::kLfoRate, 4.0f);
    sn.setCont(P::kVcfLfoMod, 0.7f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &sn.s, 57, 0.20);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
    // Sanity: the wired path actually sounds (not silent).
    bool sounding = false;
    for (float v : a) if (std::fabs(v) > 1.0e-4f) { sounding = true; break; }
    REQUIRE(sounding);
}

// =====================================================================================
// RT-SAFETY over the wired pwm_depth + vcf.lfo_mod dispatch+render path: the per-control-tick
// decode + the two new setters + the render allocate nothing and take no lock on the audio
// thread (AudioThreadGuard, the [rt] check).
// =====================================================================================
TEST_CASE("dispatch_gap: the pwm_depth and vcf lfo_mod path is allocation and lock free under the guard",
          "[dispatch_gap][rt]") {
    Snap sn;
    sn.setCont(P::kSawLevel, 0.4f); sn.setCont(P::kPulseLevel, 0.7f);
    sn.setCont(P::kVcfCutoff, 0.5f); sn.setCont(P::kVcfResonance, 0.5f);
    sn.setCont(P::kVcoPwmDepth, 0.8f);
    sn.setChoice(P::kLfoDest, 0); sn.setCont(P::kLfoRate, 6.0f);
    sn.setCont(P::kVcfLfoMod, 0.8f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    Block warm(kBlk);
    std::vector<mw::MidiEvent> warmEv{ noteOn(64, 0.9f, 0) };
    auto warmCtx = warm.ctx(warmEv, kBlk, &sn.s);
    eng.process(warmCtx);   // first-touch realized while allocation is still permitted
    REQUIRE(eng.voiceManager().activeCount() >= 1);

    std::vector<Block> blocks; blocks.reserve(8);
    for (int b = 0; b < 8; ++b) blocks.emplace_back(kBlk);
    std::vector<mw::BlockContext> ctxs; ctxs.reserve(8);
    std::vector<mw::MidiEvent> none;
    for (int b = 0; b < 8; ++b)
        ctxs.push_back(blocks[static_cast<std::size_t>(b)].ctx(none, kBlk, &sn.s));

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
    REQUIRE(peak < 64.0f);
}
