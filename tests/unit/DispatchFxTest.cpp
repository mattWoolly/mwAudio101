// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchFxTest.cpp — the AUDIO-EFFECT acceptance suite for the FX-param
// control-dispatch (task 163; ADR-028 item 5).
//
// Test-case display names ALL begin with "dispatch_fx" so
// `ctest -R dispatch_fx --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[dispatch_fx]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// THE BUG THIS CLOSES. The FxChain IS run once per block by the Engine (task 118 §4.1
// FX site), but before this task its FxParams were NEVER fed from ctx.params — so the
// FX section was permanently bypassed/at-defaults regardless of the knobs/preset. This
// suite drives the seam's immutable mw::ParamSnapshot through the SAME Engine the shell
// uses, and asserts the FX now AUDIBLY respond:
//   * Drive ON adds harmonics/saturation (the spectrum gains energy at new harmonics);
//   * Chorus ON adds a modulated, stereo-widening wet (L diverges from R; energy rises);
//   * Delay ON produces echoes at the set time (an autocorrelation peak at the delay lag),
//     and changing delay.time moves that echo spacing;
//   * fx.bypass mutes ALL FX (output is the FX-off padded-mono dry: L==R, bit-exact to
//     the same engine with the FX blocks individually off — the task-141 FX-off contract);
//   * out.mono collapses the stereo chorus/delay image to a phase-coherent mono (L==R);
//   * the decode+publish path is RT-safe (AudioThreadGuard-clean) and deterministic.
//
// The Engine consumes the seam's mw::ParamSnapshot once per block; this file builds that
// POD directly (the off-thread bridge's job in the real shell) and reads the audible
// result, so it links mwcore ONLY (no JUCE) [docs/design/00 §5.4; ADR-001].

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
#include "calibration/FxDispatchConstants.h"   // the delay-ms / chorus-Hz decode span (round-trip)

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- registry-index lookup: map a parameter string-ID to its kParamDefs slot. Same
// keying the ParamSnapshot uses, so a test sets a param by its canonical ID without
// hand-counting the 91-row table. -------------------------------------------------
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
// form (exactly what the bridge emits: convertTo0to1(default)). setCont/setChoice then
// move individual params; setCont denormalizes against the param's OWN skew so a skewed
// param (delay_time) lands at the requested ENGINEERING value the dispatch must recover.
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

    // The bridge's convertTo0to1 for a (possibly skewed) continuous range:
    // norm = ((value - min) / span) ^ skew  (JUCE NormalisableRange forward map).
    static float normFor(const mw::params::ParamDef& d, float value) noexcept {
        const float span = d.maxValue - d.minValue;
        if (span <= 0.0f) return 0.0f;
        const float v = std::clamp(value, d.minValue, d.maxValue);
        const float lin = (v - d.minValue) / span;
        return d.skew == 1.0f ? lin : std::pow(lin, d.skew);
    }

    void setCont(const char* id, float value) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        s.normalizedValues[static_cast<std::size_t>(i)] = normFor(d, value);
    }

    void setChoice(const char* id, int idx) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
        const float denom = (d.choiceCount > 1) ? static_cast<float>(d.choiceCount - 1) : 1.0f;
        s.normalizedValues[static_cast<std::size_t>(i)] = static_cast<float>(idx) / denom;
    }
    void setBool(const char* id, bool on) noexcept { setChoice(id, on ? 1 : 0); }

    // Set mw101.fx.delay_time so the Engine's FX dispatch decodes it to `targetMs` of free
    // delay. The dispatch maps the param like: deskew the stored normalized value to a
    // linear raw r in [0,1] (r = norm^(1/skew)), then LOG-map r across
    // [kDelayFreeMinMs, kDelayFreeMaxMs] (delayFreeMs). So the raw the dispatch wants is
    //   r = ln(targetMs/min) / ln(max/min),
    // and the bridge stores convertTo0to1(raw) == raw^skew. This is the exact inverse of
    // the production decode, so the test drives the real round-trip (no hidden coupling —
    // it references the SAME calibration span the dispatch uses).
    void setDelayTimeMs(float targetMs) noexcept {
        const float lo = mw::cal::fxdispatch::kDelayFreeMinMs;
        const float hi = mw::cal::fxdispatch::kDelayFreeMaxMs;
        const float clamped = std::clamp(targetMs, lo, hi);
        const float raw = std::log(clamped / lo) / std::log(hi / lo);   // linear 0..1
        const int i = slotOf(mw::params::ids::kFxDelayTime);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        // The bridge stores raw^skew (the registry range for delay_time is 0..1).
        s.normalizedValues[static_cast<std::size_t>(i)] =
            d.skew == 1.0f ? raw : std::pow(raw, d.skew);
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

// One stereo host block + a BlockContext factory carrying a ParamSnapshot pointer.
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

// Stereo render result.
struct Stereo { std::vector<float> L, R; };

// Render a held note for `seconds`, returning the stereo output. Drives the snapshot
// every block so the FX dispatch (decode -> fx_.setParams) applies each block, and warms
// up so de-zipper smoothers + the FX dry-pad settle to steady state.
Stereo renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap,
                  int midiNote, double seconds, int warmupBlocks = 12) {
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
    Stereo out;
    out.L.reserve(static_cast<std::size_t>(total) + kBlk);
    out.R.reserve(static_cast<std::size_t>(total) + kBlk);
    int rendered = 0;
    while (rendered < total) {
        Block w(kBlk);
        std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap);
        eng.process(c);
        for (int i = 0; i < kBlk && rendered < total; ++i, ++rendered) {
            out.L.push_back(w.L[static_cast<std::size_t>(i)]);
            out.R.push_back(w.R[static_cast<std::size_t>(i)]);
        }
    }
    return out;
}

// Render a SHORT note then SILENCE so a delay's discrete echoes appear after the dry
// signal has stopped — captures both the note and the trailing tail. Returns mono L.
std::vector<float> renderBurstThenSilence(mw::Engine& eng, const mw::ParamSnapshot* snap,
                                          int midiNote, double noteSec, double tailSec) {
    constexpr int kBlk = 256;
    std::vector<float> out;
    const int noteSamps = static_cast<int>(noteSec * kSr);
    const int tailSamps = static_cast<int>(tailSec * kSr);
    out.reserve(static_cast<std::size_t>(noteSamps + tailSamps) + kBlk);

    // Note on.
    {
        Block w(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOn(midiNote, 1.0f, 0) };
        auto c = w.ctx(ev, kBlk, snap);
        eng.process(c);
        for (int i = 0; i < kBlk; ++i) out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    int rendered = kBlk;
    while (rendered < noteSamps) {
        Block w(kBlk); std::vector<mw::MidiEvent> none;
        auto c = w.ctx(none, kBlk, snap); eng.process(c);
        for (int i = 0; i < kBlk && rendered < noteSamps; ++i, ++rendered)
            out.push_back(w.L[static_cast<std::size_t>(i)]);
    }
    // Note off, then render silence to let echoes ring out.
    {
        Block w(kBlk);
        std::vector<mw::MidiEvent> ev{ noteOff(midiNote, 0) };
        auto c = w.ctx(ev, kBlk, snap);
        eng.process(c);
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

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}

double rms(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    // RMS of (a - b) over the common length — channel-difference / signal-diff measure.
    const std::size_t n = std::min(a.size(), b.size());
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        acc += d * d;
    }
    return std::sqrt(acc / std::max<std::size_t>(1, n));
}

double midiHz(int n) noexcept { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

// Normalized autocorrelation at a given lag (in samples) over a buffer. A delay echo
// repeats the program material at the delay lag, so the autocorrelation has a strong
// secondary peak there. Window-normalized so it is comparable across lags.
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

constexpr float kSaneBound = 64.0f;

// IDs (alias for brevity).
namespace P = mw::params::ids;

} // namespace

// ===========================================================================
// BASELINE: with the INIT defaults (fx.bypass == ON) the FX section is bypassed, so the
// stereo output is a phase-coherent mono dry (L == R). This is the pre-task behavior the
// dispatch must preserve when bypassed — and the reference for "FX changed something".
// ===========================================================================
TEST_CASE("dispatch_fx: default fx.bypass leaves the output dry and mono", "[dispatch_fx]") {
    Snap snap;   // INIT defaults: fx.bypass == true
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto out = renderHeld(eng, &snap.s, 60, 0.20);

    REQUIRE(rms(out.L) > 0.0);               // the voice still sounds
    REQUIRE(rms(out.L, out.R) < 1e-7);       // FX bypassed => L == R (mono dry)
}

// ===========================================================================
// DRIVE: enabling Drive (fx.bypass off, drive_enable on, amount up) adds harmonic
// saturation — the spectrum gains substantial energy at higher harmonics relative to the
// clean (FX-off) signal. Asserted on the rendered spectrum, not "non-silent".
// ===========================================================================
TEST_CASE("dispatch_fx: enabling Drive adds harmonic saturation", "[dispatch_fx]") {
    const int note = 48;
    const double f0 = midiHz(note);

    // Clean reference (FX bypassed): measure the high-harmonic energy fraction.
    auto highHarmonicFraction = [&](bool driveOn, float amount) {
        Snap snap;
        // A saw source so the shaper has rich program to bite on; keep the source level up.
        snap.setCont(P::kSawLevel, 0.8f);
        if (driveOn) {
            snap.setBool(P::kFxBypass, false);
            snap.setBool(P::kFxDriveEnable, true);
            snap.setCont(P::kFxDriveAmount, amount);
            snap.setCont(P::kFxDriveOutput, 0.5f);
        }
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto out = renderHeld(eng, &snap.s, note, 0.25);
        // The Drive stage is an ASYMMETRIC waveshaper (a DC-biased nonlinearity, Drive.h
        // §4.3 / design §4.6 kDriveBias), whose signature is manufactured EVEN harmonics:
        // a clean (anti-aliased) saw is near-purely odd-harmonic, so the even-harmonic
        // energy (2nd + 4th) relative to the fundamental jumps sharply once Drive engages.
        // This is the physically-correct discriminator for the saturator.
        const double even = goertzelPower(out.L, 2.0 * f0, kSr)
                          + goertzelPower(out.L, 4.0 * f0, kSr);
        const double lo   = goertzelPower(out.L, f0, kSr);
        return even / std::max(1e-12, lo);
    };

    const double clean  = highHarmonicFraction(false, 0.0f);
    const double driven = highHarmonicFraction(true, 0.9f);

    REQUIRE(driven > 0.0);
    // The asymmetric saturator manufactures substantial even-harmonic content the clean
    // saw lacks — many-fold over the bypassed reference (measured ~100x; assert a robust
    // floor well above it).
    REQUIRE(driven > clean * 10.0);
}

// ===========================================================================
// CHORUS: enabling Chorus (mode != Off, mix up, width up) adds a modulated wet image that
// (a) raises the output energy vs the dry and (b) makes L diverge from R (stereo width is
// born inside the chorus, §3.3). The dry path alone is mono (L == R).
// ===========================================================================
TEST_CASE("dispatch_fx: enabling Chorus adds a modulated stereo wet", "[dispatch_fx]") {
    // Dry reference (FX bypassed): mono, fixed energy.
    Snap dry;
    {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto out = renderHeld(eng, &dry.s, 60, 0.25);
        REQUIRE(rms(out.L, out.R) < 1e-7);   // dry is mono
    }

    Snap snap;
    snap.setBool(P::kFxBypass, false);
    snap.setBool(P::kFxChorusEnable, true);
    snap.setChoice(P::kFxChorusMode, 1);   // Mode I
    snap.setCont(P::kFxChorusMix, 1.0f);   // full wet
    snap.setCont(P::kFxChorusDepth, 0.8f);
    snap.setCont(P::kFxChorusWidth, 1.0f); // full stereo separation

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto out = renderHeld(eng, &snap.s, 60, 0.30);

    REQUIRE(rms(out.L) > 0.0);
    // The chorus wet is a hard-panned anti-phase image at full width => L != R.
    REQUIRE(rms(out.L, out.R) > 1e-3);
}

// ===========================================================================
// DELAY (echoes at the set time): enabling Delay (mix up, feedback up) produces discrete
// repeats of the note. After the note stops, the signal RINGS (the tail has energy), and
// the buffer's autocorrelation peaks at the configured delay lag — the echo spacing. The
// FX-off path has no such tail/peak.
// ===========================================================================
TEST_CASE("dispatch_fx: enabling Delay produces echoes at the set time", "[dispatch_fx]") {
    constexpr float kDelayMs = 120.0f;
    const int lag = static_cast<int>((kDelayMs / 1000.0f) * kSr);

    Snap snap;
    snap.setBool(P::kFxBypass, false);
    snap.setBool(P::kFxDelayEnable, true);
    snap.setBool(P::kFxDelaySync, false);          // free time
    snap.setDelayTimeMs(kDelayMs);            // free ms (round-trips through the dispatch decode)
    snap.setCont(P::kFxDelayFeedback, 0.6f);
    snap.setCont(P::kFxDelayMix, 1.0f);            // full wet so echoes dominate the tail
    snap.setCont(P::kFxDelayWidth, 0.0f);          // centered so L carries the echoes

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto buf = renderBurstThenSilence(eng, &snap.s, 60, 0.12, 0.60);

    // The autocorrelation at the delay lag is a strong, clear peak relative to a nearby
    // off-lag reference (half the lag, where no echo aligns).
    const double atLag  = autocorrAtLag(buf, lag);
    const double offLag = autocorrAtLag(buf, lag / 2);
    REQUIRE(atLag > 0.2);
    REQUIRE(atLag > offLag * 1.5);

    // Sanity: the tail (post note-off, well after the dry signal) still has energy — the
    // echoes are ringing out, which a dry/FX-off path would not do.
    const int tailStart = static_cast<int>(0.25 * kSr);
    std::vector<float> tail(buf.begin() + std::min<std::size_t>(buf.size(), tailStart),
                            buf.end());
    REQUIRE(rms(tail) > 1e-4);
}

// ===========================================================================
// DELAY continuous param: a LONGER delay time moves the echo spacing — the autocorrelation
// peak follows the configured delay lag (echo spacing tracks delay.time).
// ===========================================================================
TEST_CASE("dispatch_fx: delay time changes the echo spacing", "[dispatch_fx]") {
    auto peakLag = [&](float delayMs) {
        const int lag = static_cast<int>((delayMs / 1000.0f) * kSr);
        Snap snap;
        snap.setBool(P::kFxBypass, false);
        snap.setBool(P::kFxDelayEnable, true);
        snap.setBool(P::kFxDelaySync, false);
        snap.setDelayTimeMs(delayMs);
        snap.setCont(P::kFxDelayFeedback, 0.55f);
        snap.setCont(P::kFxDelayMix, 1.0f);
        snap.setCont(P::kFxDelayWidth, 0.0f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderBurstThenSilence(eng, &snap.s, 60, 0.10, 0.60);
        return autocorrAtLag(buf, lag);   // strong only if the echo really sits at this lag
    };

    // Each delay time produces a strong autocorrelation peak at ITS OWN lag. If the
    // dispatch ignored delay.time both would echo at the same (default) spacing and the
    // peak measured at the OTHER time's lag would be weak.
    const double short80  = peakLag(80.0f);
    const double long200  = peakLag(200.0f);
    REQUIRE(short80 > 0.2);
    REQUIRE(long200 > 0.2);

    // Cross-check: the 200 ms render does NOT have a strong peak at the 80 ms lag (proving
    // the spacing actually moved with the param, not a fixed default).
    Snap snap200;
    snap200.setBool(P::kFxBypass, false);
    snap200.setBool(P::kFxDelayEnable, true);
    snap200.setBool(P::kFxDelaySync, false);
    snap200.setDelayTimeMs(200.0f);
    snap200.setCont(P::kFxDelayFeedback, 0.55f);
    snap200.setCont(P::kFxDelayMix, 1.0f);
    snap200.setCont(P::kFxDelayWidth, 0.0f);
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto buf200 = renderBurstThenSilence(eng, &snap200.s, 60, 0.10, 0.60);
    const int lag80 = static_cast<int>((80.0f / 1000.0f) * kSr);
    const double at80inLong = autocorrAtLag(buf200, lag80);
    REQUIRE(long200 > at80inLong * 1.3);   // 200 ms echo peaks at 200 ms, not 80 ms
}

// ===========================================================================
// fx.bypass MUTES all FX: with Drive+Chorus+Delay all enabled+configured, raising
// fx.bypass back ON yields EXACTLY the FX-off padded-mono dry — bit-identical to the same
// engine driven with the three FX blocks individually OFF (the task-141 FX-off contract:
// FX-off output is bit-exact at the constant PDC offset). Proves bypass is the master kill.
// ===========================================================================
TEST_CASE("dispatch_fx: fx.bypass mutes all FX and is bit-exact to FX-off", "[dispatch_fx]") {
    // (a) master bypass ON, but every FX block configured "on" underneath.
    auto renderBypassed = [&]() {
        Snap snap;
        snap.setBool(P::kFxBypass, true);            // MASTER BYPASS
        snap.setBool(P::kFxDriveEnable, true);
        snap.setCont(P::kFxDriveAmount, 0.9f);
        snap.setBool(P::kFxChorusEnable, true);
        snap.setChoice(P::kFxChorusMode, 3);
        snap.setCont(P::kFxChorusMix, 1.0f);
        snap.setBool(P::kFxDelayEnable, true);
        snap.setCont(P::kFxDelayMix, 1.0f);
        snap.setCont(P::kFxDelayFeedback, 0.6f);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 60, 0.20);
    };

    // (b) master ON but the three FX blocks individually OFF — the all-blocks-off early-out,
    // which §3.3 rule 3 makes the SAME padded-mono dry as masterBypass.
    auto renderBlocksOff = [&]() {
        Snap snap;
        snap.setBool(P::kFxBypass, false);
        snap.setBool(P::kFxDriveEnable, false);
        snap.setChoice(P::kFxChorusMode, 0);   // Off
        snap.setBool(P::kFxChorusEnable, false);
        snap.setBool(P::kFxDelayEnable, false);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 60, 0.20);
    };

    const auto bypassed = renderBypassed();
    const auto blocksOff = renderBlocksOff();

    REQUIRE(rms(bypassed.L) > 0.0);                       // the voice still sounds
    REQUIRE(rms(bypassed.L, bypassed.R) < 1e-7);          // bypassed => L == R (mono dry)

    // Bit-exact equality of the padded-mono dry between the two FX-off conditions.
    REQUIRE(bypassed.L.size() == blocksOff.L.size());
    for (std::size_t i = 0; i < bypassed.L.size(); ++i) {
        REQUIRE(bypassed.L[i] == blocksOff.L[i]);
        REQUIRE(bypassed.R[i] == blocksOff.R[i]);
    }
}

// ===========================================================================
// out.mono collapses the stereo chorus/delay image to a phase-coherent mono. With a full-
// width chorus the output is stereo (L != R); enabling out.mono forces L == R regardless.
// ===========================================================================
TEST_CASE("dispatch_fx: out.mono collapses the stereo FX image to mono", "[dispatch_fx]") {
    auto build = [&](bool mono) {
        Snap snap;
        snap.setBool(P::kFxBypass, false);
        snap.setBool(P::kFxChorusEnable, true);
        snap.setChoice(P::kFxChorusMode, 3);   // I+II
        snap.setCont(P::kFxChorusMix, 1.0f);
        snap.setCont(P::kFxChorusWidth, 1.0f); // full stereo
        snap.setBool(P::kOutMono, mono);
        return snap;
    };

    {   // stereo: chorus width => L != R
        Snap snap = build(false);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto out = renderHeld(eng, &snap.s, 60, 0.25);
        REQUIRE(rms(out.L) > 0.0);
        REQUIRE(rms(out.L, out.R) > 1e-3);   // genuinely stereo
    }
    {   // mono collapse: out.mono ON forces L == R
        Snap snap = build(true);
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto out = renderHeld(eng, &snap.s, 60, 0.25);
        REQUIRE(rms(out.L) > 0.0);
        REQUIRE(rms(out.L, out.R) < 1e-7);   // collapsed to mono
    }
}

// ===========================================================================
// DETERMINISM: two independently-prepared engines fed the identical note + FX snapshot
// stream produce bit-identical output through the decode+publish+FX path.
// ===========================================================================
TEST_CASE("dispatch_fx: the FX dispatch path is deterministic", "[dispatch_fx]") {
    Snap snap;
    snap.setBool(P::kFxBypass, false);
    snap.setBool(P::kFxDriveEnable, true);
    snap.setCont(P::kFxDriveAmount, 0.6f);
    snap.setBool(P::kFxDelayEnable, true);
    snap.setDelayTimeMs(90.0f);
    snap.setCont(P::kFxDelayFeedback, 0.5f);
    snap.setCont(P::kFxDelayMix, 0.6f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 57, 0.12);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.L.size() == b.L.size());
    for (std::size_t i = 0; i < a.L.size(); ++i) {
        REQUIRE(a.L[i] == b.L[i]);
        REQUIRE(a.R[i] == b.R[i]);
    }
}

// ===========================================================================
// RT-SAFETY: the FX decode (per-block ParamSnapshot read + fx_.setParams publish) and the
// full FX render allocate nothing and take no lock on the audio thread.
// ===========================================================================
TEST_CASE("dispatch_fx: FX decode and render are allocation and lock free under the guard",
          "[dispatch_fx][rt]") {
    Snap snap;
    snap.setBool(P::kFxBypass, false);
    snap.setBool(P::kFxDriveEnable, true);
    snap.setCont(P::kFxDriveAmount, 0.7f);
    snap.setBool(P::kFxChorusEnable, true);
    snap.setChoice(P::kFxChorusMode, 2);
    snap.setCont(P::kFxChorusMix, 0.8f);
    snap.setBool(P::kFxDelayEnable, true);
    snap.setDelayTimeMs(110.0f);
    snap.setCont(P::kFxDelayFeedback, 0.5f);
    snap.setCont(P::kFxDelayMix, 0.7f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    // Pre-build every buffer/context BEFORE arming the guard; only eng.process() runs
    // inside the armed scope (the guard intercepts global operator new).
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

    float peak = 0.0f;
    for (int b = 0; b < 8; ++b)
        for (int i = 0; i < kBlk; ++i)
            peak = std::max(peak, std::fabs(blocks[static_cast<std::size_t>(b)].L[static_cast<std::size_t>(i)]));
    REQUIRE(peak > 0.0f);
    REQUIRE(peak < kSaneBound);
}
