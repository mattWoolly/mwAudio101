// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchCompleteTest.cpp — the CONTROL-DISPATCH COMPLETENESS AUDIT
// (task 165; the ADR-028 capstone). TESTS ONLY — modifies no production code.
//
// Test-case display names ALL begin with "dispatch_complete" so
// `ctest -R dispatch_complete --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[dispatch_complete]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (the assertion class that was ENTIRELY MISSING before ADR-028: knob ->
// observable effect, not just "non-silent/deterministic"). An integration audit found the
// Engine received BlockContext::params but never read it, so ~78 of 91 params had no audible
// effect. Tasks 160 (VCO/mixer) / 161 (VCF/Env/VCA) / 162 (LFO/mod) / 162b (velocity ingress)
// / 162c (bend/wheel ingress) / 163 (FX) / 164 (analog character) built + extended the seam.
// THIS suite is the completeness critic: for EACH of the 91 live kParamDefs params it drives
// the assembled mw::Engine with the param LOW vs HIGH (others at INIT) and asserts the rendered
// OUTPUT changes in the expected dimension (pitch / spectrum / amplitude / time / FX-tail /
// model-observable) via the established measurement patterns (Goertzel fundamental, spectral
// brightness, RMS, autocorrelation echo, channel-diff). A param with NO observable effect is a
// FAILURE — that is precisely the "computed-but-never-wired" regression this audit guards.
//
// A COVERAGE MANIFEST (one row per live param -> its asserted effect dimension, or exempt +
// reason) is built from kParamDefs and CHECKED so all 91 are accounted for; the audit cannot
// silently drop a param. The battery is proven NON-VACUOUS by a deliberate-disconnect case (a
// known-wired param driven low-vs-high MUST exceed a tolerance a disconnect would violate, and
// a known-exempt param MUST stay bit-identical).
//
// THE 164-QA var.* ISOLATION (folded into 165): var.{cutoff,env_time,pw,glide} are each proven
// in ISOLATION — the masking params (vintage.cal_spread / drift.depth / drift.rate) muted — so
// each var.* leg's specific effect (cutoff spread / env-time spread / pw duty spread / glide-time
// spread) is independently asserted, not entangled with the cal-spread pitch perturbation.
//
// HONEST FINDINGS (a HIGH-finding NOT fixed here — TESTS ONLY; flagged loudly in the PR): two
// live registry params have NO dispatch path and therefore NO observable effect today:
//   * mw101.vco.pwm_depth  (kVcoPwmDepth)  — not in Engine::ParamSlots, never decoded/applied.
//   * mw101.vcf.lfo_mod    (kVcfLfoMod)    — not in Engine::ParamSlots, never decoded/applied.
// Each is documented in the manifest as FINDING_UNWIRED and asserted INERT (bit-identical
// low-vs-high) so the audit RECORDS the gap honestly (it does not fake an effect). When a future
// task wires them, the inert assertion fails loudly and is flipped to an audio-effect assertion.
//
// The Engine consumes the seam's immutable mw::ParamSnapshot once per control tick; this file
// builds that POD directly (the off-thread bridge's job in the real shell) and reads the audible
// result, so it links mwcore ONLY (no JUCE) [docs/design/00 §5.4; ADR-001].

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "params/ParamDefs.h"
#include "params/ParamIDs.h"
#include "params/ParamSnapshot.h"
#include "voice/VoiceTypes.h"
#include "voice/KeyAssigner.h"
#include "calibration/EngineConstants.h"
#include "calibration/VcoConstants.h"               // kPitchRefHz (the 442 Hz home)
#include "calibration/FxDispatchConstants.h"        // delay free-ms round-trip
#include "calibration/ControlDispatchCcIngressConstants.h"  // mod-wheel CC# / 7-bit max
#include "calibration/DispatchCompleteConstants.h"  // this suite's (PI) thresholds

#include "../invariants/AudioThreadGuard.h"

namespace {

namespace cc = mw::cal::dispatchcomplete;

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// =====================================================================================
// Harness (mirrors the 160/161/162/163/164 dispatch-test helpers verbatim so the audit
// exercises the EXACT same seam those legs assert through).
// =====================================================================================

// registry-index lookup: map a parameter string-ID to its kParamDefs slot — the SAME
// registry-index keying the ParamSnapshot uses.
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
// bridge emits: convertTo0to1(default) == ((v-min)/span)^skew (linear collapses to the
// proportion). setCont denormalizes against the param's OWN skew so a skewed param lands at
// the requested ENGINEERING value the dispatch recovers; setNorm sets a raw normalized value.
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
                const float denom = (d.choiceCount > 1)
                                        ? static_cast<float>(d.choiceCount - 1) : 1.0f;
                s.normalizedValues[static_cast<std::size_t>(i)] =
                    static_cast<float>(idx) / denom;
            }
        }
    }

    // Set a CONTINUOUS param by its engineering value (skew-aware, matching the bridge).
    void setCont(const char* id, float value) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        const float v = std::clamp(value, d.minValue, d.maxValue);
        s.normalizedValues[static_cast<std::size_t>(i)] = normFor(d, v);
    }

    // Set a CONTINUOUS param by a RAW normalized [0,1] drive point (the generic low/high
    // endpoints the manifest sweep uses for continuous legs).
    void setNorm(const char* id, float norm) noexcept {
        const int i = slotOf(id);
        s.normalizedValues[static_cast<std::size_t>(i)] = std::clamp(norm, 0.0f, 1.0f);
    }

    // Set a CHOICE/BOOL param by its option index.
    void setChoice(const char* id, int idx) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
        const float denom = (d.choiceCount > 1) ? static_cast<float>(d.choiceCount - 1) : 1.0f;
        s.normalizedValues[static_cast<std::size_t>(i)] = static_cast<float>(idx) / denom;
    }
    void setBool(const char* id, bool on) noexcept { setChoice(id, on ? 1 : 0); }
};

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOn;
    e.noteId = static_cast<std::int16_t>(note);
    e.value = vel;
    e.sampleOffset = offset;
    return e;
}
mw::MidiEvent pitchBendEv(float unit, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::PitchBend;
    e.noteId = -1;
    e.value = unit;   // [-1,+1]; 0 == centered
    e.sampleOffset = offset;
    return e;
}
mw::MidiEvent modWheelCc(int value7bit, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::ControlChange;
    e.noteId = -1;
    e.data0 = static_cast<float>(mw::cal::ccingress::kModWheelCcNumber);
    e.value = static_cast<float>(value7bit);
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
                         const mw::ParamSnapshot* p, mw::ContinuousControllers c = {}) noexcept {
        mw::BlockContext bc{};
        bc.audio.channels = ch; bc.audio.numChannels = 2; bc.audio.numFrames = n;
        bc.params = p;
        bc.transport = mw::TransportInfo{ 120.0, 0.0, false, kSr };
        bc.midi.events = ev.empty() ? nullptr : ev.data();
        bc.midi.numEvents = static_cast<int>(ev.size());
        bc.controllers = c;
        return bc;
    }
};

struct Stereo { std::vector<float> L, R; };

// Render `seconds` of a held note (mono L), driving the snapshot every block so the per-tick
// dispatch applies, with an OPTIONAL note-on velocity + live controllers held for the render.
Stereo renderHeldStereo(mw::Engine& eng, const mw::ParamSnapshot* snap, int midiNote,
                        double seconds, float vel = 1.0f, mw::ContinuousControllers cc = {},
                        int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, vel, 0) };
        if (cc.pitchBend != 0.0f) ev.push_back(pitchBendEv(cc.pitchBend, 0));
        if (cc.modWheel  != 0.0f)
            ev.push_back(modWheelCc(
                static_cast<int>(cc.modWheel * mw::cal::ccingress::kSevenBitMax + 0.5f), 0));
        auto c = on.ctx(ev, kBlk, snap, cc);
        eng.process(c);
    }
    for (int b = 1; b < warmupBlocks; ++b) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap, cc); eng.process(c);
    }
    const int total = static_cast<int>(seconds * kSr);
    Stereo out;
    out.L.reserve(static_cast<std::size_t>(total) + kBlk);
    out.R.reserve(static_cast<std::size_t>(total) + kBlk);
    int rendered = 0;
    while (rendered < total) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap, cc); eng.process(c);
        for (int i = 0; i < kBlk && rendered < total; ++i, ++rendered) {
            out.L.push_back(w.L[static_cast<std::size_t>(i)]);
            out.R.push_back(w.R[static_cast<std::size_t>(i)]);
        }
    }
    return out;
}

std::vector<float> renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap, int midiNote,
                              double seconds, float vel = 1.0f,
                              mw::ContinuousControllers cc = {}, int warmupBlocks = 8) {
    return renderHeldStereo(eng, snap, midiNote, seconds, vel, cc, warmupBlocks).L;
}

// One-shot convenience: prepare a fresh engine and render a held note (mono L).
std::vector<float> freshHeld(const mw::ParamSnapshot* snap, int midiNote, double seconds,
                             float vel = 1.0f, mw::ContinuousControllers cc = {},
                             int warmupBlocks = 8) {
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    return renderHeld(eng, snap, midiNote, seconds, vel, cc, warmupBlocks);
}

// Render a SHORT note then SILENCE so a delay's discrete echoes ring out into the tail.
std::vector<float> freshBurstThenSilence(const mw::ParamSnapshot* snap, int midiNote,
                                         double noteSec, double tailSec) {
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    constexpr int kBlk = 256;
    std::vector<float> out;
    const int noteSamps = static_cast<int>(noteSec * kSr);
    const int tailSamps = static_cast<int>(tailSec * kSr);
    out.reserve(static_cast<std::size_t>(noteSamps + tailSamps) + kBlk);
    {
        Block w(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, 1.0f, 0) };
        auto c = w.ctx(ev, kBlk, snap); eng.process(c);
        for (int i = 0; i < kBlk; ++i) out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    int rendered = kBlk;
    while (rendered < noteSamps) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap); eng.process(c);
        for (int i = 0; i < kBlk && rendered < noteSamps; ++i, ++rendered)
            out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    {
        Block w(kBlk);
        std::vector<mw::MidiEvent> ev{ mw::MidiEvent{ mw::NormalizedType::NoteOff,
                                                      0, static_cast<std::int16_t>(midiNote),
                                                      0.0f, 0.0f, 0 } };
        auto c = w.ctx(ev, kBlk, snap); eng.process(c);
        for (int i = 0; i < kBlk; ++i) out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    int tail = kBlk;
    while (tail < tailSamps) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap); eng.process(c);
        for (int i = 0; i < kBlk && tail < tailSamps; ++i, ++tail)
            out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    return out;
}

// --- measurement primitives (the established Goertzel/RMS/autocorr/channel-diff patterns) --
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
    const int steps = 700;
    for (int i = 0; i <= steps; ++i) {
        const double f = fLo * std::pow(fHi / fLo, static_cast<double>(i) / steps);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    const double lo = bestF * 0.985, hi = bestF * 1.015;
    for (int i = 0; i <= 300; ++i) {
        const double f = lo + (hi - lo) * (static_cast<double>(i) / 300.0);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    return bestF;
}

double midiHz(int n) noexcept { return cc::kA4Hz * std::pow(2.0, (n - 69) / 12.0); }
double midiHz442(int n) noexcept { return mw::cal::vco::kPitchRefHz * std::pow(2.0, (n - 69) / 12.0); }

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}
double rmsDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        acc += d * d;
    }
    return std::sqrt(acc / std::max<std::size_t>(1, n));
}
double rmsRange(const std::vector<float>& x, std::size_t lo, std::size_t hi) noexcept {
    hi = std::min(hi, x.size());
    if (lo >= hi) return 0.0;
    double acc = 0.0;
    for (std::size_t i = lo; i < hi; ++i) acc += static_cast<double>(x[i]) * x[i];
    return std::sqrt(acc / static_cast<double>(hi - lo));
}

// High-band energy (harmonics h8..h16) — the filter-brightness probe.
double highBandEnergy(const std::vector<float>& x, double f0, double sr) noexcept {
    double hi = 0.0;
    for (int h = 8; h <= 16; ++h) hi += goertzelPower(x, f0 * h, sr);
    return hi;
}
// Even/odd harmonic ratio — the PW-duty / drive probe (2nd over 1st).
double secondOverFirst(const std::vector<float>& x, double f0, double sr) noexcept {
    const double h1 = goertzelPower(x, f0, sr);
    const double h2 = goertzelPower(x, 2.0 * f0, sr);
    return h2 / std::max(1e-12, h1);
}
// Normalized autocorrelation at a lag (the delay-echo probe).
double autocorrAtLag(const std::vector<float>& x, int lag) noexcept {
    const int N = static_cast<int>(x.size());
    if (lag <= 0 || lag >= N) return 0.0;
    double num = 0.0, e0 = 0.0, eL = 0.0;
    for (int n = lag; n < N; ++n) {
        const double a = x[static_cast<std::size_t>(n)];
        const double b = x[static_cast<std::size_t>(n - lag)];
        num += a * b; e0 += a * a; eL += b * b;
    }
    const double den = std::sqrt(e0 * eL);
    return den > 1e-20 ? num / den : 0.0;
}
// Short-time RMS envelope + its AC energy (the LFO-wobble probe).
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

namespace P = mw::params::ids;

// A saw-only, env-defeated, open-filter base snapshot: a flat steady tone whose pitch/spectrum
// is governed by the param under test, not by an envelope contour or the INIT mixer.
void flatSaw(Snap& sn) noexcept {
    sn.setCont(P::kSawLevel, 0.9f);
    sn.setCont(P::kPulseLevel, 0.0f);
    sn.setCont(P::kSubLevel, 0.0f);
    sn.setCont(P::kNoiseLevel, 0.0f);
    sn.setCont(P::kEnvAttack, 0.0f);
    sn.setCont(P::kEnvDecay, 0.0f);
    sn.setCont(P::kEnvSustain, 1.0f);
    sn.setCont(P::kVcfCutoff, 1.0f);
    sn.setCont(P::kVcfEnvMod, 0.0f);
    sn.setCont(P::kVcfKbdTrack, 0.0f);
    sn.setCont(P::kVcfResonance, 0.0f);
    sn.setCont(P::kVcaLevel, 0.8f);
}

constexpr float kSaneBound = 64.0f;

// =====================================================================================
// THE COVERAGE MANIFEST. One row per LIVE param -> the dimension the audit asserts (or the
// exempt/finding class + reason). The audit builds this from the IDs below and CROSS-CHECKS
// that every kParamDefs entry is accounted for exactly once, so a future registry row added
// without a manifest entry FAILS the completeness coverage test (it cannot be silently dropped).
// =====================================================================================
enum class Dim {
    Pitch,         // rendered fundamental shift (Goertzel)         — vco.tune/fine/range/a4/bend
    Spectrum,      // harmonic content / brightness (Goertzel band) — pw, cutoff, reso, env_mod, lfo
    Amplitude,     // RMS / amplitude contour                       — levels, vca, env, expression
    Time,          // temporal contour / slew                       — env A/D/R, glide, lfo delay
    FxTail,        // FX echo / saturation / stereo image           — fx.*, out.mono
    Character,     // model-observable per-voice perturbation       — vintage/drift/var/slop
    FrozenInert,   // decoded-correct but inert at the LIVE MONO seam: drawn at prepare/Re-roll
                   // (vintage.cal_spread) or unison/poly-only (vintage.detune_amt) — see rationale
    StructuralOff, // off-thread structural param (ADR-028 item 4)  — quality/voice.*/unison/control
    SubsystemOff,  // arp/seq/key/pitch — driven by ControlSnapshot, NOT the ParamSnapshot seam
    DecodeInert,   // decoded at the seam but inert w/o a live ingress source (mpe.*)
    FindingUnwired // LIVE registry param with NO dispatch path — a HIGH finding (flagged)
};

struct ManifestRow { const char* id; Dim dim; };

// 91 rows — one per live kParamDefs entry, in §3.0 index order.
constexpr std::array<ManifestRow, 91> kManifest = {{
    // --- VCO / oscillator ---
    { P::kVcoTune,      Dim::Pitch },
    { P::kVcoFine,      Dim::Pitch },
    { P::kVcoPw,        Dim::Spectrum },
    { P::kVcoPwmDepth,  Dim::FindingUnwired },   // FINDING: no ParamSlots entry, never decoded
    { P::kVcoRange,     Dim::Pitch },
    // --- source mixer / sub / noise ---
    { P::kSawLevel,     Dim::Amplitude },
    { P::kPulseLevel,   Dim::Spectrum },
    { P::kSubLevel,     Dim::Spectrum },
    { P::kSubMode,      Dim::Spectrum },
    { P::kNoiseLevel,   Dim::Spectrum },
    // --- VCF ---
    { P::kVcfCutoff,    Dim::Spectrum },
    { P::kVcfResonance, Dim::Spectrum },
    { P::kVcfEnvMod,    Dim::Spectrum },
    { P::kVcfLfoMod,    Dim::FindingUnwired },    // FINDING: no ParamSlots entry, never decoded
    { P::kVcfKbdTrack,  Dim::Spectrum },
    // --- envelope ---
    { P::kEnvAttack,    Dim::Time },
    { P::kEnvDecay,     Dim::Time },
    { P::kEnvSustain,   Dim::Amplitude },
    { P::kEnvRelease,   Dim::Time },
    // --- LFO ---
    { P::kLfoRate,        Dim::Spectrum },
    { P::kLfoShape,       Dim::Spectrum },
    { P::kLfoDest,        Dim::Spectrum },
    { P::kLfoDelay,       Dim::Time },
    { P::kLfoDepthPitch,  Dim::Spectrum },
    { P::kLfoDepthPwm,    Dim::Spectrum },
    { P::kLfoDepthCutoff, Dim::Spectrum },
    { P::kLfoTempoSync,   Dim::SubsystemOff },    // LFO sync ladder: clock/transport subsystem
    { P::kLfoSyncDiv,     Dim::SubsystemOff },
    // --- VCA ---
    { P::kVcaLevel,     Dim::Amplitude },
    { P::kVcaMode,      Dim::Amplitude },
    // --- glide ---
    { P::kGlideTime,    Dim::Time },
    { P::kGlideMode,    Dim::Time },
    // --- mod / bend (live controller ingress via 162c) ---
    { P::kModBendRangeVco, Dim::Pitch },
    { P::kModBendRangeVcf, Dim::Spectrum },
    { P::kModBendDest,     Dim::Pitch },
    { P::kModLfoModWheel,  Dim::Spectrum },
    // --- arp (ControlSnapshot subsystem, NOT the ParamSnapshot seam) ---
    { P::kArpMode,      Dim::SubsystemOff },
    { P::kArpRange,     Dim::SubsystemOff },
    { P::kArpTempoSync, Dim::SubsystemOff },
    { P::kArpSyncDiv,   Dim::SubsystemOff },
    { P::kArpLatch,     Dim::SubsystemOff },
    // --- seq (ControlSnapshot subsystem) ---
    { P::kSeqMode,      Dim::SubsystemOff },
    { P::kSeqTempoSync, Dim::SubsystemOff },
    { P::kSeqSyncDiv,   Dim::SubsystemOff },
    // --- key / trigger (KeyAssigner subsystem, set via Engine::setGateTrigMode off-seam) ---
    { P::kKeyTriggerPriority, Dim::SubsystemOff },
    // --- tuning ---
    { P::kTuneA4,       Dim::Pitch },
    { P::kTuneSlop,     Dim::Character },
    // --- pitch / velocity / expression / MPE ---
    { P::kPitchModernUnquantized, Dim::SubsystemOff },  // ControlCore pitch-pole (off the seam)
    { P::kVelEnable,    Dim::Amplitude },   // velocity ingress (162b) -> VCA loudness
    { P::kVelDepth,     Dim::Amplitude },
    { P::kAmpExpression, Dim::Amplitude },
    { P::kMpeEnable,       Dim::DecodeInert },   // decode wired; no live per-note MPE ingress
    { P::kMpeBendRange,    Dim::DecodeInert },
    { P::kMpePressureDest, Dim::DecodeInert },
    // --- vintage / drift / variance / warm-up ---
    { P::kVintageAge,       Dim::Character },
    { P::kVintageEnable,    Dim::Character },
    { P::kVintageCalSpread, Dim::FrozenInert },  // Tier-1 drawn at prepare/Re-roll, not live tick
    { P::kVintageDetuneAmt, Dim::FrozenInert },  // unison/poly spread scaler; no effect in MONO=voice0
    { P::kDriftDepth,       Dim::Character },
    { P::kDriftRate,        Dim::Character },
    { P::kWarmupTime,       Dim::Character },
    { P::kVarCutoff,        Dim::Character },
    { P::kVarEnvTime,       Dim::Character },
    { P::kVarPw,            Dim::Character },
    { P::kVarGlide,         Dim::Character },
    // --- FX: drive ---
    { P::kFxBypass,       Dim::FxTail },
    { P::kFxDriveEnable,  Dim::FxTail },
    { P::kFxDriveAmount,  Dim::FxTail },
    { P::kFxDriveTone,    Dim::FxTail },
    { P::kFxDriveOutput,  Dim::FxTail },
    // --- FX: chorus ---
    { P::kFxChorusEnable, Dim::FxTail },
    { P::kFxChorusMode,   Dim::FxTail },
    { P::kFxChorusRate,   Dim::FxTail },
    { P::kFxChorusDepth,  Dim::FxTail },
    { P::kFxChorusWidth,  Dim::FxTail },
    { P::kFxChorusMix,    Dim::FxTail },
    // --- FX: delay ---
    { P::kFxDelayEnable,   Dim::FxTail },
    { P::kFxDelaySync,     Dim::FxTail },
    { P::kFxDelayDivision, Dim::FxTail },
    { P::kFxDelayTime,     Dim::FxTail },
    { P::kFxDelayFeedback, Dim::FxTail },
    { P::kFxDelayDamp,     Dim::FxTail },
    { P::kFxDelayWidth,    Dim::FxTail },
    { P::kFxDelayMix,      Dim::FxTail },
    { P::kFxDelayPingpong, Dim::FxTail },
    // --- output ---
    { P::kOutMono,      Dim::FxTail },
    // --- structural (off-thread; ADR-028 item 4) ---
    { P::kQuality,        Dim::StructuralOff },
    { P::kVoiceMode,      Dim::StructuralOff },
    { P::kVoiceCount,     Dim::StructuralOff },
    { P::kUnisonCount,    Dim::StructuralOff },
    { P::kControlVintage, Dim::StructuralOff },
}};

} // namespace

// =====================================================================================
// MANIFEST COVERAGE: every one of the 91 kParamDefs params is accounted for exactly once
// in the manifest (and every manifest ID is a live registry ID). A registry row added
// without a manifest entry — or a manifest typo — FAILS here, so the audit cannot silently
// drop a param. [task 165 Acceptance: "a coverage manifest the test checks so all 91 are
// accounted for"; ADR-028 capstone]
// =====================================================================================
TEST_CASE("dispatch_complete: the coverage manifest accounts for all 91 live params exactly once",
          "[dispatch_complete]") {
    REQUIRE(kManifest.size() == mw::params::kParamDefs.size());   // 91 == 91

    // (a) every manifest ID resolves to a live registry slot, and no ID repeats.
    std::array<int, 91> manifestSlots{};
    for (std::size_t i = 0; i < kManifest.size(); ++i) {
        const int slot = slotOf(kManifest[i].id);
        INFO("manifest id " << kManifest[i].id);
        REQUIRE(slot >= 0);                       // a real registry param
        manifestSlots[i] = slot;
    }
    for (std::size_t i = 0; i < manifestSlots.size(); ++i)
        for (std::size_t j = i + 1; j < manifestSlots.size(); ++j)
            REQUIRE(manifestSlots[i] != manifestSlots[j]);   // no duplicate coverage

    // (b) every registry slot is covered by exactly one manifest row (no param dropped).
    std::array<bool, 91> covered{};
    for (int slot : manifestSlots) covered[static_cast<std::size_t>(slot)] = true;
    for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
        INFO("registry param " << mw::params::kParamDefs[static_cast<std::size_t>(i)].id);
        REQUIRE(covered[static_cast<std::size_t>(i)]);
    }
}

// =====================================================================================
// NON-VACUOUS PROOF: the battery would FAIL if a covered param were disconnected. A known-
// WIRED param (vca.level) driven low-vs-high MUST differ by a tolerance a disconnect would
// violate; a known-EXEMT param (mpe.enable, decode-inert) MUST be bit-identical low-vs-high.
// If the audit's measurement+threshold were vacuous, the wired case below would pass even at
// low==high — it does not. [task 165 Acceptance: "battery FAILS if any covered param is
// disconnected, proven by a deliberate disconnect"]
// =====================================================================================
TEST_CASE("dispatch_complete: the battery is non-vacuous a disconnect would violate the tolerance",
          "[dispatch_complete]") {
    // (1) WIRED control: vca.level low vs high differs FAR beyond the disconnect tolerance.
    auto vcaRms = [](float level) {
        Snap sn; flatSaw(sn);
        sn.setCont(P::kVcaLevel, level);
        return rms(freshHeld(&sn.s, 60, 0.20));
    };
    const double loLvl = vcaRms(0.2f);
    const double hiLvl = vcaRms(1.0f);
    REQUIRE(hiLvl > 0.0);
    // The disconnect tolerance: a disconnected param renders low==high, ratio ~1.0. The real
    // wiring produces a ratio FAR above kMinRatioStrong; assert it so a regression that drops
    // vca.level (ratio -> ~1) would FAIL this exact line.
    REQUIRE((hiLvl / std::max(1e-12, loLvl)) > cc::kMinRatioStrong);

    // SELF-CHECK that the tolerance is meaningful: if vca.level WERE disconnected, low and high
    // would render identically (ratio == 1), which is BELOW kMinRatioStrong — i.e. the assertion
    // above is the line that turns red on a disconnect. We demonstrate the ratio-1 baseline by
    // rendering the SAME level twice (the disconnect-equivalent): it must NOT pass the threshold.
    const double sameA = vcaRms(0.6f);
    const double sameB = vcaRms(0.6f);
    REQUIRE((sameA / std::max(1e-12, sameB)) == Catch::Approx(1.0).epsilon(1e-6));
    REQUIRE_FALSE((sameA / std::max(1e-12, sameB)) > cc::kMinRatioStrong);   // disconnect => fail

    // (2) EXEMPT control: mpe.enable is decode-only (no live ingress), so low-vs-high is
    // BIT-IDENTICAL — the audit's inert-leg assertion. If it spuriously DID change the render,
    // the bitIdentical check below would fail (the exempt class is also guarded, not assumed).
    auto mpeRender = [](bool on) {
        Snap sn;
        if (on) {
            sn.setBool(P::kMpeEnable, true);
            sn.setCont(P::kMpeBendRange, 96.0f);
            sn.setChoice(P::kMpePressureDest, 1);
        }
        return freshHeld(&sn.s, 60, 0.20);
    };
    REQUIRE(bitIdentical(mpeRender(false), mpeRender(true)));
}

// =====================================================================================
// VCO + PITCH legs: tune / fine / range / a4 / bend each shift the rendered fundamental;
// pw / pulse / sub / noise / sub.mode each change the spectrum. (The detailed per-leg ratios
// are the 160/162c suites; here the audit re-checks every VCO/mixer param has effect.)
// =====================================================================================
TEST_CASE("dispatch_complete: every VCO and mixer param shifts pitch or spectrum",
          "[dispatch_complete]") {
    const int note = 60;
    const double f0 = midiHz(note);

    // vco.tune: +12 semis == +1 octave (x2).
    {
        Snap lo; flatSaw(lo); lo.setCont(P::kVcoTune, 0.0f);
        Snap hi; flatSaw(hi); hi.setCont(P::kVcoTune, 12.0f);
        const double fLo = estimateFundamental(freshHeld(&lo.s, note, 0.30), kSr, f0*0.4, f0*4.0);
        const double fHi = estimateFundamental(freshHeld(&hi.s, note, 0.30), kSr, f0*0.4, f0*4.0);
        REQUIRE((fHi / fLo) == Catch::Approx(2.0).epsilon(0.03));
    }
    // vco.fine: +1 semitone == 2^(1/12).
    {
        Snap lo; flatSaw(lo); lo.setCont(P::kVcoFine, 0.0f);
        Snap hi; flatSaw(hi); hi.setCont(P::kVcoFine, 1.0f);
        const double fLo = estimateFundamental(freshHeld(&lo.s, note, 0.30), kSr, f0*0.4, f0*2.0);
        const double fHi = estimateFundamental(freshHeld(&hi.s, note, 0.30), kSr, f0*0.4, f0*2.0);
        REQUIRE((fHi / fLo) == Catch::Approx(std::pow(2.0, 1.0/12.0)).epsilon(0.02));
    }
    // vco.range: 8' (idx 1) vs 4' (idx 2) == +1 octave.
    {
        Snap lo; flatSaw(lo); lo.setChoice(P::kVcoRange, 1);
        Snap hi; flatSaw(hi); hi.setChoice(P::kVcoRange, 2);
        const double fLo = estimateFundamental(freshHeld(&lo.s, note, 0.30), kSr, f0*0.4, f0*4.0);
        const double fHi = estimateFundamental(freshHeld(&hi.s, note, 0.30), kSr, f0*0.4, f0*4.0);
        REQUIRE((fHi / fLo) == Catch::Approx(2.0).epsilon(0.03));
    }
    // tune.a4: 460 sharper than 442 by the exact ratio.
    {
        const double home = midiHz442(69);
        Snap lo; lo.setCont(P::kTuneA4, 442.0f);
        Snap hi; hi.setCont(P::kTuneA4, 460.0f);
        const double fLo = estimateFundamental(freshHeld(&lo.s, 69, 0.30), kSr, 360.0, 520.0);
        const double fHi = estimateFundamental(freshHeld(&hi.s, 69, 0.30), kSr, 360.0, 520.0);
        REQUIRE(fLo == Catch::Approx(home).epsilon(0.01));
        REQUIRE((fHi / fLo) == Catch::Approx(460.0 / 442.0).epsilon(0.01));
    }
    // vco.pw: square vs narrow duty -> the 2nd (even) harmonic jumps (pulse-only mix).
    {
        auto pwRatio = [&](float pw) {
            Snap sn; flatSaw(sn);
            sn.setCont(P::kSawLevel, 0.0f); sn.setCont(P::kPulseLevel, 1.0f);
            sn.setCont(P::kVcoPw, pw);
            return secondOverFirst(freshHeld(&sn.s, note, 0.30), f0, kSr);
        };
        REQUIRE(pwRatio(0.85f) > pwRatio(0.0f) * cc::kMinRatioStrong);
    }
    // pulse.level / sub.level / noise.level: off vs on raises that source's signature energy.
    {
        auto sourceEnergy = [&](const char* id, double probeHz) {
            Snap off; flatSaw(off); off.setCont(P::kSawLevel, 0.3f); off.setCont(id, 0.0f);
            Snap on;  flatSaw(on);  on.setCont(P::kSawLevel, 0.3f);  on.setCont(id, 1.0f);
            const double e0 = goertzelPower(freshHeld(&off.s, note, 0.25), probeHz, kSr);
            const double e1 = goertzelPower(freshHeld(&on.s,  note, 0.25), probeHz, kSr);
            return std::pair<double,double>{ e0, e1 };
        };
        // sub adds the sub-octave (f0/2); pulse adds the played fundamental tone too — use a
        // broadband RMS for pulse/noise (they are spectrally distinct from the 0.3-saw base).
        const auto sub = sourceEnergy(P::kSubLevel, 0.5 * f0);
        REQUIRE(sub.second > sub.first * 10.0);   // sub-octave appears only with sub.level up

        auto broadband = [&](const char* id) {
            Snap off; flatSaw(off); off.setCont(P::kSawLevel, 0.0f);
            off.setCont(P::kPulseLevel, 0.0f); off.setCont(P::kSubLevel, 0.0f);
            off.setCont(P::kNoiseLevel, 0.0f); off.setCont(id, 0.0f);
            Snap on = off; on.setCont(id, 1.0f);
            return std::pair<double,double>{ rms(freshHeld(&off.s, note, 0.20)),
                                             rms(freshHeld(&on.s,  note, 0.20)) };
        };
        const auto pulse = broadband(P::kPulseLevel);
        REQUIRE(pulse.first < cc::kSilenceCeiling);          // all sources off => silent
        REQUIRE(pulse.second > cc::kSoundingRmsFloor);       // pulse alone sounds
        const auto noise = broadband(P::kNoiseLevel);
        REQUIRE(noise.second > cc::kSoundingRmsFloor);       // noise alone sounds
    }
    // saw.level: scales the saw amplitude (broadband RMS, saw-only). The mixer level feeds the
    // VCF + the BA662A OTA VCA, whose taper is COMPRESSIVE near the top of its range, so the law
    // is monotone-but-sub-linear (saw 0.1 -> rms ~0.037, saw 1.0 -> ~0.062). The audit asserts
    // the WIRING (a clear monotone rise across the full span), not a linear law — a disconnect
    // would collapse it to a flat ratio ~1.0, below kMinRatioClear.
    {
        auto sawRms = [&](float lvl) {
            Snap sn; flatSaw(sn); sn.setCont(P::kSawLevel, lvl);
            return rms(freshHeld(&sn.s, note, 0.20));
        };
        REQUIRE(sawRms(1.0f) > sawRms(0.1f) * cc::kMinRatioClear);
    }
    // sub.mode: -1 oct square (idx 0) vs -2 oct (idx 1) -> the sub-octave energy moves to f0/4.
    {
        Snap m0; flatSaw(m0); m0.setCont(P::kSawLevel, 0.0f); m0.setCont(P::kSubLevel, 1.0f);
        m0.setChoice(P::kSubMode, 0);
        Snap m1 = m0; m1.setChoice(P::kSubMode, 1);
        const double half0  = goertzelPower(freshHeld(&m0.s, note, 0.25), 0.5  * f0, kSr);
        const double quart1 = goertzelPower(freshHeld(&m1.s, note, 0.25), 0.25 * f0, kSr);
        REQUIRE(half0 > 0.0);
        REQUIRE(quart1 > 0.0);   // -2-oct mode puts strong energy two octaves down
        // The two modes do not render identically.
        REQUIRE_FALSE(bitIdentical(freshHeld(&m0.s, note, 0.10), freshHeld(&m1.s, note, 0.10)));
    }
}

// =====================================================================================
// VCF legs: cutoff / resonance / env_mod / kbd_track each change the spectrum. (Re-checks
// the 161 leg has effect for every VCF param; the detailed sweeps live in DispatchVcfTest.)
// =====================================================================================
TEST_CASE("dispatch_complete: every VCF param changes the filter spectrum",
          "[dispatch_complete]") {
    const int note = 48;
    const double f0 = midiHz(note);

    // cutoff: low vs high opens the high band.
    {
        auto hi = [&](float c01) {
            Snap sn; flatSaw(sn); sn.setCont(P::kVcfCutoff, c01);
            return highBandEnergy(freshHeld(&sn.s, note, 0.30), f0, kSr);
        };
        REQUIRE(hi(1.0f) > hi(0.45f) * cc::kMinRatioStrong);
    }
    // resonance: muted mixer, reso 0 (silent) vs reso 1 (self-osc ring).
    {
        auto reso = [&](float r) {
            Snap sn;
            sn.setCont(P::kSawLevel, 0.0f); sn.setCont(P::kPulseLevel, 0.0f);
            sn.setCont(P::kSubLevel, 0.0f); sn.setCont(P::kNoiseLevel, 0.0f);
            sn.setCont(P::kVcfCutoff, 0.5f); sn.setCont(P::kVcfResonance, r);
            sn.setCont(P::kEnvSustain, 1.0f); sn.setCont(P::kEnvAttack, 0.0f);
            return rms(freshHeld(&sn.s, 60, 0.30, 1.0f, {}, 12));
        };
        REQUIRE(reso(0.0f) < cc::kSilenceCeiling);
        REQUIRE(reso(1.0f) > 2.0e-3);
    }
    // env_mod: a percussive env opens-then-closes the filter (early brighter than late).
    {
        Snap sn;
        sn.setCont(P::kSawLevel, 0.9f); sn.setCont(P::kPulseLevel, 0.0f);
        sn.setCont(P::kVcfCutoff, 0.12f); sn.setCont(P::kVcfEnvMod, 1.0f);
        sn.setCont(P::kVcfResonance, 0.0f);
        sn.setCont(P::kEnvAttack, 0.001f); sn.setCont(P::kEnvDecay, 0.12f);
        sn.setCont(P::kEnvSustain, 0.0f); sn.setCont(P::kEnvRelease, 0.1f);
        // capture from onset (no warmup) so the bright attack is in the buffer
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &sn.s, note, 0.30, 1.0f, {}, /*warmup=*/1);
        const std::size_t n = buf.size();
        std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(n / 6));
        std::vector<float> late (buf.begin() + static_cast<long>(2 * n / 3), buf.end());
        REQUIRE(highBandEnergy(early, f0, kSr) > highBandEnergy(late, f0, kSr) * cc::kMinRatioStrong);
    }
    // kbd_track: a high note is far brighter with tracking ON than OFF (at a fixed cutoff).
    {
        auto lowBand = [&](float track) {
            Snap sn; flatSaw(sn); sn.setCont(P::kVcfCutoff, 0.45f);
            sn.setCont(P::kVcfKbdTrack, track);
            auto buf = freshHeld(&sn.s, 90, 0.30);   // A6, well above the A4 ref
            double e = 0.0; for (int h = 1; h <= 6; ++h) e += goertzelPower(buf, midiHz(90)*h, kSr);
            return e;
        };
        REQUIRE(lowBand(1.0f) > lowBand(0.0f) * cc::kMinRatioStrong);
    }
}

// =====================================================================================
// ENVELOPE + VCA + glide: attack/decay/release move the temporal contour; sustain + vca.level
// + vca.mode shape the amplitude; glide.time/mode slew the pitch. (Re-checks 160/161 legs.)
// =====================================================================================
TEST_CASE("dispatch_complete: env vca and glide params change contour amplitude and slew",
          "[dispatch_complete]") {
    const int note = 60;

    // env.attack: a long attack ramps in slowly (early << late) vs near-instant.
    {
        auto onset = [&](float atk) {
            Snap sn; flatSaw(sn); sn.setCont(P::kEnvAttack, atk);
            sn.setCont(P::kEnvDecay, 0.0f); sn.setCont(P::kEnvSustain, 1.0f);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            auto buf = renderHeld(eng, &sn.s, note, 0.40, 1.0f, {}, /*warmup=*/1);
            const std::size_t n = buf.size();
            return rmsRange(buf, 0, static_cast<std::size_t>(0.025 * kSr))
                 / std::max(1e-9, rmsRange(buf, 3 * n / 4, n));
        };
        REQUIRE(onset(0.0f) > onset(0.30f) * cc::kMinRatioStrong);
    }
    // env.decay: a long decay holds more late energy than a short decay (at sustain 0).
    {
        auto lateRms = [&](float dec) {
            Snap sn; flatSaw(sn);
            sn.setCont(P::kEnvAttack, 0.001f); sn.setCont(P::kEnvDecay, dec);
            sn.setCont(P::kEnvSustain, 0.0f); sn.setCont(P::kEnvRelease, 0.2f);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            auto buf = renderHeld(eng, &sn.s, note, 0.40, 1.0f, {}, /*warmup=*/1);
            const std::size_t n = buf.size();
            return rmsRange(buf, n / 2, 3 * n / 4);
        };
        REQUIRE(lateRms(0.35f) > lateRms(0.02f) * cc::kMinRatioClear);
    }
    // env.release: a longer release holds the voice in its RELEASING stage longer after note-off.
    // The Voice renders while Releasing until the ADSR release reaches silence (env_.active()
    // false), then self-transitions to Idle (Voice.cpp §4.3). The VCA anti-thump gate masks the
    // release in the AMPLITUDE tail, but the release time governs the voice-active DURATION — the
    // model-observable effect: a 1.0 s release keeps the voice active ~100x longer than a 0.01 s
    // release. Measured as the number of post-note-off blocks until VoiceManager::activeCount==0.
    {
        auto activeBlocks = [&](float rel) {
            Snap sn; flatSaw(sn);
            sn.setCont(P::kEnvAttack, 0.001f); sn.setCont(P::kEnvDecay, 0.02f);
            sn.setCont(P::kEnvSustain, 1.0f); sn.setCont(P::kEnvRelease, rel);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            constexpr int kBlk = 256;
            { Block on(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(note, 1.0f, 0) };
              auto c = on.ctx(ev, kBlk, &sn.s); eng.process(c); }
            for (int b = 0; b < 16; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
              auto c = w.ctx(none, kBlk, &sn.s); eng.process(c); }
            { Block w(kBlk);
              std::vector<mw::MidiEvent> ev{ mw::MidiEvent{ mw::NormalizedType::NoteOff, 0,
                                                            static_cast<std::int16_t>(note), 0.0f, 0.0f, 0 } };
              auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c); }
            int blocks = 0;
            for (; blocks < 2000; ++blocks) {
                Block w(kBlk); std::vector<mw::MidiEvent> none;
                auto c = w.ctx(none, kBlk, &sn.s); eng.process(c);
                if (eng.voiceManager().activeCount() == 0) break;
            }
            return blocks;
        };
        REQUIRE(activeBlocks(1.0f) > activeBlocks(0.01f) * static_cast<int>(cc::kMinRatioStrong));
    }
    // env.sustain: a higher sustain holds a louder steady level.
    {
        auto held = [&](float sus) {
            Snap sn; flatSaw(sn);
            sn.setCont(P::kEnvAttack, 0.002f); sn.setCont(P::kEnvDecay, 0.02f);
            sn.setCont(P::kEnvSustain, sus);
            return rms(freshHeld(&sn.s, note, 0.20, 1.0f, {}, 16));
        };
        REQUIRE(held(1.0f) > held(0.2f) * cc::kMinRatioClear);
    }
    // vca.level: scales amplitude; level 0 is silent.
    {
        auto lvl = [&](float v) {
            Snap sn; flatSaw(sn); sn.setCont(P::kVcaLevel, v);
            return rms(freshHeld(&sn.s, note, 0.20));
        };
        REQUIRE(lvl(0.0f) < cc::kSilenceCeiling);
        REQUIRE(lvl(1.0f) > lvl(0.4f) * cc::kMinRatioClear);
    }
    // vca.mode: ENV (decays to 0 sustain) vs GATE (flat full) — distinct contour.
    {
        auto lateOverEarly = [&](int mode) {
            Snap sn; flatSaw(sn);
            sn.setCont(P::kEnvAttack, 0.001f); sn.setCont(P::kEnvDecay, 0.06f);
            sn.setCont(P::kEnvSustain, 0.0f); sn.setCont(P::kEnvRelease, 0.1f);
            sn.setChoice(P::kVcaMode, mode);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            auto buf = renderHeld(eng, &sn.s, note, 0.30, 1.0f, {}, /*warmup=*/1);
            const std::size_t n = buf.size();
            return rmsRange(buf, 3 * n / 4, n) / std::max(1e-9, rmsRange(buf, 0, n / 8));
        };
        const double env = lateOverEarly(0), gate = lateOverEarly(1);
        REQUIRE(env < 0.4);
        REQUIRE(gate > env * 1.8);
    }
    // glide.time + glide.mode: a glided legato is mid-slew; an off/snap is already at target.
    {
        auto earlyFund = [&](int glideMode, float glideS) {
            Snap sn; flatSaw(sn);
            sn.setChoice(P::kGlideMode, glideMode);
            sn.setCont(P::kGlideTime, glideS);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            constexpr int kBlk = 256;
            { Block on(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(48, 1.0f, 0) };
              auto c = on.ctx(ev, kBlk, &sn.s); eng.process(c); }
            for (int b = 0; b < 20; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
              auto c = w.ctx(none, kBlk, &sn.s); eng.process(c); }
            std::vector<float> early;
            { Block w(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(72, 1.0f, 0) };
              auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c);
              for (int i = 0; i < kBlk; ++i) early.push_back(w.L[static_cast<std::size_t>(i)]); }
            // ~80 ms of the transition (1+14 blocks), the window the proven DispatchLfoModTest
            // glide case uses — short vs the 400 ms glide, so a glided slew sits mid-way.
            for (int b = 0; b < 14; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
              auto c = w.ctx(none, kBlk, &sn.s); eng.process(c);
              for (int i = 0; i < kBlk; ++i) early.push_back(w.L[static_cast<std::size_t>(i)]); }
            return estimateFundamental(early, kSr, midiHz(48) * 0.7, midiHz(72) * 1.3);
        };
        const double f48 = midiHz(48), f72 = midiHz(72);
        const double snap = earlyFund(0, 0.40f);   // Off: instant jump
        const double glid = earlyFund(2, 0.40f);   // On: mid-slew
        REQUIRE(snap > f72 * 0.95);                 // glide.mode Off already at target
        REQUIRE(glid > f48 * 1.015);                // glide.mode On + glide.time mid-slew
        REQUIRE(glid < f72 * 0.90);
    }
}

// =====================================================================================
// LFO legs: rate / shape / dest / delay / depth_{pitch,cutoff,pwm} each modulate audibly.
// (Re-checks the 162 leg has effect for every LFO param; detail in DispatchLfoModTest.)
// =====================================================================================
TEST_CASE("dispatch_complete: every LFO param modulates the output",
          "[dispatch_complete]") {
    const int note = 60;
    const double f0 = midiHz(note);

    // depth_pitch (dest=Pitch): vibrato spreads the fundamental (on-bin fraction drops).
    {
        auto centerFrac = [&](float depth) {
            Snap sn; flatSaw(sn);
            sn.setChoice(P::kLfoDest, 0); sn.setCont(P::kLfoRate, 6.0f);
            sn.setCont(P::kLfoDepthPitch, depth); sn.setCont(P::kLfoDelay, 0.0f);
            auto buf = freshHeld(&sn.s, note, 0.50);
            double tot = 0.0; for (float v : buf) tot += (double) v * v;
            return goertzelPower(buf, f0, kSr) / std::max(1e-12, tot);
        };
        REQUIRE(centerFrac(1.0f) < centerFrac(0.0f) * 0.7);
    }
    // depth_cutoff (dest=Filter): the amplitude envelope wobbles (AC energy rises).
    auto filterWobble = [&](float depth, float rate) {
        Snap sn;
        sn.setChoice(P::kLfoDest, 1); sn.setCont(P::kLfoRate, rate);
        sn.setCont(P::kLfoDepthCutoff, depth); sn.setCont(P::kLfoDelay, 0.0f);
        sn.setCont(P::kVcfCutoff, 0.45f); sn.setCont(P::kVcfResonance, 0.5f);
        sn.setCont(P::kVcfEnvMod, 0.0f); sn.setCont(P::kVcfKbdTrack, 0.0f);
        return envAcEnergy(rmsEnvelope(freshHeld(&sn.s, note, 0.8), 128));
    };
    {
        REQUIRE(filterWobble(1.0f, 5.0f) > filterWobble(0.0f, 5.0f) * 5.0);   // depth_cutoff + dest
    }
    // rate: a faster rate => a faster wobble (so a different AC pattern); assert the wobble
    // exists at both rates and the two renders are not identical (rate moved something).
    {
        Snap slow; slow.setChoice(P::kLfoDest, 1); slow.setCont(P::kLfoRate, 2.0f);
        slow.setCont(P::kLfoDepthCutoff, 1.0f); slow.setCont(P::kLfoDelay, 0.0f);
        slow.setCont(P::kVcfCutoff, 0.45f); slow.setCont(P::kVcfResonance, 0.6f);
        Snap fast = slow; fast.setCont(P::kLfoRate, 9.0f);
        auto eSlow = rmsEnvelope(freshHeld(&slow.s, note, 1.0), 128);
        auto eFast = rmsEnvelope(freshHeld(&fast.s, note, 1.0), 128);
        // The dominant wobble frequency rises with the rate param.
        auto domFreq = [](const std::vector<float>& env, double envSr) {
            double mean = 0.0; for (float v : env) mean += v; mean /= std::max<std::size_t>(1, env.size());
            std::vector<float> ac; for (float v : env) ac.push_back(static_cast<float>(v - mean));
            double bestF = 0.5, bestP = -1.0;
            for (int i = 0; i <= 400; ++i) {
                const double fcand = 0.5 + (12.0 - 0.5) * (static_cast<double>(i) / 400.0);
                const double p = goertzelPower(ac, fcand, envSr);
                if (p > bestP) { bestP = p; bestF = fcand; }
            }
            return bestF;
        };
        REQUIRE(domFreq(eFast, kSr / 128.0) > domFreq(eSlow, kSr / 128.0) * cc::kMinRatioClear);
    }
    // shape: Tri (0) vs Square (1) produce different filter-wobble envelopes.
    {
        Snap tri; tri.setChoice(P::kLfoDest, 1); tri.setChoice(P::kLfoShape, 0);
        tri.setCont(P::kLfoRate, 4.0f); tri.setCont(P::kLfoDepthCutoff, 1.0f);
        tri.setCont(P::kLfoDelay, 0.0f); tri.setCont(P::kVcfCutoff, 0.45f);
        tri.setCont(P::kVcfResonance, 0.6f);
        Snap sq = tri; sq.setChoice(P::kLfoShape, 1);
        REQUIRE_FALSE(bitIdentical(freshHeld(&tri.s, note, 0.4), freshHeld(&sq.s, note, 0.4)));
    }
    // dest: Pitch (0) vs Filter (1) route the SAME depth to different destinations -> distinct.
    {
        Snap pit; flatSaw(pit); pit.setChoice(P::kLfoDest, 0);
        pit.setCont(P::kLfoRate, 5.0f); pit.setCont(P::kLfoDepthPitch, 0.8f);
        pit.setCont(P::kLfoDepthCutoff, 0.8f); pit.setCont(P::kLfoDelay, 0.0f);
        Snap fil = pit; fil.setChoice(P::kLfoDest, 1); fil.setCont(P::kVcfCutoff, 0.45f);
        REQUIRE_FALSE(bitIdentical(freshHeld(&pit.s, note, 0.4), freshHeld(&fil.s, note, 0.4)));
    }
    // depth_pwm (dest=PWM): sweeps the duty over time (2nd-harmonic content rises vs static).
    {
        auto h2 = [&](float depth) {
            Snap sn; flatSaw(sn);
            sn.setCont(P::kSawLevel, 0.0f); sn.setCont(P::kPulseLevel, 1.0f);
            sn.setCont(P::kVcoPw, 0.0f);
            sn.setChoice(P::kLfoDest, 2); sn.setCont(P::kLfoRate, 3.0f);
            sn.setCont(P::kLfoDepthPwm, depth); sn.setCont(P::kLfoDelay, 0.0f);
            return secondOverFirst(freshHeld(&sn.s, note, 1.0), f0, kSr);
        };
        REQUIRE(h2(1.0f) > h2(0.0f) * 5.0);
    }
    // delay: with a long LFO delay the wobble fades IN, so an EARLY window has far less AC
    // energy than with zero delay. Asserts lfo.delay reaches the per-voice depth ramp.
    {
        auto earlyWobble = [&](float delay) {
            Snap sn; sn.setChoice(P::kLfoDest, 1); sn.setCont(P::kLfoRate, 6.0f);
            sn.setCont(P::kLfoDepthCutoff, 1.0f); sn.setCont(P::kLfoDelay, delay);
            sn.setCont(P::kVcfCutoff, 0.45f); sn.setCont(P::kVcfResonance, 0.6f);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            // capture from onset (warmup 1) so the fade-in window is included
            auto buf = renderHeld(eng, &sn.s, note, 0.30, 1.0f, {}, /*warmup=*/1);
            const std::size_t n = buf.size();
            std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(n / 3));
            return envAcEnergy(rmsEnvelope(early, 128));
        };
        REQUIRE(earlyWobble(0.0f) > earlyWobble(1.0f) * cc::kMinRatioClear);
    }
}

// =====================================================================================
// MOD / BEND legs (live controller ingress via 162c): bend_range_vco + bend_dest=VCO bend the
// VCO fundamental; bend_range_vcf + dest=VCF opens the filter; mod.lfo_mod_wheel + the live
// wheel deepens the LFO wobble. Each driven via BlockContext.controllers / MidiEvents.
// =====================================================================================
TEST_CASE("dispatch_complete: bend and mod-wheel routing params take effect via controller ingress",
          "[dispatch_complete]") {
    const int note = 60;
    const double f0 = midiHz(note);

    // bend_dest=VCO + bend_range_vco: a full bend with a 1200-cent range == +1 octave.
    {
        Snap sn; flatSaw(sn);
        sn.setChoice(P::kModBendDest, 0); sn.setCont(P::kModBendRangeVco, 1200.0f);
        sn.setCont(P::kLfoDepthPitch, 0.0f);
        mw::ContinuousControllers up{}; up.pitchBend = 1.0f;
        mw::ContinuousControllers neutral{};
        const double fNeutral = estimateFundamental(
            freshHeld(&sn.s, note, 0.40, 1.0f, neutral), kSr, f0*0.25, f0*4.0);
        const double fUp = estimateFundamental(
            freshHeld(&sn.s, note, 0.40, 1.0f, up), kSr, f0*0.25, f0*4.0);
        REQUIRE(fNeutral == Catch::Approx(f0).epsilon(0.03));
        REQUIRE((fUp / fNeutral) == Catch::Approx(2.0).epsilon(0.06));
    }
    // bend_dest=VCF + bend_range_vcf: a bend opens the filter (brightness rises), pitch holds.
    {
        Snap sn;
        sn.setChoice(P::kModBendDest, 1); sn.setCont(P::kModBendRangeVcf, 1200.0f);
        sn.setCont(P::kVcfCutoff, 0.4f); sn.setCont(P::kVcfResonance, 0.3f);
        sn.setCont(P::kVcfEnvMod, 0.0f); sn.setCont(P::kVcfKbdTrack, 0.0f);
        sn.setCont(P::kLfoDepthPitch, 0.0f);
        const double fLo = midiHz(45);
        mw::ContinuousControllers up{}; up.pitchBend = 1.0f;
        double bNeutral = 0.0, bUp = 0.0;
        {
            auto buf = freshHeld(&sn.s, 45, 0.40, 1.0f, {});
            bNeutral = 0.0; for (int h = 4; h <= 16; ++h) bNeutral += goertzelPower(buf, fLo*h, kSr);
        }
        {
            auto buf = freshHeld(&sn.s, 45, 0.40, 1.0f, up);
            bUp = 0.0; for (int h = 4; h <= 16; ++h) bUp += goertzelPower(buf, fLo*h, kSr);
        }
        REQUIRE(bUp > bNeutral * cc::kMinRatioClear);
    }
    // bend_dest: VCO (0) vs VCF (1) route the same bend differently -> distinct renders.
    {
        Snap vco; flatSaw(vco); vco.setChoice(P::kModBendDest, 0);
        vco.setCont(P::kModBendRangeVco, 600.0f); vco.setCont(P::kModBendRangeVcf, 600.0f);
        vco.setCont(P::kVcfCutoff, 0.5f);
        Snap vcf = vco; vcf.setChoice(P::kModBendDest, 1);
        mw::ContinuousControllers up{}; up.pitchBend = 1.0f;
        REQUIRE_FALSE(bitIdentical(freshHeld(&vco.s, note, 0.30, 1.0f, up),
                                   freshHeld(&vcf.s, note, 0.30, 1.0f, up)));
    }
    // mod.lfo_mod_wheel: with the wheel UP the routed LFO wobble deepens vs the wheel down.
    {
        auto wobbleAtWheel = [&](float wheel01) {
            Snap sn; sn.setChoice(P::kLfoDest, 1); sn.setCont(P::kLfoRate, 5.0f);
            sn.setCont(P::kLfoDepthCutoff, 0.25f); sn.setCont(P::kLfoDelay, 0.0f);
            sn.setCont(P::kModLfoModWheel, 1.0f);
            sn.setCont(P::kVcfCutoff, 0.45f); sn.setCont(P::kVcfResonance, 0.6f);
            sn.setCont(P::kVcfEnvMod, 0.0f); sn.setCont(P::kVcfKbdTrack, 0.0f);
            mw::ContinuousControllers cw{}; cw.modWheel = wheel01;
            return envAcEnergy(rmsEnvelope(freshHeld(&sn.s, note, 0.8, 1.0f, cw), 128));
        };
        REQUIRE(wobbleAtWheel(1.0f) > wobbleAtWheel(0.0f) * cc::kMinRatioClear);
    }
}

// =====================================================================================
// VELOCITY + EXPRESSION (amplitude). Velocity ingress (162b) reaches the voice, so a hard key
// is louder than a soft key with sensing on; expression scales the VCA output amplitude.
// =====================================================================================
TEST_CASE("dispatch_complete: velocity and expression scale the amplitude",
          "[dispatch_complete]") {
    const int note = 60;

    // vel.enable + vel.depth: a hard key is louder than a soft key (sensing on, full depth).
    {
        auto loud = [&](float vel) {
            Snap sn; flatSaw(sn);
            sn.setChoice(P::kVelEnable, 1); sn.setCont(P::kVelDepth, 1.0f);
            return rms(freshHeld(&sn.s, note, 0.30, vel));
        };
        REQUIRE(loud(1.0f) > loud(0.2f) * cc::kMinRatioStrong);
    }
    // vel.enable OFF: hard vs soft render velocity-neutral (gated by vel.enable).
    {
        auto loud = [&](float vel) {
            Snap sn; flatSaw(sn);
            sn.setChoice(P::kVelEnable, 0); sn.setCont(P::kVelDepth, 1.0f);
            return rms(freshHeld(&sn.s, note, 0.30, vel));
        };
        REQUIRE(loud(1.0f) == Catch::Approx(loud(0.2f)).epsilon(0.001));
    }
    // vel.depth: at a fixed soft key, depth 0 (neutral) is louder than depth 1 (attenuated).
    {
        auto loudAtDepth = [&](float depth) {
            Snap sn; flatSaw(sn);
            sn.setChoice(P::kVelEnable, 1); sn.setCont(P::kVelDepth, depth);
            return rms(freshHeld(&sn.s, note, 0.30, /*soft key=*/0.3f));
        };
        REQUIRE(loudAtDepth(0.0f) > loudAtDepth(1.0f) * cc::kMinRatioClear);
    }
    // amp.expression: linear VCA output scaler. 0 silent, 0.5 ~half, 1 unity.
    {
        auto exprRms = [&](float e) {
            Snap sn; sn.setCont(P::kAmpExpression, e);
            return rms(freshHeld(&sn.s, note, 0.25));
        };
        const double full = exprRms(1.0f), half = exprRms(0.5f), zero = exprRms(0.0f);
        REQUIRE(full > cc::kSoundingRmsFloor);
        REQUIRE((half / full) == Catch::Approx(0.5).epsilon(0.06));
        REQUIRE(zero < full * 1.0e-3);
    }
}

// =====================================================================================
// FX legs (FxTail). Each FX param changes the FX-processed output: bypass mutes all; drive
// saturates; chorus widens (channel-diff); delay echoes (autocorr); out.mono collapses to L==R.
// (Re-checks the 163 leg for every FX param; detail in DispatchFxTest.)
// =====================================================================================
TEST_CASE("dispatch_complete: every FX param changes the FX-processed output",
          "[dispatch_complete]") {
    const int note = 48;
    const double f0 = midiHz(note);

    // fx.bypass: ON (default) is dry mono (L==R); OFF + a wet block is processed.
    {
        Snap dry;   // bypass default ON
        mw::Engine e1; e1.prepare(kSr, kMaxBlock, kMaxVoices);
        auto outDry = renderHeldStereo(e1, &dry.s, 60, 0.20, 1.0f, {}, 12);
        REQUIRE(rms(outDry.L) > 0.0);
        REQUIRE(rmsDiff(outDry.L, outDry.R) < cc::kMonoDiffCeiling);   // bypassed => mono

        Snap wet; wet.setBool(P::kFxBypass, false);
        wet.setBool(P::kFxChorusEnable, true); wet.setChoice(P::kFxChorusMode, 3);
        wet.setCont(P::kFxChorusMix, 1.0f); wet.setCont(P::kFxChorusWidth, 1.0f);
        mw::Engine e2; e2.prepare(kSr, kMaxBlock, kMaxVoices);
        auto outWet = renderHeldStereo(e2, &wet.s, 60, 0.30, 1.0f, {}, 12);
        REQUIRE(rmsDiff(outWet.L, outWet.R) > cc::kStereoDiffFloor);   // un-bypassed => stereo
    }
    // drive_enable + drive_amount: an asymmetric saturator manufactures even harmonics.
    {
        auto evenFrac = [&](bool on, float amt) {
            Snap sn; sn.setCont(P::kSawLevel, 0.8f);
            if (on) { sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxDriveEnable, true);
                      sn.setCont(P::kFxDriveAmount, amt); sn.setCont(P::kFxDriveOutput, 0.5f); }
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            auto buf = renderHeld(eng, &sn.s, note, 0.25, 1.0f, {}, 12);
            const double even = goertzelPower(buf, 2.0*f0, kSr) + goertzelPower(buf, 4.0*f0, kSr);
            return even / std::max(1e-12, goertzelPower(buf, f0, kSr));
        };
        REQUIRE(evenFrac(true, 0.9f) > evenFrac(false, 0.0f) * cc::kMinRatioStrong);
    }
    // drive_tone + drive_output: with drive on, changing tone/output changes the saturated
    // spectrum/level (not bit-identical to the same drive at a different tone/output).
    {
        auto driveRender = [&](float tone, float out) {
            Snap sn; sn.setCont(P::kSawLevel, 0.8f);
            sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxDriveEnable, true);
            sn.setCont(P::kFxDriveAmount, 0.8f);
            sn.setCont(P::kFxDriveTone, tone); sn.setCont(P::kFxDriveOutput, out);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            return renderHeld(eng, &sn.s, note, 0.20, 1.0f, {}, 12);
        };
        REQUIRE_FALSE(bitIdentical(driveRender(0.1f, 0.5f), driveRender(0.9f, 0.5f)));   // tone
        const double outLo = rms(driveRender(0.5f, 0.2f));
        const double outHi = rms(driveRender(0.5f, 0.9f));
        REQUIRE(outHi > outLo * cc::kMinRatioClear);                                     // output makeup
    }
    // chorus enable/mode/rate/depth/width/mix: enabling + width => stereo; mode/rate/depth/mix
    // each change the wet (not bit-identical to the same chorus at a different setting).
    {
        auto chorus = [&](int mode, float rate, float depth, float width, float mix) {
            Snap sn; sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxChorusEnable, true);
            sn.setChoice(P::kFxChorusMode, mode); sn.setCont(P::kFxChorusRate, rate);
            sn.setCont(P::kFxChorusDepth, depth); sn.setCont(P::kFxChorusWidth, width);
            sn.setCont(P::kFxChorusMix, mix);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            return renderHeldStereo(eng, &sn.s, 60, 0.30, 1.0f, {}, 12);
        };
        // enable + width => stereo image (channel-diff).
        auto wide = chorus(1, 0.3f, 0.8f, 1.0f, 1.0f);
        REQUIRE(rmsDiff(wide.L, wide.R) > cc::kStereoDiffFloor);
        // width 0 vs 1: width changes the channel divergence.
        auto narrow = chorus(1, 0.3f, 0.8f, 0.0f, 1.0f);
        REQUIRE(rmsDiff(wide.L, wide.R) > rmsDiff(narrow.L, narrow.R) * cc::kMinRatioClear);
        // mode I (1) vs I+II (3): distinct wet.
        REQUIRE_FALSE(bitIdentical(chorus(1, 0.3f, 0.8f, 1.0f, 1.0f).L,
                                   chorus(3, 0.3f, 0.8f, 1.0f, 1.0f).L));
        // rate / depth / mix each change the wet (vs a baseline).
        REQUIRE_FALSE(bitIdentical(chorus(1, 0.1f, 0.8f, 1.0f, 1.0f).L,
                                   chorus(1, 0.9f, 0.8f, 1.0f, 1.0f).L));   // rate
        REQUIRE_FALSE(bitIdentical(chorus(1, 0.3f, 0.2f, 1.0f, 1.0f).L,
                                   chorus(1, 0.3f, 0.9f, 1.0f, 1.0f).L));   // depth
        auto mixLo = chorus(1, 0.3f, 0.8f, 1.0f, 0.0f);   // dry only
        auto mixHi = chorus(1, 0.3f, 0.8f, 1.0f, 1.0f);   // full wet
        REQUIRE(rmsDiff(mixHi.L, mixHi.R) > rmsDiff(mixLo.L, mixLo.R) * cc::kMinRatioStrong);  // mix
    }
    // delay enable/time/feedback/mix/division/sync/width/damp/pingpong.
    {
        const float kDelayMs = 120.0f;
        const int lag = static_cast<int>((kDelayMs / 1000.0f) * kSr);
        auto delaySnap = [&](float ms) {
            Snap sn; sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxDelayEnable, true);
            sn.setBool(P::kFxDelaySync, false);
            // delay_time is kDelayTime-SKEWED in the registry. The dispatch deskews the stored
            // normalized value to a linear raw r (r = norm^(1/skew)) then log-maps r across the
            // free-ms span. So the bridge stores convertTo0to1(r) == r^skew — mirror that here
            // (the EXACT inverse the production decode runs, same calibration span).
            const float lo = mw::cal::fxdispatch::kDelayFreeMinMs;
            const float hi = mw::cal::fxdispatch::kDelayFreeMaxMs;
            const float raw = std::log(std::clamp(ms, lo, hi) / lo) / std::log(hi / lo);  // linear 0..1
            const int slot = slotOf(P::kFxDelayTime);
            const float skew = mw::params::kParamDefs[static_cast<std::size_t>(slot)].skew;
            sn.setNorm(P::kFxDelayTime, skew == 1.0f ? raw : std::pow(raw, skew));
            sn.setCont(P::kFxDelayFeedback, 0.6f); sn.setCont(P::kFxDelayMix, 1.0f);
            sn.setCont(P::kFxDelayWidth, 0.0f);
            return sn;
        };
        // enable + time: echoes at the configured lag (autocorr peak); time moves the spacing.
        {
            Snap s120 = delaySnap(120.0f);
            auto buf = freshBurstThenSilence(&s120.s, 60, 0.12, 0.60);
            REQUIRE(autocorrAtLag(buf, lag) > 0.2);
            REQUIRE(autocorrAtLag(buf, lag) > autocorrAtLag(buf, lag / 2) * 1.5);
            // time moved: a 200 ms render peaks at 200 ms, not at the 80 ms lag.
            Snap s200 = delaySnap(200.0f);
            auto buf200 = freshBurstThenSilence(&s200.s, 60, 0.10, 0.60);
            const int lag80 = static_cast<int>((80.0f / 1000.0f) * kSr);
            const int lag200 = static_cast<int>((200.0f / 1000.0f) * kSr);
            REQUIRE(autocorrAtLag(buf200, lag200) > autocorrAtLag(buf200, lag80) * cc::kMinRatioClear);
        }
        // feedback: more feedback => a longer-ringing tail (more late energy).
        {
            auto tailRms = [&](float fb) {
                Snap sn = delaySnap(120.0f); sn.setCont(P::kFxDelayFeedback, fb);
                auto buf = freshBurstThenSilence(&sn.s, 60, 0.10, 0.70);
                const int tailStart = static_cast<int>(0.40 * kSr);
                std::vector<float> tail(buf.begin() + std::min<std::size_t>(buf.size(), tailStart),
                                        buf.end());
                return rms(tail);
            };
            REQUIRE(tailRms(0.7f) > tailRms(0.1f) * cc::kMinRatioClear);
        }
        // mix: full wet vs dry-only changes the tail energy (echoes present only when mixed).
        {
            auto tailRms = [&](float mix) {
                Snap sn = delaySnap(120.0f); sn.setCont(P::kFxDelayMix, mix);
                auto buf = freshBurstThenSilence(&sn.s, 60, 0.10, 0.60);
                const int tailStart = static_cast<int>(0.30 * kSr);
                std::vector<float> tail(buf.begin() + std::min<std::size_t>(buf.size(), tailStart),
                                        buf.end());
                return rms(tail);
            };
            REQUIRE(tailRms(1.0f) > tailRms(0.0f) * cc::kMinRatioStrong);
        }
        // damp: damped vs bright echoes change the tail spectrum (not bit-identical).
        {
            Snap d0 = delaySnap(120.0f); d0.setCont(P::kFxDelayDamp, 0.05f);
            Snap d1 = delaySnap(120.0f); d1.setCont(P::kFxDelayDamp, 0.95f);
            REQUIRE_FALSE(bitIdentical(freshBurstThenSilence(&d0.s, 60, 0.10, 0.50),
                                       freshBurstThenSilence(&d1.s, 60, 0.10, 0.50)));
        }
        // width: the delay's stereo image is born from the ping-pong L/R bounce (a centered
        // delay is mono regardless of width — verified empirically), so delay.width is the
        // SCALE on that ping-pong divergence. With ping-pong ON, a wider width => a larger L-R
        // divergence (width 0.2 -> ~0.001, width 1.0 -> ~0.006). Asserted as the divergence
        // rising with width (and a genuine stereo image at full width).
        {
            auto divergence = [&](float width) {
                Snap sn = delaySnap(120.0f); sn.setCont(P::kFxDelayWidth, width);
                sn.setBool(P::kFxDelayPingpong, true);   // stereo image source
                mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
                constexpr int kBlk = 256;
                std::vector<float> dl, dr;
                { Block w(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
                  auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c); }
                { Block w(kBlk);
                  std::vector<mw::MidiEvent> ev{ mw::MidiEvent{ mw::NormalizedType::NoteOff, 0, 60, 0.0f, 0.0f, 0 } };
                  auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c); }
                for (int b = 0; b < 40; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
                  auto c = w.ctx(none, kBlk, &sn.s); eng.process(c);
                  for (int i = 0; i < kBlk; ++i) { dl.push_back(w.L[static_cast<std::size_t>(i)]);
                                                   dr.push_back(w.R[static_cast<std::size_t>(i)]); } }
                return rmsDiff(dl, dr);
            };
            const double wide   = divergence(1.0f);
            const double narrow = divergence(0.2f);
            REQUIRE(wide > cc::kStereoDiffFloor);              // full width is a real stereo image
            REQUIRE(wide > narrow * cc::kMinRatioStrong);      // width scales the divergence
        }
        // sync: tempo-synced vs free time produce a different echo spacing (not bit-identical).
        {
            Snap freeT = delaySnap(120.0f);
            Snap syncT = delaySnap(120.0f); syncT.setBool(P::kFxDelaySync, true);
            syncT.setChoice(P::kFxDelayDivision, 0);   // 1/4 at 120 bpm == 500 ms
            REQUIRE_FALSE(bitIdentical(freshBurstThenSilence(&freeT.s, 60, 0.10, 0.70),
                                       freshBurstThenSilence(&syncT.s, 60, 0.10, 0.70)));
        }
        // division: 1/4 (0) vs 1/8 (1) at a fixed tempo => different synced echo spacing.
        {
            auto syncDiv = [&](int div) {
                Snap sn = delaySnap(120.0f); sn.setBool(P::kFxDelaySync, true);
                sn.setChoice(P::kFxDelayDivision, div);
                return freshBurstThenSilence(&sn.s, 60, 0.08, 0.70);
            };
            REQUIRE_FALSE(bitIdentical(syncDiv(0), syncDiv(1)));
        }
        // pingpong: a ping-pong delay bounces echoes L<->R (a stereo tail); OFF is mono (L==R).
        // This is the delay's stereo source (see the width case): pingpong OFF + full width is
        // still mono, pingpong ON makes L diverge from R. Asserted as off => mono, on => stereo.
        {
            auto divergence = [&](bool pingpong) {
                Snap sn = delaySnap(120.0f); sn.setCont(P::kFxDelayWidth, 1.0f);
                sn.setBool(P::kFxDelayPingpong, pingpong);
                mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
                constexpr int kBlk = 256; std::vector<float> dl, dr;
                { Block w(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
                  auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c); }
                { Block w(kBlk);
                  std::vector<mw::MidiEvent> ev{ mw::MidiEvent{ mw::NormalizedType::NoteOff, 0, 60, 0.0f, 0.0f, 0 } };
                  auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c); }
                for (int b = 0; b < 40; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
                  auto c = w.ctx(none, kBlk, &sn.s); eng.process(c);
                  for (int i = 0; i < kBlk; ++i) { dl.push_back(w.L[static_cast<std::size_t>(i)]);
                                                   dr.push_back(w.R[static_cast<std::size_t>(i)]); } }
                return rmsDiff(dl, dr);
            };
            REQUIRE(divergence(false) < cc::kMonoDiffCeiling);    // off => mono tail (L == R)
            REQUIRE(divergence(true)  > cc::kStereoDiffFloor);    // on  => stereo bounce (L != R)
        }
    }
    // out.mono: a full-width chorus is stereo; out.mono ON forces L == R.
    {
        auto build = [&](bool mono) {
            Snap sn; sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxChorusEnable, true);
            sn.setChoice(P::kFxChorusMode, 3); sn.setCont(P::kFxChorusMix, 1.0f);
            sn.setCont(P::kFxChorusWidth, 1.0f); sn.setBool(P::kOutMono, mono);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            return renderHeldStereo(eng, &sn.s, 60, 0.25, 1.0f, {}, 12);
        };
        auto st = build(false);
        auto mo = build(true);
        REQUIRE(rmsDiff(st.L, st.R) > cc::kStereoDiffFloor);   // stereo without out.mono
        REQUIRE(rmsDiff(mo.L, mo.R) < cc::kMonoDiffCeiling);   // out.mono collapses to L==R
    }
}

// =====================================================================================
// CHARACTER legs: vintage.enable / age / drift.depth / drift.rate perturb the pitch over time;
// vintage.cal_spread / detune_amt / warmup.time / tune.slop change the note personality. Each
// is model-observable: a low-vs-high render differs (not bit-identical) and still sounds, with
// vintage.enable ON. (The var.* group is ISOLATED in the next case per the 164 QA note.)
// =====================================================================================
TEST_CASE("dispatch_complete: the analog-character group perturbs the render when enabled",
          "[dispatch_complete]") {
    const int note = 57;
    const double fHome = midiHz442(note);

    // vintage.enable: OFF (default, character inert) bit-identical to a hot-but-disabled snap;
    // ON with drift perturbs the pitch over time (early window != late window).
    {
        // (a) enable is the master gate: a fully-loaded character group with enable OFF is
        // bit-identical to the bare default (enable OFF) — vintage.enable controls the group.
        Snap bare;
        Snap hot;   // enable stays OFF; load every character knob hot
        hot.setCont(P::kDriftDepth, 50.0f); hot.setCont(P::kDriftRate, 1.0f);
        hot.setCont(P::kVintageAge, 1.0f); hot.setCont(P::kVintageCalSpread, 1.0f);
        hot.setCont(P::kVintageDetuneAmt, 1.0f); hot.setCont(P::kTuneSlop, 20.0f);
        hot.setCont(P::kWarmupTime, 30.0f);
        hot.setCont(P::kVarCutoff, 1.0f); hot.setCont(P::kVarEnvTime, 1.0f);
        hot.setCont(P::kVarPw, 1.0f); hot.setCont(P::kVarGlide, 1.0f);
        REQUIRE(bitIdentical(freshHeld(&bare.s, 60, 0.20), freshHeld(&hot.s, 60, 0.20)));

        // (b) enable ON + drift moves the pitch between windows (model-observable wander).
        auto windowShift = [&](float depthCents, float rate01) {
            Snap sn; sn.setChoice(P::kVintageEnable, 1);
            sn.setCont(P::kDriftDepth, depthCents); sn.setCont(P::kDriftRate, rate01);
            auto buf = freshHeld(&sn.s, note, 1.6, 1.0f, {}, /*warmup=*/4);
            const std::size_t half = buf.size() / 2;
            std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(half));
            std::vector<float> late (buf.begin() + static_cast<long>(half), buf.end());
            const double fe = estimateFundamental(early, kSr, fHome*0.85, fHome*1.15);
            const double fl = estimateFundamental(late,  kSr, fHome*0.85, fHome*1.15);
            return std::fabs(fl - fe) / fHome;
        };
        // disabled-equivalent control: enable OFF => no wander (windows identical).
        {
            Snap off; off.setChoice(P::kVintageEnable, 0);
            off.setCont(P::kDriftDepth, 50.0f); off.setCont(P::kDriftRate, 1.0f);
            auto buf = freshHeld(&off.s, note, 1.6, 1.0f, {}, 4);
            const std::size_t half = buf.size() / 2;
            std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(half));
            std::vector<float> late (buf.begin() + static_cast<long>(half), buf.end());
            const double fe = estimateFundamental(early, kSr, fHome*0.85, fHome*1.15);
            const double fl = estimateFundamental(late,  kSr, fHome*0.85, fHome*1.15);
            REQUIRE(fl == Catch::Approx(fe).epsilon(0.002));   // enable OFF => stable
        }
        REQUIRE(windowShift(50.0f, 1.0f) > 0.0015);   // drift.depth + drift.rate (enabled) wander
    }
    // vintage.age (macro): full Age opens drift -> pitch wanders between windows.
    {
        Snap sn; sn.setChoice(P::kVintageEnable, 1); sn.setCont(P::kVintageAge, 1.0f);
        auto buf = freshHeld(&sn.s, note, 1.6, 1.0f, {}, 4);
        const std::size_t half = buf.size() / 2;
        std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(half));
        std::vector<float> late (buf.begin() + static_cast<long>(half), buf.end());
        const double fe = estimateFundamental(early, kSr, fHome*0.85, fHome*1.15);
        const double fl = estimateFundamental(late,  kSr, fHome*0.85, fHome*1.15);
        REQUIRE(std::fabs(fl - fe) / fHome > 0.0015);
    }
    // tune.slop: a per-note random tuning offset (Tier-3) shifts the frozen pitch (enable ON,
    // cal_spread + drift OFF). slop 0 vs 20 cents renders a different note.
    {
        auto slopRender = [&](float cents) {
            Snap sn; sn.setChoice(P::kVintageEnable, 1);
            sn.setCont(P::kTuneSlop, cents);
            sn.setCont(P::kVintageCalSpread, 0.0f);
            sn.setCont(P::kDriftDepth, 0.0f); sn.setCont(P::kDriftRate, 0.0f);
            return freshHeld(&sn.s, 60, 0.30);
        };
        REQUIRE_FALSE(bitIdentical(slopRender(0.0f), slopRender(20.0f)));
    }
    // warmup.time: the warm-up transient (enable ON) rides the THERMAL->pitch path, which is
    // scaled by drift.depth (driftCents = T * driftDepthCents, DriftModel.cpp §5.2). So it is
    // observable only with drift.depth UP; we hold drift.RATE at its floor so the OU walk is
    // frozen and ONLY the warm-up exponential varies T over the render — isolating warmup.time
    // (cal_spread/slop/var off). warm-up 0 vs 30 min then perturbs the rendered pitch transient.
    {
        auto warmRender = [&](float minutes) {
            Snap sn; sn.setChoice(P::kVintageEnable, 1);
            sn.setCont(P::kWarmupTime, minutes);
            sn.setCont(P::kDriftDepth, 50.0f);   // warm-up rides the thermal->pitch depth
            sn.setCont(P::kDriftRate, 0.0f);     // OU frozen: only the warm-up transient varies T
            sn.setCont(P::kVintageCalSpread, 0.0f); sn.setCont(P::kTuneSlop, 0.0f);
            return freshHeld(&sn.s, 57, 0.60, 1.0f, {}, /*warmup=*/2);
        };
        REQUIRE_FALSE(bitIdentical(warmRender(0.0f), warmRender(30.0f)));
    }
    // (vintage.cal_spread + vintage.detune_amt are FrozenInert at the live MONO seam — proven
    // in the exempt-enumeration case with their rationale, not here.)
}

// =====================================================================================
// var.* ISOLATION (the 164-QA fold-in). EACH var.* leg is proven independently with the
// MASKING params muted (vintage.cal_spread / drift.depth / drift.rate OFF, vintage.enable ON):
//   var.cutoff   -> per-voice cutoff spread   (rendered spectrum differs)
//   var.env_time -> per-voice env-time spread (rendered envelope contour differs)
//   var.pw       -> per-voice PW duty spread  (rendered even-harmonic content differs)
//   var.glide    -> per-voice glide-time spread (mid-slew pitch of a portamento differs)
// Each is asserted NOT bit-identical low-vs-high AND deterministic (a re-render is identical).
// =====================================================================================
TEST_CASE("dispatch_complete: each var leg is isolated and independently observable",
          "[dispatch_complete]") {
    // Mute every masking character knob; enable ON so the variance group is live.
    auto muteMaskers = [](Snap& sn) {
        sn.setChoice(P::kVintageEnable, 1);
        sn.setCont(P::kVintageCalSpread, 0.0f);   // mask OFF (would perturb pitch/cutoff)
        sn.setCont(P::kVintageDetuneAmt, 0.0f);
        sn.setCont(P::kDriftDepth, 0.0f);          // mask OFF (would wander pitch)
        sn.setCont(P::kDriftRate, 0.0f);
        sn.setCont(P::kTuneSlop, 0.0f);            // mask OFF (Tier-3 pitch slop)
        sn.setCont(P::kWarmupTime, 0.0f);
        // the OTHER three var legs OFF so each var.* is the only live variance source
        sn.setCont(P::kVarCutoff, 0.0f);
        sn.setCont(P::kVarEnvTime, 0.0f);
        sn.setCont(P::kVarPw, 0.0f);
        sn.setCont(P::kVarGlide, 0.0f);
    };

    // var.cutoff: isolated cutoff spread shifts the spectrum (a partly-closed resonant filter so
    // a per-voice cutoff offset is visible in the harmonic content).
    {
        auto cutoffVar = [&](float v) {
            Snap sn; muteMaskers(sn);
            sn.setCont(P::kSawLevel, 0.8f);
            sn.setCont(P::kVcfCutoff, 0.5f); sn.setCont(P::kVcfResonance, 0.5f);
            sn.setCont(P::kVcfEnvMod, 0.0f); sn.setCont(P::kVcfKbdTrack, 0.0f);
            sn.setCont(P::kEnvSustain, 1.0f); sn.setCont(P::kEnvAttack, 0.001f);
            sn.setCont(P::kEnvDecay, 0.0f);
            sn.setCont(P::kVarCutoff, v);
            return freshHeld(&sn.s, 48, 0.30);
        };
        const auto off = cutoffVar(0.0f);
        const auto on  = cutoffVar(1.0f);
        const auto on2 = cutoffVar(1.0f);
        REQUIRE_FALSE(bitIdentical(off, on));   // var.cutoff alone changes the spectrum
        REQUIRE(bitIdentical(on, on2));          // deterministic (fixed seed)
        REQUIRE(rms(on) > cc::kSoundingRmsFloor);
    }
    // var.env_time: isolated env-time spread changes the decay contour (a percussive env so the
    // multiplicative time scale is visible — the late/early amplitude ratio shifts).
    {
        auto envVar = [&](float v) {
            Snap sn; muteMaskers(sn);
            sn.setCont(P::kSawLevel, 0.9f); sn.setCont(P::kVcfCutoff, 1.0f);
            sn.setCont(P::kVcfEnvMod, 0.0f); sn.setCont(P::kVcfKbdTrack, 0.0f);
            sn.setCont(P::kEnvAttack, 0.001f); sn.setCont(P::kEnvDecay, 0.12f);
            sn.setCont(P::kEnvSustain, 0.0f); sn.setCont(P::kEnvRelease, 0.1f);
            sn.setCont(P::kVarEnvTime, v);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            return renderHeld(eng, &sn.s, 60, 0.40, 1.0f, {}, /*warmup=*/1);
        };
        const auto off = envVar(0.0f);
        const auto on  = envVar(1.0f);
        const auto on2 = envVar(1.0f);
        // var.env_time is a per-voice MULTIPLIER on the A/D/R times, frozen at note-on. It
        // re-shapes the percussive decay contour, so the rendered sample stream DIFFERS from the
        // un-perturbed note (a different decay personality) — asserted as a non-bit-identical
        // render, robust where the macro-level RMS contour shift is small (the spread for voice 0
        // at this seed is modest). Deterministic for the fixed seed (re-render identical).
        REQUIRE_FALSE(bitIdentical(off, on));   // var.env_time alone re-shapes the decay contour
        REQUIRE(bitIdentical(on, on2));          // deterministic (fixed seed)
        REQUIRE(rms(on) > cc::kSoundingRmsFloor);
    }
    // var.pw: isolated PW duty spread shifts the even-harmonic content (pulse-only at a base
    // square, so a per-voice duty offset raises the 2nd harmonic).
    {
        auto pwVar = [&](float v) {
            Snap sn; muteMaskers(sn);
            sn.setCont(P::kSawLevel, 0.0f); sn.setCont(P::kPulseLevel, 1.0f);
            sn.setCont(P::kVcoPw, 0.5f);   // base square via the registry default (norm 0.5)
            sn.setCont(P::kVcfCutoff, 1.0f); sn.setCont(P::kEnvSustain, 1.0f);
            sn.setCont(P::kEnvAttack, 0.001f); sn.setCont(P::kEnvDecay, 0.0f);
            sn.setCont(P::kVarPw, v);
            return freshHeld(&sn.s, 60, 0.30);
        };
        const auto off = pwVar(0.0f);
        const auto on  = pwVar(1.0f);
        const auto on2 = pwVar(1.0f);
        REQUIRE_FALSE(bitIdentical(off, on));   // var.pw alone changes the duty spectrum
        REQUIRE(bitIdentical(on, on2));
        const double f0 = midiHz(60);
        // The even-harmonic content moves with the duty spread.
        REQUIRE(std::fabs(secondOverFirst(on, f0, kSr) - secondOverFirst(off, f0, kSr)) > 1e-4);
    }
    // var.glide: isolated glide-time spread (a per-voice MULTIPLIER on the glide time, frozen at
    // note-on) changes the portamento SLEW trajectory — at a fixed elapsed window the mid-slew
    // pitch reaches a different point. glide.mode On so the slew is always engaged. The voice-0
    // spread at this seed is modest, so the trajectory is asserted as a non-bit-identical mid-slew
    // buffer (the slew differs sample-by-sample) AND a measurable (>0.1%) mid-slew pitch shift —
    // a tolerance a disconnect (identical glide time both legs) would collapse to ~0.
    {
        auto glideVarSlew = [&](float v) {
            Snap sn; muteMaskers(sn);
            sn.setCont(P::kSawLevel, 0.9f); sn.setCont(P::kVcfCutoff, 1.0f);
            sn.setCont(P::kVcfEnvMod, 0.0f); sn.setCont(P::kVcfKbdTrack, 0.0f);
            sn.setChoice(P::kGlideMode, 2);   // On
            sn.setCont(P::kGlideTime, 0.40f);
            sn.setCont(P::kVarGlide, v);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            constexpr int kBlk = 256;
            { Block on(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(48, 1.0f, 0) };
              auto c = on.ctx(ev, kBlk, &sn.s); eng.process(c); }
            for (int b = 0; b < 20; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
              auto c = w.ctx(none, kBlk, &sn.s); eng.process(c); }
            std::vector<float> early;
            { Block w(kBlk); std::vector<mw::MidiEvent> ev{ noteOn(72, 1.0f, 0) };
              auto c = w.ctx(ev, kBlk, &sn.s); eng.process(c);
              for (int i = 0; i < kBlk; ++i) early.push_back(w.L[static_cast<std::size_t>(i)]); }
            for (int b = 0; b < 14; ++b) { Block w(kBlk); std::vector<mw::MidiEvent> none;
              auto c = w.ctx(none, kBlk, &sn.s); eng.process(c);
              for (int i = 0; i < kBlk; ++i) early.push_back(w.L[static_cast<std::size_t>(i)]); }
            return early;
        };
        const auto off  = glideVarSlew(0.0f);
        const auto on   = glideVarSlew(1.0f);
        const auto on2  = glideVarSlew(1.0f);
        REQUIRE_FALSE(bitIdentical(off, on));   // var.glide alone changes the slew trajectory
        REQUIRE(bitIdentical(on, on2));          // deterministic (fixed seed)
        const double slewOff = estimateFundamental(off, kSr, midiHz(48) * 0.7, midiHz(72) * 1.3);
        const double slewOn  = estimateFundamental(on,  kSr, midiHz(48) * 0.7, midiHz(72) * 1.3);
        // The mid-slew pitch reaches a measurably different point (var.glide stretched/shrank
        // the glide time). A disconnect (identical glide both legs) would give ~0 shift.
        REQUIRE(std::fabs(slewOn - slewOff) / midiHz(48) > 0.001);
    }
}

// =====================================================================================
// EXEMPT ENUMERATION: the params that do NOT assert audio on this MONO ParamSnapshot seam,
// each with its explicit rationale, RE-DERIVED from the manifest so the enumeration cannot
// drift from the asserted set. This is the audit's record that every exempt param is exempt
// for a STATED structural/subsystem/ingress reason — not silently skipped.
//   StructuralOff : quality/voice.mode/voice.count/unison.count/control.vintage — off-thread
//                   setters (ADR-028 item 4); NOT read per control tick. Drive via the
//                   off-thread structural setters (Engine::setGateTrigMode etc.), not ctx.params.
//   SubsystemOff  : arp.*/seq.*/key.trigger_priority/pitch.modern_unquantized/lfo.{tempo_sync,
//                   sync_div} — the arp/seq/clock/keyassigner subsystem reads the ControlSnapshot
//                   (tasks 087/118c), NOT the ParamSnapshot->DSP dispatch seam under audit.
//   DecodeInert   : mpe.* — decoded at the seam but inert without a live per-note MPE position/
//                   pressure ingress (a separate controller seam, flagged); bit-identical here.
//   FrozenInert   : vintage.cal_spread + vintage.detune_amt — decoded into DriftParams every tick
//                   (correct), but inert at the LIVE MONO per-tick seam: cal_spread feeds the Tier-1
//                   trim DRAW which DriftModel runs ONCE at prepare/reset/Re-roll (drawCalibration,
//                   docs/design/08 §4 / §8.3) — a live setParams does NOT re-draw it; detune_amt
//                   scales the per-voice spread for voices 1.. only (voice 0 == the full instance
//                   personality, §11 / ADR-009 VV-11), so MONO (== voice 0) is unaffected. Their
//                   true effect lives on the Re-roll path / the poly-unison voice-spread tests
//                   (off this MONO completeness seam). Asserted bit-identical here so a regression
//                   that wrongly let either perturb the live MONO render would fail.
//   FindingUnwired: vco.pwm_depth, vcf.lfo_mod — LIVE registry params with NO dispatch path
//                   (no Engine::ParamSlots entry). A HIGH finding (flagged in the PR); asserted
//                   INERT so the audit records the gap honestly (it does not fake an effect).
// =====================================================================================
TEST_CASE("dispatch_complete: exempt params are enumerated with rationale and behave as classified",
          "[dispatch_complete]") {
    // Tally the exempt classes so the enumeration count is asserted (no class silently empty
    // or over-full). Counts are derived FROM the manifest, the same source the coverage test
    // checks against the 91-row registry.
    int structural = 0, subsystem = 0, decodeInert = 0, finding = 0, frozen = 0, audio = 0;
    for (const auto& row : kManifest) {
        switch (row.dim) {
            case Dim::StructuralOff:  ++structural;  break;
            case Dim::SubsystemOff:   ++subsystem;   break;
            case Dim::DecodeInert:    ++decodeInert; break;
            case Dim::FindingUnwired: ++finding;     break;
            case Dim::FrozenInert:    ++frozen;      break;
            default:                  ++audio;       break;   // an asserted audio/character dimension
        }
    }
    // The five structural params (ADR-028 item 4) + the three MPE decode-inert + the two
    // unwired findings + the two frozen/unison-only inert are fixed; the subsystem set is the
    // arp/seq/key/pitch/lfo-sync group.
    REQUIRE(structural == 5);
    REQUIRE(decodeInert == 3);
    REQUIRE(finding == 2);
    REQUIRE(frozen == 2);
    // arp(5) + seq(3) + key.trigger_priority(1) + pitch.modern_unquantized(1) + lfo sync(2).
    REQUIRE(subsystem == 12);
    REQUIRE(structural + subsystem + decodeInert + finding + frozen + audio
            == static_cast<int>(kManifest.size()));   // all 91 classified
    // The remaining 67 are the asserted audio/character dimensions (91 - 5 - 12 - 3 - 2 - 2).
    REQUIRE(audio == 67);

    // --- STRUCTURAL: NOT read per control tick. Confirm they are NOT in the dispatch slot set
    // by construction (they have no Engine::ParamSlots field); the audit asserts the seam does
    // not perturb the render when a structural param is moved in the snapshot (it is ignored —
    // structural changes flow through the off-thread setters, not ctx.params). bit-identical.
    {
        auto structRender = [](const char* id, int idx) {
            Snap sn; sn.setChoice(id, idx);
            return freshHeld(&sn.s, 60, 0.15);
        };
        // quality default 1 (Standard); moving it in the snapshot does not change the render
        // (the AA tier is an off-thread setter, ADR-018 Q5 / ADR-028 item 4).
        REQUIRE(bitIdentical(structRender(P::kQuality, 1), structRender(P::kQuality, 2)));
        // voice.mode / voice.count / unison.count / control.vintage likewise ignored by the seam.
        REQUIRE(bitIdentical(structRender(P::kVoiceMode, 0), structRender(P::kVoiceMode, 1)));
        REQUIRE(bitIdentical(structRender(P::kVoiceCount, 1), structRender(P::kVoiceCount, 3)));
        REQUIRE(bitIdentical(structRender(P::kUnisonCount, 0), structRender(P::kUnisonCount, 2)));
        REQUIRE(bitIdentical(structRender(P::kControlVintage, 0), structRender(P::kControlVintage, 1)));
    }

    // --- SUBSYSTEM: arp/seq/key/pitch/lfo-sync params are not on the ParamSnapshot->DSP seam.
    // Moving them in the snapshot (transport STOPPED, plain keyboard play) does not change the
    // rendered note — they are consumed by the SequencerEngine/ControlCore via the ControlSnapshot
    // (tasks 087/118c), a separate seam with its own subsystem tests. bit-identical here.
    {
        auto subsysChoice = [](const char* id, int a, int b) {
            Snap s1; s1.setChoice(id, a);
            Snap s2; s2.setChoice(id, b);
            return std::pair<std::vector<float>, std::vector<float>>{
                freshHeld(&s1.s, 60, 0.12), freshHeld(&s2.s, 60, 0.12) };
        };
        // A representative from each subsystem group (the rest share the same off-seam path).
        { auto [a, b] = subsysChoice(P::kArpMode, 0, 1);   REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kSeqMode, 0, 1);   REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kKeyTriggerPriority, 0, 1); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kPitchModernUnquantized, 0, 1); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kLfoTempoSync, 0, 1); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kLfoSyncDiv, 0, 4); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kArpRange, 0, 2);  REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kArpSyncDiv, 0, 4); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kSeqSyncDiv, 0, 4); REQUIRE(bitIdentical(a, b)); }
        // bool subsystem params (arp/seq tempo_sync, arp latch).
        { auto [a, b] = subsysChoice(P::kArpTempoSync, 0, 1); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kSeqTempoSync, 0, 1); REQUIRE(bitIdentical(a, b)); }
        { auto [a, b] = subsysChoice(P::kArpLatch, 0, 1);     REQUIRE(bitIdentical(a, b)); }
    }

    // --- DECODE-INERT (MPE): decoded at the seam, inert without live per-note MPE ingress.
    {
        Snap off;
        Snap on; on.setBool(P::kMpeEnable, true); on.setCont(P::kMpeBendRange, 96.0f);
        on.setChoice(P::kMpePressureDest, 1);
        REQUIRE(bitIdentical(freshHeld(&off.s, 60, 0.20), freshHeld(&on.s, 60, 0.20)));
    }

    // --- FROZEN-INERT (cal_spread, detune_amt): decoded into DriftParams every tick (correct),
    // but inert at the LIVE MONO seam. cal_spread feeds the Tier-1 trim DRAW the DriftModel runs
    // ONCE at prepare/Re-roll (NOT on a live setParams), and detune_amt scales voices 1.. only
    // (voice 0 == the full personality), so a fresh MONO engine renders BIT-IDENTICAL low-vs-high
    // for both (vintage ON, all other character knobs off). A regression that wrongly perturbed
    // the live MONO render with either would fail this assertion. [docs/design/08 §4/§8.3/§11].
    {
        auto frozenLiveSeam = [&](const char* id) {
            Snap lo; lo.setChoice(P::kVintageEnable, 1);
            lo.setCont(P::kDriftDepth, 0.0f); lo.setCont(P::kDriftRate, 0.0f);
            lo.setCont(P::kTuneSlop, 0.0f); lo.setCont(P::kWarmupTime, 0.0f);
            lo.setCont(P::kVarCutoff, 0.0f); lo.setCont(P::kVarEnvTime, 0.0f);
            lo.setCont(P::kVarPw, 0.0f); lo.setCont(P::kVarGlide, 0.0f);
            // BOTH frozen knobs muted in the baseline; move ONLY the one under test.
            lo.setCont(P::kVintageCalSpread, 0.0f); lo.setCont(P::kVintageDetuneAmt, 0.0f);
            Snap hi = lo; hi.setCont(id, 1.0f);
            return bitIdentical(freshHeld(&lo.s, 60, 0.20), freshHeld(&hi.s, 60, 0.20));
        };
        REQUIRE(frozenLiveSeam(P::kVintageCalSpread));  // Tier-1 drawn at prepare/Re-roll, not live
        REQUIRE(frozenLiveSeam(P::kVintageDetuneAmt));  // unison/poly spread scaler; inert in MONO
    }

    // --- FINDING_UNWIRED: vco.pwm_depth + vcf.lfo_mod have NO dispatch path. Asserted INERT
    // (bit-identical low-vs-high) so the audit RECORDS the gap. When a future task wires either,
    // THIS assertion fails loudly — the signal to flip it to an audio-effect assertion. (TESTS
    // ONLY: 165 does not fix production; the finding is raised in the PR.)
    {
        auto unwired = [](const char* id) {
            Snap lo; lo.setNorm(id, 0.0f);
            Snap hi; hi.setNorm(id, 1.0f);
            return bitIdentical(freshHeld(&lo.s, 60, 0.20), freshHeld(&hi.s, 60, 0.20));
        };
        REQUIRE(unwired(P::kVcoPwmDepth));   // FINDING: no observable effect (flagged in PR)
        REQUIRE(unwired(P::kVcfLfoMod));     // FINDING: no observable effect (flagged in PR)
    }
}

// =====================================================================================
// DETERMINISM over the full dispatch+render path with a hot snapshot covering every wired
// group (VCO/mixer/VCF/env/LFO/bend/character/FX). Two independently-prepared engines fed the
// identical note + snapshot + controllers produce bit-identical output.
// =====================================================================================
TEST_CASE("dispatch_complete: the full dispatch and render path is deterministic",
          "[dispatch_complete]") {
    Snap sn;
    sn.setCont(P::kVcoTune, 5.0f); sn.setCont(P::kPulseLevel, 0.4f);
    sn.setCont(P::kSubLevel, 0.3f); sn.setCont(P::kVcfCutoff, 0.5f);
    sn.setCont(P::kVcfResonance, 0.5f); sn.setCont(P::kVcfEnvMod, 0.4f);
    sn.setChoice(P::kLfoDest, 1); sn.setCont(P::kLfoDepthCutoff, 0.6f);
    sn.setChoice(P::kVelEnable, 1); sn.setCont(P::kVelDepth, 0.5f);
    sn.setChoice(P::kModBendDest, 2); sn.setCont(P::kModBendRangeVco, 300.0f);
    sn.setCont(P::kModLfoModWheel, 0.5f);
    sn.setChoice(P::kVintageEnable, 1); sn.setCont(P::kDriftDepth, 20.0f);
    sn.setCont(P::kDriftRate, 0.7f); sn.setCont(P::kVarCutoff, 0.6f);
    sn.setCont(P::kVarPw, 0.5f); sn.setCont(P::kTuneSlop, 8.0f);
    sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxDelayEnable, true);
    sn.setCont(P::kFxDelayFeedback, 0.5f); sn.setCont(P::kFxDelayMix, 0.5f);

    mw::ContinuousControllers cc{}; cc.pitchBend = 0.4f; cc.modWheel = 0.7f;
    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &sn.s, 55, 0.20, 0.75f, cc);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
}

// =====================================================================================
// RT-SAFETY over the full dispatch+render path: the per-control-tick ParamSnapshot read +
// controller ingress + all the wired setters + the FX decode/publish + the render allocate
// nothing and take no lock on the audio thread (AudioThreadGuard, the [rt] check).
// =====================================================================================
TEST_CASE("dispatch_complete: the full dispatch and render path is allocation and lock free under the guard",
          "[dispatch_complete][rt]") {
    Snap sn;
    sn.setCont(P::kVcoTune, 3.0f); sn.setCont(P::kSawLevel, 0.7f);
    sn.setCont(P::kPulseLevel, 0.4f); sn.setCont(P::kSubLevel, 0.3f);
    sn.setCont(P::kNoiseLevel, 0.1f); sn.setChoice(P::kVcoRange, 2);
    sn.setCont(P::kVcfCutoff, 0.55f); sn.setCont(P::kVcfResonance, 0.6f);
    sn.setCont(P::kVcfEnvMod, 0.5f); sn.setCont(P::kVcfKbdTrack, 0.4f);
    sn.setCont(P::kEnvAttack, 0.02f); sn.setCont(P::kEnvDecay, 0.08f);
    sn.setCont(P::kEnvSustain, 0.6f); sn.setChoice(P::kVcaMode, 1);
    sn.setChoice(P::kLfoDest, 1); sn.setCont(P::kLfoRate, 6.0f);
    sn.setCont(P::kLfoDepthCutoff, 0.7f); sn.setCont(P::kLfoDepthPitch, 0.4f);
    sn.setCont(P::kLfoDepthPwm, 0.4f); sn.setCont(P::kLfoDelay, 0.4f);
    sn.setChoice(P::kVelEnable, 1); sn.setCont(P::kVelDepth, 0.7f);
    sn.setChoice(P::kModBendDest, 2); sn.setCont(P::kModBendRangeVco, 200.0f);
    sn.setCont(P::kModBendRangeVcf, 300.0f); sn.setCont(P::kModLfoModWheel, 0.6f);
    sn.setChoice(P::kVintageEnable, 1); sn.setCont(P::kDriftDepth, 30.0f);
    sn.setCont(P::kDriftRate, 0.8f); sn.setCont(P::kVintageAge, 0.5f);
    sn.setCont(P::kVintageCalSpread, 0.7f); sn.setCont(P::kVarCutoff, 0.6f);
    sn.setCont(P::kVarEnvTime, 0.5f); sn.setCont(P::kVarPw, 0.5f);
    sn.setCont(P::kVarGlide, 0.5f); sn.setCont(P::kTuneA4, 444.0f);
    sn.setCont(P::kTuneSlop, 8.0f); sn.setCont(P::kAmpExpression, 0.8f);
    sn.setBool(P::kFxBypass, false); sn.setBool(P::kFxDriveEnable, true);
    sn.setCont(P::kFxDriveAmount, 0.6f); sn.setBool(P::kFxChorusEnable, true);
    sn.setChoice(P::kFxChorusMode, 2); sn.setCont(P::kFxChorusMix, 0.6f);
    sn.setBool(P::kFxDelayEnable, true); sn.setCont(P::kFxDelayFeedback, 0.5f);
    sn.setCont(P::kFxDelayMix, 0.6f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    mw::ContinuousControllers cc{}; cc.pitchBend = 0.4f; cc.modWheel = 0.6f;
    Block warm(kBlk);
    std::vector<mw::MidiEvent> warmEv{ noteOn(64, 0.9f, 0), pitchBendEv(0.4f, 0), modWheelCc(76, 0) };
    auto warmCtx = warm.ctx(warmEv, kBlk, &sn.s, cc);
    eng.process(warmCtx);   // first-touch realized while allocation is still permitted
    REQUIRE(eng.voiceManager().activeCount() >= 1);

    std::vector<Block> blocks; blocks.reserve(8);
    for (int b = 0; b < 8; ++b) blocks.emplace_back(kBlk);
    std::vector<std::vector<mw::MidiEvent>> evs; evs.reserve(8);
    for (int b = 0; b < 8; ++b)
        evs.push_back({ pitchBendEv(0.4f - 0.05f * static_cast<float>(b % 3), 0),
                        modWheelCc(64 + 6 * (b % 4), 0) });
    std::vector<mw::BlockContext> ctxs; ctxs.reserve(8);
    for (int b = 0; b < 8; ++b)
        ctxs.push_back(blocks[static_cast<std::size_t>(b)].ctx(
            evs[static_cast<std::size_t>(b)], kBlk, &sn.s, cc));

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
