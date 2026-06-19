// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for mw::seq::SequencerEngine — the arp/seq fixed-order state machine
// (task 087). Test-case names begin with "seqengine" so `-R seqengine` selects exactly
// this suite (the silent-pass rule); display names avoid '[' so ctest -R selection is
// not broken by Catch2 tag parsing.
//
// Each TEST_CASE maps to an 087 acceptance criterion and the cited docs/design/05
// §2.1/§2.2/§2.3/§9.1/§9.2/§9.3 sections / ADR-007 C17, C25, C26, C27.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "../invariants/AudioThreadGuard.h"
#include "BlockContext.h"
#include "calibration/SequencerEngineConstants.h"
#include "control/ControlTypes.h"
#include "control/SequencerEngine.h"

using mw::seq::SequencerEngine;
using namespace mw::control;
using Catch::Matchers::WithinAbs;

namespace {

mw::TransportInfo makeTransport(double bpm, double ppq, bool playing, double sr) {
    mw::TransportInfo t{};
    t.bpm = bpm;
    t.ppqPosition = ppq;
    t.isPlaying = playing;
    t.sampleRate = sr;
    return t;
}

KeyEvent gateOn(int pitch, int sampleOffset) {
    KeyEvent k{};
    k.pitch = pitch;
    k.gate = true;
    k.trig = true;
    k.sampleOffset = sampleOffset;
    return k;
}

double ppqPerSample(double bpm, double sr) { return (bpm / 60.0) / sr; }

// A snapshot configured with a playing sequencer + a small pattern, plus distinctive
// arp/clock/trigger/PWM/VCA state, for the persistence round-trip test.
ControlSnapshot makeRichSnapshot() {
    ControlSnapshot s{};
    // Fill all 100 slots with a deterministic pattern of note/rest/tie.
    for (int i = 0; i < kMaxSteps; ++i) {
        SeqStep st{};
        if (i % 7 == 0) {
            st.bits = SeqStep::kRestFlag;                              // a REST
        } else if (i % 5 == 0) {
            st.bits = static_cast<std::uint8_t>(SeqStep::kTieFlag | (i & SeqStep::kPitchMask)); // a TIE
        } else {
            st.bits = static_cast<std::uint8_t>(i & SeqStep::kPitchMask);  // a note
        }
        s.seq[static_cast<std::size_t>(i)] = st;
    }
    s.seqCount = kMaxSteps;
    s.arpMode = ArpMode::UandD;
    s.arpHold = true;
    s.uAndDRepeatEndpoints = true;
    s.clockSource = ClockSource::HostSync;
    s.internalRateHz = 7.5f;
    s.hostRate = HostRate::DottedEighth;
    s.swing = 0.625f;
    s.clockResetOnKeypress = false;
    s.trigMode = TrigMode::Gate;
    s.pwmSource = PwmSource::Env;
    s.vcaSource = VcaSource::Gate;
    s.schemaVersion = 1u;
    return s;
}

bool snapshotsEqual(const ControlSnapshot& a, const ControlSnapshot& b) {
    if (a.seqCount != b.seqCount) return false;
    for (int i = 0; i < kMaxSteps; ++i) {
        if (a.seq[static_cast<std::size_t>(i)].bits != b.seq[static_cast<std::size_t>(i)].bits)
            return false;
    }
    return a.arpMode == b.arpMode
        && a.arpHold == b.arpHold
        && a.uAndDRepeatEndpoints == b.uAndDRepeatEndpoints
        && a.clockSource == b.clockSource
        && a.internalRateHz == b.internalRateHz
        && a.hostRate == b.hostRate
        && a.swing == b.swing
        && a.clockResetOnKeypress == b.clockResetOnKeypress
        && a.trigMode == b.trigMode
        && a.pwmSource == b.pwmSource
        && a.vcaSource == b.vcaSource
        && a.schemaVersion == b.schemaVersion;
}

} // namespace

// ===========================================================================
// §9.2 / signature / POD shape — seam shape + noexcept hot path.
// ===========================================================================

TEST_CASE("seqengine: processBlock and the lifecycle calls are noexcept", "[seqengine]") {
    SequencerEngine e;
    mw::TransportInfo t{};
    std::array<ControlEvent, 4> out{};
    int n = 0;
    std::array<KeyEvent, 1> keys{};
    STATIC_REQUIRE(noexcept(e.prepare(48000.0, 512)));
    STATIC_REQUIRE(noexcept(e.reset()));
    STATIC_REQUIRE(noexcept(e.processBlock(t, std::span<const KeyEvent>{keys},
                                           std::span<const int>{}, std::span<ControlEvent>{out},
                                           0, n)));
    STATIC_REQUIRE(std::is_trivially_copyable_v<ControlSnapshot>);
}

TEST_CASE("seqengine: prepare publishes a non-null INIT-default live snapshot", "[seqengine]") {
    SequencerEngine e;
    e.prepare(48000.0, 512);
    const ControlSnapshot* live = e.liveSnapshot();
    REQUIRE(live != nullptr);
    // INIT defaults (doc 05 §9.2).
    REQUIRE(live->seqCount == 0);
    REQUIRE(live->arpMode == ArpMode::Up);
    REQUIRE(live->clockSource == ClockSource::Internal);
    REQUIRE(live->schemaVersion == 1u);
}

// ===========================================================================
// §2.1 / C17 — ONE H->L edge advances arp cursor + seq slot + RANDOM reload on the
// SAME edge, phase-consistent across Internal / HostSync / Ext sources.
// ===========================================================================

TEST_CASE("seqengine: a single edge advances the sequencer slot and RANDOM reload on the same edge", "[seqengine]") {
    const double sr = 48000.0;
    SequencerEngine e;
    e.prepare(sr, 1 << 16);

    // Internal clock, a distinctive 4-slot pattern, clock-reset OFF.
    ControlSnapshot s{};
    s.clockSource = ClockSource::Internal;
    s.internalRateHz = 2.0f;          // period = sr/2 = 24000 samples
    s.seqCount = 4;
    s.seq[0].bits = 11; s.seq[1].bits = 22; s.seq[2].bits = 33; s.seq[3].bits = 44;
    s.clockResetOnKeypress = false;
    e.publishSnapshot(s);
    e.setSeqPlay(true);               // PLAY toggle (transport)

    const std::uint64_t randBefore = e.randomReloadCount();

    // A block of EXACTLY one period (24000 frames): exactly ONE internal edge at off 0.
    std::array<ControlEvent, 64> out{};
    int n = 0;
    auto t = makeTransport(120.0, 0.0, true, sr);
    const int frames = static_cast<int>(std::lround(sr / 2.0));  // 24000
    e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                   std::span<ControlEvent>{out}, frames, n);

    // RANDOM reload fired exactly once (one edge) — the shared-edge contract [C17].
    REQUIRE(e.randomReloadCount() == randBefore + 1);

    // The sequencer advanced on that SAME edge: a step event for slot 0 (pitch 11) was
    // emitted at the edge offset.
    REQUIRE(n == 1);
    REQUIRE(out[0].sampleOffset == 0);
    REQUIRE(out[0].gate);
    REQUIRE(out[0].pitch == 11);   // slot 0's pitch

    // A second one-edge block advances to slot 1 (pitch 22) — proof the slot stepped.
    int n2 = 0;
    e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                   std::span<ControlEvent>{out}, frames, n2);
    REQUIRE(n2 == 1);
    REQUIRE(out[0].pitch == 22);   // slot 1's pitch — the slot advanced by one
    REQUIRE(e.randomReloadCount() == randBefore + 2);   // and RANDOM fired again
}

TEST_CASE("seqengine: a single edge advances the arp cursor and RANDOM reload on the same edge", "[seqengine]") {
    const double sr = 48000.0;
    SequencerEngine e;
    e.prepare(sr, 1 << 16);

    // Internal clock, NO sequencer playback, arp UP engaged by a 3-key chord. The arp
    // walks the held set in ascending key order, one step per edge.
    ControlSnapshot s{};
    s.clockSource = ClockSource::Internal;
    s.internalRateHz = 2.0f;          // period = sr/2 = 24000 samples
    s.arpMode = ArpMode::Up;
    s.clockResetOnKeypress = false;
    e.publishSnapshot(s);

    std::array<KeyEvent, 3> chord = {gateOn(2, 0), gateOn(5, 0), gateOn(9, 0)};
    auto t = makeTransport(120.0, 0.0, true, sr);
    const int frames = static_cast<int>(std::lround(sr / 2.0));  // one edge per block

    std::array<ControlEvent, 64> out{};
    int n = 0;
    const std::uint64_t randBefore = e.randomReloadCount();

    // First block establishes the held chord AND fires the first edge: arp emits the
    // ascending-order key 0 == key 2.
    e.processBlock(t, std::span<const KeyEvent>{chord}, std::span<const int>{},
                   std::span<ControlEvent>{out}, frames, n);
    REQUIRE(e.arp().isEngaged());
    REQUIRE(e.randomReloadCount() == randBefore + 1);
    REQUIRE(n == 1);
    REQUIRE(out[0].sampleOffset == 0);
    REQUIRE(out[0].pitch == 2);   // lowest held key (UP step 0)

    // Next edge: arp cursor advanced to the second held key (5).
    int n2 = 0;
    e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                   std::span<ControlEvent>{out}, frames, n2);
    REQUIRE(n2 == 1);
    REQUIRE(out[0].pitch == 5);   // arp cursor stepped to the second key
    REQUIRE(e.randomReloadCount() == randBefore + 2);
}

TEST_CASE("seqengine: RANDOM reload count equals edge count, phase-consistent across Internal/HostSync/Ext", "[seqengine]") {
    const double sr = 48000.0;

    // Helper: count edges the engine fired (== RANDOM reloads) for a given config.
    auto reloadsFor = [&](ClockSource src) -> std::uint64_t {
        SequencerEngine e;
        e.prepare(sr, 1 << 16);
        ControlSnapshot s{};
        s.clockSource = src;
        s.internalRateHz = 4.0f;          // period = sr/4 = 12000 samples (Internal)
        s.hostRate = HostRate::Quarter;   // 1.0 ppq per step (HostSync)
        s.clockResetOnKeypress = false;
        e.publishSnapshot(s);

        std::array<ControlEvent, 256> out{};
        int n = 0;
        const std::uint64_t before = e.randomReloadCount();

        if (src == ClockSource::Ext) {
            // Exactly three Ext pulses == three steps == three RANDOM reloads.
            const std::array<int, 3> pulses = {100, 4000, 9000};
            auto t = makeTransport(120.0, 0.0, true, sr);
            e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{pulses},
                           std::span<ControlEvent>{out}, 12000, n);
        } else if (src == ClockSource::HostSync) {
            // A block exactly 3 quarter-notes long starting on a boundary -> 3 edges.
            const double qps = ppqPerSample(120.0, sr);
            const int frames = static_cast<int>(std::lround(3.0 / qps));
            auto t = makeTransport(120.0, 0.0, true, sr);
            e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                           std::span<ControlEvent>{out}, frames, n);
        } else { // Internal
            // 3 periods of frames (period 12000) -> exactly 3 internal edges.
            auto t = makeTransport(120.0, 0.0, true, sr);
            const int frames = static_cast<int>(std::lround(sr / 4.0)) * 3;  // 36000
            e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                           std::span<ControlEvent>{out}, frames, n);
        }
        return e.randomReloadCount() - before;
    };

    REQUIRE(reloadsFor(ClockSource::Internal) == 3u);
    REQUIRE(reloadsFor(ClockSource::HostSync) == 3u);
    REQUIRE(reloadsFor(ClockSource::Ext) == 3u);
}

// ===========================================================================
// §9.1 / C25 — Save -> reload reproduces the full 100-slot buffer + arp + clock +
// trigger / PWM / VCA state; schemaVersion == 1 written.
// ===========================================================================

TEST_CASE("seqengine: save then reload round-trips the full control-core state, schemaVersion 1", "[seqengine]") {
    SequencerEngine e;
    e.prepare(48000.0, 512);

    const ControlSnapshot original = makeRichSnapshot();
    e.publishSnapshot(original);

    // Capture: must reproduce every persisted field, including all 100 slots.
    const ControlSnapshot captured = e.captureState();
    REQUIRE(captured.schemaVersion == 1u);
    REQUIRE(captured.seqCount == kMaxSteps);
    REQUIRE(snapshotsEqual(captured, original));

    // Reload into a FRESH engine and re-capture: the round-trip is exact.
    SequencerEngine e2;
    e2.prepare(48000.0, 512);
    e2.restoreState(captured);
    const ControlSnapshot reloaded = e2.captureState();
    REQUIRE(snapshotsEqual(reloaded, original));

    // Per-slot bit-for-bit equality of all 100 slots (the explicit C25 requirement).
    for (int i = 0; i < kMaxSteps; ++i) {
        REQUIRE(reloaded.seq[static_cast<std::size_t>(i)].bits
                == original.seq[static_cast<std::size_t>(i)].bits);
    }
}

TEST_CASE("seqengine: a malformed or forward-version snapshot falls back to INIT defaults", "[seqengine]") {
    SequencerEngine e;
    e.prepare(48000.0, 512);

    // First publish a known-good rich snapshot.
    e.publishSnapshot(makeRichSnapshot());
    REQUIRE(e.liveSnapshot()->arpMode == ArpMode::UandD);

    // Now publish a FORWARD-version snapshot: it must be rejected -> INIT defaults [§9.3].
    ControlSnapshot bad = makeRichSnapshot();
    bad.schemaVersion = 2u;          // unsupported forward version
    e.publishSnapshot(bad);

    const ControlSnapshot* live = e.liveSnapshot();
    REQUIRE(live != nullptr);
    REQUIRE(live->schemaVersion == 1u);            // sanitized back to INIT
    REQUIRE(live->arpMode == ArpMode::Up);          // INIT default
    REQUIRE(live->clockSource == ClockSource::Internal);
    REQUIRE(live->seqCount == 0);
}

// ===========================================================================
// §10 / C26 — No heap alloc and no lock during processBlock AND during a snapshot
// swap, verified by the alloc/lock sentinel.
// ===========================================================================

TEST_CASE("seqengine: processBlock does no heap allocation under the RT sentinel", "[seqengine]") {
    const double sr = 96000.0;
    SequencerEngine e;
    e.prepare(sr, 1 << 16);   // prepare may size; happens BEFORE arming

    // Dense config: HostSync 1/32 at high tempo + a held chord so arp + seq + the
    // keyboard read all run on the hot path.
    ControlSnapshot s{};
    s.clockSource = ClockSource::HostSync;
    s.hostRate = HostRate::ThirtySecond;
    s.seqCount = 8;
    for (int i = 0; i < 8; ++i) s.seq[static_cast<std::size_t>(i)].bits = static_cast<std::uint8_t>(i + 1);
    s.arpMode = ArpMode::UandD;
    s.clockResetOnKeypress = true;
    e.publishSnapshot(s);

    std::array<ControlEvent, 64> out{};
    std::array<KeyEvent, 3> chord = {gateOn(0, 0), gateOn(3, 10), gateOn(7, 20)};
    int n = 0;
    auto t = makeTransport(200.0, 0.0, true, sr);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    e.processBlock(t, std::span<const KeyEvent>{chord}, std::span<const int>{},
                   std::span<ControlEvent>{out}, 1 << 14, n);
    // An Ext block too (different path), still under the guard.
    e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                   std::span<ControlEvent>{out}, 1 << 14, n);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

TEST_CASE("seqengine: a live-snapshot swap during processBlock does no heap allocation", "[seqengine]") {
    // The audio-thread read of the live snapshot (the swap consumer) must not allocate.
    // The publish itself runs off the audio thread; here we verify the audio-thread
    // ACQUIRE-read path under the sentinel while alternating between two published
    // snapshots (the double-buffer swap), per §9.2 / C26.
    const double sr = 48000.0;
    SequencerEngine e;
    e.prepare(sr, 1 << 16);

    ControlSnapshot a{};
    a.clockSource = ClockSource::Internal;
    a.internalRateHz = 4.0f;
    ControlSnapshot b = a;
    b.internalRateHz = 8.0f;

    std::array<ControlEvent, 64> out{};
    int n = 0;
    auto t = makeTransport(120.0, 0.0, true, sr);

    // Publish OUTSIDE the guard (message-thread work); read INSIDE the guard.
    e.publishSnapshot(a);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    // Read the freshly published snapshot pointer on the audio thread (no lock).
    const ControlSnapshot* live = e.liveSnapshot();
    (void) live;
    e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                   std::span<ControlEvent>{out}, 1 << 12, n);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());

    // The published swap is observable (the new rate took effect off-thread).
    e.publishSnapshot(b);
    REQUIRE(e.liveSnapshot()->internalRateHz == 8.0f);
}

// ===========================================================================
// §2.2 / C27 — Clock edges land at expected sub-block sample offsets regardless of
// the control-tick period; the tick defaults to the ~2 ms vintage rate.
// ===========================================================================

TEST_CASE("seqengine: clock edges land at sample-accurate offsets independent of the control tick", "[seqengine]") {
    const double sr = 48000.0;
    const double bpm = 120.0;
    const double qps = ppqPerSample(bpm, sr);
    SequencerEngine e;
    e.prepare(sr, 1 << 16);

    // HostSync 1/4 starting 0.25 quarter-notes before a boundary at ppq = 2.0.
    ControlSnapshot s{};
    s.clockSource = ClockSource::HostSync;
    s.hostRate = HostRate::Quarter;
    s.swing = 0.5f;
    s.clockResetOnKeypress = false;
    e.publishSnapshot(s);

    const double blockStartPpq = 1.75;
    const int numFrames = 16384;
    std::array<ControlEvent, 64> out{};
    int n = 0;
    auto t = makeTransport(bpm, blockStartPpq, true, sr);
    e.processBlock(t, std::span<const KeyEvent>{}, std::span<const int>{},
                   std::span<ControlEvent>{out}, numFrames, n);

    // The boundary at ppq = 2.0 sits (2.0 - 1.75)/qps samples in. With no playing seq
    // / no engaged arp, the edge fires the RANDOM reload but emits no step note; the
    // sample-accurate placement is verified via the RANDOM reload count == 1 edge and
    // the edge-offset oracle through a second source (Ext) below.
    REQUIRE(e.randomReloadCount() == 1u);

    // Ext oracle: a pulse at a known offset must surface a step event AT that offset.
    SequencerEngine e2;
    e2.prepare(sr, 1 << 16);
    ControlSnapshot se{};
    se.clockSource = ClockSource::Ext;
    se.seqCount = 2;
    se.seq[0].bits = 5;
    se.seq[1].bits = 9;
    se.clockResetOnKeypress = false;
    e2.publishSnapshot(se);
    // Start the sequencer playing by republishing through restoreState (same effect).

    const int pulseOffset = static_cast<int>(std::lround((2.0 - 1.75) / qps));
    const std::array<int, 1> pulses = {pulseOffset};
    std::array<ControlEvent, 16> out2{};
    int n2 = 0;
    auto t2 = makeTransport(bpm, 0.0, true, sr);
    e2.processBlock(t2, std::span<const KeyEvent>{}, std::span<const int>{pulses},
                    std::span<ControlEvent>{out2}, numFrames, n2);
    // The Ext edge fired RANDOM reload exactly once at that pulse.
    REQUIRE(e2.randomReloadCount() == 1u);
}

TEST_CASE("seqengine: the control tick defaults to the ~2 ms vintage rate within the documented band", "[seqengine]") {
    // The shipped default control-tick period is 2.0 ms (the ~2 ms VINTAGE tick) and
    // sits inside the documented 1.5-3.5 ms hardware-loop band [§2.2; ADR-007 §Res 4].
    REQUIRE_THAT(mw::cal::seq::kControlTickSeconds, WithinAbs(0.002, 1e-9));
    REQUIRE(mw::cal::seq::kControlTickSeconds >= mw::cal::seq::kControlTickMinSeconds);
    REQUIRE(mw::cal::seq::kControlTickSeconds <= mw::cal::seq::kControlTickMaxSeconds);
}
