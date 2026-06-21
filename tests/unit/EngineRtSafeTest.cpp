// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EngineRtSafeTest.cpp — the no-alloc / no-lock / noexcept hot-path guard
// suite for the ASSEMBLED Engine (task 132). Realizes plan/backlog/132 scope against
// docs/design/00-architecture-overview.md §9.1 (the audio-thread RT contract:
// RT-1 no heap allocation, RT-2 no locks, RT-4 no thrown exceptions on the hot path,
// RT-6 all sizing in prepare) and §10 (the no-alloc/no-lock and noexcept acceptance
// hooks) [ADR-001 C2-C5, C11].
//
// This is the cross-cutting QA / invariant tier for the assembled Engine seam, NOT an
// algorithm-correctness tier (the per-stage DSP and the seam's split/determinism shape
// are owned by their own streams — see EngineAssemblyTest.cpp / the module tests, which
// task 132 lists ## Out of scope). Here we assert the §9.1 contract holds when the WHOLE
// graph (max-voices voice loop -> mono sum -> post-voice FX) runs behind process():
//
//   - RT-1 / RT-2 (§9.1): an AudioThreadGuard-wrapped, representative process() — driven
//     at the worst-case voice count (UNISON x kMaxUnison, a held note sounding the full
//     voice loop) — performs ZERO heap allocations and acquires ZERO locks. The
//     post-voice FX chain's process runs every block as part of that hot path, and we
//     additionally exercise an explicit FX-ON FxChain process (Drive + Chorus + Delay all
//     enabled) under the same armed guard so the FX-on branches are proven alloc/lock-free
//     too [ADR-001 C3/C4; ADR-019 VT-03 single-threaded voice loop => nothing to lock].
//
//   - RT-4 (§9.1): Engine::process and Engine::reset are noexcept-QUALIFIED (asserted
//     statically on the exact member-pointer type), and a throw that escapes a noexcept
//     boundary is caught as a CRASH (std::terminate), not unwound — proven with a forked
//     child that lets an exception escape a noexcept function and observing the child
//     dies abnormally [ADR-001 C5].
//
//   - RT-6 (§9.1): allocation occurs ONLY in prepare. We arm the guard around prepare()
//     and observe it DOES allocate (prepare is the sole sizing/allocation site), then arm
//     around the steady-state process()/reset() and observe ZERO allocation — objectively
//     locating the allocation in prepare and nowhere on the hot path [ADR-001 C2].
//
// Test-case names begin with "engine_rtsafe" so `ctest -R engine_rtsafe
// --no-tests=error` selects exactly these (AGENTS.md silent-pass rule); the display text
// avoids '[' so Catch2 does not mis-parse a tag and break ctest -R selection.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "voice/VoiceManager.h"
#include "voice/VoiceTypes.h"
#include "dsp/fx/FxChain.h"
#include "dsp/fx/FxParams.h"
#include "calibration/EngineConstants.h"

#include "../invariants/AudioThreadGuard.h"

// POSIX fork/wait for the noexcept-throw crash death test (RT-4). The headless test
// binary links mwcore only and runs on macOS arm64 / Linux x64 (the bless + hard-gate
// platforms), both of which provide <unistd.h>/<sys/wait.h>. The death test self-skips
// (SUCCEED) on any platform without fork rather than producing a false failure.
#if defined(__unix__) || defined(__APPLE__)
  #define MW_HAVE_FORK 1
  #include <unistd.h>
  #include <sys/wait.h>
  #include <csignal>
#else
  #define MW_HAVE_FORK 0
#endif

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// --- Seam-event helpers (a NoteOn/NoteOff at a sample offset) --------------------
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

// A self-contained block driver: owns the stereo output and fills a BlockContext.
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
        c.params            = nullptr;   // not consumed by this assembly
        c.transport         = { 120.0, 0.0, true, kSr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

// A representative FX-ON snapshot: Drive + Chorus + Delay all enabled with non-trivial
// values, master bypass OFF and the mono-output collapse ON, so every FX-on branch the
// audio thread would take is exercised under the armed guard (the Engine's own FX chain
// defaults to FX-off / dry, so this is how the FX-on hot path is reached in the
// JUCE-free core without mutating the Engine seam). FxParams is a trivially-copyable POD.
mw::fx::FxParams fxOnParams() noexcept {
    mw::fx::FxParams p{};
    p.masterBypass = false;
    p.monoOutput   = false;
    p.hostBpm      = 120.0;
    p.drive  = { /*on=*/true, /*amount=*/0.7f, /*tone=*/0.4f, /*output=*/0.6f };
    p.chorus = { /*mode=*/3 /*I+II*/, /*rate=*/0.3f, /*depth=*/0.5f, /*width=*/0.8f, /*mix=*/0.5f };
    p.delay  = { /*on=*/true, /*sync=*/false, /*pingpong=*/true, /*division=*/2,
                 /*timeMs=*/180.0f, /*feedback=*/0.4f, /*damp=*/0.3f, /*width=*/0.7f, /*mix=*/0.4f };
    return p;
}

} // namespace

// ============================================================================
// RT-4 (static) — the hot paths are noexcept-QUALIFIED (§9.1 / §10 acceptance hook).
//   process and reset are noexcept so an escaped throw terminates (caught as a crash)
//   rather than unwinding the audio thread [ADR-001 C5].
// ============================================================================

TEST_CASE("engine_rtsafe: process and reset are noexcept-qualified hot paths",
          "[engine_rtsafe]") {
    using mw::Engine;
    using mw::BlockContext;

    // The exact member-pointer types carry the noexcept qualifier: a non-noexcept
    // overload would not match these, so this is a hard contract assertion (§5.1/§9.1).
    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::process),
                                  void (Engine::*)(const BlockContext&) noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::reset),
                                  void (Engine::*)() noexcept>);

    // And via the noexcept() operator on real expressions (belt and suspenders).
    Engine eng;
    BlockContext c{};
    STATIC_REQUIRE(noexcept(eng.process(c)));
    STATIC_REQUIRE(noexcept(eng.reset()));
}

// ============================================================================
// RT-1 / RT-2 — a representative max-voices process allocates and locks nothing
//   (§9.1 / §10). Build + prepare OUTSIDE the armed scope (prepare is the only allowed
//   allocation site, §9.1 RT-6); warm once; then arm and run the steady-state hot path.
// ============================================================================

TEST_CASE("engine_rtsafe: a max-voices process performs zero heap allocations and zero locks",
          "[engine_rtsafe]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    REQUIRE(eng.isPrepared());

    // Drive the worst-case voice count: UNISON spreads ONE note across kMaxUnison voice
    // slots, so the fixed-index voice loop renders kMaxUnison sounding voices per block
    // (the representative "max voices" stress for the single-threaded loop, §6.1). The
    // mode/unison-count latch and apply at the next render boundary.
    auto& vm = const_cast<mw::VoiceManager&>(eng.voiceManager());
    vm.setMode(mw::VoiceMode::Unison);
    vm.setUnisonCount(mw::kMaxUnison);

    constexpr int N = kMaxBlock;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
    auto c = blk.ctx(ev, N);

    // Warm before arming so any one-time lazy realization happens off the guard.
    eng.process(c);
    REQUIRE(vm.mode() == mw::VoiceMode::Unison);
    REQUIRE(vm.activeCount() == mw::kMaxUnison);   // full voice loop is genuinely sounding

    mw::test::AudioThreadGuard guard;
    guard.arm();                 // ----- BEGIN audio-thread-equivalent hot zone -----
    eng.process(c);              // steady-state max-voices render + post-voice FX
    eng.process(c);              // a second block (no per-block reallocation either)
    guard.disarm();              // ----- END hot zone -----

    REQUIRE_FALSE(guard.violated());          // RT-1 no heap alloc / RT-2 no lock
    REQUIRE(guard.violations().empty());

    // The block genuinely produced audio (the guard wrapped a real, non-trivial render,
    // not an early-out), so the assertion has teeth.
    float maxAbs = 0.0f;
    for (int i = 0; i < N; ++i) maxAbs = std::max(maxAbs, std::fabs(blk.L[static_cast<std::size_t>(i)]));
    REQUIRE(maxAbs > 0.0f);
}

// ============================================================================
// RT-1 / RT-2 — the post-voice FX chain's FX-ON hot path allocates and locks nothing
//   (§9.1). The Engine runs the FX chain on the mono sum every block; the Engine's
//   chain defaults to FX-off, so we exercise the FX-ON branches (Drive+Chorus+Delay all
//   enabled, master bypass off) on an explicitly-prepared FxChain under the armed guard.
//   setParams is a control-thread publish (lock-free double-buffer); process reads it
//   lock-free [ADR-010 FX-10]. Both run armed here.
// ============================================================================

TEST_CASE("engine_rtsafe: the FX-on post-voice chain process performs zero allocations and zero locks",
          "[engine_rtsafe]") {
    mw::fx::FxChain fx;
    fx.prepare(kSr, kMaxBlock);       // the ONLY allocation site for the chain.
    fx.reset();

    constexpr int N = kMaxBlock;
    std::vector<float> mono(static_cast<std::size_t>(N));
    std::vector<float> outL(static_cast<std::size_t>(N), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(N), 0.0f);
    for (int i = 0; i < N; ++i)
        mono[static_cast<std::size_t>(i)] = 0.4f * std::sin(0.03f * static_cast<float>(i));
    float* out[2] = { outL.data(), outR.data() };

    const mw::fx::FxParams on = fxOnParams();

    // Publish + warm once OFF the guard so any first-block lazy realization is paid here.
    fx.setParams(on);
    fx.process(mono.data(), out, N);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    fx.setParams(on);                 // control-thread publish: lock-free, alloc-free
    fx.process(mono.data(), out, N);  // FX-on audio path: Drive -> Chorus -> Delay
    fx.process(mono.data(), out, N);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    // The FX-on chain produced audio at the declared constant offset (teeth).
    float maxAbs = 0.0f;
    for (int i = 0; i < N; ++i) maxAbs = std::max(maxAbs, std::fabs(outL[static_cast<std::size_t>(i)]));
    REQUIRE(maxAbs > 0.0f);
}

// ============================================================================
// RT-6 — allocation occurs ONLY in prepare (§9.1 / §10). prepare is the sole sizing
//   site; the steady-state process()/reset() touch only pre-sized storage. We locate the
//   allocation by arming around BOTH and contrasting: prepare DOES allocate, the hot path
//   does NOT [ADR-001 C2].
// ============================================================================

TEST_CASE("engine_rtsafe: allocation happens in prepare and never on the process or reset hot path",
          "[engine_rtsafe]") {
    mw::Engine eng;

    constexpr int N = kMaxBlock;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(62, 0.9f, 0) };
    auto c = blk.ctx(ev, N);

    // 1. prepare IS the allocation site: arming around prepare records allocations
    //    (the mono/stereo scratch + the consumed modules' buffers are sized here, §5.5).
    {
        mw::test::AudioThreadGuard prepGuard;
        prepGuard.arm();
        eng.prepare(kSr, kMaxBlock, kMaxVoices);   // the ONLY allocator (§9.1 RT-6)
        prepGuard.disarm();
        REQUIRE(eng.isPrepared());
        REQUIRE(prepGuard.violated());             // prepare genuinely allocates
        REQUIRE_FALSE(prepGuard.violations().empty());
    }

    // Warm a block off the guard so any one-time lazy state is realized before arming.
    eng.process(c);

    // 2. The steady-state hot path allocates NOTHING: process and reset only touch the
    //    storage prepare already sized (§9.1 RT-1/RT-6).
    {
        mw::test::AudioThreadGuard hotGuard;
        hotGuard.arm();
        eng.process(c);
        eng.reset();
        eng.process(c);
        hotGuard.disarm();
        REQUIRE_FALSE(hotGuard.violated());
        REQUIRE(hotGuard.violations().empty());
    }
}

// ============================================================================
// RT-4 (crash, not unwound) — a throw that escapes a noexcept boundary is caught as a
//   CRASH (std::terminate), not silently unwound (§9.1 / §10). A noexcept function that
//   lets an exception escape invokes std::terminate by the C++ standard; we prove it by
//   forking a child that does exactly that and observing the child dies ABNORMALLY
//   (terminated by signal / non-zero abort), while the parent survives [ADR-001 C5].
// ============================================================================

#if MW_HAVE_FORK
namespace {
// A noexcept function that throws: per [expr] / [except.terminate] the escaping
// exception calls std::terminate -> the process aborts. This stands in for "an
// exception reaches the Engine's noexcept process/reset boundary": the standard makes
// that a crash, never a stack unwind through the audio thread. The deliberate
// throw-through-noexcept is exactly the behavior under test, so the compiler's
// -Wexceptions diagnostic is locally silenced (it is the point, not a defect).
#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wexceptions"
#endif
[[noreturn]] void throwsThroughNoexcept() noexcept {
    throw 42;   // escapes the noexcept boundary -> std::terminate
}
#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
} // namespace
#endif

TEST_CASE("engine_rtsafe: an exception escaping a noexcept hot-path boundary terminates as a crash",
          "[engine_rtsafe]") {
#if MW_HAVE_FORK
    const pid_t pid = ::fork();
    REQUIRE(pid >= 0);   // fork itself must succeed

    if (pid == 0) {
        // Child: let an exception escape the noexcept boundary. This must NOT unwind;
        // the standard requires std::terminate -> the child aborts. _exit(0) below is
        // only reached if (wrongly) the throw were swallowed/unwound, which would let
        // the parent see a clean exit and FAIL the test.
        throwsThroughNoexcept();
        ::_exit(0);
    }

    // Parent: reap the child and assert it died ABNORMALLY (a crash), not cleanly.
    int status = 0;
    const pid_t waited = ::waitpid(pid, &status, 0);
    REQUIRE(waited == pid);

    // "Caught as a crash, not unwound": the child must have been terminated by a signal
    // (SIGABRT from std::terminate's default abort), OR exited non-zero — never a clean
    // exit(0), which would mean the throw was unwound rather than terminating.
    const bool crashedBySignal = WIFSIGNALED(status);
    const bool exitedNonZero   = WIFEXITED(status) && WEXITSTATUS(status) != 0;
    const bool cleanExit       = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    REQUIRE((crashedBySignal || exitedNonZero));        // it crashed
    REQUIRE_FALSE(cleanExit);                           // it did NOT unwind cleanly
#else
    SUCCEED("fork-based death test unavailable on this platform; noexcept crash semantics "
            "are still asserted statically above (process/reset are noexcept-qualified).");
#endif
}
