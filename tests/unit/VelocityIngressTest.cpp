// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/VelocityIngressTest.cpp — the AUDIO-EFFECT acceptance suite for the
// per-note velocity INGRESS plumbing (task 162b velocity-ingress), the missing leg of
// the ADR-028 control-dispatch repair: task 162 wired velocity->VCA/VCF in the dispatch,
// but the played note's MIDI velocity never reached the Voice (VoiceManager hardcoded
// v.noteOn(note, 1.0f); NoteDecision carried no velocity field). This task plumbs it:
//   MidiEvent.velocity -> NoteEvent.velocity (Engine, already wired)
//                       -> KeyAssigner held-key record (noteOn velocity)
//                       -> NoteDecision.velocity (the WINNING note's velocity)
//                       -> Voice::noteOn(note, velocity, ...) (was the hardcoded 1.0).
//
// Test-case display names ALL begin with "velocity_ingress" so
// `ctest -R velocity_ingress --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[velocity_ingress]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (real audible effect, measured on RENDERED OUTPUT — NOT just
// non-silent/deterministic — this is the assertion class the 162 audit found MISSING and
// the DispatchLfoModTest velocity case documented as BLOCKED on this seam):
//   * vel.enable ON, vel.depth>0: a HIGH-velocity note is audibly LOUDER (higher RMS) than
//     a LOW-velocity note — the velocity->VCA routing finally activates end-to-end;
//   * vel.enable ON, vel->VCF depth>0: a HIGH-velocity note is audibly BRIGHTER (more
//     high-harmonic energy through a partly-closed resonant filter) than a LOW one;
//   * vel.enable OFF: high vs low velocity render velocity-NEUTRAL (identical RMS) — the
//     162 dispatch gates on vel.enable, this task only supplies the real velocity;
//   * MONO velocity == the resolved (active) note's velocity (last-note priority winner);
//   * UNISON velocity == the resolved note's velocity (the one decision broadcast to U
//     voices), and a retrigger uses the NEW note's velocity;
//   * determinism + RT-safety (AudioThreadGuard, [rt]) over the dispatch+render path.
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
#include "voice/KeyAssigner.h"
#include "voice/VoiceManager.h"
#include "calibration/EngineConstants.h"

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

    // convertTo0to1(value): ((value-min)/span)^skew (the JUCE NormalisableRange the bridge
    // uses). The engine's contValueSkewed inverts this for skewed params.
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

// Render `seconds` of sustained audio for a held note at `vel`, returning the mono buffer.
// Drives the snapshot every block so the per-tick dispatch applies (the gate is held). The
// note-on velocity is the value under test — it must now reach the voice (this task).
std::vector<float> renderHeld(mw::Engine& eng, const mw::ParamSnapshot* snap,
                              int midiNote, double seconds, float vel,
                              int warmupBlocks = 8) {
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

// Goertzel power at frequency f over the buffer (single-bin DFT magnitude^2).
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

double midiHz(int n) noexcept { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}

// "Brightness": high-harmonic energy (sum of harmonics 4..10) normalized by the
// fundamental. A more-open filter (a hard key with vel->VCF) passes more high harmonics,
// raising this ratio. Measured on rendered output, isolating the velocity->cutoff effect.
double brightness(const std::vector<float>& x, double f0, double sr) noexcept {
    const double h1 = goertzelPower(x, f0, sr);
    double high = 0.0;
    for (int k = 4; k <= 10; ++k)
        high += goertzelPower(x, static_cast<double>(k) * f0, sr);
    return high / std::max(1e-12, h1);
}

constexpr float kSaneBound = 64.0f;

} // namespace

// ===========================================================================
// VELOCITY -> VCA (loudness): vel.enable ON + vel.depth>0, a HARD key is audibly LOUDER
// than a SOFT key (higher RMS). This is the headline the 162 dispatch was inert for — the
// per-note velocity now reaches the Voice through the KeyAssigner/NoteDecision. With the
// hardcoded-1.0 ingress this assertion was impossible (every note was full velocity).
// ===========================================================================
TEST_CASE("velocity_ingress: a hard key is louder than a soft key when velocity sensing is on",
          "[velocity_ingress]") {
    auto loudnessAt = [&](float vel) {
        Snap snap;
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);    // open: isolate the VCA effect
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        snap.setCont(mw::params::ids::kEnvSustain, 1.0f);   // flat sustain -> steady level
        snap.setChoice(mw::params::ids::kVelEnable, 1);     // sensing ON
        snap.setCont(mw::params::ids::kVelDepth, 1.0f);     // full velocity->VCA depth
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return rms(renderHeld(eng, &snap.s, 60, 0.30, vel));
    };

    const double hard = loudnessAt(1.0f);   // full velocity
    const double soft = loudnessAt(0.2f);   // soft key

    REQUIRE(hard > 0.0);
    REQUIRE(soft > 0.0);
    // depth 1 => the post-VCA scale is the velocity itself, so soft (~0.2) is far quieter
    // than hard (~1.0). The real per-note velocity now drives the VCA routing.
    REQUIRE(hard > soft * 2.5);
}

// ===========================================================================
// VELOCITY -> VCF (brightness): vel.enable ON + vel.depth>0 with a partly-closed resonant
// filter, a HARD key opens the cutoff more (velCutoffVolts = depth x velocity x oct) so its
// output carries more high-harmonic energy than a SOFT key. Measured as the high/fundamental
// ratio on rendered output. Activates the velocity->VCF leg of the 162 dispatch.
// ===========================================================================
TEST_CASE("velocity_ingress: a hard key is brighter than a soft key through the velocity to cutoff routing",
          "[velocity_ingress]") {
    const double f0 = midiHz(48);

    auto brightAt = [&](float vel) {
        Snap snap;
        // A partly-closed resonant filter so a cutoff shift is clearly audible in the
        // harmonic content; remove env/kbd cutoff modulation to isolate velocity->VCF.
        snap.setCont(mw::params::ids::kVcfCutoff, 0.30f);
        snap.setCont(mw::params::ids::kVcfResonance, 0.4f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        snap.setCont(mw::params::ids::kEnvSustain, 1.0f);
        snap.setChoice(mw::params::ids::kVelEnable, 1);     // sensing ON
        snap.setCont(mw::params::ids::kVelDepth, 1.0f);     // full depth (VCA + VCF)
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        // Use a low note so several harmonics sit inside the moving cutoff band.
        return brightness(renderHeld(eng, &snap.s, 48, 0.40, vel), f0, kSr);
    };

    const double hard = brightAt(1.0f);   // full velocity opens the filter
    const double soft = brightAt(0.2f);   // soft key keeps it closed

    REQUIRE(hard > 0.0);
    // A hard key opens the cutoff up to +2 octaves more than a soft key, so its high
    // harmonics pass far more strongly: the brightness ratio is clearly higher.
    REQUIRE(hard > soft * 1.5);
}

// ===========================================================================
// VELOCITY SENSING OFF: with vel.enable=0 the 162 dispatch zeroes the velocity terms, so a
// hard key and a soft key render velocity-NEUTRAL (identical). This proves the effect above
// is gated by vel.enable and that supplying the real velocity does not leak through when
// sensing is off (the neutral-1.0 contract). The ingress is correct, the routing is gated.
// ===========================================================================
TEST_CASE("velocity_ingress: velocity sensing off renders hard and soft keys identically",
          "[velocity_ingress]") {
    auto loudnessAt = [&](float vel) {
        Snap snap;
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        snap.setCont(mw::params::ids::kEnvSustain, 1.0f);
        snap.setChoice(mw::params::ids::kVelEnable, 0);     // sensing OFF
        snap.setCont(mw::params::ids::kVelDepth, 1.0f);     // depth ignored when off
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return rms(renderHeld(eng, &snap.s, 60, 0.30, vel));
    };

    const double hard = loudnessAt(1.0f);
    const double soft = loudnessAt(0.2f);

    REQUIRE(hard > 0.0);
    // vel.enable off => velocity terms are zero => velocity-neutral regardless of the key.
    REQUIRE(hard == Catch::Approx(soft).epsilon(0.001));
}

// ===========================================================================
// MONO: the resolved (winning) note's velocity reaches the voice. Under last-note priority
// (GateTrig, the default) the most-recently-pressed key wins; its velocity is what sounds.
// Hold a SOFT low note then press a HARD higher note (legato): the winner is the hard note,
// so the level rises to the hard-velocity level. Asserted directly on the KeyAssigner
// decision (the ingress contract) AND audibly on the rendered RMS.
// ===========================================================================
TEST_CASE("velocity_ingress: MONO resolves to the active note's velocity",
          "[velocity_ingress]") {
    // --- The model-level ingress contract: resolve() emits the winning note's velocity. ---
    {
        mw::KeyAssigner ka;
        ka.prepare();
        ka.setMode(mw::GateTrigMode::GateTrig);   // last-note priority (default)

        ka.noteOn(60, 0.2f);                       // soft low note
        mw::NoteDecision d = ka.resolve();
        REQUIRE(d.activeNote == 60);
        REQUIRE(d.velocity == Catch::Approx(0.2f));

        ka.noteOn(72, 0.95f);                      // hard high note, legato (60 still held)
        d = ka.resolve();
        REQUIRE(d.activeNote == 72);               // last-note priority winner
        REQUIRE(d.velocity == Catch::Approx(0.95f)); // the WINNER's velocity

        ka.noteOff(72);                            // release the winner; 60 (soft) still held
        d = ka.resolve();
        REQUIRE(d.activeNote == 60);
        REQUIRE(d.velocity == Catch::Approx(0.2f)); // fall back to the held note's velocity
    }

    // --- The audible end-to-end effect: the resolved note's velocity drives the level. ---
    auto loudnessForLegato = [&](float lowVel, float highVel) {
        Snap snap;
        snap.setCont(mw::params::ids::kVcfCutoff, 1.0f);
        snap.setCont(mw::params::ids::kVcfEnvMod, 0.0f);
        snap.setCont(mw::params::ids::kVcfKbdTrack, 0.0f);
        snap.setCont(mw::params::ids::kEnvSustain, 1.0f);
        snap.setChoice(mw::params::ids::kVelEnable, 1);
        snap.setCont(mw::params::ids::kVelDepth, 1.0f);
        // GateTrig (last-note priority) is the prepared default mode; press low then high.
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        constexpr int kBlk = 256;
        {   // soft low note
            Block on(kBlk);
            std::vector<mw::MidiEvent> ev{ noteOn(60, lowVel, 0) };
            auto c = on.ctx(ev, kBlk, &snap.s); eng.process(c);
        }
        {   // hard high note legato (60 still held) -> winner is 72 at highVel
            Block w(kBlk);
            std::vector<mw::MidiEvent> ev{ noteOn(72, highVel, 0) };
            auto c = w.ctx(ev, kBlk, &snap.s); eng.process(c);
        }
        std::vector<float> out;
        for (int b = 0; b < 24; ++b) {
            Block w(kBlk); std::vector<mw::MidiEvent> none;
            auto c = w.ctx(none, kBlk, &snap.s); eng.process(c);
            for (int i = 0; i < kBlk; ++i) out.push_back(w.L[static_cast<std::size_t>(i)]);
        }
        return rms(out);
    };

    // Winner = the HARD high note in both, but flip which is hard to show the winner's
    // velocity (not the first note's) drives the level.
    const double winnerHard = loudnessForLegato(0.2f, 1.0f);   // winner 72 @ 1.0 (loud)
    const double winnerSoft = loudnessForLegato(1.0f, 0.2f);   // winner 72 @ 0.2 (quiet)
    REQUIRE(winnerHard > winnerSoft * 2.0);   // the RESOLVED note's velocity sets the level
}

// ===========================================================================
// UNISON: the ONE NoteDecision is broadcast to U voices, so EVERY unison voice records the
// resolved note's velocity (driveUnison calls applyDecisionToVoice per voice with the same
// decision). Driven on the VoiceManager directly — the seam that broadcasts (the Engine
// exposes no public unison setter; UNISON is selected by ControlCore/params off this seam).
// Asserts each active voice's recorded velocity == the resolved velocity, plus the
// retrigger-uses-the-new-note's-velocity rule on a fresh higher key.
// ===========================================================================
TEST_CASE("velocity_ingress: UNISON broadcasts the resolved note velocity to every voice",
          "[velocity_ingress]") {
    constexpr double kVmSr = 48000.0;
    constexpr int    kVmOs = 2;
    constexpr std::uint32_t kVmSeed = 0x6d77656eu;
    constexpr int    kU = 4;

    mw::VoiceManager vm;
    vm.prepare(kVmSr, kVmOs, kVmSeed);
    vm.setMode(mw::VoiceMode::Unison);
    vm.setUnisonCount(kU);
    vm.setGateTrigMode(mw::GateTrigMode::GateTrig);   // last-note priority

    // Apply the latched mode/unison reconfig at the block boundary (top of render).
    {
        float l[64] = {0}, r[64] = {0};
        vm.render(l, r, 64);
    }

    // A hard note-on: the resolved velocity is broadcast to all U voices.
    vm.handleNoteEvent({mw::NoteEvent::Type::NoteOn, 57, 0.85f, 0});
    vm.controlTick();   // resolves the internal KeyAssigner + drives the U voices

    REQUIRE(vm.activeCount() == kU);
    for (int k = 0; k < vm.activeCount(); ++k) {
        const int vi = static_cast<int>(vm.activeIndex(k));
        INFO("unison voice index " << vi);
        REQUIRE(vm.voice(vi).currentVelocity() == Catch::Approx(0.85f));
        REQUIRE(vm.voice(vi).currentNote() == 57);
    }

    // Retrigger uses the NEW note's velocity: press a higher key (last-note winner) with a
    // different velocity; every unison voice retriggers on the new note at its velocity.
    vm.handleNoteEvent({mw::NoteEvent::Type::NoteOn, 64, 0.30f, 0});
    vm.controlTick();
    REQUIRE(vm.activeCount() == kU);
    for (int k = 0; k < vm.activeCount(); ++k) {
        const int vi = static_cast<int>(vm.activeIndex(k));
        INFO("unison voice index after retrigger " << vi);
        REQUIRE(vm.voice(vi).currentNote() == 64);
        REQUIRE(vm.voice(vi).currentVelocity() == Catch::Approx(0.30f));   // the NEW note's velocity
    }
}

// ===========================================================================
// DETERMINISM: two independently-prepared engines fed identical notes (at a non-trivial
// velocity) + a velocity-sensing snapshot produce bit-identical output through the
// dispatch+render path. The velocity ingress is a plain POD field — no nondeterminism.
// ===========================================================================
TEST_CASE("velocity_ingress: the velocity dispatch and render path is deterministic",
          "[velocity_ingress]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kVelEnable, 1);
    snap.setCont(mw::params::ids::kVelDepth, 0.7f);
    snap.setCont(mw::params::ids::kVcfCutoff, 0.5f);
    snap.setCont(mw::params::ids::kVcfResonance, 0.4f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeld(eng, &snap.s, 55, 0.15, 0.63f);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);
}

// ===========================================================================
// RT-SAFETY: the velocity ingress + dispatch + render allocate nothing and take no lock on
// the audio thread (the plumbing is a POD field through the existing path).
// ===========================================================================
TEST_CASE("velocity_ingress: the velocity ingress and dispatch is allocation and lock free under the guard",
          "[velocity_ingress][rt]") {
    Snap snap;
    snap.setChoice(mw::params::ids::kVelEnable, 1);
    snap.setCont(mw::params::ids::kVelDepth, 0.8f);
    snap.setCont(mw::params::ids::kVcfCutoff, 0.5f);
    snap.setCont(mw::params::ids::kVcfResonance, 0.3f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    Block warm(kBlk);
    std::vector<mw::MidiEvent> warmEv{ noteOn(64, 0.55f, 0) };
    auto warmCtx = warm.ctx(warmEv, kBlk, &snap.s);
    eng.process(warmCtx);
    REQUIRE(eng.voiceManager().activeCount() >= 1);

    // A second armed-window note-on to exercise the ingress (handleNoteEvent ->
    // keyAssigner_.noteOn(note, velocity) -> resolve -> noteOn) under the guard, plus a
    // release, all of which must be alloc/lock free.
    std::vector<Block> blocks; blocks.reserve(8);
    for (int b = 0; b < 8; ++b) blocks.emplace_back(kBlk);
    std::vector<std::vector<mw::MidiEvent>> evs; evs.reserve(8);
    for (int b = 0; b < 8; ++b) {
        if (b == 2)      evs.push_back({ noteOn(67, 0.9f, 8) });
        else if (b == 5) evs.push_back({ noteOff(67, 8) });
        else             evs.push_back({});
    }
    std::vector<mw::BlockContext> ctxs; ctxs.reserve(8);
    for (int b = 0; b < 8; ++b)
        ctxs.push_back(blocks[static_cast<std::size_t>(b)].ctx(
            evs[static_cast<std::size_t>(b)], kBlk, &snap.s));

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
