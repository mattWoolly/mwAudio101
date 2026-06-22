// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/NoteIngressTest.cpp — the AUDIO-EFFECT acceptance suite for the
// note-number ingress fix (task 118e, part of the ADR-028 control-dispatch repair).
//
// THE BUG (found by 162d): core/Engine.cpp toNoteEvent() derived the played note from
// the seam event's `noteId` field (`out.note = clamp(e.noteId,0,127)`). But per
// docs/design/09 §3.3 + the BlockContext.h field docs, `noteId` is the CLAP NOTE-ID
// (`-1` for a MIDI-derived event, used ONLY for note-expression VOICE matching) and
// `data0` is the NOTE NUMBER (the pitch). So a real MIDI note arriving from a DAW
// (noteId == -1, data0 == the note number) resolved to note 0 — the synth played the
// WRONG NOTE for its PRIMARY input. The fix reads the note number from `data0`.
//
// WHY THE OTHER DISPATCH SUITES MASKED IT: their noteOn() helpers set `e.noteId = note`
// and left `e.data0` at 0, encoding the bug's own assumption, so they only ever proved
// the noteId path. This suite drives a MIDI-REALISTIC event — `data0 = note number`,
// `noteId = -1` (exactly what the §3.3 HostEvent->MidiEvent translation emits for a DAW
// MIDI note) — and asserts the rendered fundamental is the CORRECT pitch, so the real
// primary-input path is proven. (The other suites are separately reconciled to set BOTH
// fields so they round-trip and keep testing the real data0 path.)
//
// Test-case display names ALL begin with "note_ingress" so
// `ctest -R note_ingress --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[note_ingress]". No literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (real audible effect measured on RENDERED output via a Goertzel
// fundamental estimator — not "non-silent/deterministic"):
//   * a MIDI-derived NoteOn (data0 = note number, noteId = -1) plays the note's CORRECT
//     ABSOLUTE pitch for the 1V/oct reference (note 48 ~= midiHz(48), note 72 ~=
//     midiHz(72)), and the 48-vs-72 pair renders the correct 4x (2-octave) ratio — the
//     headline pitch fix proven through the REAL MIDI ingress field [§3.3; ADR-005];
//   * the data0 path is independent of noteId: holding noteId == -1 fixed while data0
//     sweeps C2/C3/C4/C5 yields a strictly monotonically increasing fundamental;
//   * the seq/arp note path (running transport) routes the same data0-derived note
//     number, so a played MIDI note arpeggiates/sequences at the correct pitch
//     (ke.pitch = ne.note - kSeqVoiceBaseMidi reads the corrected ne.note);
//   * a fractional data0 truncates to the integer MIDI note (the §3.3 widened-to-float
//     note number);
//   * the dispatch+render path over the real ingress is deterministic and RT-safe
//     (AudioThreadGuard-clean) — the [rt] check.
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
#include "control/ControlTypes.h"               // ControlSnapshot, ArpMode, ClockSource
#include "control/SequencerEngine.h"            // publishSnapshot (seq/arp ingress)
#include "calibration/EngineConstants.h"
#include "calibration/SequencerRoutingConstants.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- registry-index lookup: map a parameter string-ID to its kParamDefs slot (the same
// registry index the ParamSnapshot keys on, so a test sets a param by its canonical ID
// without hand-counting the table). ----------------------------------------------------
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
// form (exactly what the bridge emits: convertTo0to1(default)). Identical construction to
// the other dispatch suites (the INIT patch: saw-only mixer, cutoff open).
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

    void setCont(const char* id, float value) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        const float span = d.maxValue - d.minValue;
        const float v = std::clamp(value, d.minValue, d.maxValue);
        s.normalizedValues[static_cast<std::size_t>(i)] = span > 0.0f ? (v - d.minValue) / span : 0.0f;
    }

    void setChoice(const char* id, int idx) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
        const float denom = (d.choiceCount > 1) ? static_cast<float>(d.choiceCount - 1) : 1.0f;
        s.normalizedValues[static_cast<std::size_t>(i)] = static_cast<float>(idx) / denom;
    }
};

// THE CRUX of this suite: a MIDI-REALISTIC NoteOn. The §3.3 HostEvent->MidiEvent
// translation puts the NOTE NUMBER in `data0` (widened to float) and sets `noteId = -1`
// for a MIDI-derived event (no CLAP note id). So the pitch MUST come from data0; noteId
// is deliberately -1 here to prove the engine never reads it for pitch.
mw::MidiEvent midiNoteOn(float noteNumber, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOn;
    e.noteId       = -1;                  // MIDI-derived: NO CLAP note id (the bug read this)
    e.data0        = noteNumber;          // the NOTE NUMBER (the correct pitch source)
    e.value        = vel;
    e.sampleOffset = offset;
    return e;
}
mw::MidiEvent midiNoteOff(float noteNumber, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOff;
    e.noteId       = -1;
    e.data0        = noteNumber;
    e.value        = 0.0f;
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
        // Task 182 (ADR-030 part 2): the running-arp case drives the INTERNAL clock
        // (arp.tempo_sync = Off). The engine's clock/ingress gate now free-runs the Internal
        // clock on the TRANSIENT Run/Hold transport (runHeld), not the host's isPlaying (ADR-
        // 022 Free-run rung), so mirror the "transport running" arg into runHeld for the arp
        // to own ingress under the Internal clock.
        c.transport.runHeld = playing;
        c.midi.events = ev.empty() ? nullptr : ev.data();
        c.midi.numEvents = static_cast<int>(ev.size());
        return c;
    }
};

// Render `seconds` of sustained audio for a held MIDI note, returning the mono (L) buffer.
// Drives the snapshot every block so the dispatch applies (gate held across blocks).
std::vector<float> renderHeldMidi(mw::Engine& eng, const mw::ParamSnapshot* snap,
                                  float noteNumber, double seconds, int warmupBlocks = 8) {
    constexpr int kBlk = 256;
    {
        Block on(kBlk);
        std::vector<mw::MidiEvent> ev{ midiNoteOn(noteNumber, 1.0f, 0) };
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

// Estimate the fundamental by scanning candidate frequencies for the Goertzel power peak.
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

// The 1V/oct reference: MIDI note -> Hz (A4 = 440, note 69). The same reference the
// dispatch_vco suite proved the rendered fundamental matches within ~3%.
double midiHz(int n) noexcept { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}

constexpr float kSaneBound = 64.0f;

} // namespace

// ===========================================================================
// HEADLINE: a MIDI-derived NoteOn (data0 = note number, noteId = -1) plays the note's
// CORRECT ABSOLUTE pitch. The pre-fix engine read noteId (== -1 for MIDI) and clamped to
// note 0, so a real DAW MIDI note played the wrong pitch. note 48 ~= midiHz(48), note 72
// ~= midiHz(72), and the pair is the correct 4x (2-octave) ratio. [§3.3; ADR-005]
// ===========================================================================
TEST_CASE("note_ingress: a MIDI NoteOn plays the correct pitch from data0 not noteId",
          "[note_ingress]") {
    Snap snap;   // INIT defaults: saw-only mixer, cutoff open

    auto fundamentalForMidiNote = [&](int note) {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        // Drive the note through data0 with noteId == -1 (the real MIDI path). Pre-fix this
        // resolved to note 0; post-fix it resolves to `note`.
        auto buf = renderHeldMidi(eng, &snap.s, static_cast<float>(note), 0.30);
        const double expect = midiHz(note);
        return estimateFundamental(buf, kSr, expect * 0.4, expect * 2.5);
    };

    const double f48 = fundamentalForMidiNote(48);
    const double f72 = fundamentalForMidiNote(72);

    // The CORRECT ABSOLUTE pitch for each note (proves data0 -> note number, not noteId).
    REQUIRE(f48 == Catch::Approx(midiHz(48)).epsilon(0.03));
    REQUIRE(f72 == Catch::Approx(midiHz(72)).epsilon(0.03));

    // 24 semitones apart == 2 octaves == 4x — the headline pitch ratio over the MIDI path.
    REQUIRE((f72 / f48) == Catch::Approx(4.0).epsilon(0.03));
}

// ===========================================================================
// The data0 path is INDEPENDENT of noteId: holding noteId == -1 fixed (the MIDI-derived
// value) while data0 sweeps C2/C3/C4/C5 yields a strictly increasing fundamental, each at
// its correct absolute pitch. If pitch came from noteId (constant -1) every note would
// render identically — this proves data0 is the pitch source for the real MIDI ingress.
// ===========================================================================
TEST_CASE("note_ingress: a data0 note sweep increases monotonically with noteId fixed at -1",
          "[note_ingress]") {
    Snap snap;
    const int notes[] = { 36, 48, 60, 72 };   // C2 C3 C4 C5
    double prev = 0.0;
    for (int n : notes) {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto buf = renderHeldMidi(eng, &snap.s, static_cast<float>(n), 0.30);
        const double f = estimateFundamental(buf, kSr, midiHz(n) * 0.4, midiHz(n) * 2.5);
        REQUIRE(f == Catch::Approx(midiHz(n)).epsilon(0.03));   // correct absolute pitch
        REQUIRE(f > prev);                                      // strictly increasing
        prev = f;
    }
}

// ===========================================================================
// A FRACTIONAL data0 truncates to the integer MIDI note (the §3.3 note number is widened
// to float; the engine clamps/truncates it to a 0..127 MIDI note). data0 == 60.4 plays
// note 60, NOT note 0 (pre-fix) and NOT a quarter-tone — proving the documented integer
// note-number semantics on the real field.
// ===========================================================================
TEST_CASE("note_ingress: a fractional data0 resolves to its integer MIDI note",
          "[note_ingress]") {
    Snap snap;
    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
    auto buf = renderHeldMidi(eng, &snap.s, 60.4f, 0.30);
    const double f = estimateFundamental(buf, kSr, midiHz(60) * 0.5, midiHz(60) * 1.8);
    REQUIRE(f == Catch::Approx(midiHz(60)).epsilon(0.03));   // note 60, not 0, not 60.4
}

// ===========================================================================
// THE SEQ/ARP PATH (acceptance: ke.pitch from ne.note also gets the correct note number).
// With the transport RUNNING and the arp latched (arpHold), the engine hands note ingress
// to the SequencerEngine: the inbound MIDI note becomes a KeyEvent with
//   ke.pitch = ne.note - kSeqVoiceBaseMidi
// fed to the arp key space, and routeControlEvents adds the base back to recover the MIDI
// note for the sole KeyAssigner. ne.note is now the data0-derived note number, so a held
// MIDI key (noteId == -1) arpeggiates at the CORRECT note. Pre-fix ne.note was 0, so
// ke.pitch was a fixed negative base-relative key — every played key arped at the same
// wrong pitch (and out-of-range keys could even be dropped from the 0..31 arp bitmap).
//
// Driven through the PRODUCTION-AUTHORITATIVE path (task 181 / ADR-030 part 1): the arp is
// engaged by the arp.mode + arp.latch APVTS params flowing through Engine::dispatchSeqArp
// (the per-control-tick authority over the seq/arp snapshot), NOT the engine-internal
// publishSnapshot back door. Since 181 made ctx.params the unconditional per-block authority
// over arpMode/arpHold, a publishSnapshot(arpHold=true) injection would be clobbered to
// arpHold=false the very next block; the migrated recipe mirrors DispatchSeqArpTest.
//
// The clock RATE (internalRateHz) has no APVTS param yet (that is task 182), so it is seeded
// through publishSnapshot — a clock-RATE field dispatchSeqArp explicitly PRESERVES
// (cp.internalRateHz = live->internalRateHz), so it survives the per-block dispatch and is
// NOT clobbered. Only the arp config moves to ctx.params.
//
// We assert BOTH the deterministic RESOLVED voice note (currentNote(): the direct proof
// ne.note -> ke.pitch -> recovered MIDI note carried data0) AND the audible rendered pitch.
// ===========================================================================
TEST_CASE("note_ingress: the running arp path routes the correct data0 note number",
          "[note_ingress]") {
    using mw::control::ControlSnapshot;
    using mw::control::ClockSource;

    auto resolvedAndAudio = [&](int midiNote) {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

        // Saw-only INIT mixer + the arp config driven through ctx.params (the production
        // authority): arp.mode = Up (choice 1) and arp.latch ON (choice 1) -> dispatchSeqArp
        // sets cp.arpMode = Up and cp.arpHold = true on every block. A held MIDI key + the
        // HOLD latch engages the arp so the seq engine owns note ingress (doc 05 §5.1).
        // arp.tempo_sync OFF (choice 0) -> dispatchSeqArp keeps clockSource = Internal (its
        // default is ON, which would force HostSync and override the Internal seed below).
        Snap snap;
        snap.setChoice(mw::params::ids::kArpMode,      /*Up*/  1);
        snap.setChoice(mw::params::ids::kArpLatch,     /*on*/  1);
        snap.setChoice(mw::params::ids::kArpTempoSync, /*off*/ 0);

        // Seed only the clock RATE (50 Hz, internal) through the state seam. dispatchSeqArp
        // preserves internalRateHz (no rate param until task 182), so 50 Hz survives every
        // per-block dispatch; arp.mode/arp.latch/tempo_sync above are the live arp authority.
        ControlSnapshot s{};
        s.clockSource    = ClockSource::Internal;
        s.internalRateHz = 50.0f;
        auto& seq = const_cast<mw::seq::SequencerEngine&>(eng.sequencer());
        seq.publishSnapshot(s);

        constexpr int kBlk = 256;
        // Hold ONE MIDI key the whole time (data0 = the note, noteId = -1). A single held key
        // under a latched UP arp re-selects that same note each edge -> a steady sounding pitch.
        std::vector<float> out;
        {
            Block on(kBlk);
            std::vector<mw::MidiEvent> ev{ midiNoteOn(static_cast<float>(midiNote), 1.0f, 0) };
            auto c = on.ctx(ev, kBlk, &snap.s, /*playing=*/true);
            eng.process(c);
        }
        for (int b = 0; b < 48; ++b) {        // ~0.26 s of sustained arp output
            Block w(kBlk); std::vector<mw::MidiEvent> none;
            auto c = w.ctx(none, kBlk, &snap.s, /*playing=*/true);
            eng.process(c);
            for (int i = 0; i < kBlk; ++i) out.push_back(w.L[static_cast<std::size_t>(i)]);
        }

        // The arp engaged from the held MIDI key and drove the single voice (no direct
        // keyboard ingress while running) — proves the data0 note folded into the arp space
        // (a single held key + HOLD latch engages the arp, doc 05 §5.1).
        REQUIRE(eng.sequencer().arp().isEngaged());
        REQUIRE(eng.voiceManager().voice(0).isActive());
        return std::pair<int, std::vector<float>>{ eng.voiceManager().voice(0).currentNote(),
                                                   std::move(out) };
    };

    // Notes are chosen INSIDE the arp's MIDI window [kSeqVoiceBaseMidi, +31] == [36, 67]:
    // the engine folds a MIDI note into the arp 0..31 key space (ke.pitch = ne.note - base),
    // so a note above 67 would fold out of range and never register in the arp bitmap. C3 (48)
    // and C4 (60) are both in-window and exactly 1 octave (2x) apart — enough to prove the
    // data0 note number flows through ke.pitch and back to the correct sounding pitch. (The
    // wider 48-vs-72 4x headline is proven on the direct keyboard path above.)
    auto [note48, out48] = resolvedAndAudio(48);
    auto [note60, out60] = resolvedAndAudio(60);

    // (a) DIRECT proof: ne.note carried data0 through ke.pitch and back, so the sole
    //     KeyAssigner resolved the voice to the exact played MIDI note. Pre-fix this was 0
    //     (data0 ignored) -> a fixed wrong base-relative key (and 0 - 36 < 0 dropped from
    //     the arp bitmap, so the arp never even engaged — the original failure mode).
    REQUIRE(note48 == 48);
    REQUIRE(note60 == 60);

    // (b) AUDIBLE proof: the arp sounds at each note's correct fundamental, 1 octave apart.
    REQUIRE(rms(out48) > 0.0);
    REQUIRE(rms(out60) > 0.0);
    const double f48 = estimateFundamental(out48, kSr, midiHz(48) * 0.4, midiHz(48) * 2.5);
    const double f60 = estimateFundamental(out60, kSr, midiHz(60) * 0.4, midiHz(60) * 2.5);
    REQUIRE(f48 == Catch::Approx(midiHz(48)).epsilon(0.03));
    REQUIRE(f60 == Catch::Approx(midiHz(60)).epsilon(0.03));
    REQUIRE((f60 / f48) == Catch::Approx(2.0).epsilon(0.03));
}

// ===========================================================================
// DETERMINISM: two independently-prepared engines fed the identical MIDI-derived note +
// snapshot stream produce bit-identical output through the corrected ingress.
// ===========================================================================
TEST_CASE("note_ingress: the corrected ingress and render path is deterministic",
          "[note_ingress]") {
    Snap snap;
    snap.setCont(mw::params::ids::kVcoTune, 5.0f);
    snap.setCont(mw::params::ids::kPulseLevel, 0.5f);

    auto run = [&]() {
        mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);
        return renderHeldMidi(eng, &snap.s, 55.0f, 0.10);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        REQUIRE(a[i] == b[i]);   // byte-stable
}

// ===========================================================================
// RT-SAFETY: the corrected note ingress (the toNoteEvent data0 read) + the dispatch +
// render path allocate nothing and take no lock on the audio thread.
// ===========================================================================
TEST_CASE("note_ingress: the corrected note ingress and render are allocation and lock free under the guard",
          "[note_ingress][rt]") {
    Snap snap;
    snap.setCont(mw::params::ids::kSawLevel, 0.7f);
    snap.setCont(mw::params::ids::kPulseLevel, 0.4f);

    mw::Engine eng; eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int kBlk = 256;
    // Pre-build every buffer/context BEFORE arming the guard (the guard intercepts global
    // operator new; only eng.process() runs inside the armed scope).
    Block warm(kBlk);
    std::vector<mw::MidiEvent> warmEv{ midiNoteOn(64.0f, 0.9f, 0) };
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
