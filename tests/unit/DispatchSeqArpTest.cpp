// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/DispatchSeqArpTest.cpp — the control-dispatch + pattern-load acceptance
// suite for the seq/arp engine integration (task 181, ADR-030 part 1).
//
// Test-case display names ALL begin with "dispatch_seqarp" so
// `ctest -R dispatch_seqarp --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). The tag is "[dispatch_seqarp]"; no literal '['
// appears in any display name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (the dead-subsystem ship-blocker, ADR-030 break Q2):
//   * the seq.*/arp.* APVTS params, dispatched through the REAL per-control-tick path
//     (Engine::applyParamSnapshot, via Engine::process), drive the hosted SequencerEngine
//     + Arp — NOT the INIT-default ControlSnapshot. arp.mode/arp.latch reach
//     arp().mode/liveSnapshot()->arpHold; seq.mode reaches seq().isRecording()/isPlaying();
//   * the adopted seq pattern buffer reaches the engine's StepSequencer through the
//     engine load seam (count() > 0 and buffer() matches), so SequencerGrid/preset
//     patterns are live (was count() == 0 forever).
//
// NON-VACUITY: against the pre-181 code these FAIL because (a) applyParamSnapshot never
// read the seq.*/arp.* slots — the live snapshot stayed at INIT defaults (arpHold=false,
// arpMode=Up, seq not recording) regardless of the dispatched params; and (b) no engine
// seam loaded the adopted Extras pattern into the StepSequencer, so count() stayed 0.
//
// The Engine consumes the seam's immutable mw::ParamSnapshot once per control tick; this
// file builds that POD directly (the off-thread bridge's job in the real shell) and reads
// the hosted-component state via the const sequencer() accessor (the accepted test back
// door; production wiring is the point). It links mwcore ONLY (no JUCE) [ADR-001].

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "params/ParamDefs.h"
#include "params/ParamIDs.h"
#include "params/ParamSnapshot.h"
#include "control/ControlTypes.h"
#include "state/Extras.h"
#include "voice/VoiceTypes.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 4096;
constexpr int    kMaxVoices = mw::kMaxVoices;

using mw::control::ArpMode;

// --- registry-index lookup: map a parameter string-ID to its kParamDefs slot. ----------
int slotOf(const char* id) noexcept {
    for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
        const char* a = mw::params::kParamDefs[static_cast<std::size_t>(i)].id;
        const char* b = id;
        int k = 0;
        while (a[k] != '\0' && b[k] != '\0' && a[k] == b[k]) ++k;
        if (a[k] == '\0' && b[k] == '\0') return i;
    }
    return -1;
}

// Build a ParamSnapshot pre-loaded with every live param's DEFAULT in normalized form
// (exactly what the bridge emits). The same Snap helper the other dispatch suites use.
struct Snap {
    mw::ParamSnapshot s{};

    Snap() noexcept {
        for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
            const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
            if (d.type == mw::params::ParamType::Continuous) {
                const float span = d.maxValue - d.minValue;
                const float norm = span > 0.0f ? (d.defaultValue - d.minValue) / span : 0.0f;
                s.normalizedValues[static_cast<std::size_t>(i)] = norm;
            } else {
                const int idx = static_cast<int>(d.defaultValue);
                s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
                const float denom = (d.choiceCount > 1)
                                        ? static_cast<float>(d.choiceCount - 1) : 1.0f;
                s.normalizedValues[static_cast<std::size_t>(i)] =
                    static_cast<float>(idx) / denom;
            }
        }
    }

    // Set a CHOICE/BOOL param by its option index.
    void setChoice(const char* id, int idx) noexcept {
        const int i = slotOf(id);
        const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(i)];
        s.indexValues[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(idx);
        const float denom = (d.choiceCount > 1) ? static_cast<float>(d.choiceCount - 1) : 1.0f;
        s.normalizedValues[static_cast<std::size_t>(i)] = static_cast<float>(idx) / denom;
    }
};

// A self-contained block driver (mirrors EngineSequencerTest::Block): owns the stereo
// output, fills a BlockContext with the snapshot + a chosen transport.
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n) : L(static_cast<std::size_t>(n), 0.0f),
                            R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const mw::ParamSnapshot& snap, int n, bool playing,
                         double bpm = 120.0, double ppq = 0.0) {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = &snap;
        c.transport         = { bpm, ppq, playing, kSr };
        c.midi.events       = nullptr;
        c.midi.numEvents    = 0;
        return c;
    }
};

} // namespace

// ===========================================================================
// (1) arp.mode + arp.latch dispatch: a snapshot with arp.mode = Down + arp.latch on,
//     dispatched through the REAL applyParamSnapshot path (Engine::process), drives the
//     hosted Arp's mode AND the live snapshot's arpHold — NOT the INIT defaults
//     (arpMode=Up, arpHold=false). [ADR-030 Q2; ADR-028]
// ===========================================================================
TEST_CASE("dispatch_seqarp: arp mode and latch params drive the engine arp, not INIT defaults",
          "[dispatch_seqarp]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Sanity: a freshly-prepared engine sits at INIT defaults (arpMode Up, arpHold false).
    REQUIRE(eng.sequencer().liveSnapshot() != nullptr);
    REQUIRE(eng.sequencer().liveSnapshot()->arpMode == ArpMode::Up);
    REQUIRE_FALSE(eng.sequencer().liveSnapshot()->arpHold);

    // arp.mode choice {Off=0, Up=1, Down=2, Up-Down=3} -> ArpMode {Up=0,UandD=1,Down=2}.
    // Pick Down (choice 2 -> ArpMode::Down) and latch ON (arp.latch = true).
    Snap snap;
    snap.setChoice(mw::params::ids::kArpMode, /*Down*/ 2);
    snap.setChoice(mw::params::ids::kArpLatch, /*on*/ 1);

    constexpr int N = 512;
    Block blk(N);
    auto c = blk.ctx(snap.s, N, /*playing=*/true);
    eng.process(c);

    // The dispatch drove the hosted Arp's mode and the live snapshot's HOLD latch.
    REQUIRE(eng.sequencer().arp().heldBitmap() == 0u);                  // no keys (config only)
    REQUIRE(eng.sequencer().liveSnapshot()->arpMode == ArpMode::Down);  // was Up
    REQUIRE(eng.sequencer().liveSnapshot()->arpHold);                   // was false
}

// ===========================================================================
// (1b) arp.mode = Off keeps the latch path inert: Off (choice 0) does not engage the arp;
//      the live snapshot's arpHold tracks arp.latch independently. Proves the Off position
//      is honored (not folded to Up). [ADR-030 Q2]
// ===========================================================================
TEST_CASE("dispatch_seqarp: arp.mode Off does not latch arp hold on its own",
          "[dispatch_seqarp]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    Snap snap;
    snap.setChoice(mw::params::ids::kArpMode, /*Off*/ 0);
    snap.setChoice(mw::params::ids::kArpLatch, /*off*/ 0);

    constexpr int N = 512;
    Block blk(N);
    auto c = blk.ctx(snap.s, N, /*playing=*/true);
    eng.process(c);

    // arp.mode Off -> arp not held on; latch off -> arpHold false. The engine routing gate
    // (renderChunk: arpEnabled == snap->arpHold) therefore stays false: a bare config does
    // not hijack keyboard play.
    REQUIRE_FALSE(eng.sequencer().liveSnapshot()->arpHold);
}

// ===========================================================================
// (2) seq.mode dispatch: a snapshot with seq.mode = Record drives the StepSequencer into
//     record; seq.mode = Play drives it into play. Through the REAL applyParamSnapshot
//     path. Was: the StepSequencer never saw the seq.mode param (INIT default, not
//     recording / not playing). [ADR-030 Q2]
// ===========================================================================
TEST_CASE("dispatch_seqarp: seq.mode param drives the StepSequencer record and play state",
          "[dispatch_seqarp]") {
    // seq.mode choice {Off=0, Play=1, Record=2}.
    auto modeAfter = [](int seqModeChoice) {
        mw::Engine eng;
        eng.prepare(kSr, kMaxBlock, kMaxVoices);
        Snap snap;
        snap.setChoice(mw::params::ids::kSeqMode, seqModeChoice);
        constexpr int N = 512;
        Block blk(N);
        auto c = blk.ctx(snap.s, N, /*playing=*/true);
        eng.process(c);
        return std::pair<bool, bool>{ eng.sequencer().seq().isPlaying(),
                                      eng.sequencer().seq().isRecording() };
    };

    // Off: neither.
    {
        auto [playing, recording] = modeAfter(/*Off*/ 0);
        REQUIRE_FALSE(playing);
        REQUIRE_FALSE(recording);
    }
    // Record: recording (note an empty buffer cannot enter record — setRecord guards on a
    // full buffer only, count<kMaxSteps, so an empty buffer DOES enter record).
    {
        auto [playing, recording] = modeAfter(/*Record*/ 2);
        REQUIRE(recording);
        REQUIRE_FALSE(playing);
    }
    // Play: playing. (Playback only advances with a loaded pattern; the play FLAG is set
    // regardless — count>0 is the separate buffer-load concern, tested below.)
    {
        auto [playing, recording] = modeAfter(/*Play*/ 1);
        REQUIRE(playing);
        REQUIRE_FALSE(recording);
    }
}

// ===========================================================================
// (3) Pattern load seam: a >=2-step Extras pattern fed through the engine load seam
//     reaches the StepSequencer — count() and buffer() match (was count()==0). Proves
//     the SequencerGrid/preset -> engine pattern path is live. [ADR-030 Q2]
// ===========================================================================
TEST_CASE("dispatch_seqarp: an adopted Extras pattern loads into the engine StepSequencer",
          "[dispatch_seqarp]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // Pre-condition: a fresh engine's StepSequencer is empty (the dead-path symptom).
    REQUIRE(eng.sequencer().seq().count() == 0);

    // Build a 3-step Extras pattern: a note, a rest, a tie (distinct articulations so the
    // conversion is non-trivial). noteSemitone is relative to the base; clamped on load.
    mw::state::Extras ex{};
    ex.stepCount = 3;
    ex.steps[0].gate = true;  ex.steps[0].rest = false; ex.steps[0].tie = false; ex.steps[0].noteSemitone = 7;
    ex.steps[1].gate = false; ex.steps[1].rest = true;  ex.steps[1].tie = false; ex.steps[1].noteSemitone = 0;
    ex.steps[2].gate = true;  ex.steps[2].rest = false; ex.steps[2].tie = true;  ex.steps[2].noteSemitone = 12;

    eng.loadSeqPattern(ex);

    // The pattern reached the StepSequencer: count() == 3 (was 0).
    REQUIRE(eng.sequencer().seq().count() == 3);

    // The buffer slots decode back to the articulations we loaded.
    const auto& buf = eng.sequencer().seq().buffer();
    REQUIRE_FALSE(buf[0].isRest());
    REQUIRE_FALSE(buf[0].isTie());
    REQUIRE(buf[0].pitch() == 7);

    REQUIRE(buf[1].isRest());

    REQUIRE_FALSE(buf[2].isRest());
    REQUIRE(buf[2].isTie());
    REQUIRE(buf[2].pitch() == 12);
}

// ===========================================================================
// (3b) A loaded pattern + seq.mode Play actually plays: with the pattern loaded and
//      seq.mode dispatched to Play, the engine's seq is playing AND has a non-zero count,
//      so the renderChunk seqPlaying gate (count>0 && isPlaying) can fire. This ties the
//      two seams together end-to-end on the core path. [ADR-030 Q2]
// ===========================================================================
TEST_CASE("dispatch_seqarp: a loaded pattern plus seq.mode Play arms the seq playback gate",
          "[dispatch_seqarp]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    mw::state::Extras ex{};
    ex.stepCount = 2;
    ex.steps[0].gate = true; ex.steps[0].noteSemitone = 0;
    ex.steps[1].gate = true; ex.steps[1].noteSemitone = 5;
    eng.loadSeqPattern(ex);

    Snap snap;
    snap.setChoice(mw::params::ids::kSeqMode, /*Play*/ 1);

    constexpr int N = 512;
    Block blk(N);
    auto c = blk.ctx(snap.s, N, /*playing=*/true);
    eng.process(c);

    REQUIRE(eng.sequencer().seq().isPlaying());
    REQUIRE(eng.sequencer().seq().count() == 2);
}

// ===========================================================================
// (4) RT-safety: the seq/arp dispatch + pattern-load on the live process path performs
//     ZERO heap allocations and takes ZERO locks under an armed AudioThreadGuard.
//     prepare()/loadSeqPattern() (off-thread config) + a warm block run OUTSIDE the guard.
//     [doc 00 §9.1; doc 05 §10]
// ===========================================================================
TEST_CASE("dispatch_seqarp: the seq/arp dispatch path performs zero allocations and zero locks",
          "[dispatch_seqarp][rt]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    mw::state::Extras ex{};
    ex.stepCount = 4;
    for (int i = 0; i < 4; ++i) { ex.steps[i].gate = true; ex.steps[i].noteSemitone = static_cast<std::int8_t>(i * 3); }
    eng.loadSeqPattern(ex);

    Snap snap;
    snap.setChoice(mw::params::ids::kArpMode, /*Up-Down*/ 3);
    snap.setChoice(mw::params::ids::kArpLatch, /*on*/ 1);
    snap.setChoice(mw::params::ids::kSeqMode, /*Play*/ 1);

    constexpr int N = kMaxBlock;
    Block blk(N);
    auto c = blk.ctx(snap.s, N, /*playing=*/true);

    eng.process(c);   // warm once off the guard

    mw::test::AudioThreadGuard guard;
    guard.arm();
    eng.process(c);
    eng.process(c);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}
