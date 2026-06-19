// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the VoiceManager pool owner + MONO/UNISON dispatch +
// control-tick propagation + fixed-index-order render/sum (task 074). Test-case
// display names begin with "voicemanager" so `ctest -R voicemanager` selects exactly
// these under the silent-pass rule; '[' is kept OUT of the display text (Catch2 parses
// it as a tag and it would break -R selection).
//
// Covers every acceptance criterion of plan/backlog/074-voicemanager.md against
// docs/design/04-voice-and-control.md §6.1-§6.3 and §8 (RT1-RT7), ADR-006 §Decision
// item 3 (MONO/UNISON) / C9 / C10 / C17 / C18, and ADR-019 VT-01/VT-02:
//   - MONO drives exactly ONE voice as a verbatim, bit-faithful pass-through of the
//     NoteDecision (oracle: identical to a bare Voice driven with the same sequence).
//   - UNISON: U voices share the SAME decision (mono-faithful note feel); detune
//     symmetric/centered (0 for U==1); pan spread from Calibration.h; per-voice drift
//     seeds distinct.
//   - render sums active voices in FIXED voice-index order; noexcept / alloc-free;
//     pool sized in prepare.
//   - mode and unison-count changes apply only at prepare or a block boundary, never
//     mid-block (latched lock-free flag).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "voice/VoiceManager.h"
#include "voice/Voice.h"
#include "voice/VoiceTypes.h"
#include "calibration/VoiceManagerConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::NoteDecision;
using mw::NoteEvent;
using mw::Voice;
using mw::VoiceManager;
using mw::VoiceMode;
using mw::VoiceState;
using mw::GateTrigMode;

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr int           kOversample = 2;
constexpr std::uint32_t kSeed       = 0xC0FFEEu;

// MIDI note -> Hz (A4 = 69 = 440 Hz), matching VoiceManager.cpp's internal mapping.
float midiToHz(int n) {
    return 440.0f * std::pow(2.0f, static_cast<float>(n - 69) / 12.0f);
}

// A NoteDecision sounding `note` as a fresh trigger (gate + retrigger).
NoteDecision freshTrigger(int note) {
    NoteDecision d;
    d.activeNote = note;
    d.gate       = true;
    d.retrigger  = true;
    return d;
}

// Render `n` samples and return the per-channel buffers (zero-init, accumulated into).
struct Stereo {
    std::array<float, 512> l{};
    std::array<float, 512> r{};
};

void renderInto(VoiceManager& vm, Stereo& s, int n) {
    vm.render(s.l.data(), s.r.data(), n);
}

} // namespace

// --- type / surface invariants ----------------------------------------------------

TEST_CASE("voicemanager: is a flat value type with noexcept hot paths", "[voicemanager]") {
    // §6.1 / ADR-019 VT-01: VoiceManager is a value type, no virtual dispatch; the
    // hot-path methods are noexcept (no-throw audio thread, ADR-001 C5).
    STATIC_REQUIRE_FALSE(std::is_polymorphic_v<VoiceManager>);
    STATIC_REQUIRE(std::is_nothrow_default_constructible_v<VoiceManager>);

    VoiceManager vm;
    NoteEvent e{NoteEvent::Type::NoteOn, 60, 1.0f, 0};
    NoteDecision d = freshTrigger(60);
    float l = 0.0f, r = 0.0f;
    STATIC_REQUIRE(noexcept(vm.handleNoteEvent(e)));
    STATIC_REQUIRE(noexcept(vm.controlTick(d)));
    STATIC_REQUIRE(noexcept(vm.render(&l, &r, 1)));
    STATIC_REQUIRE(noexcept(vm.setMode(VoiceMode::Unison)));
    STATIC_REQUIRE(noexcept(vm.setUnisonCount(4)));
    STATIC_REQUIRE(noexcept(vm.setGateTrigMode(GateTrigMode::Gate)));
}

TEST_CASE("voicemanager: starts in MONO with unison 1 and no active voices", "[voicemanager]") {
    // §6.1 / ADR-016 R-3: MONO is the default / bless target; nothing sounds yet.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    REQUIRE(vm.mode() == VoiceMode::Mono);
    REQUIRE(vm.unisonCount() == 1);
    REQUIRE(vm.activeCount() == 0);
}

// --- §8 RT6 / ADR-006 §4, C17: pool sized in prepare ------------------------------

TEST_CASE("voicemanager: prepare sizes the full pool with distinct drift seeds", "[voicemanager]") {
    // §6.5 / ADR-006 C18: every one of the kMaxVoices voices is prepared with its
    // fixed index, so each carries a distinct deterministic drift seed (the basis for
    // real unison beating, §6.3). No two pool voices share a seed.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);

    std::array<std::uint64_t, mw::kMaxVoices> seeds{};
    for (int i = 0; i < mw::kMaxVoices; ++i) {
        seeds[static_cast<std::size_t>(i)] = vm.voice(i).driftSeed();
    }
    for (int i = 0; i < mw::kMaxVoices; ++i) {
        for (int j = i + 1; j < mw::kMaxVoices; ++j) {
            REQUIRE(seeds[static_cast<std::size_t>(i)]
                    != seeds[static_cast<std::size_t>(j)]);
        }
    }
}

// --- §6.2: MONO drives exactly ONE voice ------------------------------------------

TEST_CASE("voicemanager: MONO sounds exactly one voice", "[voicemanager]") {
    // §6.2 / ADR-006 §Decision item 3 MONO: exactly ONE active Voice. After a note-on
    // resolved to a gate decision, slot 0 is Active and every other slot stays Idle.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);

    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));

    REQUIRE(vm.activeCount() == 1);
    REQUIRE(vm.voice(0).state() == VoiceState::Active);
    REQUIRE(vm.voice(0).currentNote() == 60);
    for (int i = 1; i < mw::kMaxVoices; ++i) {
        REQUIRE(vm.voice(i).state() == VoiceState::Idle);
    }
}

TEST_CASE("voicemanager: MONO gate de-assert releases the voice", "[voicemanager]") {
    // §6.2 / K7: all keys released -> gate de-asserts -> ADSR enters release in place.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);

    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));
    REQUIRE(vm.voice(0).state() == VoiceState::Active);

    NoteDecision off;  // activeNote=-1, gate=false
    vm.controlTick(off);
    REQUIRE(vm.voice(0).state() == VoiceState::Releasing);
}

TEST_CASE("voicemanager: MONO is a bit-faithful pass-through of the NoteDecision", "[voicemanager]") {
    // §6.2 / ADR-006 §Decision item 3 MONO; ADR-016 R-3: MONO is a pass-through with
    // ZERO behavioral logic on top, so its output is bit-identical to a bare Voice
    // driven by the same {activeNote, gate, retrigger} sequence. ORACLE: replicate the
    // exact public-API sequence VoiceManager applies to slot 0 onto a standalone Voice
    // prepared identically (same index 0 + same instance seed => same drift seed =>
    // FP bit-exact on the macOS arm64 bless gate, ADR-019 VT-04).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));

    Voice oracle;
    oracle.prepare(kSampleRate, kOversample, /*voiceIndex=*/0, kSeed);
    oracle.noteOn(60, /*velocity=*/1.0f, /*retrigger=*/true);
    oracle.setGlideTarget(midiToHz(60));

    // Render both for several blocks and compare sample-for-sample. MONO must add
    // nothing of its own on top of the single voice.
    for (int block = 0; block < 8; ++block) {
        Stereo mgr;
        std::array<float, 512> ol{}, orr{};
        renderInto(vm, mgr, 256);
        oracle.render(ol.data(), orr.data(), 256);
        for (int i = 0; i < 256; ++i) {
            REQUIRE(mgr.l[static_cast<std::size_t>(i)] == ol[static_cast<std::size_t>(i)]);
            REQUIRE(mgr.r[static_cast<std::size_t>(i)] == orr[static_cast<std::size_t>(i)]);
        }
    }
}

// --- §6.3: UNISON — shared decision, detune, pan, distinct drift -------------------

TEST_CASE("voicemanager: UNISON sounds U voices on the resolved note", "[voicemanager]") {
    // §6.3 / K9: with U>1, U voices (slots 0..U-1) all sound the SAME resolved note;
    // selection/retrigger are identical to MONO (the same single decision).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(4);
    { Stereo s; renderInto(vm, s, 1); }   // apply the latched reconfig at the boundary

    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 67, 1.0f, 0});
    vm.controlTick(freshTrigger(67));

    REQUIRE(vm.activeCount() == 4);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(vm.voice(i).state() == VoiceState::Active);
        REQUIRE(vm.voice(i).currentNote() == 67);   // mono-faithful: same note for all
    }
    for (int i = 4; i < mw::kMaxVoices; ++i) {
        REQUIRE(vm.voice(i).state() == VoiceState::Idle);
    }
}

TEST_CASE("voicemanager: UNISON detune law is symmetric and centered, zero for U==1", "[voicemanager]") {
    // §6.3 / ADR-006 C10: detune_i = spread * (2*i/(U-1) - 1) — symmetric about 0,
    // edges at ±spread, exactly 0 for a single voice. This is the (PI) law in
    // Calibration.h (cal::voice::unisonDetuneCents). ORACLE on the centralized law.
    using mw::cal::voice::unisonDetuneCents;
    constexpr float spread = 9.0f;

    // U == 1: the single voice is exactly on pitch.
    REQUIRE(unisonDetuneCents(0, 1, spread) == 0.0f);

    for (int u = 2; u <= mw::kMaxUnison; ++u) {
        // Edges land at exactly ±spread (the index-0 / index-(U-1) endpoints evaluate
        // 2*i/(U-1)-1 to exactly -1 / +1, so these are exact).
        REQUIRE(unisonDetuneCents(0, u, spread) == -spread);
        REQUIRE(unisonDetuneCents(u - 1, u, spread) == spread);
        // Symmetric: voice i mirrors voice (U-1-i) (tolerance: the two are computed via
        // different float roundings of 2*i/(U-1), so compare within an epsilon, not by
        // bit-equality — the symmetry is the contract, not the rounding).
        for (int i = 0; i < u; ++i) {
            REQUIRE(std::abs(unisonDetuneCents(i, u, spread)
                             + unisonDetuneCents(u - 1 - i, u, spread)) < 1.0e-4f);
        }
        // Centered: the detune values sum to (approximately) zero.
        float sum = 0.0f;
        for (int i = 0; i < u; ++i) sum += unisonDetuneCents(i, u, spread);
        REQUIRE(std::abs(sum) < 1.0e-4f);
    }
}

TEST_CASE("voicemanager: UNISON pan spread is the symmetric Calibration law, zero for U==1", "[voicemanager]") {
    // §6.3 / ADR-006 C10 / ADR-013: the stereo-spread distribution law is (PI) and
    // lives in Calibration.h (cal::voice::unisonPan). It is symmetric, centered, and 0
    // for a single voice.
    using mw::cal::voice::unisonPan;
    constexpr float amount = 1.0f;

    REQUIRE(unisonPan(0, 1, amount) == 0.0f);
    for (int u = 2; u <= mw::kMaxUnison; ++u) {
        REQUIRE(unisonPan(0, u, amount) == -amount);
        REQUIRE(unisonPan(u - 1, u, amount) == amount);
        // Symmetric within an epsilon (different float roundings of 2*i/(U-1), see the
        // detune test): the symmetry is the contract, not the bit-level rounding.
        for (int i = 0; i < u; ++i) {
            REQUIRE(std::abs(unisonPan(i, u, amount) + unisonPan(u - 1 - i, u, amount))
                    < 1.0e-4f);
        }
    }
}

TEST_CASE("voicemanager: UNISON voices carry distinct drift seeds for real beating", "[voicemanager]") {
    // §6.3 / ADR-006 C10, C18: each unison voice keeps its distinct deterministic
    // drift seed (set in prepare from its fixed index), so detune is real analog
    // beating, not a static pitch fan. The U sounding voices all have different seeds.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(mw::kMaxUnison);
    { Stereo s; renderInto(vm, s, 1); }

    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));

    for (int i = 0; i < mw::kMaxUnison; ++i) {
        for (int j = i + 1; j < mw::kMaxUnison; ++j) {
            REQUIRE(vm.voice(i).driftSeed() != vm.voice(j).driftSeed());
        }
    }
}

TEST_CASE("voicemanager: UNISON note-feel is byte-identical to MONO for the same input", "[voicemanager]") {
    // §6.3 / K9: priority/retrigger are owned by the SAME single KeyAssigner, so the
    // resolved {activeNote, gate, retrigger} sequence the unison stack receives is
    // identical to the MONO case. Drive both managers with the identical event stream
    // and assert voice 0 (the lead voice, no detune offset at i s.t. it lands on the
    // same pan) tracks the same note + state through legato/release.
    auto runSequence = [](VoiceManager& vm) {
        // Legato overlap then release, MONO+GateTrig style note feel.
        vm.setGateTrigMode(GateTrigMode::GateTrig);
        vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
        NoteDecision d1; d1.activeNote = 60; d1.gate = true; d1.retrigger = true;
        vm.controlTick(d1);
        const int n1 = vm.voice(0).currentNote();

        vm.handleNoteEvent({NoteEvent::Type::NoteOn, 64, 1.0f, 0});
        NoteDecision d2; d2.activeNote = 64; d2.gate = true; d2.retrigger = true;
        vm.controlTick(d2);
        const int n2 = vm.voice(0).currentNote();

        vm.handleNoteEvent({NoteEvent::Type::AllNotesOff, 0, 0.0f, 0});
        NoteDecision off;
        vm.controlTick(off);
        const VoiceState s = vm.voice(0).state();
        return std::array<int, 3>{n1, n2, static_cast<int>(s)};
    };

    VoiceManager mono;
    mono.prepare(kSampleRate, kOversample, kSeed);
    const auto monoSeq = runSequence(mono);

    VoiceManager uni;
    uni.prepare(kSampleRate, kOversample, kSeed);
    uni.setMode(VoiceMode::Unison);
    uni.setUnisonCount(4);
    { Stereo s; renderInto(uni, s, 1); }   // boundary-apply the unison config
    const auto uniSeq = runSequence(uni);

    REQUIRE(uniSeq[0] == monoSeq[0]);   // same first note
    REQUIRE(uniSeq[1] == monoSeq[1]);   // same last-note priority result
    REQUIRE(uniSeq[2] == monoSeq[2]);   // same release state on all-off
}

// --- §8 RT2 / ADR-019 VT-02: fixed voice-index-order render/sum --------------------

TEST_CASE("voicemanager: render walks active voices in fixed ascending voice-index order", "[voicemanager]") {
    // §8 RT2 / ADR-019 VT-02: the active-voice list is a dense ASCENDING-index prefix,
    // so the sum is in fixed voice-index order (the bless-stable FP reduction order).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(5);
    { Stereo s; renderInto(vm, s, 1); }

    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));

    REQUIRE(vm.activeCount() == 5);
    for (int k = 0; k < vm.activeCount(); ++k) {
        REQUIRE(vm.activeIndex(k) == static_cast<std::uint8_t>(k));
    }
    // The active list is strictly increasing (fixed order, no duplicates).
    for (int k = 1; k < vm.activeCount(); ++k) {
        REQUIRE(vm.activeIndex(k) > vm.activeIndex(k - 1));
    }
}

TEST_CASE("voicemanager: render is deterministic across identical runs", "[voicemanager]") {
    // §8 RT4 / ADR-019 VT-04: identical input -> bit-identical output (fixed render/sum
    // order + deterministic per-voice seeds). Two managers prepared identically and fed
    // the same events render byte-for-byte the same in UNISON.
    auto run = [](Stereo& out) {
        VoiceManager vm;
        vm.prepare(kSampleRate, kOversample, kSeed);
        vm.setMode(VoiceMode::Unison);
        vm.setUnisonCount(3);
        { Stereo s; vm.render(s.l.data(), s.r.data(), 1); }
        vm.handleNoteEvent({NoteEvent::Type::NoteOn, 62, 1.0f, 0});
        vm.controlTick(freshTrigger(62));
        for (int b = 0; b < 4; ++b) {
            Stereo s;
            vm.render(s.l.data(), s.r.data(), 256);
            if (b == 3) out = s;
        }
    };
    Stereo a, b;
    run(a);
    run(b);
    for (int i = 0; i < 256; ++i) {
        REQUIRE(a.l[static_cast<std::size_t>(i)] == b.l[static_cast<std::size_t>(i)]);
        REQUIRE(a.r[static_cast<std::size_t>(i)] == b.r[static_cast<std::size_t>(i)]);
    }
}

// --- §8 RT7 / ADR-006 C17: mode & unison-count change only at a block boundary -----

TEST_CASE("voicemanager: setMode is latched and applies only at a block boundary", "[voicemanager]") {
    // §8 RT7 / ADR-006 C17: a mode change is latched (lock-free flag) and takes effect
    // only at the next block boundary (top of render), NEVER mid-block.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    REQUIRE(vm.mode() == VoiceMode::Mono);

    vm.setMode(VoiceMode::Unison);
    // Not applied yet: no render (block boundary) has happened.
    REQUIRE(vm.mode() == VoiceMode::Mono);

    Stereo s; renderInto(vm, s, 64);   // block boundary -> apply latched change
    REQUIRE(vm.mode() == VoiceMode::Unison);
}

TEST_CASE("voicemanager: setUnisonCount is latched, clamped, and applies at a boundary", "[voicemanager]") {
    // §8 RT7 / ADR-006 C9, C17: unison-count is clamped to 1..kMaxUnison, latched, and
    // applied only at the next block boundary.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);

    vm.setUnisonCount(999);             // out of range -> clamp to kMaxUnison
    REQUIRE(vm.unisonCount() == 1);     // not applied until a boundary
    { Stereo s; renderInto(vm, s, 1); }
    REQUIRE(vm.unisonCount() == mw::kMaxUnison);

    vm.setUnisonCount(0);               // out of range -> clamp to 1
    { Stereo s; renderInto(vm, s, 1); }
    REQUIRE(vm.unisonCount() == 1);
}

TEST_CASE("voicemanager: a mode switch does not leave a stuck note", "[voicemanager]") {
    // ADR-006 §Consequences: the mode/voice-count switch is handled at the boundary so
    // it never leaves a stuck note. Sound a wide unison stack, then switch to MONO; the
    // voices outside slot 0 must be released (not stuck Active) after the boundary.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(6);
    { Stereo s; renderInto(vm, s, 1); }

    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));
    REQUIRE(vm.activeCount() == 6);

    vm.setMode(VoiceMode::Mono);
    { Stereo s; renderInto(vm, s, 64); }   // boundary applies MONO
    REQUIRE(vm.mode() == VoiceMode::Mono);
    for (int i = 1; i < mw::kMaxVoices; ++i) {
        REQUIRE(vm.voice(i).state() != VoiceState::Active);
    }
}

// --- §8 RT3/RT6 + ADR-001 C3/C4: render is alloc-free on the audio thread ----------

TEST_CASE("voicemanager: render performs no heap allocation", "[voicemanager]") {
    // §8 RT3/RT6 / ADR-001 C3: a representative UNISON render allocates nothing on the
    // audio thread (the pool is sized in prepare; render only walks/accumulates).
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(4);
    { Stereo s; renderInto(vm, s, 1); }
    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));

    Stereo warm; renderInto(vm, warm, 64);   // warm up lazy init OUTSIDE the armed window

    Stereo s;
    mw::test::AudioThreadGuard guard;
    guard.arm();
    vm.render(s.l.data(), s.r.data(), 64);
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
}

TEST_CASE("voicemanager: control-tick and note-event handling are alloc-free", "[voicemanager]") {
    // §8 RT6 / ADR-006 §4, C17: note handling and the control tick only flip existing
    // voice state; they never touch the heap.
    VoiceManager vm;
    vm.prepare(kSampleRate, kOversample, kSeed);
    vm.setMode(VoiceMode::Unison);
    vm.setUnisonCount(3);
    { Stereo s; renderInto(vm, s, 1); }

    // Warm up the event/tick path once outside the armed window.
    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick(freshTrigger(60));

    mw::test::AudioThreadGuard guard;
    guard.arm();
    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 64, 1.0f, 0});
    vm.controlTick(freshTrigger(64));
    NoteDecision off;
    vm.controlTick(off);
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
}
