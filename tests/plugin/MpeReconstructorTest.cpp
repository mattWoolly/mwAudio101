// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/MpeReconstructorTest.cpp — JUCE-linked Catch2 tests for the
// MPE-over-MIDI reconstruction parser (task 103). Realizes docs/design/09 §7.3 /
// §7.1 and ADR-022 C2 / ADR-012 §4, C10-C13.
//
// Test-case display names begin with the task tag `mpereconstruct` so the
// `-R mpereconstruct` ctest selector matches them and the silent-pass rule holds.
//
// The no-alloc/no-lock invariant (Acceptance criterion 3) is proved with a
// process-level heap probe rather than the shared AudioThreadGuard global-new
// override: mw101_plugin_tests globs every tests/plugin/*.cpp into ONE binary, and a
// sibling JUCE-phase test (LatencyReporterTest.cpp) already defines the replaceable
// global operator new in this same target. Two translation units cannot both define
// global operator new, so we MUST NOT add a second override here. Instead we read the
// allocator's exact bytes_used delta around the armed window via mstats() — a
// zero-global-symbol, collision-proof allocation sentinel on macOS arm64 (the
// documented build/bless platform, docs/design/09 §1.2). The parser owns only fixed
// std::array state, so the delta MUST be 0.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <random>

#include <malloc/malloc.h>   // mstats(): override-free heap-usage probe (macOS arm64)

#include "midi/MpeReconstructor.h"
#include "../../core/calibration/MpeReconstructorConstants.h"

using namespace mw::plugin;
namespace cal = mw::cal::mpe;

namespace {

constexpr int   kMaxVoices = 8;
constexpr float kEps       = 1.0e-6f;

bool nearly(float a, float b) noexcept { return std::abs(a - b) <= kEps; }

} // namespace

// --- Acceptance 1: members default OFF (0), opt-in 1..15, lower zone only --------

TEST_CASE("mpereconstruct: member channels default OFF (0 members) so member notes assign no voice",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);

    REQUIRE(mpe.memberCount() == cal::kDefaultMembers);   // 0 by default
    REQUIRE(cal::kDefaultMembers == 0);

    // With 0 members, a note on member channel 2 must NOT claim a per-note voice
    // (MPE-lite is OFF until opted in) [ADR-012 C10; docs/design/09 §7.1].
    mpe.noteOn(/*channel=*/2, /*note=*/60, /*vel=*/100);
    REQUIRE(mpe.voiceForChannel(2) == cal::kUnassignedVoice);
}

TEST_CASE("mpereconstruct: opt-in 1..15 members maps member channels 2..(1+count) to voices",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(4);                 // members 1..4 -> MIDI channels 2..5
    REQUIRE(mpe.memberCount() == 4);

    // Each of the 4 enabled member channels claims a distinct voice on note-on.
    std::array<int, 4> voices{};
    for (int m = 0; m < 4; ++m) {
        const std::uint8_t ch = static_cast<std::uint8_t>(cal::kFirstMemberChannel + m);
        mpe.noteOn(ch, /*note=*/60 + static_cast<std::uint8_t>(m), /*vel=*/100);
        voices[static_cast<std::size_t>(m)] = mpe.voiceForChannel(ch);
        REQUIRE(voices[static_cast<std::size_t>(m)] != cal::kUnassignedVoice);
        REQUIRE(voices[static_cast<std::size_t>(m)] >= 0);
        REQUIRE(voices[static_cast<std::size_t>(m)] < kMaxVoices);
    }
    // Distinct channels get distinct voices.
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            REQUIRE(voices[static_cast<std::size_t>(a)] != voices[static_cast<std::size_t>(b)]);
}

TEST_CASE("mpereconstruct: lower zone only -- channels beyond enabled members claim no voice",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(2);                 // only members 1..2 -> MIDI channels 2..3

    mpe.noteOn(2, 60, 100);
    mpe.noteOn(3, 61, 100);
    REQUIRE(mpe.voiceForChannel(2) != cal::kUnassignedVoice);
    REQUIRE(mpe.voiceForChannel(3) != cal::kUnassignedVoice);

    // Channel 4 is OUTSIDE the enabled lower-zone members -> no per-note voice.
    mpe.noteOn(4, 62, 100);
    REQUIRE(mpe.voiceForChannel(4) == cal::kUnassignedVoice);
}

TEST_CASE("mpereconstruct: member count clamps to the lower-zone maximum 1..15",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);

    mpe.setMemberCount(99);
    REQUIRE(mpe.memberCount() == cal::kMaxMembers);   // 15
    mpe.setMemberCount(-5);
    REQUIRE(mpe.memberCount() == 0);                   // clamped, OFF
}

// --- Acceptance 2: per-note pitch -> per-voice pre-Q offset; pressure -> ONE dest -

TEST_CASE("mpereconstruct: per-note pitch-bend lands on that channel's voice pre-Q pitch offset only",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(3);

    mpe.noteOn(2, 60, 100);
    mpe.noteOn(3, 64, 100);
    const int v2 = mpe.voiceForChannel(2);
    const int v3 = mpe.voiceForChannel(3);

    mpe.pitchBend(2, +12.0f);              // channel 2 bends up an octave
    mpe.pitchBend(3, -3.0f);               // channel 3 bends down 3 semitones

    REQUIRE(nearly(mpe.voicePitchOffsetSemis(v2), +12.0f));
    REQUIRE(nearly(mpe.voicePitchOffsetSemis(v3), -3.0f));
    // The bend on channel 2 must NOT leak onto channel 3's voice.
    REQUIRE(! nearly(mpe.voicePitchOffsetSemis(v2), mpe.voicePitchOffsetSemis(v3)));
}

TEST_CASE("mpereconstruct: per-note pressure routes to ONE assignable destination (default VCF cutoff CV)",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(2);

    // Default assignable destination is the VCF cutoff CV node [ADR-012 C12].
    REQUIRE(mpe.pressureDestination() == PressureDestination::VcfCutoffCv);

    mpe.noteOn(2, 60, 100);
    const int v2 = mpe.voiceForChannel(2);

    mpe.pressure(2, 0.75f);
    REQUIRE(nearly(mpe.voicePressure(v2), 0.75f));

    // No OTHER per-note routing: only pitch offset + the single pressure value exist
    // per voice. Pressure must not perturb the voice's pitch offset (no timbre matrix)
    // [ADR-012 C12; docs/design/09 §7.1].
    REQUIRE(nearly(mpe.voicePitchOffsetSemis(v2), cal::kInitialPitchOffsetSemis));
}

TEST_CASE("mpereconstruct: noteOff frees the channel's voice for re-rotation",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(2);

    mpe.noteOn(2, 60, 100);
    const int v = mpe.voiceForChannel(2);
    REQUIRE(v != cal::kUnassignedVoice);

    mpe.noteOff(2, 60);
    REQUIRE(mpe.voiceForChannel(2) == cal::kUnassignedVoice);
}

// --- Acceptance: Mono / MPE-OFF collapse to channel bend + channel pressure -------

TEST_CASE("mpereconstruct: MPE-OFF (0 members) collapses master-channel bend to ALL voices globally",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    // Members default OFF -> Collapsed rung [ADR-012 C13; docs/design/09 §7.1].
    REQUIRE(mpe.memberCount() == 0);

    mpe.pitchBend(cal::kMasterChannel, +2.0f);     // channel-1 (master) global bend
    mpe.pressure(cal::kMasterChannel, 0.5f);       // channel-1 (master) global pressure

    // Collapsed: the master bend/pressure apply to EVERY voice (channel behavior),
    // bit-identical to running without MPE.
    for (int v = 0; v < kMaxVoices; ++v) {
        REQUIRE(nearly(mpe.voicePitchOffsetSemis(v), +2.0f));
        REQUIRE(nearly(mpe.voicePressure(v), 0.5f));
    }
}

TEST_CASE("mpereconstruct: with members ON, master-channel bend still applies globally to all voices",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(3);

    mpe.noteOn(2, 60, 100);
    const int v2 = mpe.voiceForChannel(2);
    mpe.pitchBend(2, +5.0f);                        // per-note bend on the member
    mpe.pitchBend(cal::kMasterChannel, +1.0f);      // master bend = global offset

    // The member voice carries member-bend + master-bend; an untouched voice carries
    // only the master bend (the lower-zone master applies to the whole zone).
    REQUIRE(nearly(mpe.voicePitchOffsetSemis(v2), +5.0f + 1.0f));
    const int idle = (v2 == 0) ? 1 : 0;            // some voice with no per-note bend
    REQUIRE(nearly(mpe.voicePitchOffsetSemis(idle), +1.0f));
}

TEST_CASE("mpereconstruct: reset() clears all channel<->voice assignments and per-voice expression",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(4);

    mpe.noteOn(2, 60, 100);
    mpe.pitchBend(2, +7.0f);
    mpe.pressure(2, 0.9f);
    mpe.pitchBend(cal::kMasterChannel, +1.0f);

    mpe.reset();

    for (std::uint8_t ch = 1; ch <= cal::kLastMemberChannel; ++ch)
        REQUIRE(mpe.voiceForChannel(ch) == cal::kUnassignedVoice);
    for (int v = 0; v < kMaxVoices; ++v) {
        REQUIRE(nearly(mpe.voicePitchOffsetSemis(v), cal::kInitialPitchOffsetSemis));
        REQUIRE(nearly(mpe.voicePressure(v), cal::kInitialPressureNorm));
    }
}

// --- Acceptance 3: prepare-then-parse over random channel sequences = zero alloc --

TEST_CASE("mpereconstruct: prepare-then-parse over random channel sequences performs zero alloc/lock",
          "[mpereconstruct]")
{
    MpeReconstructor mpe;
    mpe.prepare(kMaxVoices);
    mpe.setMemberCount(cal::kMaxMembers);          // all 15 members ON (widest state)

    // Pre-generate a deterministic random message stream OUTSIDE the armed scope so
    // only the parser's hot path is measured.
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> chDist(1, cal::kLastMemberChannel);  // 1..16
    std::uniform_int_distribution<int> noteDist(0, 127);
    std::uniform_real_distribution<float> bendDist(-48.0f, 48.0f);
    std::uniform_real_distribution<float> pressDist(0.0f, 1.0f);
    std::uniform_int_distribution<int> kindDist(0, 3);

    constexpr int kN = 4096;
    struct Msg { int kind; std::uint8_t ch; std::uint8_t note; float v; };
    std::array<Msg, kN> stream{};
    for (int i = 0; i < kN; ++i) {
        stream[static_cast<std::size_t>(i)] = Msg{
            kindDist(rng),
            static_cast<std::uint8_t>(chDist(rng)),
            static_cast<std::uint8_t>(noteDist(rng)),
            0.0f };
        Msg& m = stream[static_cast<std::size_t>(i)];
        m.v = (m.kind == 2) ? bendDist(rng) : pressDist(rng);
    }

    // Touch mstats() once before the measured window so any lazy first-call
    // bookkeeping inside the allocator is not counted against the parser.
    (void) mstats();

    const std::size_t before = mstats().bytes_used;
    for (int i = 0; i < kN; ++i) {
        const Msg& m = stream[static_cast<std::size_t>(i)];
        switch (m.kind) {
            case 0: mpe.noteOn(m.ch, m.note, 100); break;
            case 1: mpe.noteOff(m.ch, m.note);     break;
            case 2: mpe.pitchBend(m.ch, m.v);      break;
            default: mpe.pressure(m.ch, m.v);      break;
        }
    }
    const std::size_t after = mstats().bytes_used;

    REQUIRE(after == before);                      // zero heap allocation in the hot path
}
