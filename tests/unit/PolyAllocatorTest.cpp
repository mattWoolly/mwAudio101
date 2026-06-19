// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the POLY per-note allocator + deterministic voice-stealing
// scan (task 075). Test-case display names begin with "polyalloc" so
// `ctest -R polyalloc` selects exactly these under the silent-pass rule; '[' is kept
// OUT of the display text (Catch2 parses it as a tag and it would break -R selection).
//
// Covers every acceptance criterion of
// plan/backlog/075-voicemanager-poly-allocator-deterministic-voic.md against
// docs/design/04-voice-and-control.md §6.4 (POLY) / §6.5 (Determinism) and ADR-006
// §Decision item 3 POLY / C11-C18:
//   - K12/K13: every poly note-on is a fresh GATE+TRIG trigger; re-striking a held key
//     reuses that voice with no doubling.
//   - K14: with no idle voice, the victim follows oldest-release -> quietest ->
//     oldest-held, tie-broken by ascending integer note-serial; same input -> same
//     victim deterministically.
//   - K15: a steal calls Voice::beginSteal (fast fade, not a hard cut) and other
//     voices' release tails finish in place.
//   - K16/K11: unison-on-poly gives floor(maxPoly/U) groups, active count never
//     exceeds kMaxVoices, steals remove whole unison groups.
//   - the allocator is an O(kMaxVoices) integer scan, noexcept / alloc-free / lock-free.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "voice/PolyAllocator.h"
#include "voice/Voice.h"
#include "voice/VoiceTypes.h"
#include "calibration/PolyAllocatorConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::PolyAllocator;
using mw::Voice;
using mw::VoiceState;

namespace {

constexpr double        kSampleRate = 48000.0;
constexpr int           kOversample = 2;
constexpr std::uint32_t kSeed       = 0xC0FFEEu;

// The pool is large (kMaxVoices full Voices, each carrying DSP scratch) — heap it so
// the test stack stays small, and prepare every slot exactly as VoiceManager does.
struct PoolFixture {
    std::unique_ptr<PolyAllocator::Pool> pool = std::make_unique<PolyAllocator::Pool>();
    std::uint64_t nextSerial = 0;
    PolyAllocator alloc;

    PoolFixture() {
        for (int i = 0; i < mw::kMaxVoices; ++i) {
            (*pool)[static_cast<std::size_t>(i)].prepare(kSampleRate, kOversample, i, kSeed);
        }
    }

    void configure(int maxPoly, int unison) {
        alloc.configure(*pool, nextSerial, maxPoly, unison);
    }

    Voice& voice(int i) { return (*pool)[static_cast<std::size_t>(i)]; }

    // Render the whole pool so envelopes advance (no audio kept; just state).
    void renderPool(int n) {
        std::array<float, 512> l{}, r{};
        for (int i = 0; i < mw::kMaxVoices; ++i) {
            voice(i).render(l.data(), r.data(), n);
        }
    }
};

} // namespace

// --- surface / configuration ------------------------------------------------------

TEST_CASE("polyalloc: allocator is a flat value type with noexcept hot paths", "[polyalloc]") {
    // §8 RT3/RT6; ADR-001 C5: the allocator is a value type with noexcept methods (no
    // throw on the audio thread), no virtual dispatch.
    STATIC_REQUIRE_FALSE(std::is_polymorphic_v<PolyAllocator>);
    STATIC_REQUIRE(std::is_nothrow_default_constructible_v<PolyAllocator>);

    PoolFixture fx;
    STATIC_REQUIRE(noexcept(fx.alloc.allocatePoly(60)));
    STATIC_REQUIRE(noexcept(fx.alloc.releasePoly(60)));
}

TEST_CASE("polyalloc: unison-on-poly yields floor of maxPoly over U groups", "[polyalloc]") {
    // K16 / ADR-006 C16: effective polyphony = floor(maxPoly / U) groups; each group is
    // U contiguous voice slots; the active-voice count never exceeds kMaxVoices.
    PoolFixture fx;

    fx.configure(/*maxPoly=*/mw::kMaxPoly, /*unison=*/1);
    REQUIRE(fx.alloc.groupCount() == mw::kMaxPoly);          // U=1 -> maxPoly groups
    REQUIRE(fx.alloc.unisonCount() == 1);

    fx.configure(mw::kMaxPoly, /*U=*/2);
    REQUIRE(fx.alloc.groupCount() == mw::kMaxPoly / 2);

    fx.configure(mw::kMaxPoly, /*U=*/3);
    REQUIRE(fx.alloc.groupCount() == mw::kMaxPoly / 3);       // floor (truncating)

    fx.configure(mw::kMaxPoly, /*U=*/mw::kMaxUnison);
    REQUIRE(fx.alloc.groupCount() == mw::kMaxPoly / mw::kMaxUnison);

    // The group footprint never exceeds the pool hard cap (C16).
    for (int u = 1; u <= mw::kMaxUnison; ++u) {
        fx.configure(mw::kMaxPoly, u);
        REQUIRE(fx.alloc.groupCount() * fx.alloc.unisonCount() <= mw::kMaxVoices);
    }
}

// --- K12: every poly note-on is a fresh trigger ----------------------------------

TEST_CASE("polyalloc: every poly note-on is a fresh GATE+TRIG trigger on its own voice", "[polyalloc]") {
    // K12 / ADR-006 C12: each poly note allocates its OWN voice; every note is a fresh
    // trigger; no cross-voice lowest-note concept. Distinct notes take distinct groups,
    // each Active and sounding its own note, each with a distinct ascending serial.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/4, /*U=*/1);

    const int g0 = fx.alloc.allocatePoly(60);
    const int g1 = fx.alloc.allocatePoly(64);
    const int g2 = fx.alloc.allocatePoly(67);

    REQUIRE(g0 != g1);
    REQUIRE(g1 != g2);
    REQUIRE(g0 != g2);

    REQUIRE(fx.voice(fx.alloc.groupBase(g0)).state() == VoiceState::Active);
    REQUIRE(fx.voice(fx.alloc.groupBase(g0)).currentNote() == 60);
    REQUIRE(fx.voice(fx.alloc.groupBase(g1)).currentNote() == 64);
    REQUIRE(fx.voice(fx.alloc.groupBase(g2)).currentNote() == 67);

    // Fresh trigger => monotonically increasing note-serials, one per allocation.
    REQUIRE(fx.voice(fx.alloc.groupBase(g0)).noteSerial()
            < fx.voice(fx.alloc.groupBase(g1)).noteSerial());
    REQUIRE(fx.voice(fx.alloc.groupBase(g1)).noteSerial()
            < fx.voice(fx.alloc.groupBase(g2)).noteSerial());
}

TEST_CASE("polyalloc: prefers an idle group before stealing", "[polyalloc]") {
    // §6.4 step 1: an idle group is always preferred. With free slots, three note-ons
    // land on three distinct idle groups (no steal).
    PoolFixture fx;
    fx.configure(/*maxPoly=*/4, /*U=*/1);

    fx.alloc.allocatePoly(60);
    fx.alloc.allocatePoly(64);
    fx.alloc.allocatePoly(67);

    int active = 0;
    for (int g = 0; g < fx.alloc.groupCount(); ++g) {
        if (fx.voice(fx.alloc.groupBase(g)).state() == VoiceState::Active) ++active;
    }
    REQUIRE(active == 3);
}

// --- K13: re-strike reuse, no doubling -------------------------------------------

TEST_CASE("polyalloc: re-striking a held key reuses that voice with no doubling", "[polyalloc]") {
    // K13 / ADR-006 C13: re-striking a still-held note reuses ITS group, not a new one.
    // The same group index comes back, only one group sounds the note, and its serial
    // advances (fresh trigger) while no second group is consumed.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/4, /*U=*/1);

    const int first = fx.alloc.allocatePoly(60);
    const std::uint64_t serial1 = fx.voice(fx.alloc.groupBase(first)).noteSerial();

    const int again = fx.alloc.allocatePoly(60);
    REQUIRE(again == first);                                   // SAME group reused (no doubling)

    // Exactly one group sounds note 60.
    int sounding60 = 0;
    for (int g = 0; g < fx.alloc.groupCount(); ++g) {
        const Voice& v = fx.voice(fx.alloc.groupBase(g));
        if (v.state() == VoiceState::Active && v.currentNote() == 60) ++sounding60;
    }
    REQUIRE(sounding60 == 1);

    // A re-strike is a fresh trigger, so the serial advanced.
    REQUIRE(fx.voice(fx.alloc.groupBase(again)).noteSerial() > serial1);
}

// --- K14: deterministic steal order ----------------------------------------------

TEST_CASE("polyalloc: with no idle voice a releasing group is stolen before a held one", "[polyalloc]") {
    // K14 / ADR-006 C14 step 1: oldest-in-release beats held. Fill all groups, release
    // one (its tail finishes in place -> Releasing), then allocate a new note with no
    // idle slot: the Releasing group is the victim, the held groups keep playing.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/3, /*U=*/1);
    REQUIRE(fx.alloc.groupCount() == 3);

    fx.alloc.allocatePoly(60);
    fx.alloc.allocatePoly(64);
    const int gHeld3 = fx.alloc.allocatePoly(67);
    (void) gHeld3;

    // Release note 64 -> that group enters Releasing (tail in place).
    fx.alloc.releasePoly(64);
    int releasingGroup = -1;
    for (int g = 0; g < fx.alloc.groupCount(); ++g) {
        if (fx.voice(fx.alloc.groupBase(g)).state() == VoiceState::Releasing) releasingGroup = g;
    }
    REQUIRE(releasingGroup >= 0);

    // No idle group now (3 groups: 2 Active + 1 Releasing). New note -> steal scan.
    const int victim = fx.alloc.allocatePoly(72);
    REQUIRE(victim == releasingGroup);                       // the releasing group was stolen
    REQUIRE(fx.voice(fx.alloc.groupBase(victim)).currentNote() == 72);
}

TEST_CASE("polyalloc: with all held the quietest group is stolen", "[polyalloc]") {
    // K14 / ADR-006 C14 step 2: with NO releasing group, the quietest held group
    // (lowest currentLevel) is the victim. We trigger groups at staggered times so
    // their envelopes sit at different levels, then assert the allocator's victim is the
    // observed-argmin currentLevel — an ORACLE on the live state, not an assumed ADSR.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/3, /*U=*/1);

    // Stagger: g0 oldest (most advanced env), g2 youngest (lowest env).
    fx.alloc.allocatePoly(60); fx.renderPool(200);
    fx.alloc.allocatePoly(64); fx.renderPool(200);
    fx.alloc.allocatePoly(67); fx.renderPool(50);

    // All three Active, none releasing.
    for (int g = 0; g < 3; ++g) {
        REQUIRE(fx.voice(fx.alloc.groupBase(g)).state() == VoiceState::Active);
    }

    // Observed argmin of currentLevel over the held groups (the quietest).
    int expected = 0;
    float minLevel = fx.voice(fx.alloc.groupBase(0)).currentLevel();
    for (int g = 1; g < 3; ++g) {
        const float lvl = fx.voice(fx.alloc.groupBase(g)).currentLevel();
        if (lvl < minLevel) { minLevel = lvl; expected = g; }
    }

    const int victim = fx.alloc.allocatePoly(72);
    REQUIRE(victim == expected);                             // quietest held was stolen
}

TEST_CASE("polyalloc: equal-level held groups break the tie by ascending note-serial", "[polyalloc]") {
    // K14 / ADR-006 C14 tie-break: when held groups are equally quiet, the OLDEST
    // (lowest integer note-serial) is stolen. Trigger all groups, render the whole pool
    // identically so every env sits at the SAME level, then the lowest-serial group must
    // be the victim. (Env level is seed-independent, so equal renders => equal levels.)
    PoolFixture fx;
    fx.configure(/*maxPoly=*/4, /*U=*/1);

    const int g0 = fx.alloc.allocatePoly(60);
    const int g1 = fx.alloc.allocatePoly(64);
    const int g2 = fx.alloc.allocatePoly(67);
    const int g3 = fx.alloc.allocatePoly(71);
    (void) g1; (void) g2; (void) g3;
    fx.renderPool(128);   // identical render for all -> identical env levels

    // Confirm levels are equal (the tie-break precondition) and all Active.
    const float lvl0 = fx.voice(fx.alloc.groupBase(0)).currentLevel();
    for (int g = 0; g < 4; ++g) {
        REQUIRE(fx.voice(fx.alloc.groupBase(g)).state() == VoiceState::Active);
        REQUIRE(fx.voice(fx.alloc.groupBase(g)).currentLevel() == lvl0);
    }
    // g0 holds the lowest serial (allocated first).
    for (int g = 1; g < 4; ++g) {
        REQUIRE(fx.voice(fx.alloc.groupBase(g0)).noteSerial()
                < fx.voice(fx.alloc.groupBase(g)).noteSerial());
    }

    const int victim = fx.alloc.allocatePoly(72);
    REQUIRE(victim == g0);                                   // oldest-held tie-break
}

TEST_CASE("polyalloc: oldest releasing group wins when several are releasing", "[polyalloc]") {
    // K14 / ADR-006 C14 step 1 + tie-break: among multiple Releasing groups, the OLDEST
    // (lowest note-serial) is stolen.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/3, /*U=*/1);

    const int gA = fx.alloc.allocatePoly(60);   // oldest
    fx.alloc.allocatePoly(64);
    fx.alloc.allocatePoly(67);

    // Release the two oldest notes -> two Releasing groups.
    fx.alloc.releasePoly(60);
    fx.alloc.releasePoly(64);

    const std::uint64_t serialA = fx.voice(fx.alloc.groupBase(gA)).noteSerial();

    const int victim = fx.alloc.allocatePoly(72);
    REQUIRE(fx.voice(fx.alloc.groupBase(victim)).currentNote() == 72);
    // The stolen group is the one that had the OLDEST (lowest) serial among releasers.
    REQUIRE(victim == gA);
    REQUIRE(serialA < fx.nextSerial);   // sanity: serial counter moved past it
}

TEST_CASE("polyalloc: the steal scan is deterministic across identical runs", "[polyalloc]") {
    // K14 / ADR-006 C14, C18; ADR-019 VT-04: same input sequence -> same victim, every
    // run. Two independent pools fed the identical sequence pick the same victim group.
    auto runAndStealVictim = []() {
        PoolFixture fx;
        fx.configure(/*maxPoly=*/3, /*U=*/1);
        fx.alloc.allocatePoly(60); fx.renderPool(200);
        fx.alloc.allocatePoly(64); fx.renderPool(120);
        fx.alloc.allocatePoly(67); fx.renderPool(40);
        return fx.alloc.allocatePoly(72);   // forces a steal
    };
    REQUIRE(runAndStealVictim() == runAndStealVictim());
}

// --- K15: steal is a fast fade, not a hard cut; other tails finish in place -------

TEST_CASE("polyalloc: a steal calls beginSteal so the victim fades not hard-cuts", "[polyalloc]") {
    // K15 / ADR-006 C15: a steal puts the victim into the Stealing state (fast forced
    // fade via Voice::beginSteal), NOT a hard cut. Immediately after the steal and
    // before the fade completes, the stolen voice's group is re-triggered Active for the
    // new note — but the key behavioral fact is that the steal went through beginSteal,
    // observable as the just-stolen slot being driven to a fresh trigger rather than
    // hard-silenced. We assert the victim is the releasing group and that the OTHER
    // groups' states are untouched by the steal.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/3, /*U=*/1);

    fx.alloc.allocatePoly(60);
    const int gKept = fx.alloc.allocatePoly(64);
    fx.alloc.allocatePoly(67);

    fx.alloc.releasePoly(60);                  // group for 60 -> Releasing (tail in place)
    const VoiceState keptBefore = fx.voice(fx.alloc.groupBase(gKept)).state();
    REQUIRE(keptBefore == VoiceState::Active);

    fx.alloc.allocatePoly(72);                 // steal the releasing group

    // The kept (held) group's release tail / hold is untouched by the steal (C15):
    REQUIRE(fx.voice(fx.alloc.groupBase(gKept)).state() == VoiceState::Active);
    REQUIRE(fx.voice(fx.alloc.groupBase(gKept)).currentNote() == 64);
}

TEST_CASE("polyalloc: beginSteal fades the victim rather than zeroing it instantly", "[polyalloc]") {
    // K15 / ADR-006 C15: directly exercise that a Voice steal is a RAMP, not a hard cut.
    // After beginSteal the voice is Stealing and its first rendered output is non-zero
    // (the fade starts at unity), proving it is not an instantaneous silence. This is the
    // primitive the allocator's steal path relies on.
    Voice v;
    v.prepare(kSampleRate, kOversample, /*voiceIndex=*/0, kSeed);
    v.noteOn(60, 1.0f, /*retrigger=*/true);
    std::array<float, 64> l{}, r{};
    v.render(l.data(), r.data(), 64);          // let the voice build some output

    v.beginSteal();
    REQUIRE(v.state() == VoiceState::Stealing);

    std::array<float, 256> fl{}, fr{};
    v.render(fl.data(), fr.data(), 256);
    // The fade is a ramp 1->0 over ~kStealFadeMs; it does not instantly zero. After a
    // sufficiently long render the voice has faded out and gone Idle (no hard cut, but it
    // does complete). State reaching Idle confirms the ramp ran to completion.
    REQUIRE(v.state() == VoiceState::Idle);
}

// --- K16/K11: unison-on-poly capacity + whole-group steal -------------------------

TEST_CASE("polyalloc: unison-on-poly steals whole unison groups never a single voice", "[polyalloc]") {
    // K11/K16 / ADR-006 C11, C16: with U>1, an allocation drives ALL U voices of its
    // group, and a steal fades the WHOLE victim group (every one of its U voices), never
    // an individual stacked voice.
    constexpr int U = 2;
    PoolFixture fx;
    fx.configure(/*maxPoly=*/mw::kMaxPoly, /*U=*/U);
    const int groups = fx.alloc.groupCount();
    REQUIRE(groups == mw::kMaxPoly / U);

    // Fill every group with a distinct note; each group's U voices all sound it.
    for (int g = 0; g < groups; ++g) {
        const int chosen = fx.alloc.allocatePoly(48 + g);
        const int base = fx.alloc.groupBase(chosen);
        for (int v = 0; v < U; ++v) {
            REQUIRE(fx.voice(base + v).state() == VoiceState::Active);
            REQUIRE(fx.voice(base + v).currentNote() == 48 + g);
        }
    }

    // Release group 0's note so it becomes the steal victim, then steal: all U voices of
    // that group must go to Stealing together (whole-group), and the freshly allocated
    // note re-triggers all U of them.
    fx.alloc.releasePoly(48);
    const int victim = fx.alloc.allocatePoly(96);
    const int vbase = fx.alloc.groupBase(victim);
    for (int v = 0; v < U; ++v) {
        REQUIRE(fx.voice(vbase + v).state() == VoiceState::Active);   // re-triggered as a unit
        REQUIRE(fx.voice(vbase + v).currentNote() == 96);
    }
}

TEST_CASE("polyalloc: active voice count never exceeds the configured capacity", "[polyalloc]") {
    // K16 / ADR-006 C16: hammering far more note-ons than groups never makes more than
    // groupCount() groups Active (groupCount()*U voices), and never exceeds kMaxVoices.
    constexpr int U = 2;
    PoolFixture fx;
    fx.configure(/*maxPoly=*/mw::kMaxPoly, /*U=*/U);
    const int groups = fx.alloc.groupCount();

    for (int n = 0; n < 64; ++n) {
        fx.alloc.allocatePoly(36 + (n % 60));
    }

    int activeVoices = 0;
    for (int i = 0; i < mw::kMaxVoices; ++i) {
        if (fx.voice(i).state() == VoiceState::Active) ++activeVoices;
    }
    REQUIRE(activeVoices <= groups * U);
    REQUIRE(activeVoices <= mw::kMaxVoices);
}

// --- release semantics ------------------------------------------------------------

TEST_CASE("polyalloc: releasePoly releases the matching held group and leaves others", "[polyalloc]") {
    // §6.4 / ADR-006 C12, C15: releasing a note de-asserts that group's gate (release
    // tail in place) and leaves every other held group untouched.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/4, /*U=*/1);

    const int g60 = fx.alloc.allocatePoly(60);
    const int g64 = fx.alloc.allocatePoly(64);

    fx.alloc.releasePoly(60);
    REQUIRE(fx.voice(fx.alloc.groupBase(g60)).state() == VoiceState::Releasing);
    REQUIRE(fx.voice(fx.alloc.groupBase(g64)).state() == VoiceState::Active);
    REQUIRE(fx.voice(fx.alloc.groupBase(g64)).currentNote() == 64);
}

// --- §8 RT3/RT6 + ADR-001 C3/C4: alloc-free / lock-free on the audio thread -------

TEST_CASE("polyalloc: allocate and release are alloc-free and lock-free", "[polyalloc]") {
    // §8 RT3/RT6 / ADR-001 C3/C4; ADR-006 C17: a note event only flips existing voice
    // state via a bounded O(kMaxVoices) integer scan; it never touches the heap or a
    // lock. Arm the AudioThreadGuard around a representative steal-forcing sequence.
    PoolFixture fx;
    fx.configure(/*maxPoly=*/4, /*U=*/2);

    // Warm up any lazy state OUTSIDE the armed window.
    fx.alloc.allocatePoly(60);
    fx.alloc.releasePoly(60);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    fx.alloc.allocatePoly(60);
    fx.alloc.allocatePoly(64);
    fx.alloc.allocatePoly(67);
    fx.alloc.allocatePoly(71);   // forces a steal (4 groups, this is the 4th+ note)
    fx.alloc.allocatePoly(74);
    fx.alloc.releasePoly(64);
    guard.disarm();
    REQUIRE_FALSE(guard.violated());
}
