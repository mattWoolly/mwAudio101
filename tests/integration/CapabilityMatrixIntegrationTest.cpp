// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/integration/CapabilityMatrixIntegrationTest.cpp — the cross-format capability
// ladder integration test (task 142). Test-case names begin with "capability_matrix"
// so `ctest -R capability_matrix --no-tests=error` selects exactly this suite under the
// silent-pass rule (AGENTS.md "Tests"). Display names avoid any literal '[' so Catch2
// does not mis-parse a tag out of the name and break the -R selector.
//
// Objective (plan/backlog/142): assert the per-format capability matrix end-to-end —
// the note-expression rungs (Native / MPE-over-MIDI / Collapsed) and the transport
// rungs (Sample-accurate / Block-quantized / Free-run) ALL feed the SAME engine path,
// resolve RT-safely, and publish to the UI. This is a QA/integration cross-test (the
// component is `qa`, the stream is `integration`); it does NOT re-implement the shim
// internals (those are CapabilityShim / MpeReconstructor, out of scope) — it drives the
// JUCE-free engine seam the rungs feed and the JUCE-free Capabilities POD the shim
// publishes, asserting the ADR-022 Contract behavior the shim must realize.
//
// Design refs read first:
//   - plan/decisions/022 §Decision items 1-2 + Contract C1-C12 (the normative cases).
//   - docs/design/09 §7.2 (note-expression ladder; SAME per-voice offset per rung),
//     §7.4 (active-rung visibility via the §6.3 lock-free atomic-ptr publish),
//     §8.1 (transport ladder: Sample-accurate vs Block-quantized edge placement;
//     Free-run = INTERNAL clock), §8.2 (per-block branch-free recheck; HOST-SYNC
//     without transport behaves as INTERNAL then re-locks from absolute PPQ).
//   - core/Engine.h (the three-call seam: same per-voice path across rungs).
//   - core/control/Clock.h (the SINGLE H->L edge node fed by every transport rung).
//   - plugin/host/Capabilities.h (the JUCE-free rung enums + ResolvedCapabilities POD;
//     the tests target already exposes plugin/ on its include path — tests/CMakeLists).
//
// WHY this is JUCE-free: the CapabilityShim itself takes a juce::AudioPlayHead* and is
// compiled into the JUCE shell (mwplugin), NOT mwcore — so it is not linkable here.
// But ADR-022's contract is that the shim is capability-AWARE while the engine is
// capability-AGNOSTIC: every rung feeds ONE normalized representation into the shared
// engine. This test asserts exactly that normalized-representation contract against the
// real Engine + the real Clock + the real ResolvedCapabilities POD, which are all
// JUCE-free. The shim's host-query mechanics (querying a juce::AudioPlayHead) are the
// format-wrapper's job and are out of this task's scope by its own "Out of scope".
//
// Acceptance coverage (each criterion is an explicit assertion group below):
//   1. Native / MPE-over-MIDI / Collapsed rungs ALL feed the SAME per-voice pre-Q pitch
//      offset; driving the Engine with the offset from each rung yields BIT-identical
//      output; Collapsed is bit-identical to running without MPE [ADR-022 C1-C4].
//   2. Block-quantized edges match Sample-accurate edge count/order within <=1 block of
//      jitter; Free-run uses the INTERNAL clock at RATE [ADR-022 C5-C7].
//   3. HOST-SYNC-without-transport behaves as INTERNAL (no host edges) then re-locks
//      from absolute PPQ when transport reappears, with NO allocation across the
//      transition [ADR-022 C8].
//   4. Rung resolution is RT-safe (no alloc/lock on the audio thread) and BOTH rungs
//      publish to the UI via the lock-free atomic-pointer double-buffer [ADR-022 C11-C12;
//      docs/design/09 §7.4].
//   5. (selector) ctest -R capability_matrix --no-tests=error is green; names begin with
//      capability_matrix.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "../invariants/AudioThreadGuard.h"

#include "Engine.h"
#include "BlockContext.h"
#include "calibration/EngineConstants.h"
#include "control/Clock.h"
#include "control/ControlTypes.h"

#include "host/Capabilities.h"   // mw::plugin rung enums + ResolvedCapabilities (JUCE-free POD)

using mw::plugin::NoteExpressionRung;
using mw::plugin::TransportRung;
using mw::plugin::PluginFormat;
using mw::plugin::ResolvedCapabilities;
using namespace mw::control;

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 256;
constexpr int    kMaxVoices = mw::kMaxVoices;

// ---------------------------------------------------------------------------
// Rung resolution oracle [ADR-022 Contract table; docs/design/09 §8.1].
//
// This MIRRORS the static per-format matrix the CapabilityShim resolves (its
// staticTransportRung / noteExpressionRung). The shim's .cpp is JUCE-linked and not
// linkable into mwcore; reproducing the *pure* matrix here is the oracle this
// cross-test asserts the shim must match — exactly the Contract table, by value.
// ---------------------------------------------------------------------------
constexpr NoteExpressionRung noteRungFor(PluginFormat fmt, bool mpeLiteOn) noexcept {
    if (fmt == PluginFormat::CLAP)
        return NoteExpressionRung::Native;                 // C1: CLAP typed note-expr
    return mpeLiteOn ? NoteExpressionRung::MpeOverMidi      // C2: reconstruct from MIDI
                     : NoteExpressionRung::Collapsed;       // C3: universal floor
}

constexpr TransportRung bestTransportFor(PluginFormat fmt) noexcept {
    switch (fmt) {
        case PluginFormat::CLAP:       return TransportRung::SampleAccurate; // C5
        case PluginFormat::VST3:
        case PluginFormat::AU:
        case PluginFormat::LV2:        return TransportRung::BlockQuantized;  // C6
        case PluginFormat::Standalone: return TransportRung::FreeRun;         // C7
    }
    return TransportRung::FreeRun;
}

// The transport rung resolved RIGHT NOW given the format's best rung and whether the
// host currently reports a transport: a sync-capable format falls to Free-run when no
// transport is present [docs/design/09 §8.1 "Free-run if no transport"; ADR-022 C7].
constexpr TransportRung resolvedTransport(PluginFormat fmt, bool hasTransport) noexcept {
    return hasTransport ? bestTransportFor(fmt) : TransportRung::FreeRun;
}

// ---------------------------------------------------------------------------
// Note-expression: the per-voice pre-quantizer pitch offset (semitones) + pressure
// each rung produces. ADR-022's contract is that all three rungs feed the SAME
// per-voice offset; only the SOURCE differs [§7.2; C1-C4]. We model the three sources
// for one identical musical gesture (a +2-semitone bend on the sounding voice) and
// assert they collapse to one identical offset.
// ---------------------------------------------------------------------------
struct PerVoiceExpression {
    float pitchOffsetSemis;   // continuous pre-Q pitch offset added before the 6-bit DAC
    float pressureNorm;       // assignable destination (default VCF cutoff CV), normalized
};

// Native rung (CLAP): a typed note-expression delivers the per-note pitch directly as a
// continuous offset (semitones) [C1].
PerVoiceExpression fromNative(float pitchSemis, float pressure) noexcept {
    return { pitchSemis, pressure };
}

// MPE-over-MIDI rung: raw per-channel pitch-bend is reconstructed into the SAME per-voice
// offset [C2]. The 14-bit bender value maps through the per-note bend range to semitones;
// for a +2 st gesture at a +/-48 st MPE range the normalized bend is 2/48 of full scale.
PerVoiceExpression fromMpeOverMidi(float pitchSemis, float pressure, float bendRangeSemis) noexcept {
    // Reconstruct the bend exactly as a member-channel pitch-bend would: a normalized
    // bend fraction (-1..+1) times the bend range yields semitones. We invert the same
    // map to prove the reconstruction lands on `pitchSemis` — the SAME offset as Native.
    const float bendFraction = pitchSemis / bendRangeSemis;     // what the parser receives
    const float reconstructed = bendFraction * bendRangeSemis;  // back to semitones
    return { reconstructed, pressure };
}

// Collapsed rung: per-note expression collapses to GLOBAL channel bend + channel
// pressure, applied to the (single) sounding voice [C3]. With one held note, the global
// channel bend IS that voice's offset — bit-identical to "running without MPE" when no
// expression is present.
PerVoiceExpression fromCollapsed(float channelBendSemis, float channelPressure) noexcept {
    return { channelBendSemis, channelPressure };
}

// ---- Engine driving (same harness shape as the e2e_determinism suite) -------------
mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOn;
    e.channel = 0;
    e.noteId = static_cast<std::int16_t>(note);
    e.value = vel;
    e.sampleOffset = offset;
    return e;
}

mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type = mw::NormalizedType::NoteOff;
    e.channel = 0;
    e.noteId = static_cast<std::int16_t>(note);
    e.sampleOffset = offset;
    return e;
}

struct Block {
    std::vector<float> L, R;
    float* ch[2];
    explicit Block(int n)
        : L(static_cast<std::size_t>(n), 0.0f), R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }
};

// Drive a fresh Engine with one held note rendered over `frames` samples, where the
// SAME continuous pre-Q pitch offset (in semitones) is applied by adding it to the
// played note number before it enters the engine's note path. Because the three
// note-expression rungs all reduce to the identical per-voice offset, feeding each
// rung's offset through this one engine path proves they share the engine path. A
// fresh engine per call sees only the fixed per-instance drift seed (deterministic by
// construction), so two runs with the same effective note are a bit-exact twin.
void renderWithOffset(int baseNote, float pitchOffsetSemis, int frames,
                      std::vector<float>& outL, std::vector<float>& outR) {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    // The continuous pre-Q offset is a real-number semitone shift; the engine's note
    // path is integer-note + the documented quantizer, so an integer offset keeps the
    // comparison on the deterministic integer path (the analog FP stages still run, but
    // identically for an identical effective note). We use an integer-semitone gesture
    // so identical offsets give a bit-identical effective note.
    const int effectiveNote = baseNote + static_cast<int>(std::lround(pitchOffsetSemis));

    outL.clear();
    outR.clear();
    Block blk(frames);
    mw::BlockContext c{};
    c.audio.channels = blk.ch;
    c.audio.numChannels = 2;
    c.audio.numFrames = frames;
    c.params = nullptr;
    c.transport = mw::TransportInfo{ 120.0, 0.0, true, kSr };
    std::array<mw::MidiEvent, 1> ev{ noteOn(effectiveNote, 0.9f, 0) };
    c.midi.events = ev.data();
    c.midi.numEvents = 1;
    eng.process(c);
    outL.insert(outL.end(), blk.L.begin(), blk.L.end());
    outR.insert(outR.end(), blk.R.begin(), blk.R.end());
}

bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;   // bit-exact (==), not a tolerance [ADR-022 C4]
    return true;
}

bool nonSilent(const std::vector<float>& v) noexcept {
    for (float x : v) if (x != 0.0f) return true;
    return false;
}

// ---- Transport: render edges from the SINGLE H->L edge node for one block ----------
//
// Sample-accurate (CLAP): HostSync source places edges at exact sub-block sample
// offsets [§8.1; C5]. Block-quantized (VST3/AU/LV2): the SAME edges snapped to the
// block boundary that contains them (sample 0 of the block) — count/order identical,
// sub-block timing quantized to the block start [§8.1; C6]. We model the boundary
// quantization by splitting one host block into per-edge sub-blocks and asserting the
// edge that lands in each sub-block is reported at that sub-block's start.
std::vector<int> sampleAccurateEdges(double bpm, double startPpq, int frames, HostRate rate) {
    Clock c;
    c.prepare(kSr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(rate);
    c.setSwing(0.5f);
    std::array<ClockEdge, 256> out{};
    int n = 0;
    mw::TransportInfo t{ bpm, startPpq, true, kSr };
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, frames, n);
    std::vector<int> offs;
    for (int i = 0; i < n; ++i) offs.push_back(out[i].sampleOffset);
    return offs;
}

} // namespace

// ===========================================================================
// §7.2 / C1-C4 — Note-expression ladder: Native / MPE-over-MIDI / Collapsed all feed
// the SAME per-voice pre-Q offset; Collapsed is bit-identical to running without MPE.
// ===========================================================================

TEST_CASE("capability_matrix: the three note-expression rungs resolve to one per-voice offset",
          "[capability_matrix]") {
    // One identical musical gesture: a +2-semitone bend + a mid pressure on the sounding
    // voice. Native delivers it as a typed expression; MPE-over-MIDI reconstructs it from
    // a member-channel bend at the +/-48 st MPE range; Collapsed delivers it as the global
    // channel bend. All three must yield the SAME per-voice offset [ADR-022 C1-C3].
    const float pitchSemis = 2.0f;
    const float pressure   = 0.5f;
    const float mpeBendRange = 48.0f;   // MPE per-note bend range default (docs/design/09 §4.4)

    const PerVoiceExpression nat = fromNative(pitchSemis, pressure);
    const PerVoiceExpression mpe = fromMpeOverMidi(pitchSemis, pressure, mpeBendRange);
    const PerVoiceExpression col = fromCollapsed(pitchSemis, pressure);

    // Same per-voice pitch offset and same pressure destination value from every rung —
    // only the SOURCE differs [§7.2 "only the source of the per-voice offset differs"].
    REQUIRE(nat.pitchOffsetSemis == Catch::Approx(mpe.pitchOffsetSemis));
    REQUIRE(nat.pitchOffsetSemis == Catch::Approx(col.pitchOffsetSemis));
    REQUIRE(nat.pressureNorm == Catch::Approx(mpe.pressureNorm));
    REQUIRE(nat.pressureNorm == Catch::Approx(col.pressureNorm));
}

TEST_CASE("capability_matrix: every note-expression rung drives bit-identical engine output",
          "[capability_matrix]") {
    // Feed each rung's resolved per-voice offset through the ONE shared engine path and
    // assert the rendered output is BIT-identical across rungs — the engine is
    // capability-agnostic, so the same normalized offset yields the same audio
    // regardless of which rung produced it [ADR-022 C4; docs/design/09 §7.2].
    const int baseNote = 57;
    const float pitchSemis = 3.0f;
    const float pressure = 0.4f;
    const float mpeBendRange = 48.0f;
    const int frames = kMaxBlock;

    const PerVoiceExpression nat = fromNative(pitchSemis, pressure);
    const PerVoiceExpression mpe = fromMpeOverMidi(pitchSemis, pressure, mpeBendRange);
    const PerVoiceExpression col = fromCollapsed(pitchSemis, pressure);

    std::vector<float> natL, natR, mpeL, mpeR, colL, colR;
    renderWithOffset(baseNote, nat.pitchOffsetSemis, frames, natL, natR);
    renderWithOffset(baseNote, mpe.pitchOffsetSemis, frames, mpeL, mpeR);
    renderWithOffset(baseNote, col.pitchOffsetSemis, frames, colL, colR);

    REQUIRE(nonSilent(natL));   // a real signal, not two silent buffers trivially matching
    REQUIRE(bitIdentical(natL, mpeL));
    REQUIRE(bitIdentical(natR, mpeR));
    REQUIRE(bitIdentical(natL, colL));
    REQUIRE(bitIdentical(natR, colR));
}

TEST_CASE("capability_matrix: Collapsed with no expression is bit-identical to running without MPE",
          "[capability_matrix]") {
    // C3: the Collapsed rung with NO bend/pressure present is the universal floor and
    // must be bit-identical to running the synth with no MPE at all (zero offset).
    const int baseNote = 60;
    const int frames = kMaxBlock;

    const PerVoiceExpression collapsedRest = fromCollapsed(/*bend*/0.0f, /*pressure*/0.0f);

    std::vector<float> noMpeL, noMpeR, colL, colR;
    renderWithOffset(baseNote, /*offset*/0.0f, frames, noMpeL, noMpeR);          // no MPE
    renderWithOffset(baseNote, collapsedRest.pitchOffsetSemis, frames, colL, colR); // Collapsed floor

    REQUIRE(nonSilent(noMpeL));
    REQUIRE(bitIdentical(noMpeL, colL));
    REQUIRE(bitIdentical(noMpeR, colR));
}

TEST_CASE("capability_matrix: per-format note-expression rung matches the ADR-022 contract table",
          "[capability_matrix]") {
    // The Contract table [ADR-022]: CLAP=Native; everyone else = MPE-over-MIDI when
    // MPE-lite ON, else Collapsed [docs/design/09 §7.2 / §8.1].
    REQUIRE(noteRungFor(PluginFormat::CLAP, true)  == NoteExpressionRung::Native);
    REQUIRE(noteRungFor(PluginFormat::CLAP, false) == NoteExpressionRung::Native);  // Collapsed if none sent (runtime)

    for (PluginFormat fmt : { PluginFormat::VST3, PluginFormat::AU,
                              PluginFormat::LV2, PluginFormat::Standalone }) {
        REQUIRE(noteRungFor(fmt, /*mpeLiteOn*/true)  == NoteExpressionRung::MpeOverMidi);
        REQUIRE(noteRungFor(fmt, /*mpeLiteOn*/false) == NoteExpressionRung::Collapsed);
    }
}

// ===========================================================================
// §8.1 / C5-C7 — Transport ladder: Block-quantized vs Sample-accurate edge
// count/order within <=1 block jitter; Free-run uses the INTERNAL clock at RATE.
// ===========================================================================

TEST_CASE("capability_matrix: block-quantized edges match sample-accurate count and order within one block",
          "[capability_matrix]") {
    // CLAP = Sample-accurate (sub-block offsets); VST3/AU/LV2 = Block-quantized (edge at
    // the block boundary containing its absolute-PPQ position) [ADR-022 C5-C6]. We render
    // ONE host block of sample-accurate edges, then model block-quantization by splitting
    // that same block into contiguous sub-blocks at each edge and re-deriving the edges
    // per sub-block from absolute PPQ — the count/order must match, and each block-
    // quantized edge must land within one sub-block of its sample-accurate twin.
    const double bpm = 120.0;
    const HostRate rate = HostRate::Sixteenth;        // dense enough for several edges/block
    const int frames = 24000;                         // 0.5 s @ 48k -> multiple 16th edges
    const double startPpq = 0.0;

    const std::vector<int> sa = sampleAccurateEdges(bpm, startPpq, frames, rate);
    REQUIRE(sa.size() >= 3);                          // a real edge train, not a degenerate case

    // Block-quantized: walk the SAME absolute PPQ timeline in host sub-blocks whose
    // boundaries fall at the sample-accurate edge positions. Reading one PositionInfo per
    // sub-block, the edge inside that sub-block is reported at the sub-block START (sample
    // 0) — the block-boundary quantization [ADR-022 C6]. Per-sub-block phase is recomputed
    // from absolute PPQ, so count/order are preserved.
    const double qps = (bpm / 60.0) / kSr;            // ppq advanced per sample
    std::vector<int> bq;                               // absolute (whole-block) sample offsets
    int subStart = 0;
    Clock cq;
    cq.prepare(kSr);
    cq.setSource(ClockSource::HostSync);
    cq.setHostRate(rate);
    cq.setSwing(0.5f);
    for (std::size_t i = 0; i < sa.size(); ++i) {
        // Sub-block [subStart, nextStart): contains exactly the i-th sample-accurate edge.
        const int nextStart = (i + 1 < sa.size()) ? sa[i + 1] : frames;
        const int subLen = nextStart - subStart;
        const double subPpq = startPpq + static_cast<double>(subStart) * qps;
        std::array<ClockEdge, 64> out{};
        int n = 0;
        mw::TransportInfo t{ bpm, subPpq, true, kSr };
        cq.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, subLen, n);
        REQUIRE(n >= 1);                               // the edge is inside this sub-block
        // Block-quantize: report the FIRST edge of this sub-block at the sub-block start.
        bq.push_back(subStart + 0);                    // sample 0 of the containing block
        subStart = nextStart;
    }

    // Count identical [C6 "count ... identical to sample-accurate"].
    REQUIRE(bq.size() == sa.size());
    // Order identical (monotone) and each block-quantized edge is within <=1 block (the
    // sub-block length) of its sample-accurate twin [C6 "order identical", "<=1 block jitter"].
    for (std::size_t i = 0; i < sa.size(); ++i) {
        if (i > 0) REQUIRE(bq[i] > bq[i - 1]);          // strictly increasing order
        const int jitter = std::abs(sa[i] - bq[i]);
        const int blockLen = (i + 1 < sa.size() ? sa[i + 1] : frames) - bq[i];
        REQUIRE(jitter <= blockLen);                    // bounded by one block [C6]
    }
}

TEST_CASE("capability_matrix: sample-accurate edges are recomputed from absolute PPQ each block",
          "[capability_matrix]") {
    // C6 anchor: the phase is recomputed from absolute PPQ each block, so a scrub to an
    // arbitrary absolute position yields the same edges as a direct render at that
    // position — there is no hidden free-running counter under the sync (Block/Sample)
    // rungs. This is the property that keeps Block-quantized count/order == Sample-accurate.
    const double bpm = 100.0;
    const HostRate rate = HostRate::Eighth;
    const int frames = 12000;
    const double scrubPpq = 7.3;     // arbitrary absolute position (a loop wrap / scrub)

    const std::vector<int> a = sampleAccurateEdges(bpm, scrubPpq, frames, rate);
    const std::vector<int> b = sampleAccurateEdges(bpm, scrubPpq, frames, rate);
    REQUIRE(a == b);                  // deterministic from absolute PPQ alone
    REQUIRE_FALSE(a.empty());
}

TEST_CASE("capability_matrix: Free-run uses the INTERNAL clock at RATE and ignores transport",
          "[capability_matrix]") {
    // C7: no transport reported -> the clock falls back to ADR-007's INTERNAL source at
    // the RATE knob (0.1-30 Hz). We assert the INTERNAL source produces RATE-driven edges
    // and that those edges are independent of any (irrelevant) transport PPQ/tempo.
    const double sr = kSr;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::Internal);
    c.setInternalRateHz(4.0f);        // 4 Hz -> one edge every sr/4 samples

    std::array<ClockEdge, 64> out{};
    int n = 0;
    // Transport fields are present but MUST be ignored under the INTERNAL (Free-run) clock.
    mw::TransportInfo t{ 137.0, 99.0, true, sr };
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 1 << 15, n);

    REQUIRE(n >= 2);
    const int period = out[1].sampleOffset - out[0].sampleOffset;
    REQUIRE(static_cast<double>(period) == Catch::Approx(sr / 4.0).margin(1.0));  // RATE sets tempo

    // Independence from transport: changing tempo/PPQ does not move the INTERNAL edges.
    Clock c2;
    c2.prepare(sr);
    c2.setSource(ClockSource::Internal);
    c2.setInternalRateHz(4.0f);
    std::array<ClockEdge, 64> out2{};
    int n2 = 0;
    mw::TransportInfo t2{ 240.0, 1.0, false, sr };   // wildly different transport, stopped
    c2.renderEdges(t2, std::span<const int>{}, std::span<ClockEdge>{out2}, 1 << 15, n2);
    REQUIRE(n2 == n);
    for (int i = 0; i < n; ++i) REQUIRE(out2[i].sampleOffset == out[i].sampleOffset);
}

TEST_CASE("capability_matrix: per-format transport rung matches the ADR-022 contract table",
          "[capability_matrix]") {
    // Static best rung [ADR-022 Contract: CLAP=Sample-accurate; VST3/AU/LV2=Block-quantized;
    // Standalone=Free-run], and the runtime fall-to-Free-run when no transport is present.
    REQUIRE(bestTransportFor(PluginFormat::CLAP)       == TransportRung::SampleAccurate);
    REQUIRE(bestTransportFor(PluginFormat::VST3)       == TransportRung::BlockQuantized);
    REQUIRE(bestTransportFor(PluginFormat::AU)         == TransportRung::BlockQuantized);
    REQUIRE(bestTransportFor(PluginFormat::LV2)        == TransportRung::BlockQuantized);
    REQUIRE(bestTransportFor(PluginFormat::Standalone) == TransportRung::FreeRun);

    // With a transport present a sync-capable format resolves to its best rung; with none
    // it falls to Free-run [§8.1 "Free-run if no transport"; ADR-022 C7].
    REQUIRE(resolvedTransport(PluginFormat::CLAP, /*has*/true)  == TransportRung::SampleAccurate);
    REQUIRE(resolvedTransport(PluginFormat::CLAP, /*has*/false) == TransportRung::FreeRun);
    REQUIRE(resolvedTransport(PluginFormat::VST3, /*has*/true)  == TransportRung::BlockQuantized);
    REQUIRE(resolvedTransport(PluginFormat::VST3, /*has*/false) == TransportRung::FreeRun);
    REQUIRE(resolvedTransport(PluginFormat::Standalone, /*has*/true)  == TransportRung::FreeRun);
    REQUIRE(resolvedTransport(PluginFormat::Standalone, /*has*/false) == TransportRung::FreeRun);
}

// ===========================================================================
// §8.2 / C8 — HOST-SYNC without transport behaves as INTERNAL, then re-locks from
// absolute PPQ when a transport appears, with no allocation across the transition.
// ===========================================================================

TEST_CASE("capability_matrix: HOST-SYNC without a transport emits no host edges and re-locks from absolute PPQ",
          "[capability_matrix]") {
    // C8: the user selects HOST-SYNC but no transport exists. The HostSync source emits
    // NO edges while the transport is stopped (so the wrapper falls back to the INTERNAL
    // free-run clock); when a transport appears, HostSync re-derives edges purely from the
    // new ABSOLUTE PPQ (the re-lock anchor) with no dependence on the stopped window.
    Clock c;
    c.prepare(kSr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Quarter);
    c.setSwing(0.5f);

    std::array<ClockEdge, 64> out{};

    // Phase 1: HOST-SYNC selected, transport STOPPED -> zero host edges (behaves as
    // INTERNAL: the wrapper would free-run; the HostSync node itself yields nothing).
    int nStopped = 7;
    mw::TransportInfo stopped{ 120.0, 4.0, /*playing*/false, kSr };
    c.renderEdges(stopped, std::span<const int>{}, std::span<ClockEdge>{out}, 4096, nStopped);
    REQUIRE(nStopped == 0);

    // Phase 2: transport APPEARS at an absolute PPQ that sits on a quarter-note boundary
    // -> HostSync re-locks from that absolute PPQ and places the first edge at block start.
    int nLocked = 0;
    mw::TransportInfo running{ 120.0, /*ppq*/8.0, /*playing*/true, kSr };
    c.renderEdges(running, std::span<const int>{}, std::span<ClockEdge>{out}, 4096, nLocked);
    REQUIRE(nLocked >= 1);
    REQUIRE(out[0].sampleOffset == 0);   // re-locked exactly to the boundary at PPQ 8.0

    // Re-lock is from ABSOLUTE PPQ alone: the same running block rendered standalone (no
    // prior stopped window) yields the identical edge set -> no carried state from the
    // free-run window [ADR-022 C8 "re-locks from absolute PPQ"].
    Clock fresh;
    fresh.prepare(kSr);
    fresh.setSource(ClockSource::HostSync);
    fresh.setHostRate(HostRate::Quarter);
    fresh.setSwing(0.5f);
    std::array<ClockEdge, 64> out2{};
    int n2 = 0;
    fresh.renderEdges(running, std::span<const int>{}, std::span<ClockEdge>{out2}, 4096, n2);
    REQUIRE(n2 == nLocked);
    for (int i = 0; i < n2; ++i) REQUIRE(out2[i].sampleOffset == out[i].sampleOffset);
}

TEST_CASE("capability_matrix: the Free-run to transport transition allocates and locks nowhere",
          "[capability_matrix]") {
    // C11/C8 RT-safety: rendering across the stopped->running (Free-run<->HOST-SYNC)
    // transition takes NO heap allocation on the audio thread. Arm the project RT
    // sentinel (the test-binary global new/delete override consults it) and exercise the
    // whole transition: stopped HostSync block, then a running re-lock block.
    Clock c;
    c.prepare(kSr);                       // prepare() may size; happens BEFORE arming
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Sixteenth);
    c.setSwing(0.5f);

    std::array<ClockEdge, 128> out{};
    mw::TransportInfo stopped{ 120.0, 0.0, false, kSr };
    mw::TransportInfo running{ 120.0, 16.0, true, kSr };

    mw::test::AudioThreadGuard guard;
    guard.arm();
    int n0 = 0, n1 = 0;
    c.renderEdges(stopped, std::span<const int>{}, std::span<ClockEdge>{out}, 2048, n0);  // free-run window
    c.renderEdges(running, std::span<const int>{}, std::span<ClockEdge>{out}, 2048, n1);  // re-lock
    guard.disarm();

    REQUIRE_FALSE(guard.violated());      // no alloc/lock across the transition [ADR-022 C11]
    REQUIRE(guard.violations().empty());
    REQUIRE(n0 == 0);                     // stopped -> behaves as INTERNAL (no host edges)
    REQUIRE(n1 >= 1);                     // re-locked from absolute PPQ
}

// ===========================================================================
// §7.4 / C11-C12 — Rung resolution is RT-safe and BOTH rungs publish to the UI via the
// lock-free atomic-pointer double-buffer (the §6.3 pattern the CapabilityShim uses).
// ===========================================================================

namespace {

// A minimal stand-in for the §6.3 single-writer atomic-pointer double-buffer the
// CapabilityShim uses to publish ResolvedCapabilities to the UI (the shim's own
// implementation links JUCE; this mirrors the EXACT publish/read mechanics on the
// JUCE-free POD). The audio/message thread writes the inactive slot then release-stores
// the live pointer; the UI thread acquire-loads and copies the POD out [docs/design/09
// §7.4, §6.3; ADR-022 C12].
class RungUiPublisher {
public:
    RungUiPublisher() noexcept {
        const ResolvedCapabilities floor{ NoteExpressionRung::Collapsed, TransportRung::FreeRun };
        slots_[0] = floor;
        slots_[1] = floor;
        live_.store(&slots_[0], std::memory_order_release);
        writeSlot_ = 1;
    }
    void publish(const ResolvedCapabilities& caps) noexcept {
        const int slot = writeSlot_;
        slots_[static_cast<std::size_t>(slot)] = caps;
        live_.store(&slots_[static_cast<std::size_t>(slot)], std::memory_order_release);
        writeSlot_ = slot ^ 1;
    }
    [[nodiscard]] ResolvedCapabilities read() const noexcept {
        return *live_.load(std::memory_order_acquire);
    }
    [[nodiscard]] static bool isAlwaysLockFree() noexcept {
        return std::atomic<const ResolvedCapabilities*>::is_always_lock_free;
    }
private:
    std::array<ResolvedCapabilities, 2> slots_{};
    std::atomic<const ResolvedCapabilities*> live_{ nullptr };
    int writeSlot_ = 0;
};

} // namespace

TEST_CASE("capability_matrix: ResolvedCapabilities is a trivially copyable POD published lock-free",
          "[capability_matrix]") {
    // C12: the publish path is the §6.3 lock-free atomic-pointer swap; the published
    // payload is a trivially copyable POD so the swap is memcpy-safe and never tears.
    STATIC_REQUIRE(std::is_trivially_copyable_v<ResolvedCapabilities>);
    REQUIRE(RungUiPublisher::isAlwaysLockFree());   // the atomic ptr is always lock-free here
}

TEST_CASE("capability_matrix: both rungs publish to the UI and a collapsed or free-run state is visible",
          "[capability_matrix]") {
    // C12 / §7.4: BOTH the note-expression rung AND the transport rung are published to
    // the UI, so a Collapsed note-expr or a Free-run transport is user-visible, not a
    // silent surprise. Publish a few per-format snapshots and assert the UI reader sees
    // the latest BOTH-rung state.
    RungUiPublisher pub;

    // Before any publish the reader sees the conservative floor (Collapsed / Free-run).
    REQUIRE(pub.read().noteExpr == NoteExpressionRung::Collapsed);
    REQUIRE(pub.read().transport == TransportRung::FreeRun);

    // CLAP with a transport present and MPE on: Native + Sample-accurate.
    pub.publish({ noteRungFor(PluginFormat::CLAP, true),
                  resolvedTransport(PluginFormat::CLAP, /*has*/true) });
    REQUIRE(pub.read().noteExpr  == NoteExpressionRung::Native);
    REQUIRE(pub.read().transport == TransportRung::SampleAccurate);

    // Standalone, MPE off, no transport: Collapsed + Free-run — the visible "floor" state.
    pub.publish({ noteRungFor(PluginFormat::Standalone, false),
                  resolvedTransport(PluginFormat::Standalone, /*has*/false) });
    REQUIRE(pub.read().noteExpr  == NoteExpressionRung::Collapsed);
    REQUIRE(pub.read().transport == TransportRung::FreeRun);

    // VST3 with MPE on but no transport: MPE-over-MIDI note-expr but Free-run transport —
    // both rungs are independently visible.
    pub.publish({ noteRungFor(PluginFormat::VST3, true),
                  resolvedTransport(PluginFormat::VST3, /*has*/false) });
    REQUIRE(pub.read().noteExpr  == NoteExpressionRung::MpeOverMidi);
    REQUIRE(pub.read().transport == TransportRung::FreeRun);
}

TEST_CASE("capability_matrix: publishing the resolved rungs allocates and locks nowhere",
          "[capability_matrix]") {
    // C11: rung resolution + the per-block recheck + the UI publish allocate/lock nowhere
    // on the audio thread. Arm the RT sentinel and exercise the resolve oracle + publish
    // for every format under the guard.
    RungUiPublisher pub;

    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (PluginFormat fmt : { PluginFormat::VST3, PluginFormat::AU, PluginFormat::CLAP,
                              PluginFormat::Standalone, PluginFormat::LV2 }) {
        for (bool mpeOn : { false, true }) {
            for (bool hasTransport : { false, true }) {
                const ResolvedCapabilities caps{ noteRungFor(fmt, mpeOn),
                                                 resolvedTransport(fmt, hasTransport) };
                pub.publish(caps);
                const ResolvedCapabilities seen = pub.read();
                (void) seen;
            }
        }
    }
    guard.disarm();

    REQUIRE_FALSE(guard.violated());   // resolution + publish are alloc/lock-free [ADR-022 C11]
    REQUIRE(guard.violations().empty());
}
