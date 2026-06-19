// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/LifecycleFuzzTest.cpp — the lifecycle/fuzz tier for the assembled Engine
// seam (task 134). Test-case names begin with "lifecycle_fuzz" so
// `ctest -R lifecycle_fuzz --no-tests=error` selects them under the silent-pass rule
// (AGENTS.md "Tests"; the display names deliberately avoid '[' so ctest -R selection is
// not confused by Catch2 tag parsing).
//
// Fuzzes the ALREADY-ASSEMBLED Engine (task 118) over randomized sequences of
// prepare (sample-rate / block-size / voice-cap changes) + process + reset, with random
// valid block sizes (<= the prepared maxBlockSize), random valid voice configurations,
// and random valid note-event streams, asserting against docs/design/00:
//
//   - §10 "Lifecycle/fuzz" + §9.1 RT-1/RT-2/RT-6 — prepare-then-process over random
//     valid block sizes and params NEVER allocates on the hot path and NEVER trips the
//     AudioThreadGuard (alloc sentinel) [ADR-001 C2, C3, C4];
//   - §5.5 — re-prepare is IDEMPOTENT on sample-rate / block-size change: a re-prepared
//     engine and a freshly-constructed-and-prepared engine fed the IDENTICAL input
//     produce bit-identical output, and the deterministic oversample-factor selection
//     is stable for a given host rate [ADR-001 C2];
//   - §5.5 — reset() is alloc-free, and the documented full re-init (prepare(), which
//     calls reset() and re-prepares every consumed module) returns the engine to the
//     known START: after an arbitrary fuzzed history, a re-init'd engine matches a
//     fresh-prepared engine bit-for-bit on the next identical block [ADR-001 Decision].
//     (See the reset test for the documented as-built bare-reset() completeness gap.)
//
// SCOPE NOTE (per the task's Out-of-scope): this is a lifecycle/fuzz tier, NOT a
// determinism-corpus bit-exactness check (that is integration-5), NOT a CPU-budget gate
// (golden/qa), and NOT module-internal fuzzing. The bit-identity assertions here are the
// *lifecycle* properties (re-prepare idempotency, reset-to-known-start) only.
//
// The Engine assembly holds only a borrowed `const ParamSnapshot*` and the as-built
// voice/FX path does not dereference it (the param-inversion smoothers are exercised by
// their own stream); the fuzzable, assembly-consumed dimensions are therefore the block
// size, the voice mode / unison count, and the note-event stream. We fuzz exactly those
// and keep `params == nullptr` exactly as the assembled seam consumes it.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "voice/VoiceTypes.h"
#include "calibration/EngineConstants.h"
#include "calibration/OversampledZoneConstants.h"
#include "util/Prng.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

// The blessed sample-rate set (§8.4) plus one strictly-above-ceiling rate (§8.5) so the
// fuzz spans both the 2x and the clamped-to-1x oversample-factor branches.
constexpr double kBlessedSr[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr double kUnblessedHiSr = 192000.0;   // 2x would exceed OS_CEILING -> clamps to 1x

constexpr int kMaxVoices = mw::kMaxVoices;

// ---------------------------------------------------------------------------
// A self-contained block driver: owns the stereo output for `n` frames, fills a
// BlockContext over a borrowed event span, and exposes the channel pointer table. No
// ParamSnapshot is constructed (the assembly consumes only the borrowed pointer, which
// stays null exactly as the seam expects).
// ---------------------------------------------------------------------------
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
        c.params            = nullptr;          // not dereferenced by this assembly
        c.transport         = { 120.0, 0.0, true, sr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

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

// Generate a random VALID event stream for a block of `n` frames: 0..kMaxEvents events,
// each a note-on or note-off with an in-range MIDI note, normalized velocity, and a
// sample offset strictly within [0, n). The span is sorted ascending by sampleOffset as
// the seam contract requires (§5.3 "ordered by sampleOffset").
std::vector<mw::MidiEvent> randomEvents(mw::util::Prng& rng, int n) {
    std::vector<mw::MidiEvent> ev;
    if (n <= 0) return ev;
    constexpr int kMaxEvents = 6;
    const int count = static_cast<int>(rng.nextU32() % (kMaxEvents + 1));
    ev.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int note   = static_cast<int>(rng.nextU32() % 128u);          // 0..127
        const int offset = static_cast<int>(rng.nextU32() % static_cast<std::uint32_t>(n)); // 0..n-1
        if ((rng.nextU32() & 1u) != 0u) {
            const float vel = std::max(0.05f, rng.nextFloat());             // audible velocity
            ev.push_back(noteOn(note, vel, offset));
        } else {
            ev.push_back(noteOff(note, offset));
        }
    }
    std::sort(ev.begin(), ev.end(),
              [](const mw::MidiEvent& a, const mw::MidiEvent& b) {
                  return a.sampleOffset < b.sampleOffset;
              });
    return ev;
}

// Pick a random valid voice configuration (mode + unison count) and latch it onto the
// engine's voice manager before the next render boundary. Mirrors how the shell drives
// the manager off the param snapshot; here it is the fuzzed, assembly-consumed surface.
void randomVoiceConfig(mw::util::Prng& rng, mw::Engine& eng) noexcept {
    auto& vm = const_cast<mw::VoiceManager&>(eng.voiceManager());
    const mw::VoiceMode mode = static_cast<mw::VoiceMode>(rng.nextU32() % 3u); // Mono/Unison/Poly
    vm.setMode(mode);
    const int u = 1 + static_cast<int>(rng.nextU32() % static_cast<std::uint32_t>(mw::kMaxUnison));
    vm.setUnisonCount(u);
}

bool allFinite(const std::vector<float>& v) noexcept {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// §10 lifecycle/fuzz + §9.1 RT-1/RT-6 — prepare-then-process over random valid block
// sizes (<= maxBlockSize) and random valid event/voice configs NEVER allocates and
// NEVER trips the AudioThreadGuard. The engine is prepared once (allocation allowed
// there only), warmed once, then armed; every fuzzed process()/reset() inside the armed
// scope must be alloc-free [ADR-001 C2, C3; §5.5].
// ---------------------------------------------------------------------------
TEST_CASE("lifecycle_fuzz: random valid blocks and events never allocate on the hot path",
          "[lifecycle_fuzz]") {
    constexpr double kSr       = 48000.0;
    constexpr int    kMaxBlock = 512;

    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);   // the ONLY allocation site (§5.5).
    REQUIRE(eng.isPrepared());

    mw::util::Prng rng(0x11FEC0DEULL);

    // Warm once before arming so any lazy first-call state is already realized (the
    // event-vector storage below is reused across iterations to avoid per-iter alloc).
    {
        Block warm(kMaxBlock);
        std::vector<mw::MidiEvent> none;
        auto c = warm.ctx(none, kMaxBlock, kSr);
        eng.process(c);
    }
    eng.reset();

    // Pre-size the reusable I/O so the armed scope contains zero test-side allocation:
    // one max-size Block and one events vector with capacity for the worst-case stream.
    Block io(kMaxBlock);
    std::vector<mw::MidiEvent> events;
    events.reserve(8);

    mw::test::AudioThreadGuard guard;
    guard.arm();

    constexpr int kIters = 200;
    bool sawSignal = false;
    for (int it = 0; it < kIters; ++it) {
        // Random valid block size in [1, maxBlockSize].
        const int n = 1 + static_cast<int>(rng.nextU32()
                          % static_cast<std::uint32_t>(kMaxBlock));

        // Random valid voice config (assembly-consumed param surface) and event stream.
        randomVoiceConfig(rng, eng);

        // Build the event stream WITHOUT growing capacity inside the armed scope: clear
        // and re-push into the reserved vector (count is bounded by the reserve above).
        events.clear();
        const int count = static_cast<int>(rng.nextU32() % 7u); // 0..6, <= reserve
        for (int i = 0; i < count; ++i) {
            const int note   = static_cast<int>(rng.nextU32() % 128u);
            const int offset = static_cast<int>(rng.nextU32()
                                 % static_cast<std::uint32_t>(n));
            if ((rng.nextU32() & 1u) != 0u)
                events.push_back(noteOn(note, std::max(0.05f, rng.nextFloat()), offset));
            else
                events.push_back(noteOff(note, offset));
        }
        std::sort(events.begin(), events.end(),
                  [](const mw::MidiEvent& a, const mw::MidiEvent& b) {
                      return a.sampleOffset < b.sampleOffset;
                  });

        auto c = io.ctx(events, n, kSr);
        eng.process(c);             // hot path: must not allocate or lock.

        // Occasionally reset mid-fuzz: reset() is a hot path too (§5.5) and must be
        // alloc-free; it is exercised inside the armed scope here.
        if ((rng.nextU32() % 5u) == 0u)
            eng.reset();

        for (int i = 0; i < n; ++i)
            if (io.L[static_cast<std::size_t>(i)] != 0.0f) { sawSignal = true; break; }
    }

    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
    // Sanity: the fuzz actually drove sounding voices (the run was not vacuously silent).
    REQUIRE(sawSignal);
}

// ---------------------------------------------------------------------------
// §5.5 — process output is always FINITE and bounded under fuzzed lifecycle churn: no
// random block-size / event / reset sequence ever produces a NaN/Inf or an unbounded
// blow-up. (A self-oscillating filter / long tail under denormal flush stays finite —
// this is the lifecycle-tier finiteness guard, not a golden value check.)
// ---------------------------------------------------------------------------
TEST_CASE("lifecycle_fuzz: fuzzed prepare-process-reset churn keeps output finite",
          "[lifecycle_fuzz]") {
    mw::util::Prng rng(0xFA57BEEFULL);

    constexpr int kRounds = 16;
    for (int round = 0; round < kRounds; ++round) {
        const double sr = (round % 5 == 4)
                              ? kUnblessedHiSr
                              : kBlessedSr[round % 4];
        const int maxBlock = 64 + static_cast<int>(rng.nextU32() % 257u); // 64..320

        mw::Engine eng;
        eng.prepare(sr, maxBlock, kMaxVoices);
        REQUIRE(eng.isPrepared());

        // A handful of random process calls with random sub-blocks, events, and an
        // occasional reset, asserting finiteness after each.
        const int calls = 3 + static_cast<int>(rng.nextU32() % 4u); // 3..6
        for (int k = 0; k < calls; ++k) {
            const int n = 1 + static_cast<int>(rng.nextU32()
                              % static_cast<std::uint32_t>(maxBlock));
            randomVoiceConfig(rng, eng);
            auto ev = randomEvents(rng, n);
            Block blk(n);
            auto c = blk.ctx(ev, n, sr);
            eng.process(c);

            REQUIRE(allFinite(blk.L));
            REQUIRE(allFinite(blk.R));

            if ((rng.nextU32() % 4u) == 0u)
                eng.reset();
        }
    }
}

// ---------------------------------------------------------------------------
// §5.5 — re-prepare is IDEMPOTENT on sample-rate / block-size change. After processing
// an arbitrary fuzzed history, calling prepare() again with given (sr, maxBlock, voices)
// returns the engine to the SAME known start as a freshly-constructed engine prepared
// with those args: both, fed the IDENTICAL next block, produce bit-identical output, and
// expose identical observable lifecycle state (sampleRate, maxBlockSize, oversample
// factor). The deterministic oversample-factor selection is stable per host rate
// [ADR-001 C2; §5.5; §8.5].
// ---------------------------------------------------------------------------
TEST_CASE("lifecycle_fuzz: re-prepare on sample-rate and block-size change is idempotent",
          "[lifecycle_fuzz]") {
    mw::util::Prng rng(0x1DE0407EULL);

    // Probe several (sr, maxBlock) targets, including a blessed-2x rate and the
    // above-ceiling rate that must deterministically clamp the oversample factor to 1x.
    struct Target { double sr; int maxBlock; };
    const Target targets[] = {
        { 44100.0,  256 },
        { 48000.0,  512 },
        { 96000.0,  128 },
        { kUnblessedHiSr, 384 },   // must clamp oversample factor to 1x (§8.5 V15)
    };

    for (const Target& t : targets) {
        // (A) A "dirty" engine: prepared at a DIFFERENT config, then driven through a
        // fuzzed history, then RE-PREPARED at the target. prepare() must wipe the
        // history and re-derive all state from the target alone (idempotency).
        mw::Engine reused;
        reused.prepare(/*sr=*/22050.0 + static_cast<double>(rng.nextU32() % 8000u),
                       /*maxBlock=*/96 + static_cast<int>(rng.nextU32() % 400u),
                       kMaxVoices);
        {
            const int hist = 2 + static_cast<int>(rng.nextU32() % 4u);
            for (int h = 0; h < hist; ++h) {
                const int n = 1 + static_cast<int>(rng.nextU32() % 200u);
                randomVoiceConfig(rng, reused);
                auto ev = randomEvents(rng, n);
                Block blk(n);
                auto c = blk.ctx(ev, n, reused.sampleRate());
                reused.process(c);
            }
        }
        reused.prepare(t.sr, t.maxBlock, kMaxVoices); // RE-PREPARE at the target.

        // (B) A pristine engine prepared straight at the target.
        mw::Engine fresh;
        fresh.prepare(t.sr, t.maxBlock, kMaxVoices);

        // Observable lifecycle state matches (idempotent on sr / block-size change).
        REQUIRE(reused.sampleRate()       == fresh.sampleRate());
        REQUIRE(reused.maxBlockSize()     == fresh.maxBlockSize());
        REQUIRE(reused.oversampleFactor() == fresh.oversampleFactor());
        REQUIRE(reused.sampleRate()       == t.sr);
        REQUIRE(reused.maxBlockSize()     == t.maxBlock);

        // The above-ceiling rate deterministically clamps to 1x; blessed rates use 2x.
        const int expectFactor =
            mw::cal::oszone::wouldExceedCeiling(t.sr, mw::cal::oszone::kDefaultFactor)
                ? mw::cal::oszone::kFactor1x
                : mw::cal::oszone::kDefaultFactor;
        REQUIRE(reused.oversampleFactor() == expectFactor);

        // Drive BOTH engines with the IDENTICAL next block; output must be bit-identical:
        // re-prepare put the reused engine in exactly the fresh engine's known start.
        const int n = std::min(t.maxBlock, 240);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 0.9f, 0), noteOff(60, n - 1) };

        Block ra(n), fa(n);
        auto rc = ra.ctx(ev, n, t.sr);
        auto fc = fa.ctx(ev, n, t.sr);
        reused.process(rc);
        fresh.process(fc);

        for (int i = 0; i < n; ++i) {
            REQUIRE(ra.L[static_cast<std::size_t>(i)] == fa.L[static_cast<std::size_t>(i)]);
            REQUIRE(ra.R[static_cast<std::size_t>(i)] == fa.R[static_cast<std::size_t>(i)]);
        }
    }
}

// ---------------------------------------------------------------------------
// §5.5 — reset() / re-init clears state to a KNOWN START with no allocation.
//
// This asserts the two §5.5 reset properties this assembly objectively guarantees:
//   (1) reset() performs NO allocation (it is an RT hot path) [ADR-001 Decision]; and
//   (2) the documented full re-init (prepare(), which calls reset() internally and
//       re-prepares every consumed module) returns the engine to the SAME known start as
//       a freshly-constructed engine — even after an arbitrary fuzzed history — so a
//       subsequent IDENTICAL block renders bit-for-bit identically [ADR-001 C2; §5.5].
//
// DEPENDENCY FINDING (reported, not hidden): the as-built Engine::reset() (task 118,
// core/Engine.cpp) clears only keys_/fx_ and the local mix scratch; it does NOT clear
// the consumed VoiceManager or ControlCore (neither of which currently exposes a public
// reset()). A bare reset() is therefore NOT a deterministic fixed point — residual
// per-voice / control state survives, so a bare-reset() engine does NOT match a fresh
// engine on the next block. That is a §5.5 gap OUTSIDE this test task's editable surface
// (the conflict-avoidance rule forbids editing core/Engine.cpp here); it warrants a
// follow-up task on Engine::reset() completeness. This test therefore asserts the
// known-start contract through the supported full re-init path (prepare), and asserts
// only the alloc-freedom of bare reset() — it does NOT silently weaken §5.5 to a passing
// no-op; the gap is documented above and in the PR notes.
// ---------------------------------------------------------------------------
TEST_CASE("lifecycle_fuzz: reset is alloc-free and re-init returns a clean known start",
          "[lifecycle_fuzz]") {
    constexpr double kSr       = 48000.0;
    constexpr int    kMaxBlock = 256;

    mw::util::Prng rng(0xC1EA0FFEULL);

    mw::Engine dirty;
    dirty.prepare(kSr, kMaxBlock, kMaxVoices);

    // Drive a fuzzed, definitely-sounding history so there is real state to clear.
    {
        const int hist = 5;
        for (int h = 0; h < hist; ++h) {
            const int n = 64 + static_cast<int>(rng.nextU32() % 192u);
            randomVoiceConfig(rng, dirty);
            std::vector<mw::MidiEvent> ev{ noteOn(48 + h, 1.0f, 0) };
            Block blk(n);
            auto c = blk.ctx(ev, n, kSr);
            dirty.process(c);
        }
    }

    // (1) reset() must be alloc-free (it is a §5.5 hot path). dirty is already warm, so
    //     arm directly around the bare reset call.
    {
        mw::test::AudioThreadGuard guard;
        guard.arm();
        dirty.reset();
        guard.disarm();
        REQUIRE_FALSE(guard.violated());
        REQUIRE(guard.violations().empty());
    }

    // (2) Full re-init: prepare() at the SAME config wipes the fuzzed history and
    //     re-derives every consumed module's state, returning the engine to the known
    //     start. Compare against a pristine engine prepared identically.
    dirty.prepare(kSr, kMaxBlock, kMaxVoices);   // documented full re-init (§5.5).

    mw::Engine fresh;
    fresh.prepare(kSr, kMaxBlock, kMaxVoices);

    // Both are now at the canonical known start; feed an IDENTICAL block — the re-init'd
    // engine must match the fresh engine bit-for-bit (no residual tail / control state).
    constexpr int N = 200;
    std::vector<mw::MidiEvent> ev{ noteOn(60, 0.8f, 4), noteOff(60, 150) };

    Block da(N), fb(N);
    auto dc = da.ctx(ev, N, kSr);
    auto fc = fb.ctx(ev, N, kSr);
    dirty.process(dc);
    fresh.process(fc);

    bool sounded = false;
    for (int i = 0; i < N; ++i) {
        REQUIRE(da.L[static_cast<std::size_t>(i)] == fb.L[static_cast<std::size_t>(i)]);
        REQUIRE(da.R[static_cast<std::size_t>(i)] == fb.R[static_cast<std::size_t>(i)]);
        if (da.L[static_cast<std::size_t>(i)] != 0.0f) sounded = true;
    }
    // The compared block actually drove a sounding voice (the equality is non-vacuous).
    REQUIRE(sounded);
}
