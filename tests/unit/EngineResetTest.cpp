// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EngineResetTest.cpp — the engine-reset-completeness tier for the
// assembled Engine seam (task 134b). Test-case names begin with "engine_reset" so
// `ctest -R engine_reset --no-tests=error` selects exactly these under the silent-pass
// rule (AGENTS.md "Tests"); the display names deliberately avoid '[' so ctest -R is not
// confused by Catch2 tag parsing.
//
// Closes the §5.5 gap the task-134 lifecycle fuzz documented (LifecycleFuzzTest.cpp,
// "DEPENDENCY FINDING"): the as-built bare Engine::reset() cleared only keys_/fx_ and the
// mix scratch, but NOT the consumed ControlCore — so a bare reset() was NOT a
// deterministic fixed point (residual control-tick phase + crossfade + jitter PRNG state
// survived, so two divergent histories + reset() + the same block diverged by ~0.28
// max-abs). Task 134b adds ControlCore::reset() and wires Engine::reset() to call it (the
// VoiceManager already exposes reset()), so reset() ALONE — no re-prepare — re-establishes
// the §5.5 known start.
//
// Asserts every task-134b acceptance criterion against docs/design/00 §5.5
// [ADR-001 Decision / C2-C5]:
//   - AC1: after two DIVERGENT play histories, reset() + an IDENTICAL block yields
//     BIT-IDENTICAL output to a freshly-prepared engine fed the same block — i.e. reset()
//     (not prepare()) is the deterministic fixed point [§5.5];
//   - AC2: Engine::reset() and ControlCore::reset() are noexcept-QUALIFIED and allocate /
//     lock NOTHING under an armed AudioThreadGuard [ADR-001 C2-C5];
//   - AC3: a fuzzed-history reset assertion that exercises reset() (NOT the prepare()
//     re-init path the original lifecycle fuzz used) — bare reset() returns the engine to
//     the known start after an arbitrary fuzzed, sounding history.
//
// SCOPE NOTE (task-134b Out-of-scope): no JUCE/plugin reset marshalling and no golden
// rebless — reset() is a runtime path, not a blessed render input. These are pure
// JUCE-free core assertions on the assembled seam + the ControlCore.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "control/ControlCore.h"
#include "voice/VoiceManager.h"
#include "voice/VoiceTypes.h"
#include "util/Prng.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOn;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.data0        = 0.0f;
    e.value        = vel;
    e.sampleOffset = offset;
    return e;
}

mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOff;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.value        = 0.0f;
    e.sampleOffset = offset;
    return e;
}

// A self-contained stereo block driver: owns the output for `n` frames and fills a
// BlockContext over a borrowed event span. No ParamSnapshot is constructed (the assembly
// consumes only the borrowed pointer, which stays null exactly as the seam expects).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n)
        : L(static_cast<std::size_t>(std::max(0, n)), 0.0f),
          R(static_cast<std::size_t>(std::max(0, n)), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n, double sr) noexcept {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = nullptr;        // not dereferenced by this assembly
        c.transport         = { 120.0, 0.0, true, sr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

void setVoiceConfig(mw::Engine& eng, mw::VoiceMode mode, int unison) noexcept {
    auto& vm = const_cast<mw::VoiceManager&>(eng.voiceManager());
    vm.setMode(mode);
    vm.setUnisonCount(unison);
}

std::vector<mw::MidiEvent> randomEvents(mw::util::Prng& rng, int n) {
    std::vector<mw::MidiEvent> ev;
    if (n <= 0) return ev;
    const int count = static_cast<int>(rng.nextU32() % 7u); // 0..6
    ev.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int note   = static_cast<int>(rng.nextU32() % 128u);
        const int offset = static_cast<int>(rng.nextU32() % static_cast<std::uint32_t>(n));
        if ((rng.nextU32() & 1u) != 0u)
            ev.push_back(noteOn(note, std::max(0.05f, rng.nextFloat()), offset));
        else
            ev.push_back(noteOff(note, offset));
    }
    std::sort(ev.begin(), ev.end(),
              [](const mw::MidiEvent& a, const mw::MidiEvent& b) {
                  return a.sampleOffset < b.sampleOffset;
              });
    return ev;
}

} // namespace

// ============================================================================
// AC2 (static) — Engine::reset and ControlCore::reset are noexcept-QUALIFIED hot paths
//   (§5.5 / §9.1 RT-4). A reset is an RT hot path; an escaped throw must terminate rather
//   than unwind the audio thread [ADR-001 C5].
// ============================================================================

TEST_CASE("engine_reset: Engine and ControlCore reset are noexcept-qualified hot paths",
          "[engine_reset]") {
    using mw::Engine;
    using mw::ControlCore;

    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::reset),
                                  void (Engine::*)() noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&ControlCore::reset),
                                  void (ControlCore::*)() noexcept>);

    Engine eng;
    ControlCore cc;
    STATIC_REQUIRE(noexcept(eng.reset()));
    STATIC_REQUIRE(noexcept(cc.reset()));
}

// ============================================================================
// AC1 — reset() is a DETERMINISTIC FIXED POINT across DIVERGENT histories (§5.5).
//   Two engines are prepared identically, then driven through TWO DIFFERENT play
//   histories (different modes, notes, block sizes), then BOTH bare-reset() (no
//   re-prepare). Fed the SAME next block, they must match each other AND a freshly
//   prepared engine BIT-FOR-BIT — proving reset() (not only prepare()) re-establishes the
//   §5.5 known start. Before task 134b this diverged (control-tick phase + crossfade +
//   jitter PRNG residue survived bare reset()).
// ============================================================================

TEST_CASE("engine_reset: two divergent histories plus reset match a fresh engine bit-for-bit",
          "[engine_reset]") {
    // (A) History one: MONO, a sustained low note over a few oddly-sized blocks.
    mw::Engine a;
    a.prepare(kSr, kMaxBlock, kMaxVoices);
    setVoiceConfig(a, mw::VoiceMode::Mono, 1);
    {
        const int sizes[] = { 37, 211, 128, 5, 300 };
        for (int n : sizes) {
            std::vector<mw::MidiEvent> ev{ noteOn(41, 0.95f, 0) };
            Block blk(n);
            auto c = blk.ctx(ev, n, kSr);
            a.process(c);
        }
    }

    // (B) History two: a DIFFERENT history — UNISON spread, different notes, different
    //     block sizes and gate timings, so the ControlCore tick phase / crossfade / PRNG
    //     and the voice pool land in a genuinely different dirty state than (A).
    mw::Engine b;
    b.prepare(kSr, kMaxBlock, kMaxVoices);
    setVoiceConfig(b, mw::VoiceMode::Unison, mw::kMaxUnison);
    {
        const int sizes[] = { 64, 64, 64, 64, 64, 64, 64 };
        int note = 72;
        for (int n : sizes) {
            std::vector<mw::MidiEvent> ev{ noteOn(note, 0.6f, 0), noteOff(note, n - 1) };
            Block blk(n);
            auto c = blk.ctx(ev, n, kSr);
            b.process(c);
            note -= 2;
        }
    }

    // Sanity: the two histories really did diverge the control-tick phase (otherwise the
    // fixed-point test would be vacuous). controlCore().samplesToNextTick() is the
    // residual phase that bare reset() must wipe; the two dirty engines differ on it.
    REQUIRE(a.controlCore().sampleCounter() != b.controlCore().sampleCounter());

    // Both engines back to the SAME voice config the fresh engine will use, then BARE
    // reset() (NO re-prepare) — this is the path under test.
    setVoiceConfig(a, mw::VoiceMode::Mono, 1);
    setVoiceConfig(b, mw::VoiceMode::Mono, 1);
    a.reset();
    b.reset();

    // reset() must have re-established the control-core known start (sampleCounter /
    // tickCount zeroed, crossfade at the macro pole) — the same as a fresh prepare.
    REQUIRE(a.controlCore().sampleCounter() == 0);
    REQUIRE(a.controlCore().tickCount()     == 0);
    REQUIRE(b.controlCore().sampleCounter() == 0);
    REQUIRE(b.controlCore().tickCount()     == 0);

    // (C) A pristine engine prepared identically and given the same voice config.
    mw::Engine fresh;
    fresh.prepare(kSr, kMaxBlock, kMaxVoices);
    setVoiceConfig(fresh, mw::VoiceMode::Mono, 1);

    // Drive all three with the IDENTICAL next block. reset() being a fixed point => the
    // two divergent-then-reset engines match the fresh engine BIT-FOR-BIT.
    constexpr int N = 256;
    std::vector<mw::MidiEvent> ev{ noteOn(60, 0.8f, 3), noteOff(60, 200) };

    Block ba(N), bb(N), bf(N);
    auto ca = ba.ctx(ev, N, kSr);
    auto cb = bb.ctx(ev, N, kSr);
    auto cf = bf.ctx(ev, N, kSr);
    a.process(ca);
    b.process(cb);
    fresh.process(cf);

    bool sounded = false;
    for (int i = 0; i < N; ++i) {
        const auto k = static_cast<std::size_t>(i);
        REQUIRE(ba.L[k] == bf.L[k]);
        REQUIRE(ba.R[k] == bf.R[k]);
        REQUIRE(bb.L[k] == bf.L[k]);
        REQUIRE(bb.R[k] == bf.R[k]);
        if (bf.L[k] != 0.0f) sounded = true;
    }
    // The compared block actually drove a sounding voice (the equality has teeth).
    REQUIRE(sounded);
}

// ============================================================================
// AC1 (control oracle) — ControlCore::reset() re-derives EXACTLY the post-prepare known
//   start WITHOUT re-prepare. We dirty an isolated ControlCore by advancing it through a
//   control-tick history (so sampleCounter_/tickCount_/samplesToNextTick_ and the jitter
//   PRNG all move off the start), then reset() it and compare every observable to a
//   freshly-prepared core. Crucially, the seeded jitter stream must also reset: a
//   jitter-ON core advanced after reset() must produce the SAME tick spacing as after a
//   fresh prepare (the PRNG was re-seeded), which a sampleCounter-only reset would miss.
// ============================================================================

TEST_CASE("engine_reset: ControlCore reset re-derives the prepare known start including the jitter stream",
          "[engine_reset]") {
    // A minimal duck-typed VoiceManager stand-in counting controlTick() calls — advance()
    // is templated on `controlTick()` only (ControlCore.h §7.8), so this keeps the oracle
    // independent of the concrete VoiceManager.
    struct TickCounter { int ticks = 0; void controlTick() noexcept { ++ticks; } };

    mw::ControlCore dirty;
    dirty.prepare(kSr);
    dirty.setPole(mw::VintageControlPole::Vintage);  // crossfade target moves off 0
    dirty.setJitterEnabled(true);                    // exercise the seeded jitter PRNG

    // Advance a substantial history so tick phase + crossfade + PRNG state all diverge.
    {
        TickCounter vm;
        for (int k = 0; k < 64; ++k) dirty.advance(257, vm);
        REQUIRE(vm.ticks > 0);                       // genuinely fired ticks (PRNG drawn)
    }
    REQUIRE(dirty.sampleCounter() > 0);
    REQUIRE(dirty.tickCount()     > 0);

    // reset() (NOT re-prepare): re-derive the known start. sampleRate-derived sizing and
    // the macro pole / jitter toggles are NOT a reset concern; the tick phase, counters,
    // crossfade and the seeded PRNG ARE.
    dirty.reset();

    mw::ControlCore freshAfterPrepare;
    freshAfterPrepare.prepare(kSr);
    freshAfterPrepare.setPole(mw::VintageControlPole::Vintage);
    freshAfterPrepare.setJitterEnabled(true);
    // prepare() already established the post-prepare start for THIS pole/jitter config;
    // re-prepare with the same settings to capture the canonical known start to match.
    freshAfterPrepare.prepare(kSr);
    freshAfterPrepare.setPole(mw::VintageControlPole::Vintage);
    freshAfterPrepare.setJitterEnabled(true);

    // Observable known-start scalars match a fresh prepare.
    REQUIRE(dirty.sampleCounter()     == freshAfterPrepare.sampleCounter());
    REQUIRE(dirty.tickCount()         == freshAfterPrepare.tickCount());
    REQUIRE(dirty.samplesToNextTick() == freshAfterPrepare.samplesToNextTick());
    REQUIRE(dirty.crossfadeBlend()    == freshAfterPrepare.crossfadeBlend());
    REQUIRE(dirty.sampleRate()        == kSr);       // reset does NOT discard sizing

    // The seeded jitter stream reset too: advancing BOTH identically now yields the SAME
    // monotonic counters/tick phase — only a re-seeded PRNG reproduces the tick spacing.
    TickCounter va, vb;
    for (int k = 0; k < 40; ++k) {
        dirty.advance(193, va);
        freshAfterPrepare.advance(193, vb);
        REQUIRE(dirty.sampleCounter()     == freshAfterPrepare.sampleCounter());
        REQUIRE(dirty.tickCount()         == freshAfterPrepare.tickCount());
        REQUIRE(dirty.samplesToNextTick() == freshAfterPrepare.samplesToNextTick());
    }
    REQUIRE(va.ticks == vb.ticks);
    REQUIRE(va.ticks > 0);
}

// ============================================================================
// AC2 — Engine::reset() and ControlCore::reset() allocate and lock NOTHING under an armed
//   AudioThreadGuard (§5.5 reset is an RT hot path) [ADR-001 C2-C4]. Build + prepare +
//   warm OUTSIDE the armed scope (prepare is the only allowed allocation site, §9.1 RT-6),
//   then arm and exercise the bare reset() path repeatedly.
// ============================================================================

TEST_CASE("engine_reset: Engine and ControlCore reset perform zero allocations and zero locks",
          "[engine_reset]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    REQUIRE(eng.isPrepared());

    mw::ControlCore cc;
    cc.prepare(kSr);

    // Warm a sounding block + an advance so any one-time lazy realization is paid off the
    // guard, and there is genuine state for reset() to clear.
    {
        setVoiceConfig(eng, mw::VoiceMode::Unison, mw::kMaxUnison);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
        Block blk(kMaxBlock);
        auto c = blk.ctx(ev, kMaxBlock, kSr);
        eng.process(c);

        struct TickCounter { void controlTick() noexcept {} } vm;
        cc.advance(kMaxBlock, vm);
    }

    mw::test::AudioThreadGuard guard;
    guard.arm();                 // ----- BEGIN audio-thread-equivalent hot zone -----
    eng.reset();                 // Engine::reset -> voices_.reset() + control_.reset() + scratch
    eng.reset();                 // idempotent second call still alloc/lock-free
    cc.reset();                  // ControlCore::reset directly
    guard.disarm();              // ----- END hot zone -----

    REQUIRE_FALSE(guard.violated());          // RT-1 no heap alloc / RT-2 no lock
    REQUIRE(guard.violations().empty());

    // reset() genuinely returned the engine to a silent known start (teeth: the warmed
    // history was actually cleared).
    REQUIRE(eng.controlCore().sampleCounter() == 0);
    REQUIRE(eng.voiceManager().activeCount()  == 0);
}

// ============================================================================
// AC3 — the fuzzed-history reset assertion, now exercising reset() (NOT the prepare()
//   re-init path the original task-134 lifecycle fuzz used). After an arbitrary fuzzed,
//   definitely-sounding history, a BARE reset() must return the engine to the SAME known
//   start as a freshly-prepared engine, so the next identical block renders bit-for-bit
//   identically [§5.5]. This is the strengthened reset assertion task 134b asks for.
// ============================================================================

TEST_CASE("engine_reset: bare reset after a fuzzed history matches a fresh engine on the next block",
          "[engine_reset]") {
    mw::util::Prng rng(0xC1EA0FFEULL);

    mw::Engine dirty;
    dirty.prepare(kSr, kMaxBlock, kMaxVoices);

    // Drive a fuzzed, definitely-sounding history so there is real voice + control state.
    {
        const int hist = 6;
        for (int h = 0; h < hist; ++h) {
            const int n = 64 + static_cast<int>(rng.nextU32() % 192u);
            const auto mode = static_cast<mw::VoiceMode>(rng.nextU32() % 3u);
            const int u = 1 + static_cast<int>(rng.nextU32()
                              % static_cast<std::uint32_t>(mw::kMaxUnison));
            setVoiceConfig(dirty, mode, u);
            auto ev = randomEvents(rng, n);
            ev.push_back(noteOn(48 + h, 1.0f, 0));   // guarantee a sounding voice
            std::sort(ev.begin(), ev.end(),
                      [](const mw::MidiEvent& x, const mw::MidiEvent& y) {
                          return x.sampleOffset < y.sampleOffset;
                      });
            Block blk(n);
            auto c = blk.ctx(ev, n, kSr);
            dirty.process(c);
        }
    }

    // BARE reset() (NOT prepare) — the path the original lifecycle fuzz could not assert.
    setVoiceConfig(dirty, mw::VoiceMode::Mono, 1);
    dirty.reset();

    // A pristine engine at the same canonical known start.
    mw::Engine fresh;
    fresh.prepare(kSr, kMaxBlock, kMaxVoices);
    setVoiceConfig(fresh, mw::VoiceMode::Mono, 1);

    constexpr int N = 220;
    std::vector<mw::MidiEvent> ev{ noteOn(60, 0.8f, 4), noteOff(60, 170) };

    Block bd(N), bf(N);
    auto cd = bd.ctx(ev, N, kSr);
    auto cf = bf.ctx(ev, N, kSr);
    dirty.process(cd);
    fresh.process(cf);

    bool sounded = false;
    for (int i = 0; i < N; ++i) {
        const auto k = static_cast<std::size_t>(i);
        REQUIRE(bd.L[k] == bf.L[k]);
        REQUIRE(bd.R[k] == bf.R[k]);
        if (bf.L[k] != 0.0f) sounded = true;
    }
    REQUIRE(sounded);
}
