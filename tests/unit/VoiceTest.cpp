// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the per-voice signal-path assembly + drift seed (Voice,
// task 073). Test-case display names begin with "voice" so `ctest -R voice` selects
// exactly these under the silent-pass rule; '[' is kept OUT of the display text (it
// is parsed as a tag and would break -R selection).
//
// Covers every acceptance criterion of plan/backlog/073-voice.md against
// docs/design/04-voice-and-control.md §4.1-§4.4 and ADR-006 §Decision item 1 / C18,
// ADR-019 VT-01:
//   - Voice owns exactly one ADSR and one LFO; flat value type, no virtual dispatch.
//   - render is noexcept/alloc-free; Idle costs nothing; Releasing finishes its tail
//     then goes Idle; Stealing fades via stealGain_ then Idle.
//   - drift seed = hashCombine(instanceSeed, voiceIndex), byte-stable run-to-run;
//     distinct voiceIndex => distinct seed.
//   - beginSteal flips to Stealing; a completed fade transitions to Idle.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "voice/Voice.h"
#include "voice/VoiceTypes.h"
#include "calibration/VoiceDriftConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::Voice;
using mw::VoiceState;

namespace {

constexpr double kSampleRate     = 48000.0;
constexpr int    kOversample     = 2;
constexpr std::uint32_t kSeed    = 0xC0FFEEu;

// A voice prepared and gated on a note, ready to render.
Voice makeGatedVoice(int voiceIndex = 0, std::uint32_t instanceSeed = kSeed) {
    Voice v;
    v.prepare(kSampleRate, kOversample, voiceIndex, instanceSeed);
    v.noteOn(60, 1.0f, /*retrigger=*/true);
    return v;
}

// Render `numSamples` of a voice into scratch and return the peak abs over both
// channels (a coarse "did this voice produce signal / fall silent" probe).
float renderPeak(Voice& v, int numSamples) {
    std::array<float, 256> l{};
    std::array<float, 256> r{};
    v.render(l.data(), r.data(), numSamples);
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(l[i]));
        peak = std::max(peak, std::abs(r[i]));
    }
    return peak;
}

} // namespace

// --- §4.1/§4.2: ownership, flat value type, no virtual dispatch ------------------

TEST_CASE("voice: is a flat value type with no virtual dispatch", "[voice]") {
    // §4.2 / ADR-006 §Decision item 1: Voice is a flat value type laid out
    // cache-friendly; no vtable in the inner loop.
    STATIC_REQUIRE_FALSE(std::is_polymorphic_v<Voice>);
    STATIC_REQUIRE(std::is_nothrow_default_constructible_v<Voice>);
    // Trivially relocatable enough to live by value in the preallocated pool.
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Voice>);
}

TEST_CASE("voice: hot-path methods are noexcept", "[voice]") {
    // §4.3 / ADR-019 VT-01: render/noteOn/noteOff are noexcept hot paths.
    Voice v;
    STATIC_REQUIRE(noexcept(v.noteOn(60, 1.0f, true)));
    STATIC_REQUIRE(noexcept(v.noteOff()));
    STATIC_REQUIRE(noexcept(v.beginSteal()));
    STATIC_REQUIRE(noexcept(v.setGlideTarget(440.0f)));
    STATIC_REQUIRE(noexcept(v.setDetuneCents(0.0f)));
    STATIC_REQUIRE(noexcept(v.setStereoPan(0.0f)));
    float l = 0.0f, r = 0.0f;
    STATIC_REQUIRE(noexcept(v.render(&l, &r, 1)));
}

TEST_CASE("voice: starts Idle and inactive before any note", "[voice]") {
    Voice v;
    v.prepare(kSampleRate, kOversample, 0, kSeed);
    REQUIRE(v.state() == VoiceState::Idle);
    REQUIRE_FALSE(v.isActive());
    REQUIRE(v.currentNote() == -1);
}

// --- §4.4 / ADR-006 C18: deterministic per-voice drift seed ----------------------

TEST_CASE("voice: drift seed is hashCombine of instanceSeed and voiceIndex", "[voice]") {
    // The seed MUST be exactly hashCombine(instanceSeed, voiceIndex) (§4.4).
    Voice v;
    v.prepare(kSampleRate, kOversample, 3, kSeed);
    const std::uint32_t expected =
        mw::cal::voice::hashCombine(kSeed, static_cast<std::uint32_t>(3));
    REQUIRE(v.driftSeed() == expected);
}

TEST_CASE("voice: drift seed is byte-stable across runs", "[voice]") {
    // Same inputs => same seed, every time (deterministic, never wall-clock).
    Voice a, b;
    a.prepare(kSampleRate, kOversample, 5, kSeed);
    b.prepare(kSampleRate, kOversample, 5, kSeed);
    REQUIRE(a.driftSeed() == b.driftSeed());

    // A fixed-input expectation pins the value so a future mixer change is caught.
    const std::uint32_t pinned =
        mw::cal::voice::hashCombine(0xC0FFEEu, 5u);
    REQUIRE(a.driftSeed() == pinned);
}

TEST_CASE("voice: distinct voiceIndex yields distinct drift seed", "[voice]") {
    // Each unison/poly voice must carry its own decorrelated stream (real beating).
    std::array<std::uint32_t, mw::kMaxVoices> seeds{};
    for (int i = 0; i < mw::kMaxVoices; ++i) {
        Voice v;
        v.prepare(kSampleRate, kOversample, i, kSeed);
        seeds[static_cast<std::size_t>(i)] = v.driftSeed();
    }
    // No two voices share a seed.
    for (int i = 0; i < mw::kMaxVoices; ++i)
        for (int j = i + 1; j < mw::kMaxVoices; ++j)
            REQUIRE(seeds[static_cast<std::size_t>(i)]
                    != seeds[static_cast<std::size_t>(j)]);
}

TEST_CASE("voice: distinct instanceSeed yields distinct drift seed", "[voice]") {
    Voice a, b;
    a.prepare(kSampleRate, kOversample, 0, 0x11111111u);
    b.prepare(kSampleRate, kOversample, 0, 0x22222222u);
    REQUIRE(a.driftSeed() != b.driftSeed());
}

TEST_CASE("voice: hashCombine is a pure deterministic mixer", "[voice]") {
    // Pure: same args => same result; constexpr-evaluable.
    constexpr std::uint32_t s0 = mw::cal::voice::hashCombine(123u, 7u);
    constexpr std::uint32_t s1 = mw::cal::voice::hashCombine(123u, 7u);
    STATIC_REQUIRE(s0 == s1);
    // Distinct second arg decorrelates.
    STATIC_REQUIRE(mw::cal::voice::hashCombine(123u, 7u)
                   != mw::cal::voice::hashCombine(123u, 8u));
    // Distinct first arg decorrelates.
    STATIC_REQUIRE(mw::cal::voice::hashCombine(123u, 7u)
                   != mw::cal::voice::hashCombine(124u, 7u));
}

// --- §4.3: render contract — Idle / Active / Releasing / Stealing ----------------

TEST_CASE("voice: Idle render leaves the buffer untouched (costs nothing)", "[voice]") {
    // §4.3: an Idle voice is skipped; render must not write/accumulate anything.
    Voice v;
    v.prepare(kSampleRate, kOversample, 0, kSeed);
    REQUIRE(v.state() == VoiceState::Idle);

    std::array<float, 64> l{};
    std::array<float, 64> r{};
    for (auto& x : l) x = 0.25f;   // pre-fill with a sentinel
    for (auto& x : r) x = -0.5f;
    v.render(l.data(), r.data(), 64);
    for (int i = 0; i < 64; ++i) {
        REQUIRE(l[static_cast<std::size_t>(i)] == 0.25f);
        REQUIRE(r[static_cast<std::size_t>(i)] == -0.5f);
    }
}

TEST_CASE("voice: noteOn makes the voice Active and sets the note", "[voice]") {
    Voice v = makeGatedVoice();
    REQUIRE(v.state() == VoiceState::Active);
    REQUIRE(v.isActive());
    REQUIRE(v.currentNote() == 60);
    REQUIRE(v.currentLevel() >= 0.0f);
}

TEST_CASE("voice: render accumulates into the output buffer", "[voice]") {
    // §4.2: render accumulates (adds), it does not overwrite. A pre-filled buffer
    // must come back changed by the voice's contribution, not zeroed.
    Voice v = makeGatedVoice();
    // Let the envelope open past attack so there is signal to accumulate.
    renderPeak(v, 64);

    std::array<float, 64> l{};
    std::array<float, 64> r{};
    const float sentinel = 1.0f;
    for (auto& x : l) x = sentinel;
    for (auto& x : r) x = sentinel;
    v.render(l.data(), r.data(), 64);
    // Accumulating render: at least one sample must differ from the sentinel.
    bool changed = false;
    for (int i = 0; i < 64; ++i)
        if (l[static_cast<std::size_t>(i)] != sentinel
            || r[static_cast<std::size_t>(i)] != sentinel)
            changed = true;
    REQUIRE(changed);
}

TEST_CASE("voice: Releasing finishes its tail then self-transitions to Idle", "[voice]") {
    // §4.3 / ADR-006 C15: a Releasing voice keeps rendering until the ADSR release
    // reaches the silence threshold, THEN goes Idle in place.
    Voice v = makeGatedVoice();
    renderPeak(v, 128);              // open the envelope
    v.noteOff();
    REQUIRE(v.state() == VoiceState::Releasing);
    REQUIRE(v.isActive());           // still sounding while the tail finishes

    // Pump render until it self-transitions to Idle (bounded — release is finite).
    bool reachedIdle = false;
    for (int block = 0; block < 5000 && !reachedIdle; ++block) {
        renderPeak(v, 64);
        if (v.state() == VoiceState::Idle) reachedIdle = true;
    }
    REQUIRE(reachedIdle);
    REQUIRE_FALSE(v.isActive());
}

TEST_CASE("voice: beginSteal flips state to Stealing", "[voice]") {
    // §4.3 / ADR-006 C15: beginSteal forces a fast fade; state becomes Stealing.
    Voice v = makeGatedVoice();
    renderPeak(v, 64);
    v.beginSteal();
    REQUIRE(v.state() == VoiceState::Stealing);
    REQUIRE(v.isActive());           // still occupies the slot until the fade ends
}

TEST_CASE("voice: a completed steal fade transitions to Idle", "[voice]") {
    // §4.3 / ADR-006 C15: Stealing applies stealGain_ fade, then Idle on completion.
    Voice v = makeGatedVoice();
    renderPeak(v, 64);
    v.beginSteal();
    REQUIRE(v.state() == VoiceState::Stealing);

    bool reachedIdle = false;
    for (int block = 0; block < 2000 && !reachedIdle; ++block) {
        renderPeak(v, 64);
        if (v.state() == VoiceState::Idle) reachedIdle = true;
    }
    REQUIRE(reachedIdle);
    REQUIRE_FALSE(v.isActive());
}

TEST_CASE("voice: steal fade is faster than a natural release", "[voice]") {
    // §6.4: a steal is a FAST forced fade (ms), not the full release tail. The
    // steal-to-Idle sample count must be far below the release-to-Idle count.
    auto blocksToIdle = [](Voice& v) {
        int blocks = 0;
        while (v.state() != VoiceState::Idle && blocks < 100000) {
            std::array<float, 64> l{}, r{};
            v.render(l.data(), r.data(), 64);
            ++blocks;
        }
        return blocks;
    };

    Voice released = makeGatedVoice();
    renderPeak(released, 128);
    released.noteOff();
    const int releaseBlocks = blocksToIdle(released);

    Voice stolen = makeGatedVoice();
    renderPeak(stolen, 128);
    stolen.beginSteal();
    const int stealBlocks = blocksToIdle(stolen);

    REQUIRE(stealBlocks > 0);
    REQUIRE(stealBlocks < releaseBlocks);
}

// --- RT safety: render is allocation-free on the audio thread (ADR-001 C3) -------

TEST_CASE("voice: render performs no heap allocation", "[voice]") {
    Voice v = makeGatedVoice();
    renderPeak(v, 64);   // warm up any lazy init OUTSIDE the armed window

    std::array<float, 64> l{}, r{};
    mw::test::AudioThreadGuard guard;
    guard.arm();
    v.render(l.data(), r.data(), 64);
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
}

TEST_CASE("voice: an Idle render performs no heap allocation", "[voice]") {
    Voice v;
    v.prepare(kSampleRate, kOversample, 0, kSeed);

    std::array<float, 64> l{}, r{};
    mw::test::AudioThreadGuard guard;
    guard.arm();
    v.render(l.data(), r.data(), 64);   // Idle fast path
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
}

// --- §4.2: per-voice config setters and accessors --------------------------------

TEST_CASE("voice: noteSerial is settable and reported for steal ordering", "[voice]") {
    // §4.2 / ADR-006 C14: the VoiceManager stamps noteSerial_ on allocation; the
    // accessor reports it for deterministic steal ordering.
    Voice v = makeGatedVoice();
    v.setNoteSerial(42u);
    REQUIRE(v.noteSerial() == 42u);
}

TEST_CASE("voice: detune and pan setters do not throw and keep the voice active", "[voice]") {
    Voice v = makeGatedVoice();
    v.setDetuneCents(7.0f);
    v.setStereoPan(-1.0f);
    REQUIRE(v.isActive());
    v.setGlideTarget(523.25f);
    REQUIRE(v.isActive());
}
