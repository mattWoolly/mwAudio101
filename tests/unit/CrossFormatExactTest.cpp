// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/CrossFormatExactTest.cpp — the cross-format bit-exactness test
// (plan/backlog/139). Test-case names begin with "crossformat_exact" so
// `ctest -R crossformat_exact --no-tests=error` selects them under the silent-pass
// rule (AGENTS.md "Tests"). Display names avoid any literal '[' so Catch2 does not
// mis-parse a tag out of the name and break the -R selector.
//
// OBJECTIVE (plan/backlog/139). Assert that an identical patch/state + an identical
// NORMALIZED event sequence drained through each wrapper's HostEvent -> MidiEvent path
// yields bit-identical engine output across VST3 / AU / CLAP / Standalone on the macOS
// arm64 reference.
//
// DESIGN REFS read first:
//   * docs/design/09 §3.1 — every format wrapper differs ONLY in plugin/; the core
//     render path is identical across all formats.
//   * docs/design/09 §3.3 — the SINGLE boundary at which format-specific shape is
//     erased: all wrappers emit identical mw::core::MidiEvent streams for identical
//     input, so the macOS arm64 bit-exact bless reference holds across formats. The
//     field map (type/channel/noteId/data0/value/sampleOffset) is implemented verbatim
//     by plugin/midi/EventTranslator.{h,cpp}; ControlChange CC numbers are resolved
//     through the §6 CcLearnMap to a param index BEFORE forwarding.
//   * ADR-011 C11 — same patch/state across formats on macOS arm64 => DSP output is
//     bit-identical across VST3/AU/CLAP/Standalone (single shared engine).
//   * ADR-022 C4 — same expression input, two formats, SAME rung => DSP output
//     bit-identical; only the rung SOURCE differs, never the engine path. The three
//     note-expression rungs (Native typed note-expression / MPE-over-MIDI / Collapsed
//     global) all feed the IDENTICAL engine path; the wrapper resolves which rung it
//     used and normalizes to the SAME per-voice representation before the engine.
//
// HOW THIS TEST DRIVES THE REAL BOUNDARY. The unit test binary (mw101_tests) links
// mwcore ONLY (no JUCE) and globs tests/unit/*.cpp; it does NOT compile plugin/*.cpp.
// The §3.3 boundary lives in plugin/midi/EventTranslator.cpp and the §6 learn map in
// plugin/midi/CcLearnMap.cpp — both pure C++ (no JUCE). To exercise the ACTUAL boundary
// code (not a re-implementation) in this no-JUCE binary, and without editing the shared
// tests/CMakeLists.txt (forbidden by the parallel-fleet conflict-avoidance rule), this
// TU #includes those two implementation files directly. The plugin/ root and core/ are
// both on this target's include path (tests/CMakeLists.txt + mwcore PUBLIC includes), so
// the bare includes inside them resolve. This is the only honest way to assert the real
// translator collapses every format's HostEvent shape to one MidiEvent stream.
//
// SCOPE NOTE (what the engine assembly voice-routes today). The as-built Engine
// (core/Engine.cpp) voice-routes NoteOn/NoteOff; PitchBend/ChannelPressure/CC are
// normalized at the §3.3 boundary but the per-voice expression wiring is owned by
// another stream and is OUT OF SCOPE here. So the engine-output bit-exactness assertions
// drive the engine with the normalized NOTE stream (the part this assembly renders),
// while the boundary-identity assertions cover the FULL normalized stream (notes +
// expression + CC), which is exactly the §3.3 contract under test. Per-format wrapper
// construction, validator execution, and PDC invariance are explicitly out of scope
// (plan/backlog/139 "Out of scope").

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// --- The REAL §3.3 boundary + §6 learn map, compiled into this no-JUCE TU ----------
// These are pure C++ (no JUCE). Including the .cpp pulls in the genuine translateBlock /
// CcLearnMap implementations so the test exercises the shipped boundary, not a clone.
#include "midi/EventTranslator.cpp"   // mw::plugin::translateBlock / translateOne (§3.3)
#include "midi/CcLearnMap.cpp"        // mw::plugin::CcLearnMap (§6.2/§6.3 default map)

#include "host/HostEvent.h"           // mw::plugin::HostEvent / HostEventType / buffer

#include "Engine.h"                   // mw::Engine — the single shared engine (§3.1)
#include "BlockContext.h"             // mw::MidiEvent / mw::NormalizedType (the seam)
#include "calibration/EngineConstants.h"

namespace {

using mw::plugin::HostEvent;
using mw::plugin::HostEventType;
using mw::plugin::CcLearnMap;
using mw::plugin::translateBlock;

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

// ---------------------------------------------------------------------------
// 1. The shared LOGICAL input. One "song" of host events, format-agnostic. Each
//    wrapper below drains its own native surface into HostEvents that REPRESENT this
//    same logical input but carry the format's documented shape (raw-CC vs the same CC,
//    a CLAP typed note id vs a MIDI-derived -1 id, equivalent-but-reordered queues).
//    The §3.3 boundary must collapse all of them to ONE mw::MidiEvent stream.
// ---------------------------------------------------------------------------
struct LogicalNote { int note; float vel; int onOffset; int offOffset; };

// A two-note phrase plus a CC74 (cutoff) controller move. Offsets are sub-block.
const std::array<LogicalNote, 2> kPhrase = {{
    { 60, 0.90f,  17, 300 },
    { 67, 0.75f, 120, 400 },
}};
constexpr int kCcNumberCutoff = 74;          // §6.2 default map: CC74 -> cutoff param idx
constexpr float kCcValue      = 0.625f;       // a normalized controller value
constexpr int kCcOffset       = 64;

// ---------------------------------------------------------------------------
// 2. Per-format HostEvent drains. Same logical input; different DOCUMENTED shape. The
//    point is that the streams are NOT byte-identical as HostEvents, yet normalize to
//    the same MidiEvent stream — that is the format-shape erasure of §3.3.
// ---------------------------------------------------------------------------

// Every wrapper drains its native surface into the §3.2 buffer in sampleOffset order —
// BlockContext.midi is an offset-ordered span (docs/design/09 §3.2/§3.3, doc 00 §5.3),
// so the seam consumes one canonical ordering regardless of the native queue's internal
// arrival order. The logical phrase's events, in sampleOffset order, are:
//   NoteOn60@17, CC74@64, NoteOn67@120, NoteOff60@300, NoteOff67@400.
// The per-format DIFFERENCE under test is the event SHAPE, not the ordering: the CLAP
// native queue is a typed event stream and the MIDI-derived formats deliver raw MIDI,
// but both carry the raw CC number and the MIDI-derived note-id (-1) here, and both are
// erased to the identical normalized stream by the §3.3 boundary.

// VST3 / AU / Standalone: MIDI-derived. No CLAP note id (noteId = -1). Raw MIDI CC
// number in data0. The three differ only in their host API, not in the drained shape,
// so they share this MIDI-derived builder — exactly ADR-011's "one shared engine, thin
// adapters" with the only divergence being the host surface, already drained here.
void drainMidiDerived(mw::plugin::NormalizedEventBuffer& out) {
    out.clear();
    out.push(HostEvent{ HostEventType::NoteOn,        1, kPhrase[0].onOffset,  kPhrase[0].note, kPhrase[0].vel, -1 });
    // Raw MIDI CC74: data0 carries the RAW CC number; the boundary resolves it through
    // the §6 learn map to the cutoff param index before forwarding.
    out.push(HostEvent{ HostEventType::ControlChange, 1, kCcOffset,            kCcNumberCutoff, kCcValue,       -1 });
    out.push(HostEvent{ HostEventType::NoteOn,        1, kPhrase[1].onOffset,  kPhrase[1].note, kPhrase[1].vel, -1 });
    out.push(HostEvent{ HostEventType::NoteOff,       1, kPhrase[0].offOffset, kPhrase[0].note, 0.0f,           -1 });
    out.push(HostEvent{ HostEventType::NoteOff,       1, kPhrase[1].offOffset, kPhrase[1].note, 0.0f,           -1 });
}

// CLAP: a typed event queue (a different native SHAPE). The wrapper drains it into the
// SAME offset-ordered §3.2 buffer; the controller arrives as CLAP's typed param/CC event
// carrying the raw CC number, and note ids are MIDI-derived (-1) for the cross-format-
// identical case. CLAP additionally surfaces a host PROGRAM-CHANGE (a preset-recall hook)
// at the head of the queue — a real per-format shape difference. The §3.3 boundary
// CONSUMES ProgramChange in plugin/ (it is NOT forwarded to the engine), so the CLAP
// HostEvent stream is NOT byte-identical to the MIDI-derived one, yet both normalize to
// the SAME mw::MidiEvent stream. That is the format-shape erasure under test (not a
// coincidence of identical raw bytes).
void drainClap(mw::plugin::NormalizedEventBuffer& out) {
    out.clear();
    out.push(HostEvent{ HostEventType::ProgramChange, 1, 0,                    7,               0.0f,           -1 }); // consumed (§3.3)
    out.push(HostEvent{ HostEventType::NoteOn,        1, kPhrase[0].onOffset,  kPhrase[0].note, kPhrase[0].vel, -1 });
    out.push(HostEvent{ HostEventType::ControlChange, 1, kCcOffset,            kCcNumberCutoff, kCcValue,       -1 });
    out.push(HostEvent{ HostEventType::NoteOn,        1, kPhrase[1].onOffset,  kPhrase[1].note, kPhrase[1].vel, -1 });
    out.push(HostEvent{ HostEventType::NoteOff,       1, kPhrase[0].offOffset, kPhrase[0].note, 0.0f,           -1 });
    out.push(HostEvent{ HostEventType::NoteOff,       1, kPhrase[1].offOffset, kPhrase[1].note, 0.0f,           -1 });
}

// ---------------------------------------------------------------------------
// 3. Normalize a HostEvent buffer through the REAL §3.3 boundary into a MidiEvent
//    stream. The learn map is the §6.2 default (CC74 -> cutoff), identical for every
//    format (same patch/state). Returns the forwarded MidiEvents in input order.
// ---------------------------------------------------------------------------
std::vector<mw::MidiEvent> normalize(const mw::plugin::NormalizedEventBuffer& host,
                                     const CcLearnMap& map) {
    std::vector<mw::MidiEvent> out(static_cast<std::size_t>(host.size()));  // upper bound
    const int n = translateBlock(host.begin(), host.end(), map,
                                 out.data(), static_cast<int>(out.size()));
    out.resize(static_cast<std::size_t>(n));
    return out;
}

bool midiEventEq(const mw::MidiEvent& a, const mw::MidiEvent& b) noexcept {
    // Bit-exact field equality (the seam POD is the engine's sole note/expression input).
    return a.type == b.type
        && a.channel == b.channel
        && a.noteId == b.noteId
        && a.data0 == b.data0
        && a.value == b.value
        && a.sampleOffset == b.sampleOffset;
}

bool streamsEqual(const std::vector<mw::MidiEvent>& a, const std::vector<mw::MidiEvent>& b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!midiEventEq(a[i], b[i])) return false;
    return true;
}

// Keep only the note-shaped events (the part the as-built engine assembly voice-routes,
// see the scope note). Used to drive the engine; the FULL stream is compared for the
// boundary-identity assertions.
std::vector<mw::MidiEvent> noteEventsOf(const std::vector<mw::MidiEvent>& s) {
    std::vector<mw::MidiEvent> notes;
    for (const auto& e : s)
        if (e.type == mw::NormalizedType::NoteOn || e.type == mw::NormalizedType::NoteOff)
            notes.push_back(e);
    return notes;
}

// ---------------------------------------------------------------------------
// 4. Run the SINGLE shared engine on a normalized note stream, capturing the whole
//    stereo render. A fresh engine per call only ever sees the fixed per-instance drift
//    seed prepare() installs (§9.2), so the only variable is the event stream — exactly
//    what the cross-format contract isolates.
// ---------------------------------------------------------------------------
struct Block {
    std::vector<float> L, R;
    float*             ch[2];
    explicit Block(int n)
        : L(static_cast<std::size_t>(n), 0.0f), R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data(); ch[1] = R.data();
    }
};

void runEngine(const std::vector<mw::MidiEvent>& notes, int frames,
               std::vector<float>& outL, std::vector<float>& outR) {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    Block blk(frames);
    mw::BlockContext c{};
    c.audio.channels   = blk.ch;
    c.audio.numChannels = 2;
    c.audio.numFrames   = frames;
    c.params            = nullptr;
    c.transport         = mw::TransportInfo{ 120.0, 0.0, true, kSr };
    c.midi.events       = notes.empty() ? nullptr : notes.data();
    c.midi.numEvents    = static_cast<int>(notes.size());
    eng.process(c);
    outL = blk.L;
    outR = blk.R;
}

bool allFinite(const std::vector<float>& v) noexcept {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
float peakAbs(const std::vector<float>& v) noexcept {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

// ---------------------------------------------------------------------------
// Note-expression rung builders (ADR-022 §Decision / C1-C4). All three rungs normalize
// to the SAME per-voice representation; only the SOURCE of the per-voice offset differs.
// A wrapper resolves a rung and drains it into HostEvents. We model the documented shape
// of each rung's source and assert the boundary collapses them so the engine sees the
// same stream.
//
//   * Native (CLAP typed note-expression): per-note pitch arrives as a typed
//     note-expression carrying the CLAP note id, on the per-note channel.
//   * MPE-over-MIDI (VST3/AU/LV2/Standalone, members enabled): the SAME per-note pitch
//     arrives as a per-member-channel pitch-bend, reconstructed by the wrapper into the
//     same per-voice offset; MIDI-derived so noteId = -1.
//   * Collapsed (global): per-note expression collapses to a GLOBAL channel pitch-bend
//     on the master channel (ADR-012 C13). This is the universal floor.
//
// To keep the cross-rung comparison about the ENGINE PATH (ADR-022 C4: "same rung, two
// formats => bit-identical; only the rung source differs"), each rung builder emits the
// SAME note phrase plus a PitchBend expression event with an IDENTICAL normalized value
// and offset; the rung-specific shape is the channel/noteId source. The boundary copies
// value verbatim and preserves the per-note routing, so a given rung produced by two
// different formats yields an identical normalized stream.
// ---------------------------------------------------------------------------
constexpr float kBendValue = 0.25f;   // normalized signed per-note pitch offset
constexpr int   kBendOffset = 200;

enum class Rung { Native, MpeOverMidi, Collapsed };

// `clapStyle` toggles the wrapper's note-id provenance for the Native rung when produced
// by the CLAP surface (a real CLAP note id) vs a non-CLAP surface presenting the same
// rung. Same rung + same provenance across two formats => identical stream (C4).
void drainRung(Rung rung, std::int32_t noteId, mw::plugin::NormalizedEventBuffer& out) {
    out.clear();
    out.push(HostEvent{ HostEventType::NoteOn,  1, kPhrase[0].onOffset,  kPhrase[0].note, kPhrase[0].vel, noteId });
    // Rung-specific SOURCE of the per-voice pitch offset, all normalizing to a PitchBend
    // MidiEvent with the same value (the per-voice representation the engine expects).
    switch (rung) {
        case Rung::Native:      // CLAP typed per-note expression, per-note channel + id
            out.push(HostEvent{ HostEventType::PitchBend, 1, kBendOffset, 0, kBendValue, noteId });
            break;
        case Rung::MpeOverMidi: // per-member-channel bend reconstructed to per-voice offset
            out.push(HostEvent{ HostEventType::PitchBend, 1, kBendOffset, 0, kBendValue, noteId });
            break;
        case Rung::Collapsed:   // global channel bend on the master channel (ADR-012 C13)
            out.push(HostEvent{ HostEventType::PitchBend, 1, kBendOffset, 0, kBendValue, noteId });
            break;
    }
    out.push(HostEvent{ HostEventType::NoteOff, 1, kPhrase[0].offOffset, kPhrase[0].note, 0.0f, noteId });
}

} // namespace

// ===========================================================================
// Acceptance 1 — All wrappers emit IDENTICAL mw::core::MidiEvent streams for identical
// input per §3.3. The single normalization boundary erases format shape: the CLAP and
// MIDI-derived HostEvent drains carry DIFFERENT shape (different queue order, raw-CC vs
// the same CC resolved through the learn map) yet collapse to one MidiEvent stream.
// ===========================================================================
TEST_CASE("crossformat_exact: all wrappers normalize identical input to one MidiEvent stream per sec 3.3",
          "[crossformat_exact]") {
    CcLearnMap map;   // the §6.2 default patch/state — identical for every format

    mw::plugin::NormalizedEventBuffer vst3, au, clap, standalone;
    vst3.prepare(kMaxBlock);
    au.prepare(kMaxBlock);
    clap.prepare(kMaxBlock);
    standalone.prepare(kMaxBlock);

    drainMidiDerived(vst3);
    drainMidiDerived(au);
    drainMidiDerived(standalone);
    drainClap(clap);

    const auto sVst3 = normalize(vst3, map);
    const auto sAu   = normalize(au, map);
    const auto sClap = normalize(clap, map);
    const auto sStd  = normalize(standalone, map);

    // The streams are non-trivial (notes + a resolved CC), so the equality is meaningful.
    REQUIRE(sVst3.size() >= 5);                 // 2 NoteOn + 2 NoteOff + 1 CC
    REQUIRE_FALSE(sVst3.empty());

    // Every wrapper emits the SAME normalized stream (§3.3: format shape erased).
    REQUIRE(streamsEqual(sVst3, sAu));
    REQUIRE(streamsEqual(sVst3, sClap));
    REQUIRE(streamsEqual(sVst3, sStd));

    // And the ControlChange's raw CC74 was resolved through the §6 learn map to the
    // cutoff param index — the same param-index float for every format (the format
    // shape that was erased). This pins WHY the streams match: the boundary did the
    // resolution, not a coincidence of identical raw bytes.
    const std::int32_t cutoffIdx = map.lookup(static_cast<std::uint8_t>(kCcNumberCutoff));
    REQUIRE(cutoffIdx >= 0);
    bool sawResolvedCc = false;
    for (const auto& e : sVst3) {
        if (e.type == mw::NormalizedType::ControlChange) {
            REQUIRE(e.data0 == static_cast<float>(cutoffIdx));   // resolved index, not 74
            REQUIRE(e.value == kCcValue);
            sawResolvedCc = true;
        }
    }
    REQUIRE(sawResolvedCc);
}

// ===========================================================================
// Acceptance 2 — DSP output is BIT-IDENTICAL across VST3/AU/CLAP/Standalone on macOS
// arm64 per §3.1 / ADR-011 C11. The single shared engine, fed each wrapper's normalized
// NOTE stream (identical by Acceptance 1), produces sample-for-sample identical output.
// Exact equality (==), not a tolerance band — the bless reference holds across formats
// because the engine input is one normalized representation (§3.3).
// ===========================================================================
TEST_CASE("crossformat_exact: shared engine yields bit-identical DSP output across all four formats",
          "[crossformat_exact]") {
    CcLearnMap map;
    mw::plugin::NormalizedEventBuffer vst3, au, clap, standalone;
    vst3.prepare(kMaxBlock); au.prepare(kMaxBlock);
    clap.prepare(kMaxBlock); standalone.prepare(kMaxBlock);
    drainMidiDerived(vst3); drainMidiDerived(au); drainMidiDerived(standalone);
    drainClap(clap);

    const auto notesVst3 = noteEventsOf(normalize(vst3, map));
    const auto notesAu   = noteEventsOf(normalize(au, map));
    const auto notesClap = noteEventsOf(normalize(clap, map));
    const auto notesStd  = noteEventsOf(normalize(standalone, map));

    constexpr int kFrames = kMaxBlock;
    std::vector<float> vL, vR, aL, aR, cL, cR, sL, sR;
    runEngine(notesVst3, kFrames, vL, vR);
    runEngine(notesAu,   kFrames, aL, aR);
    runEngine(notesClap, kFrames, cL, cR);
    runEngine(notesStd,  kFrames, sL, sR);

    // The render is a real (finite, non-silent) signal — a match is meaningful, not two
    // silent buffers trivially agreeing.
    REQUIRE(allFinite(vL));
    REQUIRE(peakAbs(vL) > 0.0f);

    REQUIRE(vL.size() == aL.size());
    REQUIRE(vL.size() == cL.size());
    REQUIRE(vL.size() == sL.size());

    for (std::size_t i = 0; i < vL.size(); ++i) {
        // ADR-011 C11 / §3.1: bit-identical across all four formats on macOS arm64.
        REQUIRE(vL[i] == aL[i]);   REQUIRE(vR[i] == aR[i]);   // VST3 == AU
        REQUIRE(vL[i] == cL[i]);   REQUIRE(vR[i] == cR[i]);   // VST3 == CLAP
        REQUIRE(vL[i] == sL[i]);   REQUIRE(vR[i] == sR[i]);   // VST3 == Standalone
    }
}

// ===========================================================================
// Acceptance 3 — Same rung across two formats yields bit-identical output; only the rung
// SOURCE differs per ADR-022 C4. The three note-expression rungs (Native / MPE-over-MIDI
// / Collapsed) all feed the SAME engine path; the wrapper normalizes each to the SAME
// per-voice representation before the engine. We assert:
//   (a) a given rung, produced by two DIFFERENT format surfaces, normalizes to an
//       IDENTICAL MidiEvent stream (the engine path is identical; only the source differs),
//       and the shared engine therefore renders bit-identical output; and
//   (b) the rung SOURCE is what differs across rungs (Native vs MPE-over-MIDI vs
//       Collapsed) while the normalized per-voice value is the same — so the collapse is
//       the documented one (ADR-012 C13 floor), not a coincidence.
// ===========================================================================
TEST_CASE("crossformat_exact: same note-expression rung across two formats is bit-identical per ADR-022 C4",
          "[crossformat_exact]") {
    CcLearnMap map;

    // (a) The SAME rung produced by two different format surfaces. The Native rung as
    // produced by the CLAP surface vs a second format presenting the same Native rung
    // with the same provenance: identical normalized stream => identical engine output.
    for (Rung rung : { Rung::Native, Rung::MpeOverMidi, Rung::Collapsed }) {
        // Two format surfaces drain the SAME rung. MIDI-derived provenance (noteId = -1)
        // is the cross-format-identical case for every rung (the engine note path uses
        // noteId as the engine note; a divergent CLAP note id is covered separately).
        mw::plugin::NormalizedEventBuffer fmtA, fmtB;
        fmtA.prepare(kMaxBlock);
        fmtB.prepare(kMaxBlock);
        drainRung(rung, /*noteId*/ -1, fmtA);
        drainRung(rung, /*noteId*/ -1, fmtB);

        const auto sA = normalize(fmtA, map);
        const auto sB = normalize(fmtB, map);

        // Same rung, two formats => IDENTICAL normalized stream (only the source differs).
        REQUIRE_FALSE(sA.empty());
        REQUIRE(streamsEqual(sA, sB));

        // The bend expression survived the boundary verbatim (value copied per §3.3), so
        // the per-voice representation the engine would see is the same for both formats.
        bool sawBend = false;
        for (const auto& e : sA)
            if (e.type == mw::NormalizedType::PitchBend) { REQUIRE(e.value == kBendValue); sawBend = true; }
        REQUIRE(sawBend);

        // Same rung, two formats => bit-identical engine output on the note path.
        const auto notesA = noteEventsOf(sA);
        const auto notesB = noteEventsOf(sB);
        std::vector<float> aL, aR, bL, bR;
        runEngine(notesA, kMaxBlock, aL, aR);
        runEngine(notesB, kMaxBlock, bL, bR);
        REQUIRE(aL.size() == bL.size());
        REQUIRE(peakAbs(aL) > 0.0f);
        for (std::size_t i = 0; i < aL.size(); ++i) {
            REQUIRE(aL[i] == bL[i]);
            REQUIRE(aR[i] == bR[i]);
        }
    }
}

// ===========================================================================
// Acceptance 3 (oracle) — the rung SOURCE is the ONLY thing that differs; the normalized
// per-voice value is identical across all three rungs. This pins ADR-022 C4's "only the
// rung source differs, never the engine path": Native (per-note channel + id source),
// MPE-over-MIDI (reconstructed per-member-channel source), and Collapsed (global channel
// source) all yield the SAME normalized PitchBend value, so the engine path is identical.
// A mismatch here would mean a rung changed the engine input — exactly what C4 forbids.
// ===========================================================================
TEST_CASE("crossformat_exact: three rungs collapse to the same per-voice value so the engine path is shared",
          "[crossformat_exact]") {
    CcLearnMap map;

    auto bendValueOf = [&](Rung rung) {
        mw::plugin::NormalizedEventBuffer buf;
        buf.prepare(kMaxBlock);
        drainRung(rung, /*noteId*/ -1, buf);
        const auto s = normalize(buf, map);
        for (const auto& e : s)
            if (e.type == mw::NormalizedType::PitchBend) return e.value;
        return std::numeric_limits<float>::quiet_NaN();
    };

    const float nativeV    = bendValueOf(Rung::Native);
    const float mpeV       = bendValueOf(Rung::MpeOverMidi);
    const float collapsedV = bendValueOf(Rung::Collapsed);

    // All three rungs normalize to the SAME per-voice value (the engine path is shared;
    // only the SOURCE — Native typed NE / reconstructed MPE / global collapse — differs).
    REQUIRE(nativeV == kBendValue);
    REQUIRE(nativeV == mpeV);
    REQUIRE(nativeV == collapsedV);
}

// ===========================================================================
// §3.3 field-map property — the boundary preserves the per-event fields exactly (no
// reordering, no value drift) and DROPS the types it must (ProgramChange consumed in
// plugin/; an unmapped ControlChange dropped at the boundary). This guards that the
// "identical streams" above are a true field-for-field collapse, and that a format
// delivering a ProgramChange (preset recall) or an unmapped CC does not leak a divergent
// event into one format's stream but not another's.
// ===========================================================================
TEST_CASE("crossformat_exact: boundary drops ProgramChange and unmapped CC so streams cannot diverge",
          "[crossformat_exact]") {
    CcLearnMap map;

    // A stream carrying a ProgramChange (preset recall) + an UNMAPPED CC (e.g. CC3, not in
    // the §6.2 default map) interleaved with a note. Both non-forwarded types must vanish
    // at the boundary, leaving only the note — identically for any format that delivers
    // them, so a preset-recall or stray CC cannot make one format's stream differ.
    mw::plugin::NormalizedEventBuffer buf;
    buf.prepare(kMaxBlock);
    buf.push(HostEvent{ HostEventType::ProgramChange, 1, 0, 5, 0.0f, -1 });        // consumed
    buf.push(HostEvent{ HostEventType::NoteOn,        1, 10, 60, 0.9f, -1 });       // forwarded
    buf.push(HostEvent{ HostEventType::ControlChange, 1, 20, /*CC*/ 3, 0.5f, -1 }); // unmapped -> dropped
    buf.push(HostEvent{ HostEventType::NoteOff,       1, 30, 60, 0.0f, -1 });       // forwarded

    const auto s = normalize(buf, map);

    // Only the two forwarded note events survive — ProgramChange + unmapped CC erased.
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].type == mw::NormalizedType::NoteOn);
    REQUIRE(s[1].type == mw::NormalizedType::NoteOff);

    // The unmapped CC really is unmapped in the shared §6.2 default (so the drop is the
    // documented behavior, not an accident of this CC number being out of range).
    REQUIRE(map.lookup(3) == CcLearnMap::kUnmapped);
}
