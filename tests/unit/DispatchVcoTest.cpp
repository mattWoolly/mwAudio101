// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchVcoTest.cpp — the AUDIO-EFFECT acceptance suite for the
// control-dispatch seam + VCO/source-mixer wiring (task 160, the ADR-028 keystone).
//
// Test-case display names ALL begin with "dispatch_vco" so
// `ctest -R dispatch_vco --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[dispatch_vco]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (the assertion class the audit found MISSING — real audible effect,
// not "non-silent/deterministic"). Every criterion is measured on RENDERED OUTPUT via a
// Goertzel fundamental estimator and broadband-energy probes:
//   * note 48 vs 72 render at the correct 1V/oct ratio (24 semitones == 2 octaves == 4x),
//     and a 4-octave pair (note 24 vs 72) renders ~16x — the headline pitch fix [ADR-005];
//   * a C2/C3/C4/C5 sweep is strictly monotonically increasing in fundamental;
//   * vco.tune and vco.fine shift the pitch; vco.range switches octave/footage;
//     vco.pw changes the pulse duty (asymmetry/spectrum);
//   * the source mixer sums saw+pulse+sub+noise by their levels: saw-only vs pulse-only
//     differ spectrally; raising noise.level raises broadband content;
//   * portamento (glide) slews between two notes (not an instant jump);
//   * determinism is preserved across two identical runs;
//   * the dispatch+render path is RT-safe (AudioThreadGuard-clean) — the [rt] check.
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

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- registry-index lookup: map a parameter string-ID to its kParamDefs slot. This is
// the SAME registry-index keying the ParamSnapshot uses, so a test sets a param by its
// canonical ID without hand-counting the 91-row table. -----------------------------
int slotOf(const char* id) noexcept {
    for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
        const char* a = mw::params::kParamDefs[static_cast<std::size_t>(i)].id;
        const char* b = id;
        // plain C-string equality (constexpr ids; no <cstring> needed for clarity)
        int k = 0;
        while (a[k] != '\0' && b[k] != '\0' && a[k] == b[k]) ++k;
        if (a[k] == '\0' && b[k] == '\0') return i;
    }
    return -1;
}

// Build a ParamSnapshot pre-loaded with every live param's DEFAULT, in normalized [0,1]
// form (exactly what the bridge emits: convertTo0to1(default)). For the linear-skew
// params this suite drives, normalized == (value - min) / (max - min); choice/bool slots
// carry their option index in indexValues and the normalized projection in normalized.
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

    // Set a CONTINUOUS param by its engineering value (linear skew => simple rescale).
    void setCont(const char* id, float value) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        const float span = d.maxValue - d.minValue;
        const float v = std::clamp(value, d.minValue, d.maxValue);
        s.normalizedValues[static_cast<std::size_t>(i)] = span > 0.0f ? (v - d.minValue) / span : 0.0f;
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
    e.value = vel;
    e.sampleOffset = offset;
    return e;
}
mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOff;
    e.noteId = static_cast<std::int16_t>(note);
    e.value = 0.0f;
    e.sampleOffset = offset;
    return e;
}

// One mono-channel host block + a BlockContext factory carrying a ParamSnapshot pointer.
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

// Render `seconds` of sustained audio for a held note, returning the mono (L) buffer.
// Drives the snapshot every block so the dispatch applies (the gate is held across blocks).
std::vector<float> renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap,
                              int midiNote, double seconds, int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    // Note-on, then warm up so glide/escalation/CV all settle to steady state.
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

// Goertzel power at frequency f over the buffer (single-bin DFT magnitude^2).
double goertzelPower(const std::vector<float>& x, double f, double sr) noexcept {
    const int N = static_cast<int>(x.size());
    if (N == 0) return 0.0;
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int n = 0; n < N; ++n) {
        s0 = static_cast<double>(x[static_cast<std::size_t>(n)]) + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// Estimate the fundamental by scanning candidate frequencies and picking the Goertzel
// power peak. Coarse log scan then a fine local refine — robust to harmonics because the
// fundamental of a saw/pulse dominates the low end of the spectrum.
double estimateFundamental(const std::vector<float>& x, double sr,
                           double fLo, double fHi) noexcept {
    double bestF = fLo, bestP = -1.0;
    const int steps = 600;
    for (int i = 0; i <= steps; ++i) {
        const double f = fLo * std::pow(fHi / fLo, static_cast<double>(i) / steps);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    // Fine refine around the coarse peak.
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

// A saw-only mixer with the default cutoff (1.0, fully open) and a held note produces a
// strong, near-440-band fundamental; a generous sanity ceiling catches blowups.
constexpr float kSaneBound = 64.0f;

} // namespace

// ===========================================================================
// HEADLINE: different MIDI notes render different VCO fundamentals at the correct
// 1V/oct ratio. note 48 vs 72 == 24 semitones == 2 octaves == 4x; note 24 vs 72 ==
// 48 semitones == 4 octaves == 16x. The pre-fix engine ignored ctx.params and the
// resolved note, playing one fixed pitch — this is the bug the keystone fixes.
// [ADR-005; ADR-028 item 3]
// ===========================================================================
TEST_CASE("dispatch_vco: different notes render different pitches at 1V per octave",
          "[dispatch_vco]") {
    Snap snap;                              // INIT defaults: saw-only mixer, cutoff open

    auto fundamentalFor = [&](int note) {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, note, 0.30);
        const double expect = midiHz(note);
        return estimateFundamental(buf, kSr, expect * 0.4, expect * 2.5);
    };

    const double f48 = fundamentalFor(48);
    const double f72 = fundamentalFor(72);
    const double f24 = fundamentalFor(24);

    // Each lands near its 1V/oct target (within ~3%).
    REQUIRE(f48 == Catch::Approx(midiHz(48)).epsilon(0.03));
    REQUIRE(f72 == Catch::Approx(midiHz(72)).epsilon(0.03));

    // 24 semitones apart == 2 octaves == 4x (the physically-correct ratio for 48->72).
    REQUIRE((f72 / f48) == Catch::Approx(4.0).epsilon(0.03));

    // 48 semitones apart (note 24 vs 72) == 4 octaves == 16x — the 4-octave headline.
    REQUIRE((f72 / f24) == Catch::Approx(16.0).epsilon(0.04));
}

// ===========================================================================
// A C2/C3/C4/C5 sweep is STRICTLY monotonically increasing in fundamental — the
// resolved note flows to the oscillator pitch for every note, not just a pair.
// ===========================================================================
TEST_CASE("dispatch_vco: a C2 C3 C4 C5 sweep increases monotonically in fundamental",
          "[dispatch_vco]") {
    Snap snap;
    const int notes[] = { 36, 48, 60, 72 };   // C2 C3 C4 C5
    double prev = 0.0;
    for (int n : notes) {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, n, 0.30);
        const double f = estimateFundamental(buf, kSr, midiHz(n) * 0.4, midiHz(n) * 2.5);
        REQUIRE(f == Catch::Approx(midiHz(n)).epsilon(0.03));
        REQUIRE(f > prev);                  // strictly increasing
        prev = f;
    }
}

// ===========================================================================
// vco.tune (coarse semitones) and vco.fine shift the pitch audibly.
// ===========================================================================
TEST_CASE("dispatch_vco: vco tune and fine shift the rendered pitch",
          "[dispatch_vco]") {
    auto fundWith = [](float tuneSemis, float fineSemis) {
        Snap snap;
        snap.setCont(mw::params::ids::kVcoTune, tuneSemis);
        snap.setCont(mw::params::ids::kVcoFine, fineSemis);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.30);
        return estimateFundamental(buf, kSr, midiHz(60) * 0.25, midiHz(72) * 1.5);
    };

    const double base = fundWith(0.0f, 0.0f);
    const double up12 = fundWith(12.0f, 0.0f);   // +1 octave coarse
    const double fineUp = fundWith(0.0f, 1.0f);  // +1 semitone fine

    REQUIRE(base == Catch::Approx(midiHz(60)).epsilon(0.03));
    REQUIRE((up12 / base) == Catch::Approx(2.0).epsilon(0.03));   // +12 semis == 2x
    // +1 semitone fine == 2^(1/12) ~= 1.0595x (clearly above the 3% estimator band).
    REQUIRE((fineUp / base) == Catch::Approx(std::pow(2.0, 1.0 / 12.0)).epsilon(0.02));
}

// ===========================================================================
// vco.range switches the footage octave: 8' (index 1) is the reference, 4' (index 2)
// is +1 octave, 16' (index 0) is -1 octave.
// ===========================================================================
TEST_CASE("dispatch_vco: vco range switches the footage octave",
          "[dispatch_vco]") {
    auto fundForRange = [](int rangeIdx) {
        Snap snap;
        snap.setChoice(mw::params::ids::kVcoRange, rangeIdx);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.30);
        return estimateFundamental(buf, kSr, midiHz(48) * 0.4, midiHz(72) * 1.5);
    };

    const double eight = fundForRange(1);   // 8' reference
    const double four  = fundForRange(2);   // 4' == +1 octave
    const double six16 = fundForRange(0);   // 16' == -1 octave

    REQUIRE((four / eight) == Catch::Approx(2.0).epsilon(0.03));
    REQUIRE((eight / six16) == Catch::Approx(2.0).epsilon(0.03));
}

// ===========================================================================
// vco.pw changes the pulse duty: a pulse-only mixer at duty 0.5 (square) has near-zero
// even harmonics; at an asymmetric duty the 2nd harmonic rises substantially. Measured
// on the spectrum of the rendered pulse.
// ===========================================================================
TEST_CASE("dispatch_vco: vco pw changes the pulse duty spectrum",
          "[dispatch_vco]") {
    auto secondToFirstRatio = [](float pw) {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel, 0.0f);    // pulse-only
        snap.setCont(mw::params::ids::kPulseLevel, 1.0f);
        snap.setCont(mw::params::ids::kVcoPw, pw);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.30);
        const double f0 = midiHz(60);
        const double h1 = goertzelPower(buf, f0, kSr);
        const double h2 = goertzelPower(buf, 2.0 * f0, kSr);
        return h2 / std::max(1e-12, h1);
    };

    // pwmCvNorm 0.0 => 50% duty (square, weak even harmonics); 0.85 => narrow/asymmetric
    // duty (strong even harmonics) [VcoShapeConstants pwmDutyFromCvNorm].
    const double square = secondToFirstRatio(0.0f);   // square => weak 2nd harmonic
    const double narrow = secondToFirstRatio(0.85f);  // asymmetric => strong 2nd harmonic

    REQUIRE(narrow > square * 3.0);   // the duty change is audible in the spectrum
}

// ===========================================================================
// SOURCE MIXER (the §4.1 mixer that summed ONLY src.saw before this task). Proves all
// four sources are summed by their level params, with assertions robust to the (still
// unconfigured, task-161-owned) VCF/VCA defaults: a muted mixer is silent, each source
// alone contributes, and the sources are spectrally DISTINCT (tonal saw vs broadband
// noise vs sub-octave sub vs a different-spectrum pulse).
// ===========================================================================
TEST_CASE("dispatch_vco: the source mixer sums saw pulse sub and noise by their levels",
          "[dispatch_vco]") {
    const double f0 = midiHz(60);

    // "tonality" = fundamental power fraction of total energy. ~1 for a clean tone, ~0 for
    // broadband noise — the discriminator between tonal sources and the noise source.
    auto tonality = [&](const std::vector<float>& b) {
        double tot = 0.0; for (float v : b) tot += static_cast<double>(v) * v;
        const double h1 = goertzelPower(b, f0, kSr) / (0.5 * b.size());
        return h1 / std::max(1e-12, tot);
    };

    // (a) ALL FOUR LEVELS ZERO -> the mixer sums nothing -> effectively silent. (The only
    // residual is the VCF anti-denormal DC bias leaking through a zero input, ~1e-19 — far
    // below any real signal; we bound it well under that, not bit-exact zero.)
    {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel,   0.0f);
        snap.setCont(mw::params::ids::kPulseLevel, 0.0f);
        snap.setCont(mw::params::ids::kSubLevel,   0.0f);
        snap.setCont(mw::params::ids::kNoiseLevel, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.20);
        double peak = 0.0; for (float v : buf) peak = std::max(peak, std::fabs((double) v));
        REQUIRE(peak < 1.0e-9);   // nothing summed => silence (only the denorm-bias floor)
    }

    // (b) SAW-ONLY -> non-silent and strongly TONAL (energy concentrated at the fundamental).
    double sawTon = 0.0;
    {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel,   1.0f);
        snap.setCont(mw::params::ids::kPulseLevel, 0.0f);
        snap.setCont(mw::params::ids::kSubLevel,   0.0f);
        snap.setCont(mw::params::ids::kNoiseLevel, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.25);
        REQUIRE(rms(buf) > 0.0);
        sawTon = tonality(buf);
        REQUIRE(sawTon > 0.3);   // saw is a tone
    }

    // (c) NOISE-ONLY -> non-silent but BROADBAND (very low tonality) — proves the noise
    // source is summed and is spectrally distinct from the tonal sources (the saw).
    {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel,   0.0f);
        snap.setCont(mw::params::ids::kPulseLevel, 0.0f);
        snap.setCont(mw::params::ids::kSubLevel,   0.0f);
        snap.setCont(mw::params::ids::kNoiseLevel, 1.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.25);
        REQUIRE(rms(buf) > 0.0);                // noise.level raises the output (mixed in)
        REQUIRE(tonality(buf) < sawTon * 0.1);  // and it is broadband, not a tone
    }

    // (d) SUB ON vs OFF -> the sub adds strong SUB-OCTAVE (f0/2) energy the saw lacks
    // (the -1-oct square default), which is below the VCF cutoff and survives — proving the
    // sub source is summed by sub.level.
    {
        auto subOctPow = [&](float subLvl) {
            Snap snap;
            snap.setCont(mw::params::ids::kSawLevel, 0.3f);
            snap.setCont(mw::params::ids::kSubLevel, subLvl);
            mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
            auto buf = renderHeld(eng, &snap.s, 60, 0.25);
            return goertzelPower(buf, 0.5 * f0, kSr);
        };
        const double subOff = subOctPow(0.0f);
        const double subOn  = subOctPow(1.0f);
        REQUIRE(subOn > subOff * 50.0);   // the sub-octave appears only with sub.level up
    }

    // (e) PULSE-ONLY -> genuinely SOUNDS at the played fundamental. Pre-fix, render()
    // summed ONLY src.saw, so a saw=0/pulse=1 mix would have been SILENT; here it is a clear
    // tone at f0, proving the pulse source is summed by pulse.level. (Its distinct duty
    // SPECTRUM vs. a square is separately asserted by the vco.pw test.)
    {
        Snap snap;
        snap.setCont(mw::params::ids::kSawLevel,   0.0f);
        snap.setCont(mw::params::ids::kPulseLevel, 1.0f);
        snap.setCont(mw::params::ids::kSubLevel,   0.0f);
        snap.setCont(mw::params::ids::kNoiseLevel, 0.0f);
        snap.setCont(mw::params::ids::kVcoPw, 0.0f);   // square
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeld(eng, &snap.s, 60, 0.25);
        REQUIRE(rms(buf) > 0.0);                    // pulse genuinely sounds (was dropped)
        REQUIRE(tonality(buf) > 0.3);               // a tone, not noise/silence
        const double pf = estimateFundamental(buf, kSr, f0 * 0.4, f0 * 2.5);
        REQUIRE(pf == Catch::Approx(f0).epsilon(0.03));   // at the played pitch
    }
}

// ===========================================================================
// PORTAMENTO: with glide ON and a non-zero time, moving from one held note to another
// SLEWS the pitch (not an instant jump): the fundamental measured in an early window of
// the transition sits strictly between the two endpoint pitches.
// ===========================================================================
TEST_CASE("dispatch_vco: glide slews between two notes when portamento is on",
          "[dispatch_vco]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kGlideMode, 2);   // "On" (index 2 in kGlideMode)
    snap.setCont(mw::params::ids::kGlideTime, 0.30f); // 300 ms glide

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    // Hold note 48 to settle.
    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(48, 1.0f, 0) };
        auto c = on.ctx(ev, kBlk, &snap.s);
        eng.process(c);
    }
    for (int b = 0; b < 20; ++b) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, &snap.s); eng.process(c);
    }

    // Now legato to note 72 and capture a SHORT early window of the transition (~40 ms),
    // far shorter than the 300 ms glide, so the pitch is mid-slew.
    std::vector<float> early;
    {
        Block w(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(72, 1.0f, 0) };
        auto c = w.ctx(ev, kBlk, &snap.s);
        eng.process(c);
        for (int i = 0; i < kBlk; ++i) early.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    for (int b = 0; b < 6; ++b) {   // ~32 ms more of the transition
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, &snap.s); eng.process(c);
        for (int i = 0; i < kBlk; ++i) early.push_back(w.L[static_cast<std::size_t>(i)]);
    }

    const double f48 = midiHz(48), f72 = midiHz(72);
    const double fEarly = estimateFundamental(early, kSr, f48 * 0.7, f72 * 1.3);
    // Mid-slew: strictly between the two endpoints (an instant jump would already be at f72).
    REQUIRE(fEarly > f48 * 1.03);
    REQUIRE(fEarly < f72 * 0.97);
}

// ===========================================================================
// DETERMINISM: two independently-prepared engines fed the identical note + snapshot
// stream produce bit-identical output through the dispatch+render path.
// ===========================================================================
TEST_CASE("dispatch_vco: the dispatch and render path is deterministic",
          "[dispatch_vco]") {
    Snap snap;
    snap.setCont(mw::params::ids::kVcoTune, 7.0f);
    snap.setCont(mw::params::ids::kPulseLevel, 0.5f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 55, 0.10);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);   // byte-stable
}

// ===========================================================================
// RT-SAFETY: the dispatch (per-control-tick ParamSnapshot read + setters) and the
// source-mixer render allocate nothing and take no lock on the audio thread.
// ===========================================================================
TEST_CASE("dispatch_vco: dispatch and render are allocation and lock free under the guard",
          "[dispatch_vco][rt]") {
    Snap snap;
    snap.setCont(mw::params::ids::kVcoTune, 3.0f);
    snap.setCont(mw::params::ids::kSawLevel, 0.7f);
    snap.setCont(mw::params::ids::kPulseLevel, 0.4f);
    snap.setCont(mw::params::ids::kSubLevel, 0.3f);
    snap.setCont(mw::params::ids::kNoiseLevel, 0.2f);
    snap.setChoice(mw::params::ids::kVcoRange, 2);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    // Pre-build every buffer/context BEFORE arming the guard (the guard intercepts global
    // operator new; only eng.process() runs inside the armed scope).
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

    float peak = 0.0f;
    for (int b = 0; b < 8; ++b)
        for (int i = 0; i < kBlk; ++i)
            peak = std::max(peak, std::fabs(blocks[static_cast<std::size_t>(b)].L[static_cast<std::size_t>(i)]));
    REQUIRE(peak > 0.0f);
    REQUIRE(peak < kSaneBound);
}
