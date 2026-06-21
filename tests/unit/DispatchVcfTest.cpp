// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchVcfTest.cpp — the AUDIO-EFFECT acceptance suite for the
// control-dispatch seam's VCF + Envelope + VCA leg (task 161, extending the ADR-028
// keystone built by task 160).
//
// Test-case display names ALL begin with "dispatch_vcf" so
// `ctest -R dispatch_vcf --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[dispatch_vcf]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (the assertion class the audit found MISSING — real audible effect,
// not "non-silent/deterministic"). Every criterion is measured on RENDERED OUTPUT via a
// Goertzel single-bin power estimator + broadband/amplitude probes:
//   * vcf.cutoff sweeps the filter: a low cutoff attenuates the saw's high harmonics far
//     more than a high cutoff (the high/low harmonic-energy ratio rises with cutoff);
//   * high vcf.resonance self-oscillates: with the mixer muted, reso=1 produces a sine
//     where reso=0 is silent;
//   * vcf.env_mod opens the filter WITH the envelope: a percussive env + env_mod produces
//     a bright onset that decays darker over time (early window brighter than late);
//   * vcf.kbd_track raises cutoff with note pitch: a high note is brighter than a low note
//     at the SAME cutoff when tracking is on, and not when it is off;
//   * the ADSR params change the envelope contour: a long attack ramps in slowly (early
//     amplitude << late), a short attack is full almost immediately; sustain level audible;
//   * vca.level scales the output amplitude; vca.mode ENV vs GATE changes the amp contour;
//   * determinism preserved + the dispatch+render path is RT-safe (AudioThreadGuard, [rt]).
//
// The Engine consumes the seam's immutable mw::ParamSnapshot once per control tick; this
// file builds that POD directly (the off-thread bridge's job in the real shell, including
// the JUCE NormalisableRange skew on cutoff/env-time so the engine de-skews the SAME way)
// and reads the audible result, so it links mwcore ONLY (no JUCE) [docs/design/00 §5.4].

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

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- registry-index lookup: map a parameter string-ID to its kParamDefs slot. The SAME
// registry-index keying the ParamSnapshot uses. -------------------------------------
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

// Build a ParamSnapshot pre-loaded with every live param's DEFAULT, in normalized [0,1]
// form EXACTLY as the bridge emits it: convertTo0to1(default). For a JUCE NormalisableRange
// with skew s (non-symmetric), convertTo0to1(v) == ((v-min)/span)^s — so this test applies
// the registry skew when normalizing, and the engine's dispatch inverts it (v = min +
// span*norm^(1/s)). For linear (skew==1) params this collapses to the plain proportion.
struct Snap {
    mw::ParamSnapshot s{};

    // Forward map a CONTINUOUS engineering value to the bridge's normalized [0,1] using the
    // registry skew (the exact convertTo0to1 the real bridge runs). norm = proportion^skew.
    static float normalizeCont(const mw::params::ParamDef& d, float value) noexcept {
        const float span = d.maxValue - d.minValue;
        if (span <= 0.0f) return 0.0f;
        const float v = std::clamp(value, d.minValue, d.maxValue);
        const float prop = (v - d.minValue) / span;
        return d.skew == 1.0f ? prop : std::pow(prop, d.skew);
    }

    Snap() noexcept {
        for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
            const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
            if (d.type == mw::params::ParamType::Continuous) {
                s.normalizedValues[static_cast<std::size_t>(i)] = normalizeCont(d, d.defaultValue);
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
        s.normalizedValues[static_cast<std::size_t>(i)] = normalizeCont(d, value);
    }

    // Set a CHOICE/BOOL param by its option index.
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
mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOff;
    e.noteId = static_cast<std::int16_t>(note);
    e.data0 = static_cast<float>(note);   // task 118e: note number = pitch (read from data0)
    e.value = 0.0f;
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

// Render `seconds` of sustained audio for a held note, returning the mono (L) buffer. Drives
// the snapshot every block so the dispatch applies; warms up so the env/glide settle.
std::vector<float> renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap,
                              int midiNote, double seconds, int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, 1.0f, 0) };
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

// Render the FULL note from key-down, capturing every sample from the first block (so the
// envelope ONSET is included — needed for the attack/env-mod contour tests). No warmup.
std::vector<float> renderFromOnset(mw::Engine& eng, const mw::ParamSnapshot* snap,
                                   int midiNote, double seconds) {
    constexpr int kBlk = 128;
    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total) + kBlk);
    int rendered = 0;
    bool first = true;
    while (rendered < total) {
        Block w(kBlk);
        std::vector<mw::MidiEvent> ev;
        if (first) { ev.push_back(noteOn(midiNote, 1.0f, 0)); first = false; }
        auto c = w.ctx(ev, kBlk, snap);
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

double rmsRange(const std::vector<float>& x, std::size_t lo, std::size_t hi) noexcept {
    hi = std::min(hi, x.size());
    if (lo >= hi) return 0.0;
    double acc = 0.0;
    for (std::size_t i = lo; i < hi; ++i) acc += static_cast<double>(x[i]) * x[i];
    return std::sqrt(acc / static_cast<double>(hi - lo));
}

double peak(const std::vector<float>& x) noexcept {
    double p = 0.0; for (float v : x) p = std::max(p, std::fabs((double) v)); return p;
}

// Absolute high-band energy: the summed Goertzel power of the upper harmonics (h8..h16) of
// the played note. A saw through a LOW cutoff loses these highs (the 4-pole ladder rolls
// them off); opening the cutoff lets progressively more of them through, so this rises
// monotonically as the filter opens. An ABSOLUTE measure (not a ratio) is robust where a
// very low cutoff also attenuates the fundamental — exactly the regime a wide cutoff span
// reaches (the SH-101 cutoff sweeps from near-DC to ~20 kHz).
double highBandEnergy(const std::vector<float>& x, double f0, double sr) noexcept {
    double hi = 0.0;
    for (int h = 8; h <= 16; ++h) hi += goertzelPower(x, f0 * h, sr);
    return hi;
}

// A saw-only mixer through a fully-open filter at full VCA gives a strong tone; this
// generous ceiling catches a blow-up.
constexpr float kSaneBound = 64.0f;

// A snapshot that DEFEATS the envelope contour for the pure-cutoff tests: a near-instant
// attack and full sustain so the steady-state amplitude is flat and the filter (not the
// envelope) governs the measured spectrum. Saw-only, default open VCA.
void makeFlatSawSnap(Snap& snap) noexcept {
    using namespace mw::params::ids;
    snap.setCont(kSawLevel, 0.9f);
    snap.setCont(kPulseLevel, 0.0f);
    snap.setCont(kSubLevel, 0.0f);
    snap.setCont(kNoiseLevel, 0.0f);
    snap.setCont(kEnvAttack, 0.0f);     // instant
    snap.setCont(kEnvDecay, 0.0f);
    snap.setCont(kEnvSustain, 1.0f);    // hold full
    snap.setCont(kEnvRelease, 0.05f);
    snap.setCont(kVcfEnvMod, 0.0f);     // no env->cutoff for the pure-cutoff tests
    snap.setCont(kVcfKbdTrack, 0.0f);
    snap.setCont(kVcfResonance, 0.0f);
    snap.setCont(kVcaLevel, 0.8f);
}

} // namespace

// ===========================================================================
// CUTOFF: the cutoff param sweeps the filter. A saw held at a fixed low note carries more
// high-harmonic energy as the cutoff opens — the filter actually moves. Measured as the
// ABSOLUTE upper-harmonic energy (robust across the wide musical cutoff span, where a low
// cutoff also attenuates the fundamental), at cutoff points where the note clearly sounds.
// ===========================================================================
TEST_CASE("dispatch_vcf: cutoff param sweeps the filter brightness", "[dispatch_vcf]") {
    const double f0 = midiHz(48);   // C3 ~ 130 Hz: many harmonics ride across the cutoff sweep
    auto highEnergyAt = [&](float cutoff01) {
        Snap snap; makeFlatSawSnap(snap);
        snap.setCont(mw::params::ids::kVcfCutoff, cutoff01);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 48, 0.30);
        return highBandEnergy(buf, f0, kSr);
    };

    const double low  = highEnergyAt(0.45f);
    const double mid  = highEnergyAt(0.65f);
    const double high = highEnergyAt(1.0f);    // fully open

    // Opening the cutoff lets progressively MORE high-harmonic energy through (strictly).
    REQUIRE(high > mid * 2.0);
    REQUIRE(mid  > low  * 2.0);
    REQUIRE(low  > 0.0);
}

// ===========================================================================
// RESONANCE: high resonance self-oscillates. With the source mixer MUTED (no oscillator
// input) the filter is silent at reso=0 but rings into a sustained sine at reso=1.
// ===========================================================================
TEST_CASE("dispatch_vcf: high resonance self oscillates", "[dispatch_vcf]") {
    auto selfOscRms = [&](float reso01) {
        Snap snap;
        // Mute every source so any output is the filter's own resonant ring, not the VCO.
        snap.setCont(mw::params::ids::kSawLevel, 0.0f);
        snap.setCont(mw::params::ids::kPulseLevel, 0.0f);
        snap.setCont(mw::params::ids::kSubLevel, 0.0f);
        snap.setCont(mw::params::ids::kNoiseLevel, 0.0f);
        snap.setCont(mw::params::ids::kVcfCutoff, 0.5f);
        snap.setCont(mw::params::ids::kVcfResonance, reso01);
        snap.setCont(mw::params::ids::kEnvSustain, 1.0f);
        snap.setCont(mw::params::ids::kEnvAttack, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        // A note-on kicks the filter (the gate + a brief transient) so it can break into
        // oscillation; then it sustains on its own feedback.
        auto buf = renderHeld(eng, &snap.s, 60, 0.30, /*warmupBlocks=*/12);
        return rms(buf);
    };

    const double quiet  = selfOscRms(0.0f);   // no feedback => effectively silent
    const double ringing = selfOscRms(1.0f);  // self-osc onset => sustained tone

    // The diode-clamp governs the self-oscillation amplitude to a fixed point well above the
    // established self-osc RMS floor (FilterGoldenCorpusConstants kSelfOscRmsFloor = 1e-3) but
    // far below the muted-mixer silence (only the ~1e-19 anti-denormal bias) — a clear,
    // governed ring [docs/design/02 §5.4; ADR-003 F-04].
    REQUIRE(quiet < 1.0e-6);                  // muted mixer + no resonance => silence
    REQUIRE(ringing > 2.0e-3);                // self-oscillation produces a real, governed tone
    REQUIRE(ringing > quiet * 1.0e6);
    REQUIRE(ringing < kSaneBound);            // governed (the diode clamp bounds amplitude)
}

// ===========================================================================
// ENV->CUTOFF: env_mod opens the filter WITH the envelope. A percussive envelope (fast
// attack, fast decay to a low sustain) + env_mod makes a BRIGHT onset that decays DARKER:
// the high-harmonic energy in an early window far exceeds a later window.
// ===========================================================================
TEST_CASE("dispatch_vcf: env_mod opens the filter with the envelope", "[dispatch_vcf]") {
    const double f0 = midiHz(48);
    Snap snap;
    snap.setCont(mw::params::ids::kSawLevel, 0.9f);
    snap.setCont(mw::params::ids::kPulseLevel, 0.0f);
    snap.setCont(mw::params::ids::kVcfCutoff, 0.12f);   // low base cutoff (dark at rest)
    snap.setCont(mw::params::ids::kVcfEnvMod, 1.0f);    // full env->cutoff
    snap.setCont(mw::params::ids::kVcfResonance, 0.0f);
    snap.setCont(mw::params::ids::kEnvAttack, 0.001f);  // near-instant onset
    snap.setCont(mw::params::ids::kEnvDecay, 0.12f);    // decay to the dark sustain
    snap.setCont(mw::params::ids::kEnvSustain, 0.0f);   // collapses to base cutoff
    snap.setCont(mw::params::ids::kEnvRelease, 0.1f);
    snap.setCont(mw::params::ids::kVcaLevel, 0.8f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto buf = renderFromOnset(eng, &snap.s, 48, 0.30);
    REQUIRE(buf.size() > static_cast<std::size_t>(0.2 * kSr));

    // Early window (the bright filter-envelope onset) vs late window (collapsed to the dark
    // base cutoff). Brightness must fall as the envelope closes the filter.
    const std::size_t n = buf.size();
    std::vector<float> early(buf.begin(), buf.begin() + static_cast<long>(n / 6));
    std::vector<float> late (buf.begin() + static_cast<long>(2 * n / 3), buf.end());

    const double bEarly = highBandEnergy(early, f0, kSr);
    const double bLate  = highBandEnergy(late,  f0, kSr);
    REQUIRE(bEarly > bLate * 2.0);   // the envelope visibly opens then closes the filter
}

// ===========================================================================
// KEYBOARD TRACK: kbd_track raises cutoff with note pitch. For a high note (well above the
// A4 tracking reference) turning tracking ON rides the cutoff UP by ~the pitch interval, so
// the SAME note carries far more energy (low harmonics + RMS) than with tracking OFF, where
// the cutoff is fixed regardless of note. Measured as fundamental-band energy + RMS at a
// fixed, partly-closed cutoff. A high note keeps its harmonics ABOVE Nyquist out of the
// probe, so the low-harmonic comb (h1..h6) is the robust band here.
// ===========================================================================
TEST_CASE("dispatch_vcf: kbd_track raises cutoff with note pitch", "[dispatch_vcf]") {
    auto lowBandEnergy = [&](const std::vector<float>& x, double f0) {
        double e = 0.0; for (int h = 1; h <= 6; ++h) e += goertzelPower(x, f0 * h, kSr); return e;
    };
    auto measure = [&](int note, float track) {
        Snap snap; makeFlatSawSnap(snap);
        snap.setCont(mw::params::ids::kVcfCutoff, 0.45f);   // a fixed, partly-closed cutoff
        snap.setCont(mw::params::ids::kVcfKbdTrack, track);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, note, 0.30);
        return std::pair<double, double>{ lowBandEnergy(buf, midiHz(note)), rms(buf) };
    };

    // A high note (A6 = 90, +21 semitones above the A4 = 69 tracking reference): tracking ON
    // rides its cutoff up ~1.75 octaves, so the same note is dramatically louder/brighter than
    // with the cutoff fixed (tracking OFF), where the fixed cutoff sits below its harmonics.
    const auto offHi = measure(90, 0.0f);
    const auto onHi  = measure(90, 1.0f);
    REQUIRE(onHi.first  > offHi.first  * 4.0);   // tracking opens the filter with the pitch
    REQUIRE(onHi.second > offHi.second * 2.0);   // and it is audibly louder

    // Sanity: at the A4 reference note itself tracking adds ~no offset (the note IS the
    // pivot), so ON and OFF are close — tracking is a pitch-RELATIVE cutoff shift centered on
    // the reference, not a flat boost. note 69 == ref, delta 0 => identical cutoff either way.
    const auto offRef = measure(69, 0.0f);
    const auto onRef  = measure(69, 1.0f);
    REQUIRE(onRef.second == Catch::Approx(offRef.second).epsilon(0.05));
}

// ===========================================================================
// ADSR ATTACK: a long attack ramps in slowly (early amplitude << late), a near-instant
// attack is full almost immediately — the ADSR times reach env_.setParams.
// ===========================================================================
TEST_CASE("dispatch_vcf: env attack time changes the amplitude onset", "[dispatch_vcf]") {
    auto onsetRatio = [&](float attackSec) {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel, 0.9f);
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);     // open filter (isolate the VCA env)
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kEnvAttack, attackSec);
        snap.setCont(mw::params::ids::kEnvDecay, 0.0f);
        snap.setCont(mw::params::ids::kEnvSustain, 1.0f);    // hold full after attack
        snap.setCont(mw::params::ids::kVcaLevel, 0.8f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderFromOnset(eng, &snap.s, 60, 0.40);
        const std::size_t n = buf.size();
        // RMS of the first ~25 ms vs a settled late window.
        const double early = rmsRange(buf, 0, static_cast<std::size_t>(0.025 * kSr));
        const double late  = rmsRange(buf, 3 * n / 4, n);
        return early / std::max(1e-9, late);
    };

    const double fast = onsetRatio(0.0f);    // near-instant: early ~ late
    const double slow = onsetRatio(0.30f);   // 300 ms attack: early << late

    REQUIRE(fast > 0.6);                     // full almost immediately
    REQUIRE(slow < 0.3);                     // still ramping in early
    REQUIRE(fast > slow * 2.0);              // the attack time is clearly audible
}

// ===========================================================================
// ADSR SUSTAIN: the sustain level scales the held (post-decay) amplitude.
// ===========================================================================
TEST_CASE("dispatch_vcf: env sustain level scales the held amplitude", "[dispatch_vcf]") {
    auto heldRms = [&](float sustain) {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel, 0.9f);
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kEnvAttack, 0.002f);
        snap.setCont(mw::params::ids::kEnvDecay, 0.02f);
        snap.setCont(mw::params::ids::kEnvSustain, sustain);
        snap.setCont(mw::params::ids::kVcaLevel, 0.8f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        // Warm past the decay so we measure the steady sustain.
        auto buf = renderHeld(eng, &snap.s, 60, 0.20, /*warmupBlocks=*/16);
        return rms(buf);
    };

    const double full = heldRms(1.0f);
    const double half = heldRms(0.4f);
    const double low  = heldRms(0.1f);

    REQUIRE(full > half);
    REQUIRE(half > low);
    REQUIRE(low > 0.0);   // still sounding (a non-zero sustain holds a tone)
}

// ===========================================================================
// VCA LEVEL: the level param scales the output amplitude (a clean monotone scale).
// ===========================================================================
TEST_CASE("dispatch_vcf: vca level scales the output amplitude", "[dispatch_vcf]") {
    auto levelRms = [&](float level) {
        Snap snap; makeFlatSawSnap(snap);
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);
        snap.setCont(mw::params::ids::kVcaLevel, level);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.20);
        return rms(buf);
    };

    const double zero = levelRms(0.0f);
    const double half = levelRms(0.4f);
    const double full = levelRms(1.0f);

    REQUIRE(zero < 1.0e-6);          // level 0 => silence
    REQUIRE(full > half);            // monotone with level
    REQUIRE(half > 0.0);
    // Roughly linear: full is well above half (the level is a clean amplitude scale).
    REQUIRE((full / std::max(1e-9, half)) > 1.5);
}

// ===========================================================================
// VCA MODE: ENV vs GATE changes the amplitude contour. In GATE mode the amplitude is a flat
// full level for the whole gate (organ-style); in ENV mode a percussive envelope makes the
// late amplitude far quieter than the early peak. The contours therefore differ.
// ===========================================================================
TEST_CASE("dispatch_vcf: vca mode ENV vs GATE changes the amplitude contour", "[dispatch_vcf]") {
    auto lateOverEarly = [&](int mode) {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel, 0.9f);
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kEnvAttack, 0.001f);
        snap.setCont(mw::params::ids::kEnvDecay, 0.06f);
        snap.setCont(mw::params::ids::kEnvSustain, 0.0f);   // ENV decays to silence
        snap.setCont(mw::params::ids::kEnvRelease, 0.1f);
        snap.setCont(mw::params::ids::kVcaLevel, 0.8f);
        snap.setChoice(mw::params::ids::kVcaMode, mode);    // 0=ENV, 1=GATE
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderFromOnset(eng, &snap.s, 60, 0.30);
        const std::size_t n = buf.size();
        const double early = rmsRange(buf, 0, n / 8);
        const double late  = rmsRange(buf, 3 * n / 4, n);
        return late / std::max(1e-9, early);
    };

    const double env  = lateOverEarly(0);   // env decays to ~0 sustain: late << early
    const double gate = lateOverEarly(1);   // flat full gate: late ~ early

    REQUIRE(env < 0.4);            // ENV mode clearly decays away
    REQUIRE(gate > 0.7);           // GATE mode holds a flat full level
    REQUIRE(gate > env * 1.8);     // the two contours are distinct
}

// ===========================================================================
// DETERMINISM: two independently-prepared engines fed the identical note + snapshot stream
// (with VCF/Env/VCA params set) produce bit-identical output through the dispatch path.
// ===========================================================================
TEST_CASE("dispatch_vcf: the VCF Env VCA dispatch path is deterministic", "[dispatch_vcf]") {
    Snap snap;
    snap.setCont(mw::params::ids::kVcfCutoff, 0.4f);
    snap.setCont(mw::params::ids::kVcfResonance, 0.6f);
    snap.setCont(mw::params::ids::kVcfEnvMod, 0.5f);
    snap.setCont(mw::params::ids::kVcfKbdTrack, 0.3f);
    snap.setCont(mw::params::ids::kEnvAttack, 0.05f);
    snap.setCont(mw::params::ids::kEnvDecay, 0.1f);
    snap.setCont(mw::params::ids::kEnvSustain, 0.6f);
    snap.setCont(mw::params::ids::kVcaLevel, 0.7f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 57, 0.10);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);
}

// ===========================================================================
// RT-SAFETY: the VCF/Env/VCA dispatch (per-control-tick ParamSnapshot read + the filter/
// env/vca setters) and the render allocate nothing and take no lock on the audio thread.
// ===========================================================================
TEST_CASE("dispatch_vcf: VCF Env VCA dispatch is allocation and lock free under the guard",
          "[dispatch_vcf][rt]") {
    Snap snap;
    snap.setCont(mw::params::ids::kVcfCutoff, 0.55f);
    snap.setCont(mw::params::ids::kVcfResonance, 0.7f);
    snap.setCont(mw::params::ids::kVcfEnvMod, 0.6f);
    snap.setCont(mw::params::ids::kVcfKbdTrack, 0.4f);
    snap.setCont(mw::params::ids::kEnvAttack, 0.02f);
    snap.setCont(mw::params::ids::kEnvDecay, 0.08f);
    snap.setCont(mw::params::ids::kEnvSustain, 0.5f);
    snap.setCont(mw::params::ids::kEnvRelease, 0.1f);
    snap.setCont(mw::params::ids::kVcaLevel, 0.8f);
    snap.setChoice(mw::params::ids::kVcaMode, 1);

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
    for (int b = 0; b < 8; ++b) ctxs.push_back(blocks[static_cast<std::size_t>(b)].ctx(none, kBlk, &snap.s));

    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (int b = 0; b < 8; ++b) eng.process(ctxs[static_cast<std::size_t>(b)]);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    float pk = 0.0f;
    for (int b = 0; b < 8; ++b)
        for (int i = 0; i < kBlk; ++i)
            pk = std::max(pk, std::fabs(blocks[static_cast<std::size_t>(b)].L[static_cast<std::size_t>(i)]));
    REQUIRE(pk < kSaneBound);
}
