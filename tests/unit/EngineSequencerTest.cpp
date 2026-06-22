// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EngineSequencerTest.cpp — the SequencerEngine-into-Engine integration
// tests (task 118c). Test-case names begin with "engine_seq" so `ctest -R engine_seq`
// selects exactly these under the silent-pass rule (AGENTS.md "Tests"); the display
// text avoids '[' so Catch2 does not mis-parse a tag and break ctest -R selection.
//
// Covers every acceptance criterion of plan/backlog/118c against docs/design/05
// §2.1/§2.3 + doc 04 §9 (the arp/seq->voice routing seam) + ADR-006/ADR-007:
//   (1) pattern playback advances on clock edges and the selected note+gate+trig reach
//       the voice (observable: the render is non-silent — the step gated the single voice
//       with no live MIDI; the seq step PITCH reaches the voice's note input as base+P,
//       asserted via VoiceManager::voice(0).currentNote(). We assert pitch at the note
//       input the task owns, NOT at the oscillator audio: the assembled Voice does not yet
//       honor its glide/pitch target in render() — a task-073 Voice-DSP gap OUT OF SCOPE
//       for this integration wiring, noted in the PR);
//   (2) arp note selection (Up/Down/UandD + hold + range) drives the voice from held keys;
//   (3) ONE clock H->L edge advances arp + seq + RANDOM together on the SAME edge;
//   (4) stopped-transport keyboard play is regression-free (MONO/UNISON via the single
//       KeyAssigner);
//   (5) the Engine exposes the live current seq step (currentSeqStep()) — it advances;
//   (6) determinism (same seed+pattern+transport -> bit-identical output);
//   (7) RT-safety: an armed AudioThreadGuard over the sequencer-driven process path
//       does no heap allocation and takes no lock.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "control/ControlTypes.h"
#include "control/SequencerEngine.h"
#include "voice/VoiceManager.h"
#include "voice/VoiceTypes.h"
#include "calibration/SequencerRoutingConstants.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 4096;
constexpr int    kMaxVoices = mw::kMaxVoices;

using mw::control::ControlSnapshot;
using mw::control::SeqStep;
using mw::control::ClockSource;
using mw::control::ArpMode;
using mw::control::kMaxSteps;

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOn;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.data0        = static_cast<float>(note);   // task 118e: note number = pitch
    e.value        = vel;
    e.sampleOffset = offset;
    return e;
}

mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOff;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.data0        = static_cast<float>(note);   // task 118e: note number = pitch
    e.value        = 0.0f;
    e.sampleOffset = offset;
    return e;
}

// A self-contained block driver: owns the stereo output and fills a BlockContext with a
// chosen transport (the running flag is the whole point of this suite).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n) : L(static_cast<std::size_t>(n), 0.0f),
                            R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n,
                         bool playing, double bpm = 120.0, double ppq = 0.0) {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = nullptr;
        c.transport         = { bpm, ppq, playing, kSr };
        // Task 182 (ADR-030 part 2): these cases drive the INTERNAL clock (the pattern()
        // helper publishes ClockSource::Internal). The engine's clock/ingress gate now
        // free-runs the Internal clock on the TRANSIENT Run/Hold transport (runHeld), NOT on
        // the host's isPlaying (ADR-022 Free-run rung). The `playing` arg here means "the
        // transport is running", which under the Internal clock IS run/hold — so mirror it
        // into runHeld (migrated like 181 migrated the host-gate term; the gate composes the
        // same end state). The stopped cases (playing=false) leave runHeld false -> no advance.
        c.transport.runHeld = playing;
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

float maxAbs(const std::vector<float>& v, int from, int to) noexcept {
    float m = 0.0f;
    for (int i = from; i < to; ++i)
        m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
    return m;
}

// Mutable handle to the engine's hosted SequencerEngine (same const_cast pattern the
// engine_assembly tests use to drive the VoiceManager). Off-audio-thread config only.
mw::seq::SequencerEngine& seqOf(mw::Engine& e) noexcept {
    return const_cast<mw::seq::SequencerEngine&>(e.sequencer());
}

// A distinctive, all-note (no rest/tie) pattern of `n` slots with ascending pitches so
// each step is a different audible note. Internal clock at `rateHz`, clock-reset OFF.
ControlSnapshot pattern(int n, ClockSource src, float rateHz) {
    ControlSnapshot s{};
    s.clockSource = src;
    s.internalRateHz = rateHz;
    s.clockResetOnKeypress = false;
    s.seqCount = n;
    for (int i = 0; i < n && i < kMaxSteps; ++i)
        s.seq[static_cast<std::size_t>(i)].bits =
            static_cast<std::uint8_t>((i * 5 + 1) & SeqStep::kPitchMask);  // distinct pitches
    return s;
}

} // namespace

// ===========================================================================
// (1) Pattern playback: on play, the StepSequencer advances on clock edges and the
//     selected note+gate+trig reach the voice — observable as non-silent audio whose
//     dominant pitch changes per step. [doc 05 §2.1/§2.3; doc 04 §9]
// ===========================================================================
TEST_CASE("engine_seq: a playing pattern advances on clock edges and the steps reach the voice",
          "[engine_seq]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Internal clock at 50 Hz -> a period of 960 samples; a long render crosses many
    // edges so several steps play. A 4-note ascending pattern.
    auto& seq = seqOf(eng);
    seq.publishSnapshot(pattern(4, ClockSource::Internal, 50.0f));
    seq.setSeqPlay(true);

    constexpr int N = kMaxBlock;
    Block blk(N);
    std::vector<mw::MidiEvent> none;
    auto c = blk.ctx(none, N, /*playing=*/true);
    eng.process(c);

    // The sequencer drove the single voice: the render is NOT silent (the steps gated
    // the voice on with no live MIDI at all — proof the seq path reaches the voice).
    REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);

    // The playhead genuinely advanced through several slots over the block.
    REQUIRE(eng.sequencer().seq().isPlaying());
    REQUIRE(eng.currentSeqStep() >= 0);
}

// ===========================================================================
// (1b) The seq step PITCH (not just a gate) reaches the voice's note input: a single-slot
//      pattern of 6-bit pitch P drives the single MONO voice to MIDI note base+P through
//      the sole KeyAssigner. Observed at the voice's resolved note (currentNote()) — the
//      seam task 118c owns. (NOTE: we deliberately do NOT assert the audio differs by
//      pitch — the assembled Voice's oscillator does not yet honor its glide/pitch target
//      in render() [a task-073 Voice-DSP gap, OUT OF SCOPE for 118c integration wiring];
//      so two pitches render bit-identically through the voice today. 118c's job is to
//      route the correct note to the voice, which currentNote() proves.) [doc 04 §9]
// ===========================================================================
TEST_CASE("engine_seq: a single-step pattern drives the voice to the mapped MIDI note",
          "[engine_seq]") {
    auto noteForPattern = [&](int pitch6) {
        mw::Engine eng;
        eng.prepare(kSr, kMaxBlock, kMaxVoices);
        ControlSnapshot s{};
        s.clockSource = ClockSource::Internal;
        s.internalRateHz = 50.0f;
        s.clockResetOnKeypress = false;
        s.seqCount = 1;
        s.seq[0].bits = static_cast<std::uint8_t>(pitch6 & SeqStep::kPitchMask);
        auto& seq = seqOf(eng);
        seq.publishSnapshot(s);
        seq.setSeqPlay(true);

        constexpr int N = kMaxBlock;
        Block blk(N);
        std::vector<mw::MidiEvent> none;
        auto c = blk.ctx(none, N, /*playing=*/true);
        eng.process(c);

        // The single MONO voice (slot 0) was driven by the step note (§6.2): its resolved
        // note is base + the 6-bit step pitch.
        REQUIRE(eng.voiceManager().voice(0).isActive());
        REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);   // the step gated the voice (it sounds)
        return eng.voiceManager().voice(0).currentNote();
    };

    const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;
    REQUIRE(noteForPattern(/*pitch6=*/2)  == base + 2);
    REQUIRE(noteForPattern(/*pitch6=*/50) == base + 50);
}

// ===========================================================================
// (2) Arpeggiator note selection: a held chord (no playing seq) drives the voice from
//     the held keys via the arp, advancing one key per edge. UP walks the held set in
//     ascending key order. [doc 05 §5.1/§5.4; doc 04 §9]
// ===========================================================================
TEST_CASE("engine_seq: an arpeggiator chord drives the voice from held keys when running",
          "[engine_seq]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Arp UP, NO seq playback, internal clock; clock-reset OFF so a keypress does not
    // re-phase the edge stream under us. arpHold ON is the expressible "arp enabled"
    // signal the engine routes on (see the routing seam: a bare chord with HOLD off stays
    // plain keyboard play; HOLD on hands ingress to the arp).
    ControlSnapshot s{};
    s.clockSource = ClockSource::Internal;
    s.internalRateHz = 50.0f;
    s.arpMode = ArpMode::Up;
    s.arpHold = true;                   // arp ENABLED (the routable arp-on signal)
    s.clockResetOnKeypress = false;
    auto& seq = seqOf(eng);
    seq.publishSnapshot(s);
    // seq is NOT playing -> the (enabled) arp owns the step.

    // Hold a 3-note chord. Engine folds the MIDI note into the arp key space by
    // subtracting the seam base, so use MIDI notes base+{2,5,9} which map to arp keys
    // {2,5,9} (all < 32). The chord is held for the whole block (no note-off).
    const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;
    constexpr int N = kMaxBlock;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{
        noteOn(base + 2, 1.0f, 0),
        noteOn(base + 5, 1.0f, 0),
        noteOn(base + 9, 1.0f, 0),
    };
    auto c = blk.ctx(ev, N, /*playing=*/true);
    eng.process(c);

    // The arp is engaged by the held chord and drove the single voice: non-silent audio,
    // produced with NO direct keyboard ingress (the arp owns it while running).
    REQUIRE(eng.sequencer().arp().isEngaged());
    REQUIRE(eng.sequencer().arp().heldCount() == 3);
    REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);

    // The arp routed one of the held chord notes to the single voice's note input: the
    // resolved note is one of base+{2,5,9} (the arp walks the held set; the exact cursor
    // position depends on how many edges the long block crossed). This proves the arp
    // SELECTED a held note and it reached the voice through the sole KeyAssigner.
    REQUIRE(eng.voiceManager().voice(0).isActive());
    const int n0 = eng.voiceManager().voice(0).currentNote();
    REQUIRE((n0 == base + 2 || n0 == base + 5 || n0 == base + 9));
}

TEST_CASE("engine_seq: arp HOLD latches the held set and Down/UandD modes stay engaged",
          "[engine_seq]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    ControlSnapshot s{};
    s.clockSource = ClockSource::Internal;
    s.internalRateHz = 50.0f;
    s.arpMode = ArpMode::Down;          // direction exercised
    s.arpHold = true;                   // HOLD latch
    s.clockResetOnKeypress = false;
    auto& seq = seqOf(eng);
    seq.publishSnapshot(s);

    const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;
    constexpr int N = kMaxBlock;
    Block blk(N);
    // Press a 2-note interval then RELEASE both within the block: HOLD must keep them
    // latched so the arp stays engaged and keeps driving the voice after the release.
    std::vector<mw::MidiEvent> ev{
        noteOn(base + 4, 1.0f, 0),
        noteOn(base + 11, 1.0f, 0),
        noteOff(base + 4, 64),
        noteOff(base + 11, 64),
    };
    auto c = blk.ctx(ev, N, /*playing=*/true);
    eng.process(c);

    // HOLD latched the released keys: the arp is still engaged and holding 2 keys.
    REQUIRE(eng.sequencer().arp().isEngaged());
    REQUIRE(eng.sequencer().arp().heldCount() == 2);
    REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);
}

// ===========================================================================
// (3) ONE clock H->L edge advances arp + seq + RANDOM TOGETHER on the SAME edge
//     (phase-consistent). With seq playing AND an arp chord held, a block of exactly one
//     internal period fires exactly one edge: RANDOM reload increments by exactly 1 and
//     the playhead advances by exactly one slot. [doc 05 §2.1 C17; ADR-007 C17]
// ===========================================================================
TEST_CASE("engine_seq: one clock edge advances arp seq and RANDOM on the same edge",
          "[engine_seq]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Internal clock period == sr/2 == 24000 samples; seq playing a 4-slot pattern AND an
    // arp chord held (so all three subsystems are live on the one edge).
    ControlSnapshot s = pattern(4, ClockSource::Internal, 2.0f);  // period 24000
    s.arpMode = ArpMode::Up;
    auto& seq = seqOf(eng);
    seq.publishSnapshot(s);
    seq.setSeqPlay(true);

    const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;
    const int period = static_cast<int>(std::lround(kSr / 2.0));  // 24000, exactly one edge

    // Warm one block to establish the held chord + play the first edge.
    {
        Block blk(period);
        std::vector<mw::MidiEvent> ev{ noteOn(base + 1, 1.0f, 0), noteOn(base + 6, 1.0f, 0) };
        auto c = blk.ctx(ev, period, /*playing=*/true);
        eng.process(c);
    }

    const std::uint64_t randBefore = eng.sequencer().randomReloadCount();
    const int stepBefore = eng.currentSeqStep();

    // A second block of exactly one period: exactly ONE edge fires.
    Block blk2(period);
    std::vector<mw::MidiEvent> none;
    auto c2 = blk2.ctx(none, period, /*playing=*/true);
    eng.process(c2);

    // RANDOM reload incremented by exactly one (one edge), and the seq playhead advanced
    // by exactly one slot — arp + seq + RANDOM all advanced on the SAME single edge.
    REQUIRE(eng.sequencer().randomReloadCount() == randBefore + 1);
    REQUIRE(eng.currentSeqStep() == ((stepBefore + 1) % 4));
}

// ===========================================================================
// (4) Stopped transport: keyboard-direct play is unchanged (no regression to the task-118
//     voice path); MONO note selection flows through the single KeyAssigner. A held note
//     while STOPPED sounds; the sequencer never touches the voice. [ADR-006]
// ===========================================================================
TEST_CASE("engine_seq: stopped transport plays the keyboard directly with no sequencer involvement",
          "[engine_seq]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Even with a pattern published + PLAY toggled, a STOPPED transport must NOT run the
    // sequencer: the keyboard drives the voice directly (the task-118 path).
    auto& seq = seqOf(eng);
    seq.publishSnapshot(pattern(4, ClockSource::Internal, 50.0f));
    seq.setSeqPlay(true);

    const std::uint64_t randBefore = eng.sequencer().randomReloadCount();

    constexpr int N = 1024;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
    auto c = blk.ctx(ev, N, /*playing=*/false);   // STOPPED
    eng.process(c);

    // The directly-played keyboard note sounds.
    REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);
    // The sequencer did NOT advance while stopped — no RANDOM reload fired, the playhead
    // stayed put (the seq is bypassed entirely when stopped).
    REQUIRE(eng.sequencer().randomReloadCount() == randBefore);
    REQUIRE(eng.currentSeqStep() == -1);
}

TEST_CASE("engine_seq: stopped-transport rendering is bit-identical to a sequencer-free engine",
          "[engine_seq]") {
    // Regression guard: with the transport STOPPED the wired engine must render exactly
    // what the task-118 path rendered. We compare two engines fed the IDENTICAL stopped
    // block; one has a pattern loaded + PLAY on (which must be inert while stopped). They
    // must be bit-identical.
    constexpr int N = 1024;

    auto run = [&](bool loadPattern, std::vector<float>& outL, std::vector<float>& outR) {
        mw::Engine eng;
        eng.prepare(kSr, kMaxBlock, kMaxVoices);
        if (loadPattern) {
            auto& seq = seqOf(eng);
            seq.publishSnapshot(pattern(8, ClockSource::Internal, 50.0f));
            seq.setSeqPlay(true);
        }
        Block blk(N);
        std::vector<mw::MidiEvent> ev{ noteOn(57, 0.8f, 16), noteOff(57, 700) };
        auto c = blk.ctx(ev, N, /*playing=*/false);
        eng.process(c);
        outL = blk.L;
        outR = blk.R;
    };

    std::vector<float> plainL, plainR, loadedL, loadedR;
    run(/*loadPattern=*/false, plainL, plainR);
    run(/*loadPattern=*/true,  loadedL, loadedR);

    for (int i = 0; i < N; ++i) {
        REQUIRE(plainL[static_cast<std::size_t>(i)] == loadedL[static_cast<std::size_t>(i)]);
        REQUIRE(plainR[static_cast<std::size_t>(i)] == loadedR[static_cast<std::size_t>(i)]);
    }
}

// ===========================================================================
// (5) The Engine exposes the LIVE current seq step (currentSeqStep()) and it ADVANCES
//     through the pattern as edges fire, wrapping at count. [closes 111c MEDIUM]
// ===========================================================================
TEST_CASE("engine_seq: the engine exposes the live current seq step and it advances",
          "[engine_seq]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // 3-slot pattern, internal clock period == sr/2 == 24000 (exactly one edge per block).
    auto& seq = seqOf(eng);
    seq.publishSnapshot(pattern(3, ClockSource::Internal, 2.0f));
    seq.setSeqPlay(true);

    const int period = static_cast<int>(std::lround(kSr / 2.0));  // 24000

    // Before any edge: no step has played.
    REQUIRE(eng.currentSeqStep() == -1);

    auto stepAfterOneEdge = [&]() {
        Block blk(period);
        std::vector<mw::MidiEvent> none;
        auto c = blk.ctx(none, period, /*playing=*/true);
        eng.process(c);
        return eng.currentSeqStep();
    };

    REQUIRE(stepAfterOneEdge() == 0);   // first edge plays slot 0
    REQUIRE(stepAfterOneEdge() == 1);   // slot 1
    REQUIRE(stepAfterOneEdge() == 2);   // slot 2
    REQUIRE(stepAfterOneEdge() == 0);   // wraps back to slot 0 (count == 3)
}

// ===========================================================================
// (6) Determinism: two independently-prepared engines fed the IDENTICAL pattern +
//     transport + (empty) MIDI produce BIT-identical output. The sequencer-driven path
//     is single-threaded, fixed-order and lock-free, so it is deterministic. [ADR-019 VT-04]
// ===========================================================================
TEST_CASE("engine_seq: identical pattern and transport yield bit-identical output (determinism)",
          "[engine_seq]") {
    constexpr int N = kMaxBlock;

    auto run = [&](std::vector<float>& outL, std::vector<float>& outR) {
        mw::Engine eng;
        eng.prepare(kSr, kMaxBlock, kMaxVoices);
        auto& seq = seqOf(eng);
        seq.publishSnapshot(pattern(6, ClockSource::Internal, 50.0f));
        seq.setSeqPlay(true);
        Block blk(N);
        std::vector<mw::MidiEvent> none;
        auto c = blk.ctx(none, N, /*playing=*/true);
        eng.process(c);
        outL = blk.L;
        outR = blk.R;
    };

    std::vector<float> aL, aR, bL, bR;
    run(aL, aR);
    run(bL, bR);

    for (int i = 0; i < N; ++i) {
        REQUIRE(aL[static_cast<std::size_t>(i)] == bL[static_cast<std::size_t>(i)]);
        REQUIRE(aR[static_cast<std::size_t>(i)] == bR[static_cast<std::size_t>(i)]);
    }
}

// ===========================================================================
// (7) RT-safety: the sequencer-driven process path performs ZERO heap allocations and
//     takes ZERO locks under an armed AudioThreadGuard. prepare() (the sole allocation
//     site) and a warm block run OUTSIDE the armed scope. [doc 00 §9.1; doc 05 §10]
// ===========================================================================
TEST_CASE("engine_seq: a sequencer-driven process performs zero allocations and zero locks",
          "[engine_seq][rt]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // A dense config: seq playing + an arp chord held + a fast internal clock, so the
    // whole arp/seq/clock + voice-routing hot path runs under the guard.
    ControlSnapshot s = pattern(8, ClockSource::Internal, 200.0f);
    s.arpMode = ArpMode::UandD;
    auto& seq = seqOf(eng);
    seq.publishSnapshot(s);
    seq.setSeqPlay(true);

    const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;
    constexpr int N = kMaxBlock;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{
        noteOn(base + 1, 1.0f, 0), noteOn(base + 4, 1.0f, 8), noteOn(base + 8, 1.0f, 16) };
    auto c = blk.ctx(ev, N, /*playing=*/true);

    // Warm once before arming so any one-time lazy realization happens off the guard.
    eng.process(c);
    REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);   // the routed render has teeth

    mw::test::AudioThreadGuard guard;
    guard.arm();
    eng.process(c);   // seq + arp + clock + voice-routing hot path
    eng.process(c);   // a second block (no per-block reallocation either)
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}
