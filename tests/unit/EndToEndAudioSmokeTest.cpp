// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EndToEndAudioSmokeTest.cpp — the end-to-end audio smoke test (task 133).
// Test-case names begin with "e2e_smoke" so `ctest -R e2e_smoke --no-tests=error`
// selects them under the silent-pass rule (AGENTS.md "Tests"). The display names avoid
// any literal '[' so Catch2 does not mis-parse a tag out of the name.
//
// Objective (plan/backlog/133): drive the assembled Engine HEADLESSLY through the POD
// seam — a BlockContext carrying AudioBlockView / ParamSnapshot* / TransportInfo /
// MidiEventView with a note-on — and assert the full graph (docs/design/00 §4.1)
// yields finite, non-silent, BOUNDED mono output during the note and returns toward
// silence after note-off, across a sequence of varied valid block sizes <= maxBlockSize.
//
// Acceptance coverage (each criterion is an explicit assertion below):
//   1. A note-on through the full graph (§4.1) yields finite, non-silent output and
//      silence-after-release — driven over the same prepare/process/reset seam the shell
//      uses (§5.1, §9.3), with an objective release oracle: every voice returns to Idle
//      (VoiceManager::activeCount() == 0) and the steady-state output goes silent.
//   2. The test binary links mwcore ONLY with no JUCE / plugin / audio-device dependency
//      (§9.3) — this file includes ONLY core/ headers; the tests/CMakeLists links mwcore
//      and Catch2 alone, so the link closure is asserted by construction at build time.
//   3. Runs over varied valid block sizes <= maxBlockSize without tripping the RT guards
//      — a streamed render across a mix of small/large/odd block sizes (§4.4), each <=
//      maxBlockSize, under an armed AudioThreadGuard (§9.1 RT-1/RT-2/RT-6).
//   4. (selector) ctest -R e2e_smoke --no-tests=error is green; names begin with e2e_smoke.
//
// Out of scope (other tasks; NOT asserted here): bit-exact golden comparison
// (golden-harness), cross-format equivalence (integration-9), parameter-by-parameter
// audit. The ParamSnapshot is the seam's immutable-snapshot pointer (§5.4); this
// assembly's voice path is event-driven and does not consume it, so a nullptr snapshot
// mirrors the real shell contract for an init patch (the Engine holds only the pointer).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "voice/VoiceTypes.h"
#include "calibration/EngineConstants.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- seam-side event builders (host->POD marshalling already happened in plugin/) ---
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

// One block of stereo host-borrowed output + a BlockContext factory. Mirrors the real
// shell: the core only ever sees borrowed channel pointers + value-typed PODs (§5.3).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n)
        : L(static_cast<std::size_t>(n), 0.0f),
          R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n) noexcept {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels  = 2;
        c.audio.numFrames    = n;
        c.params             = nullptr;             // immutable-snapshot pointer (§5.4)
        c.transport          = mw::TransportInfo{ 120.0, 0.0, true, kSr };
        c.midi.events        = events.empty() ? nullptr : events.data();
        c.midi.numEvents     = static_cast<int>(events.size());
        return c;
    }
};

// Peak magnitude over [from,to) — the non-silence / boundedness probe.
float maxAbs(const std::vector<float>& v, int from, int to) noexcept {
    float m = 0.0f;
    for (int i = from; i < to; ++i)
        m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
    return m;
}

// True iff every sample in [0,n) is finite (no NaN, no +/-Inf) — RT output hygiene.
bool allFinite(const std::vector<float>& v, int n) noexcept {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(v[static_cast<std::size_t>(i)])) return false;
    return true;
}

// A generous bound: any well-behaved monophonic voice + dry FX stays inside this.
// (The acceptance criterion is "bounded", not a calibrated ceiling; this just catches
// runaway/blowup output. It is deliberately loose, not a (PI) sonic constant.)
constexpr float kSaneBound = 64.0f;

} // namespace

// ---------------------------------------------------------------------------
// §4.1 / §9.3 — a note-on through the FULL assembled graph yields finite, non-silent,
// bounded mono output; after note-off the voice path returns to silence. The release
// is verified by an OBJECTIVE oracle (every voice back to Idle) AND by the audio going
// quiet, so "silence-after-release" is not merely an amplitude heuristic.
// ---------------------------------------------------------------------------
TEST_CASE("e2e_smoke: note-on drives finite non-silent output then silence after release",
          "[e2e_smoke]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    REQUIRE(eng.isPrepared());
    REQUIRE(eng.sampleRate() == kSr);

    // --- Phase 1: note-on. Render one block with the note-on at the head; the full
    // graph (VCO -> mixer -> oversampled ladder/VCA-drive -> VCA -> FX-dry) must
    // produce finite, non-silent, bounded audio. -------------------------------------
    constexpr int kOnBlock = 256;
    Block on(kOnBlock);
    std::vector<mw::MidiEvent> onEv{ noteOn(60, 1.0f, 0) };
    auto onCtx = on.ctx(onEv, kOnBlock);
    eng.process(onCtx);

    REQUIRE(allFinite(on.L, kOnBlock));
    REQUIRE(allFinite(on.R, kOnBlock));
    const float onPeak = std::max(maxAbs(on.L, 0, kOnBlock), maxAbs(on.R, 0, kOnBlock));
    REQUIRE(onPeak > 0.0f);                  // non-silent: the note sounds (§4.1)
    REQUIRE(onPeak < kSaneBound);            // bounded: no blowup
    REQUIRE(eng.voiceManager().activeCount() >= 1); // a voice is sounding

    // --- Phase 2: sustain for a few blocks; output stays finite, non-silent, bounded.
    for (int b = 0; b < 8; ++b) {
        Block sus(kOnBlock);
        std::vector<mw::MidiEvent> none;
        auto susCtx = sus.ctx(none, kOnBlock);
        eng.process(susCtx);
        REQUIRE(allFinite(sus.L, kOnBlock));
        REQUIRE(allFinite(sus.R, kOnBlock));
        REQUIRE(maxAbs(sus.L, 0, kOnBlock) < kSaneBound);
        REQUIRE(maxAbs(sus.R, 0, kOnBlock) < kSaneBound);
    }
    REQUIRE(eng.voiceManager().activeCount() >= 1); // still sounding while gated

    // --- Phase 3: note-off, then render until the voice path drains. The default
    // release is short (~0.1 s); we render a bounded number of blocks (~3 s ceiling)
    // and require the voice to return to Idle and the tail to fall silent. --------
    {
        Block off(kOnBlock);
        std::vector<mw::MidiEvent> offEv{ noteOff(60, 0) };
        auto offCtx = off.ctx(offEv, kOnBlock);
        eng.process(offCtx);
        REQUIRE(allFinite(off.L, kOnBlock));
        REQUIRE(allFinite(off.R, kOnBlock));
    }

    // Bounded drain loop: ceiling is ~3 s of audio so the test can never hang even if
    // a regression left a stuck voice — it just fails the post-loop assertions.
    const int kMaxDrainBlocks =
        static_cast<int>((kSr * 3.0) / static_cast<double>(kOnBlock)) + 1;
    float lastPeak = 1.0f;
    int   drained  = 0;
    for (int b = 0; b < kMaxDrainBlocks; ++b) {
        Block tail(kOnBlock);
        std::vector<mw::MidiEvent> none;
        auto tailCtx = tail.ctx(none, kOnBlock);
        eng.process(tailCtx);
        REQUIRE(allFinite(tail.L, kOnBlock));
        REQUIRE(allFinite(tail.R, kOnBlock));
        lastPeak = std::max(maxAbs(tail.L, 0, kOnBlock), maxAbs(tail.R, 0, kOnBlock));
        if (eng.voiceManager().activeCount() == 0 && lastPeak == 0.0f) {
            drained = b + 1;
            break;
        }
    }

    // Objective release oracle: every voice has returned to Idle (§4.1 VCA -> Idle on
    // release end) and the steady-state output is exactly silent.
    REQUIRE(eng.voiceManager().activeCount() == 0);
    REQUIRE(lastPeak == 0.0f);
    REQUIRE(drained > 0);   // it actually drained inside the bounded ceiling

    // A subsequent silent block stays silent (no resurrection / stuck tail).
    Block postSilence(kOnBlock);
    std::vector<mw::MidiEvent> none;
    auto postCtx = postSilence.ctx(none, kOnBlock);
    eng.process(postCtx);
    REQUIRE(maxAbs(postSilence.L, 0, kOnBlock) == 0.0f);
    REQUIRE(maxAbs(postSilence.R, 0, kOnBlock) == 0.0f);
}

// ---------------------------------------------------------------------------
// §4.4 / §9.1 — the same note-on, streamed over a sequence of VARIED valid block sizes
// (all <= maxBlockSize, including the cap, sub-kRenderBlock sizes, and odd sizes), runs
// without tripping the RT guards: under an armed AudioThreadGuard there is zero heap
// allocation and zero lock acquisition, and the output stays finite and bounded. This
// also exercises that the gate is held across block boundaries (the note remains audible
// across the varied stream). [§9.1 RT-1/RT-2/RT-6; §4.4]
// ---------------------------------------------------------------------------
TEST_CASE("e2e_smoke: streamed render over varied block sizes stays clean under the guard",
          "[e2e_smoke]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // A deliberate spread of valid block sizes, all <= maxBlockSize: the worst-case
    // cap, sizes below and around the internal kRenderBlock chunk cap, and odd sizes
    // that are not multiples of the chunk cap (§4.4 split/chunk robustness).
    const std::vector<int> sizes = {
        kMaxBlock, 1, 7, mw::cal::engine::kRenderBlock,
        mw::cal::engine::kRenderBlock - 1, mw::cal::engine::kRenderBlock + 1,
        64, 333, 200, 17, kMaxBlock, 96
    };
    for (int s : sizes) REQUIRE(s <= kMaxBlock);   // precondition of the criterion

    // Pre-allocate EVERY per-iteration buffer + context BEFORE arming the guard: the
    // guard intercepts global operator new, so any std::vector growth inside the armed
    // scope would be the TEST harness allocating, not the Engine. We allocate all blocks
    // and the (empty) sustain event span up front; the armed scope then calls ONLY
    // eng.process(), so a tripped guard can only be the Engine's hot path. The first
    // block also carries the warm-up note-on so first-touch state is realized while
    // allocation is still permitted (it is processed before arming).
    std::vector<Block>            blocks;
    blocks.reserve(sizes.size());
    for (int s : sizes) blocks.emplace_back(s);

    const std::vector<mw::MidiEvent> none;        // sustain across the stream (no events)
    std::vector<mw::MidiEvent>       warmEv{ noteOn(64, 0.9f, 0) };

    // Build all contexts up front (no allocation happens inside ctx()).
    std::vector<mw::BlockContext> ctxs;
    ctxs.reserve(sizes.size());
    ctxs.push_back(blocks[0].ctx(warmEv, sizes[0]));   // first block: note-on
    for (std::size_t i = 1; i < sizes.size(); ++i)
        ctxs.push_back(blocks[i].ctx(none, sizes[i])); // rest: sustain

    // Warm-up (note-on) processed before arming so any lazy first-touch is realized.
    eng.process(ctxs[0]);
    REQUIRE(eng.voiceManager().activeCount() >= 1);

    float streamPeak = 0.0f;
    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (std::size_t i = 1; i < sizes.size(); ++i)
        eng.process(ctxs[i]);                     // hot path ONLY: no alloc, no lock
    guard.disarm();

    // Finiteness / boundedness is checked outside the armed scope (these scans do not
    // allocate, but keeping them out keeps the armed window to process() alone).
    for (std::size_t i = 1; i < sizes.size(); ++i) {
        const int n = sizes[i];
        REQUIRE(allFinite(blocks[i].L, n));
        REQUIRE(allFinite(blocks[i].R, n));
        streamPeak = std::max(streamPeak,
                              std::max(maxAbs(blocks[i].L, 0, n), maxAbs(blocks[i].R, 0, n)));
    }

    // The varied-size stream tripped no RT guard (§9.1 RT-1/RT-2/RT-6).
    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    // The gated note remained audible and bounded across the whole varied stream.
    REQUIRE(streamPeak > 0.0f);
    REQUIRE(streamPeak < kSaneBound);
}

// ---------------------------------------------------------------------------
// §9.3 — the seam is identical headless: a re-prepared engine fed the SAME note-on at a
// non-zero sample offset is silent before the onset and audible after, confirming the
// note-on is honored sample-accurately through the assembled graph with no audio device
// in the loop. (A focused, finite end-to-end onset oracle.) [§4.4; §9.3]
// ---------------------------------------------------------------------------
TEST_CASE("e2e_smoke: a mid-block note-on through the full graph is silent before onset",
          "[e2e_smoke]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int N      = 480;
    constexpr int kOnset = 201;   // mid-block, not a chunk-cap multiple
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(57, 1.0f, kOnset) };
    auto c = blk.ctx(ev, N);
    eng.process(c);

    REQUIRE(allFinite(blk.L, N));
    REQUIRE(allFinite(blk.R, N));

    // Before the onset, nothing was sounding and FX defaults to dry, so the pre-onset
    // region is exactly silent (§4.4 sample-accurate event application).
    REQUIRE(maxAbs(blk.L, 0, kOnset) == 0.0f);
    REQUIRE(maxAbs(blk.R, 0, kOnset) == 0.0f);

    // At/after the onset the assembled voice path produces finite, bounded energy.
    const float postPeak = std::max(maxAbs(blk.L, kOnset, N), maxAbs(blk.R, kOnset, N));
    REQUIRE(postPeak > 0.0f);
    REQUIRE(postPeak < kSaneBound);
}
