// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/EventTranslatorTest.cpp — acceptance tests for the field-for-field
// HostEvent -> mw::MidiEvent translation (task 101), compiled into the JUCE-linked
// mw101_plugin_tests target. Every test-case display name begins with the
// `evttranslate` tag so the `-R evttranslate` ctest selector matches >= 1
// [docs/design/09 §3.3; AGENTS.md silent-pass rule].
//
// Covers each acceptance criterion of plan/backlog/101-hostevent-mw.md:
//   1. Each field maps EXACTLY per the §3.3 table; ProgramChange is NOT forwarded.
//   2. Identical HostEvent input yields identical mw::MidiEvent output (cross-format
//      determinism precondition) [§3.3; ADR-011 C11].
//   3. Translation performs ZERO heap allocation [§3.3; ADR-011 C9 — AudioThreadGuard
//      contract]. Asserted STRUCTURALLY (see the no-alloc test): mw101_plugin_tests
//      already defines its single global operator-new override in
//      tests/plugin/LatencyReporterTest.cpp, so a second runtime alloc-sentinel here
//      would be a duplicate-symbol link error. The translator is stateless, noexcept,
//      and writes into a caller-owned pre-sized buffer, so it CANNOT allocate — a
//      stronger guarantee than a single sampled run (holds for every input). This
//      mirrors the sibling tests/plugin/CcLearnMapTest.cpp strategy.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

#include "midi/EventTranslator.h"     // unit under test
#include "midi/CcLearnMap.h"          // §6 learn map (CC -> param index resolution)
#include "host/HostEvent.h"           // mw::plugin::HostEvent / HostEventType
#include "params/ParamDefs.h"         // mw::params::kParamDefs (independent index check)

#include "BlockContext.h"             // mw::MidiEvent / mw::NormalizedType (core seam)

namespace {

// Independent registry-index resolver (NOT the unit-under-test's path) so the
// CC-resolution assertions are not circular.
int registryIndexOf(std::string_view id) noexcept {
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (std::string_view{ mw::params::kParamDefs[i].id } == id)
            return static_cast<int>(i);
    return -1;
}

// A fully-populated HostEvent with distinctive field values for the field-map checks.
mw::plugin::HostEvent makeHostEvent(mw::plugin::HostEventType type,
                                    std::uint8_t channel,
                                    std::int32_t sampleOffset,
                                    std::int32_t data0,
                                    float value,
                                    std::int32_t noteId) noexcept {
    return mw::plugin::HostEvent{ type, channel, sampleOffset, data0, value, noteId };
}

bool midiEventsEqual(const mw::MidiEvent& a, const mw::MidiEvent& b) noexcept {
    return a.type == b.type
        && a.channel == b.channel
        && a.noteId == b.noteId
        && a.data0 == b.data0
        && a.value == b.value
        && a.sampleOffset == b.sampleOffset;
}

} // namespace

// ============================================================================
// Acceptance criterion 1: each NON-CC field maps EXACTLY per the §3.3 table, with the
// documented narrowings (channel uint8->int8, noteId int32->int16, data0->float,
// value verbatim, sampleOffset->int). NoteOn/NoteOff/PitchBend/pressure/Poly/ClockEdge/
// ParamValue all map 1:1 on type.
// ============================================================================
TEST_CASE("evttranslate maps each non-CC field per the doc-09 §3.3 table", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;   // default-seeded; only consulted for CC events

    struct Row {
        mw::plugin::HostEventType in;
        mw::NormalizedType        expect;
    };
    const Row rows[] = {
        { mw::plugin::HostEventType::NoteOn,          mw::NormalizedType::NoteOn },
        { mw::plugin::HostEventType::NoteOff,         mw::NormalizedType::NoteOff },
        { mw::plugin::HostEventType::PitchBend,       mw::NormalizedType::PitchBend },
        { mw::plugin::HostEventType::ChannelPressure, mw::NormalizedType::ChannelPressure },
        { mw::plugin::HostEventType::PolyPressure,    mw::NormalizedType::PolyPressure },
        { mw::plugin::HostEventType::ClockEdge,       mw::NormalizedType::ClockEdge },
        { mw::plugin::HostEventType::ParamValue,      mw::NormalizedType::ParamValue },
    };

    for (const auto& row : rows) {
        // Distinctive fields, incl. values that exercise the narrowings: channel 16
        // (top of 1..16), noteId 30000 (> int8, fits int16), data0 64, a fractional
        // value, sampleOffset 257.
        const auto host = makeHostEvent(row.in, /*channel=*/16, /*sampleOffset=*/257,
                                        /*data0=*/64, /*value=*/0.625f, /*noteId=*/30000);
        mw::MidiEvent out{};
        REQUIRE(mw::plugin::translateOne(host, ccMap, out) == true);

        // type: enum remap 1:1.
        REQUIRE(out.type == row.expect);
        // channel: uint8 1..16 -> int8 narrowing copy.
        REQUIRE(out.channel == static_cast<std::int8_t>(16));
        // noteId: int32 -> int16 (CLAP id; -1 preserved for MIDI-derived, tested below).
        REQUIRE(out.noteId == static_cast<std::int16_t>(30000));
        // data0: int32 -> float widening (non-CC types forward the raw value).
        REQUIRE(out.data0 == 64.0f);
        // value: copied verbatim.
        REQUIRE(out.value == 0.625f);
        // sampleOffset: int32 -> int copied.
        REQUIRE(out.sampleOffset == 257);
    }
}

// ============================================================================
// Acceptance criterion 1 (cont.): noteId == -1 (MIDI-derived) is PRESERVED, and the
// lower-zone master channel = 1 narrows cleanly.
// ============================================================================
TEST_CASE("evttranslate preserves a MIDI-derived note id of minus one and master channel one", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;

    const auto host = makeHostEvent(mw::plugin::HostEventType::NoteOn, /*channel=*/1,
                                    /*sampleOffset=*/0, /*data0=*/60, /*value=*/1.0f,
                                    /*noteId=*/-1);
    mw::MidiEvent out{};
    REQUIRE(mw::plugin::translateOne(host, ccMap, out) == true);
    REQUIRE(out.noteId == static_cast<std::int16_t>(-1));   // -1 preserved
    REQUIRE(out.channel == static_cast<std::int8_t>(1));    // lower-zone master = 1
    REQUIRE(out.data0 == 60.0f);
}

// ============================================================================
// Acceptance criterion 1 (cont.): ProgramChange is CONSUMED in plugin/ and NOT
// forwarded [§3.3]. translateOne returns false; translateBlock skips it entirely.
// ============================================================================
TEST_CASE("evttranslate does not forward ProgramChange", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;

    const auto pc = makeHostEvent(mw::plugin::HostEventType::ProgramChange, /*channel=*/1,
                                  /*sampleOffset=*/0, /*data0=*/5, /*value=*/0.0f,
                                  /*noteId=*/-1);
    mw::MidiEvent out{};
    REQUIRE(mw::plugin::translateOne(pc, ccMap, out) == false);   // consumed, not forwarded

    // In a block, the ProgramChange is dropped and the surrounding NoteOn/NoteOff still
    // forward in order.
    const mw::plugin::HostEvent block[] = {
        makeHostEvent(mw::plugin::HostEventType::NoteOn,  1, 0,  60, 1.0f, -1),
        pc,
        makeHostEvent(mw::plugin::HostEventType::NoteOff, 1, 10, 60, 0.0f, -1),
    };
    std::vector<mw::MidiEvent> outBuf(8);
    const int n = mw::plugin::translateBlock(std::begin(block), std::end(block), ccMap,
                                             outBuf.data(), static_cast<int>(outBuf.size()));
    REQUIRE(n == 2);                                            // PC dropped, two notes kept
    REQUIRE(outBuf[0].type == mw::NormalizedType::NoteOn);
    REQUIRE(outBuf[1].type == mw::NormalizedType::NoteOff);
    REQUIRE(outBuf[1].sampleOffset == 10);                     // order + offsets preserved
}

// ============================================================================
// Acceptance criterion 1 (cont.): a ControlChange's raw CC number is RESOLVED through
// the §6 CcLearnMap to a param index BEFORE forwarding; data0 carries that index. A CC
// mapped to nothing (unmapped/disabled or out-of-range) is NOT forwarded [§3.3].
// ============================================================================
TEST_CASE("evttranslate resolves a ControlChange CC number to a param index via the learn map", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;   // default §6.2 seed: CC74 -> mw101.vcf.cutoff

    // CC74 (cutoff) is a default-mapped binding -> resolves to the cutoff registry index.
    const int cutoffIdx = registryIndexOf("mw101.vcf.cutoff");
    REQUIRE(cutoffIdx >= 0);
    REQUIRE(ccMap.lookup(74) == cutoffIdx);   // precondition (the default seed)

    const auto cc = makeHostEvent(mw::plugin::HostEventType::ControlChange, /*channel=*/1,
                                  /*sampleOffset=*/3, /*data0=*/74, /*value=*/0.5f,
                                  /*noteId=*/-1);
    mw::MidiEvent out{};
    REQUIRE(mw::plugin::translateOne(cc, ccMap, out) == true);
    REQUIRE(out.type == mw::NormalizedType::ControlChange);
    REQUIRE(out.data0 == static_cast<float>(cutoffIdx));   // CC number -> PARAM INDEX, not 74
    REQUIRE(out.value == 0.5f);                            // value verbatim
    REQUIRE(out.sampleOffset == 3);

    // An unmapped CC (CC2 is not in the §6.2 default map) resolves to kUnmapped and is
    // therefore NOT forwarded.
    REQUIRE(ccMap.lookup(2) == mw::plugin::CcLearnMap::kUnmapped);   // precondition
    const auto unmapped = makeHostEvent(mw::plugin::HostEventType::ControlChange, 1, 0,
                                        /*data0=*/2, 0.5f, -1);
    mw::MidiEvent dropped{};
    REQUIRE(mw::plugin::translateOne(unmapped, ccMap, dropped) == false);

    // An out-of-range CC number is also dropped (never indexes outside the 0..127 map).
    const auto oob = makeHostEvent(mw::plugin::HostEventType::ControlChange, 1, 0,
                                   /*data0=*/200, 0.5f, -1);
    mw::MidiEvent dropped2{};
    REQUIRE(mw::plugin::translateOne(oob, ccMap, dropped2) == false);
}

// ============================================================================
// Acceptance criterion 2: identical HostEvent input yields identical mw::MidiEvent
// output (cross-format determinism precondition) [§3.3; ADR-011 C11]. The same input
// translated repeatedly, and a block translated whole, are bit-identical.
// ============================================================================
TEST_CASE("evttranslate is deterministic for identical input", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;

    const mw::plugin::HostEvent inputs[] = {
        makeHostEvent(mw::plugin::HostEventType::NoteOn,          2, 0,   60, 1.00f, 7),
        makeHostEvent(mw::plugin::HostEventType::PitchBend,       1, 4,   0,  0.25f, -1),
        makeHostEvent(mw::plugin::HostEventType::ControlChange,   1, 8,   74, 0.50f, -1),  // -> cutoff idx
        makeHostEvent(mw::plugin::HostEventType::ChannelPressure, 3, 12,  0,  0.75f, -1),
        makeHostEvent(mw::plugin::HostEventType::ProgramChange,   1, 16,  3,  0.00f, -1),  // dropped
        makeHostEvent(mw::plugin::HostEventType::NoteOff,         2, 20,  60, 0.00f, 7),
        makeHostEvent(mw::plugin::HostEventType::ClockEdge,       1, 24,  0,  0.00f, -1),
        makeHostEvent(mw::plugin::HostEventType::ParamValue,      1, 28,  9,  0.33f, -1),
    };

    // Per-event determinism: translate each input twice; the two outputs are identical.
    for (const auto& in : inputs) {
        mw::MidiEvent a{};
        mw::MidiEvent b{};
        const bool fa = mw::plugin::translateOne(in, ccMap, a);
        const bool fb = mw::plugin::translateOne(in, ccMap, b);
        REQUIRE(fa == fb);
        if (fa)
            REQUIRE(midiEventsEqual(a, b));
    }

    // Whole-block determinism: two independent runs over the same block produce the
    // same count AND bit-identical events in the same order.
    std::vector<mw::MidiEvent> run1(16);
    std::vector<mw::MidiEvent> run2(16);
    const int n1 = mw::plugin::translateBlock(std::begin(inputs), std::end(inputs), ccMap,
                                              run1.data(), static_cast<int>(run1.size()));
    const int n2 = mw::plugin::translateBlock(std::begin(inputs), std::end(inputs), ccMap,
                                              run2.data(), static_cast<int>(run2.size()));
    REQUIRE(n1 == n2);
    REQUIRE(n1 == 7);   // 8 inputs minus the single ProgramChange (consumed)
    for (int i = 0; i < n1; ++i)
        REQUIRE(midiEventsEqual(run1[static_cast<std::size_t>(i)],
                                run2[static_cast<std::size_t>(i)]));
}

// ============================================================================
// Acceptance criterion 3: translation performs ZERO heap allocation [§3.3; ADR-011 C9].
// Asserted STRUCTURALLY (see file header for why a runtime sentinel would duplicate the
// LatencyReporterTest operator-new override): translateOne/translateBlock are noexcept,
// the function set is free (the translator owns NO state and therefore NO heap), and it
// writes only into the caller-owned pre-sized output buffer. These hold for EVERY input.
// ============================================================================
TEST_CASE("evttranslate is noexcept and allocation-free by construction", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;
    mw::MidiEvent out{};
    const mw::plugin::HostEvent host =
        makeHostEvent(mw::plugin::HostEventType::NoteOn, 1, 0, 60, 1.0f, -1);

    // (a) The hot paths are noexcept (no throw -> no implicit allocation/unwind path).
    STATIC_REQUIRE(noexcept(mw::plugin::translateOne(host, ccMap, out)));
    STATIC_REQUIRE(noexcept(mw::plugin::translateBlock(nullptr, nullptr, ccMap, nullptr, 0)));

    // (b) The PODs the translator copies between are trivially copyable (a field copy
    // cannot allocate); both are JUCE-free [§3.2/§3.3].
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::plugin::HostEvent>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::MidiEvent>);

    // (c) Drive a representative block into a PRE-SIZED, caller-owned buffer (the only
    // storage involved); the translator never resizes or owns it. The output buffer is
    // sized BEFORE the call, exactly as prepareToPlay sizes the NormalizedEventBuffer.
    const mw::plugin::HostEvent block[] = {
        makeHostEvent(mw::plugin::HostEventType::NoteOn,        1, 0,  60, 1.0f, -1),
        makeHostEvent(mw::plugin::HostEventType::ControlChange, 1, 1,  74, 0.5f, -1),
        makeHostEvent(mw::plugin::HostEventType::ProgramChange, 1, 2,  3,  0.0f, -1),
        makeHostEvent(mw::plugin::HostEventType::NoteOff,       1, 3,  60, 0.0f, -1),
    };
    std::vector<mw::MidiEvent> outBuf(8);   // pre-sized; sole allocation, OFF the hot path
    const int n = mw::plugin::translateBlock(std::begin(block), std::end(block), ccMap,
                                             outBuf.data(), static_cast<int>(outBuf.size()));
    REQUIRE(n == 3);   // PC dropped; NoteOn + CC(->cutoff) + NoteOff forwarded
}

// ============================================================================
// Acceptance criterion 3 (cont.): drop-never-grow — a full output buffer drops surplus
// forwarded events; the buffer is NEVER resized [ADR-011 C9].
// ============================================================================
TEST_CASE("evttranslate drops surplus events when the output buffer is full and never grows", "[evttranslate]")
{
    const mw::plugin::CcLearnMap ccMap;

    const mw::plugin::HostEvent block[] = {
        makeHostEvent(mw::plugin::HostEventType::NoteOn,  1, 0, 60, 1.0f, -1),
        makeHostEvent(mw::plugin::HostEventType::NoteOn,  1, 1, 62, 1.0f, -1),
        makeHostEvent(mw::plugin::HostEventType::NoteOn,  1, 2, 64, 1.0f, -1),
    };

    // Capacity 2: only the first two forwarded events are written; the third is dropped.
    std::vector<mw::MidiEvent> outBuf(2);
    const std::size_t capacityBefore = outBuf.capacity();
    const int n = mw::plugin::translateBlock(std::begin(block), std::end(block), ccMap,
                                             outBuf.data(), static_cast<int>(outBuf.size()));
    REQUIRE(n == 2);                            // capped at the buffer capacity
    REQUIRE(outBuf.capacity() == capacityBefore); // never resized
    REQUIRE(outBuf[0].data0 == 60.0f);
    REQUIRE(outBuf[1].data0 == 62.0f);
}
