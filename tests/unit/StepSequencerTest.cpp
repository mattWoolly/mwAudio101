// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the 100-slot StepSequencer (task 085). Test-case names begin
// with "stepseq" so `-R stepseq` selects exactly this suite (the silent-pass
// rule). Each TEST_CASE maps to an 085 acceptance criterion and to the cited
// docs/design/05 sections (§6.1/§6.3/§6.4/§6.5) and ADR-007 C12–C16 / ADR-025.
//
// Display names deliberately avoid the '[' character (Catch2 parses it as a tag),
// so `ctest -R stepseq` selection stays intact.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <type_traits>

#include "control/StepSequencer.h"
#include "control/ControlTypes.h"

#include "../invariants/AudioThreadGuard.h"

using namespace mw::control;

// ---------------------------------------------------------------------------
// §6.1 / C12,C13 / ADR-025 — Step model: one event per slot; payload has NO
// per-step accent field (note/rest/tie/gate only).
// ---------------------------------------------------------------------------

TEST_CASE("stepseq: note, REST and each tie-extension each consume exactly one slot",
          "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);

    REQUIRE(seq.count() == 0);

    seq.recordNote(12);   // a note: one slot
    REQUIRE(seq.count() == 1);

    seq.recordRest();     // a REST: one slot
    REQUIRE(seq.count() == 2);

    seq.recordTie(15);    // a tie-extension: one slot
    REQUIRE(seq.count() == 3);

    seq.recordTie(15);    // another tie-extension: another slot
    REQUIRE(seq.count() == 4);

    // Slot 0 is a plain note (no flags); slot 1 is REST; slots 2,3 are TIE.
    const SeqBuffer& buf = seq.buffer();
    REQUIRE(buf[0].pitch() == 12);
    REQUIRE_FALSE(buf[0].isRest());
    REQUIRE_FALSE(buf[0].isTie());

    REQUIRE(buf[1].isRest());
    REQUIRE_FALSE(buf[1].isTie());

    REQUIRE(buf[2].isTie());
    REQUIRE_FALSE(buf[2].isRest());
    REQUIRE(buf[2].pitch() == 15);
    REQUIRE(buf[3].isTie());
}

TEST_CASE("stepseq: per-step payload is note/rest/tie only with NO accent field in the type",
          "[stepseq]") {
    // SeqStep is a single byte: 6-bit pitch + REST + TIE. There is no member that
    // could carry accent or per-step gate-time (removed per ADR-025; §6.1 C13).
    STATIC_REQUIRE(sizeof(SeqStep) == 1u);
    STATIC_REQUIRE(std::is_trivially_copyable_v<SeqStep>);
    // The byte is fully partitioned into pitch | REST | TIE — no spare bits remain
    // for an accent flag.
    STATIC_REQUIRE((SeqStep::kPitchMask | SeqStep::kRestFlag | SeqStep::kTieFlag)
                   == 0xFF);
    // SeqPlayResult decodes only note/rest/tie/gate articulation, no accent.
    STATIC_REQUIRE(std::is_trivially_copyable_v<SeqPlayResult>);
}

TEST_CASE("stepseq: 6-bit pitch is masked to the low 6 bits on record (DAC range)",
          "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);

    seq.recordNote(63);   // max in-range pitch
    seq.recordNote(0);    // min
    // Out-of-range high bits must not leak into other flags; masked to 6 bits.
    seq.recordNote(0xC1); // 0b1100_0001 -> low 6 bits = 1, flags clear
    seq.recordTie(0xFF);  // tie + low 6 bits = 63

    const SeqBuffer& buf = seq.buffer();
    REQUIRE(buf[0].pitch() == 63);
    REQUIRE(buf[1].pitch() == 0);

    REQUIRE(buf[2].pitch() == 1);
    REQUIRE_FALSE(buf[2].isRest());
    REQUIRE_FALSE(buf[2].isTie());

    REQUIRE(buf[3].isTie());
    REQUIRE(buf[3].pitch() == 63);
}

// ---------------------------------------------------------------------------
// §6.3 / C14,C15 — Transport: LOAD records keyboard-only, auto-exits at 100;
// PLAY loops wrapping last->first, one slot per edge.
// ---------------------------------------------------------------------------

TEST_CASE("stepseq: recording only appends while LOAD/record is on", "[stepseq]") {
    StepSequencer seq;
    seq.prepare();

    // Not recording: writes are ignored.
    REQUIRE_FALSE(seq.isRecording());
    seq.recordNote(5);
    seq.recordRest();
    seq.recordTie(7);
    REQUIRE(seq.count() == 0);

    // LOAD on: writes append.
    seq.setRecord(true);
    REQUIRE(seq.isRecording());
    seq.recordNote(5);
    REQUIRE(seq.count() == 1);

    // LOAD off: writes ignored again.
    seq.setRecord(false);
    REQUIRE_FALSE(seq.isRecording());
    seq.recordNote(9);
    REQUIRE(seq.count() == 1);
}

TEST_CASE("stepseq: LOAD recording auto-exits when all 100 slots are filled",
          "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);

    for (int i = 0; i < kMaxSteps; ++i) {
        REQUIRE(seq.isRecording());            // still recording up to the 100th write
        seq.recordNote(i & SeqStep::kPitchMask);
    }
    // After the 100th slot the sequencer auto-exits record (C14).
    REQUIRE(seq.count() == kMaxSteps);
    REQUIRE_FALSE(seq.isRecording());

    // Further writes are dropped — count stays at kMaxSteps, never overruns.
    seq.recordNote(1);
    seq.recordRest();
    seq.recordTie(2);
    REQUIRE(seq.count() == kMaxSteps);
}

TEST_CASE("stepseq: PLAY advances exactly one slot per edge and wraps last to first",
          "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);
    seq.recordNote(10);   // slot 0
    seq.recordNote(20);   // slot 1
    seq.recordNote(30);   // slot 2
    seq.setRecord(false);
    REQUIRE(seq.count() == 3);

    seq.setPlay(true);
    REQUIRE(seq.isPlaying());

    // First edge sounds slot 0, then 1, then 2, then wraps to 0 (last -> first).
    SeqPlayResult r0 = seq.advanceOnEdge();
    REQUIRE(r0.slotIndex == 0);
    REQUIRE(r0.pitch6 == 10);

    SeqPlayResult r1 = seq.advanceOnEdge();
    REQUIRE(r1.slotIndex == 1);
    REQUIRE(r1.pitch6 == 20);

    SeqPlayResult r2 = seq.advanceOnEdge();
    REQUIRE(r2.slotIndex == 2);
    REQUIRE(r2.pitch6 == 30);

    SeqPlayResult r3 = seq.advanceOnEdge();   // wrap-around
    REQUIRE(r3.slotIndex == 0);
    REQUIRE(r3.pitch6 == 10);
}

TEST_CASE("stepseq: advanceOnEdge is a no-op when no slots are filled", "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setPlay(true);
    // count_ == 0 -> playPos stays 0, no wrap divide-by-zero (§6.5).
    SeqPlayResult r = seq.advanceOnEdge();
    REQUIRE(r.slotIndex == 0);
    SeqPlayResult r2 = seq.advanceOnEdge();
    REQUIRE(r2.slotIndex == 0);
}

TEST_CASE("stepseq: resetToStart re-phases playback to slot 0", "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);
    seq.recordNote(1);
    seq.recordNote(2);
    seq.recordNote(3);
    seq.setRecord(false);
    seq.setPlay(true);

    (void) seq.advanceOnEdge();   // slot 0
    (void) seq.advanceOnEdge();   // slot 1

    seq.resetToStart();           // clock-reset re-phase
    SeqPlayResult r = seq.advanceOnEdge();
    REQUIRE(r.slotIndex == 0);    // back to the first slot
    REQUIRE(r.pitch6 == 1);
}

// ---------------------------------------------------------------------------
// §6.4 / C16 — Articulation: REST drops gate; TIE sets tie/sustain + suppresses
// retrigger; a plain note gates + retriggers.
// ---------------------------------------------------------------------------

TEST_CASE("stepseq: REST drops the gate; TIE sustains and suppresses retrigger; note gates+retriggers",
          "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);
    seq.recordNote(40);   // slot 0: plain note
    seq.recordRest();     // slot 1: REST
    seq.recordTie(42);    // slot 2: TIE
    seq.setRecord(false);
    seq.setPlay(true);

    SeqPlayResult note = seq.advanceOnEdge();   // slot 0
    REQUIRE(note.pitch6 == 40);
    REQUIRE(note.gateOn);          // note gates
    REQUIRE_FALSE(note.tie);
    REQUIRE(note.retrigger);       // and retriggers the envelope

    SeqPlayResult rest = seq.advanceOnEdge();   // slot 1
    REQUIRE_FALSE(rest.gateOn);    // REST drops the gate (§6.4)
    REQUIRE_FALSE(rest.tie);

    SeqPlayResult tie = seq.advanceOnEdge();    // slot 2
    REQUIRE(tie.pitch6 == 42);
    REQUIRE(tie.gateOn);           // TIE sustains the gate (no drop)
    REQUIRE(tie.tie);              // tie/sustain set
    REQUIRE_FALSE(tie.retrigger);  // no re-gate on a TIE (§6.4 C16)
}

// ---------------------------------------------------------------------------
// §6.5 — loadBuffer/buffer round-trip for preset restore; clear resets count.
// ---------------------------------------------------------------------------

TEST_CASE("stepseq: loadBuffer restores a preset buffer and count for playback",
          "[stepseq]") {
    SeqBuffer src{};
    src[0].bits = static_cast<std::uint8_t>(5);                        // note pitch 5
    src[1].bits = SeqStep::kRestFlag;                                  // REST
    src[2].bits = static_cast<std::uint8_t>(SeqStep::kTieFlag | 7);    // TIE pitch 7

    StepSequencer seq;
    seq.prepare();
    seq.loadBuffer(src, 3);
    REQUIRE(seq.count() == 3);

    // The buffer is restored verbatim.
    REQUIRE(seq.buffer()[0].pitch() == 5);
    REQUIRE(seq.buffer()[1].isRest());
    REQUIRE(seq.buffer()[2].isTie());
    REQUIRE(seq.buffer()[2].pitch() == 7);

    // And it plays back correctly.
    seq.setPlay(true);
    SeqPlayResult r0 = seq.advanceOnEdge();
    REQUIRE(r0.pitch6 == 5);
    REQUIRE(r0.gateOn);
    SeqPlayResult r1 = seq.advanceOnEdge();
    REQUIRE_FALSE(r1.gateOn);
}

TEST_CASE("stepseq: clear resets the filled count to zero", "[stepseq]") {
    StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);
    seq.recordNote(1);
    seq.recordNote(2);
    REQUIRE(seq.count() == 2);

    seq.clear();
    REQUIRE(seq.count() == 0);

    // After clear, recording appends from slot 0 again.
    seq.recordNote(9);
    REQUIRE(seq.count() == 1);
    REQUIRE(seq.buffer()[0].pitch() == 9);
}

// ---------------------------------------------------------------------------
// §6.5 / ADR-007 C26 — RT-safety: advanceOnEdge and record-path do zero heap
// allocation under the AudioThreadGuard sentinel.
// ---------------------------------------------------------------------------

TEST_CASE("stepseq: advanceOnEdge and record do no heap alloc under the sentinel",
          "[stepseq]") {
    // Hot paths are noexcept (RT-4).
    static_assert(noexcept(std::declval<StepSequencer&>().advanceOnEdge()),
                  "advanceOnEdge() must be noexcept.");
    static_assert(noexcept(std::declval<StepSequencer&>().recordNote(0)),
                  "recordNote() must be noexcept.");
    static_assert(noexcept(std::declval<StepSequencer&>().recordRest()),
                  "recordRest() must be noexcept.");
    static_assert(noexcept(std::declval<StepSequencer&>().recordTie(0)),
                  "recordTie() must be noexcept.");

    StepSequencer seq;
    seq.prepare();                       // prepare may size; happens BEFORE arming

    mw::test::AudioThreadGuard g;
    g.arm();

    // Record path: append a full set of slots (no heap alloc — fixed array).
    seq.setRecord(true);
    int filled = 0;
    for (int i = 0; i < 16; ++i) {
        seq.recordNote(i & SeqStep::kPitchMask);
        seq.recordRest();
        seq.recordTie(i & SeqStep::kPitchMask);
        filled += 3;
    }
    seq.setRecord(false);

    // Play path: advance many edges (wraps; fixed array, integer cursor).
    seq.setPlay(true);
    int acc = 0;
    for (int i = 0; i < 4096; ++i) {
        SeqPlayResult r = seq.advanceOnEdge();
        acc += r.slotIndex + r.pitch6 + (r.gateOn ? 1 : 0);
    }

    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
    REQUIRE(filled == 48);
    REQUIRE(acc >= 0);   // touch acc so the loop is not optimized away
}
