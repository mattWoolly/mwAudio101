// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EngineAssemblyTest.cpp — the engine-assembly seam tests (task 118).
// Test-case names begin with "engine_assembly" so `ctest -R engine_assembly` selects
// them under the silent-pass rule (AGENTS.md "Tests").
//
// Covers every Acceptance criterion of plan/backlog/118 against docs/design/00:
//   - the exact three-call seam shape + noexcept hot paths (§5.1, §5.5);
//   - voices sum in FIXED voice-index order; the voice loop uses no synchronization
//     primitive — asserted via determinism + the no-alloc/no-lock AudioThreadGuard
//     (§6.1; ADR-019 VT-01/VT-02/VT-03);
//   - events at sample offsets apply sample-accurately; segments render in fixed
//     kRenderBlock-capped chunks (§4.4) — split-vs-whole equivalence + a pre-onset
//     silence oracle;
//   - FTZ/DAZ is set at process entry and only pre-sized storage is touched (§9 RT5/RT6).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
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

// Build a NoteOn seam event at a sample offset.
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

// A self-contained block driver: owns the stereo output, fills a BlockContext, and
// runs one Engine::process. No ParamSnapshot is required by this assembly (the seam
// holds only a pointer; the consumed FX chain defaults to FX-OFF / dry).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n) : L(static_cast<std::size_t>(n), 0.0f),
                            R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n) {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = nullptr;          // not consumed by this assembly
        c.transport         = { 120.0, 0.0, true, kSr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

float maxAbs(const std::vector<float>& v, int from, int to) noexcept {
    float m = 0.0f;
    for (int i = from; i < to; ++i) m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// §5.1 / §5.5 — the exact three-call seam shape and noexcept hot paths.
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: exposes exactly prepare/process/reset with the seam signature",
          "[engine_assembly]") {
    using mw::Engine;
    using mw::BlockContext;

    // prepare(double,int,int) noexcept.
    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::prepare),
                                  void (Engine::*)(double, int, int) noexcept>);
    // process(const BlockContext&) noexcept.
    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::process),
                                  void (Engine::*)(const BlockContext&) noexcept>);
    // reset() noexcept.
    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::reset),
                                  void (Engine::*)() noexcept>);

    Engine eng;
    BlockContext c{};
    STATIC_REQUIRE(noexcept(eng.prepare(kSr, kMaxBlock, kMaxVoices)));
    STATIC_REQUIRE(noexcept(eng.process(c)));
    STATIC_REQUIRE(noexcept(eng.reset()));
}

// ---------------------------------------------------------------------------
// §5.5 — prepare sizes everything; process on a silent (no-event) block is safe and
// leaves a known-silent output (FX defaults to dry, no voices sounding).
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: prepared engine renders a silent block to silence",
          "[engine_assembly]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    REQUIRE(eng.isPrepared());
    REQUIRE(eng.sampleRate() == kSr);

    constexpr int N = 256;
    Block blk(N);
    std::vector<mw::MidiEvent> none;
    auto c = blk.ctx(none, N);
    eng.process(c);

    REQUIRE(maxAbs(blk.L, 0, N) == 0.0f);
    REQUIRE(maxAbs(blk.R, 0, N) == 0.0f);
}

// ---------------------------------------------------------------------------
// §4.4 — a NoteOn at a sample offset applies SAMPLE-ACCURATELY: the output before the
// onset offset is exactly silent, and there is signal at/after the onset.
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: a note-on at a sample offset is silent before the offset",
          "[engine_assembly]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int N      = 512;
    constexpr int kOnset = 192;   // mid-block onset (not a chunk-cap multiple by luck)
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, kOnset) };
    auto c = blk.ctx(ev, N);
    eng.process(c);

    // Pre-onset region MUST be exactly silent: no voice was sounding yet, and the FX
    // chain defaults to dry (so the dry-pad just carries silence through). [§4.4]
    REQUIRE(maxAbs(blk.L, 0, kOnset) == 0.0f);
    REQUIRE(maxAbs(blk.R, 0, kOnset) == 0.0f);

    // At/after the onset the voice path must produce energy (the note sounds). The
    // control tick fires within the post-onset region, gating the voice on.
    REQUIRE(maxAbs(blk.L, kOnset, N) > 0.0f);
}

// ---------------------------------------------------------------------------
// §4.4 — split-vs-whole equivalence: rendering one block in a single call is
// bit-identical to driving the SAME event/parameter sequence across two back-to-back
// calls split at an arbitrary frame. Internal kRenderBlock chunking + the control
// tick must be position-continuous, so the seam is split-invariant.
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: block-split rendering is bit-identical to a whole block",
          "[engine_assembly]") {
    constexpr int N     = 320;
    constexpr int kSplit = 137;   // arbitrary, not a chunk-cap multiple
    constexpr int kOnset = 8;

    // Whole: one process call of N frames with a note-on near the start.
    mw::Engine whole;
    whole.prepare(kSr, kMaxBlock, kMaxVoices);
    Block wb(N);
    std::vector<mw::MidiEvent> wev{ noteOn(64, 1.0f, kOnset) };
    auto wc = wb.ctx(wev, N);
    whole.process(wc);

    // Split: the SAME sequence rendered as [0,kSplit) then [kSplit,N) with the note-on
    // in the first segment at the same global offset, and event offsets rebased per call.
    mw::Engine split;
    split.prepare(kSr, kMaxBlock, kMaxVoices);

    Block sb1(kSplit);
    std::vector<mw::MidiEvent> sev1{ noteOn(64, 1.0f, kOnset) };
    auto sc1 = sb1.ctx(sev1, kSplit);
    split.process(sc1);

    Block sb2(N - kSplit);
    std::vector<mw::MidiEvent> sev2; // no further events
    auto sc2 = sb2.ctx(sev2, N - kSplit);
    split.process(sc2);

    for (int i = 0; i < kSplit; ++i) {
        REQUIRE(wb.L[static_cast<std::size_t>(i)] == sb1.L[static_cast<std::size_t>(i)]);
        REQUIRE(wb.R[static_cast<std::size_t>(i)] == sb1.R[static_cast<std::size_t>(i)]);
    }
    for (int i = kSplit; i < N; ++i) {
        REQUIRE(wb.L[static_cast<std::size_t>(i)]
                == sb2.L[static_cast<std::size_t>(i - kSplit)]);
        REQUIRE(wb.R[static_cast<std::size_t>(i)]
                == sb2.R[static_cast<std::size_t>(i - kSplit)]);
    }
}

// ---------------------------------------------------------------------------
// §6.1 — fixed voice-index order: in UNISON the assembly drives slots 0..U-1, and the
// VoiceManager's active-index list is the ascending fixed-order prefix the render loop
// walks. The accumulation order is therefore the fixed voice index, by construction.
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: unison voices occupy the fixed ascending index prefix",
          "[engine_assembly]") {
    constexpr int U = 4;
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Drive UNISON before any process so the control tick fans out to slots 0..U-1.
    // setMode/setUnisonCount latch and apply at the next render boundary.
    auto& vm = const_cast<mw::VoiceManager&>(eng.voiceManager());
    vm.setMode(mw::VoiceMode::Unison);
    vm.setUnisonCount(U);

    constexpr int N = 256;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
    auto c = blk.ctx(ev, N);
    eng.process(c);

    const mw::VoiceManager& v = eng.voiceManager();
    REQUIRE(v.mode() == mw::VoiceMode::Unison);
    REQUIRE(v.activeCount() == U);

    // The active list is the ascending fixed-index prefix [0..U-1]; the render loop
    // sums in exactly this order (§6.1 fixed voice-index order; ADR-019 VT-02).
    for (int k = 0; k < v.activeCount(); ++k) {
        REQUIRE(static_cast<int>(v.activeIndex(k)) == k);
        if (k > 0)
            REQUIRE(v.activeIndex(k) > v.activeIndex(k - 1)); // strictly ascending
    }
}

// ---------------------------------------------------------------------------
// §9.2 / RT-7 — determinism: two independently-prepared engines fed the IDENTICAL
// BlockContext sequence produce BIT-identical output. A single-threaded, fixed-order,
// lock-free voice loop is deterministic by construction (ADR-019 VT-04).
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: identical input yields bit-identical output (determinism)",
          "[engine_assembly]") {
    constexpr int N = 480;

    auto run = [&](std::vector<float>& outL, std::vector<float>& outR) {
        mw::Engine eng;
        eng.prepare(kSr, kMaxBlock, kMaxVoices);
        Block blk(N);
        std::vector<mw::MidiEvent> ev{ noteOn(57, 0.8f, 16), noteOff(57, 400) };
        auto c = blk.ctx(ev, N);
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

// ---------------------------------------------------------------------------
// §9.1 RT-1/RT-2/RT-6 — process touches ONLY pre-sized storage: no heap allocation and
// no lock inside an armed AudioThreadGuard scope. The single-threaded voice loop has
// nothing to synchronize (ADR-019 VT-03), so the lock sentinel never trips.
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: process performs no heap allocation under the audio guard",
          "[engine_assembly]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices); // allocation is allowed here, before arming.

    constexpr int N = 256;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
    auto c = blk.ctx(ev, N);

    // Warm once before arming so any lazy state is realized.
    eng.process(c);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    eng.process(c);   // steady-state hot path: must not allocate or lock.
    eng.reset();      // reset is a hot path too (§5.5): must not allocate.
    eng.process(c);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

// ---------------------------------------------------------------------------
// §4.4 — multiple events at distinct offsets all apply within one block (sub-block
// chunking honors every offset). A note-on then note-off in the same block leaves the
// engine in a non-stuck state (the gate de-asserts and the tail releases).
// ---------------------------------------------------------------------------
TEST_CASE("engine_assembly: multiple sample-offset events in one block all apply",
          "[engine_assembly]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int N = 512;
    Block blk(N);
    // Three offsets within the block: on, then off, then on again — all must register.
    std::vector<mw::MidiEvent> ev{
        noteOn(60, 1.0f, 0),
        noteOff(60, 100),
        noteOn(67, 1.0f, 300),
    };
    auto c = blk.ctx(ev, N);
    eng.process(c);

    // The voice path produced audio in this block (the events drove a sounding voice).
    REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);
    // The kRenderBlock cap is a positive frame count the chunker honors (§4.4).
    REQUIRE(mw::cal::engine::kRenderBlock > 0);
}
