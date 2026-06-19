// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/MidiFrontEndTest.cpp — JUCE-linked Catch2 tests for the MIDI front-end
// note/gate/bend/pressure/CC translator (task 104). Realizes docs/design/09 §4.1-4.5 /
// §6.4 and ADR-012 C8/C9/C21-C24 / ADR-016 R-2.
//
// Test-case display names begin with the task tag `midifront` so the `-R midifront`
// ctest selector matches them and the silent-pass rule holds [docs/design/11 §8.2;
// AGENTS.md].
//
// The no-alloc/no-lock invariant (Acceptance 3) is proved with a process-level heap
// probe via mstats() rather than a global operator-new override: mw101_plugin_tests
// globs every tests/plugin/*.cpp into ONE binary and a sibling test
// (LatencyReporterTest.cpp) already defines the replaceable global operator new in
// this same target. Two translation units cannot both define it, so we MUST read the
// allocator's bytes_used delta around the armed window instead — a zero-global-symbol,
// collision-proof allocation sentinel on macOS arm64 (the documented bless platform).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include <malloc/malloc.h>   // mstats(): override-free heap-usage probe (macOS arm64)

#include <juce_audio_basics/juce_audio_basics.h>

#include "midi/MidiFrontEnd.h"
#include "midi/CcLearnMap.h"
#include "host/HostEvent.h"
#include "host/Capabilities.h"
#include "../../core/calibration/MidiFrontEndConstants.h"
#include "../../core/calibration/CcLearnMapConstants.h"

using namespace mw::plugin;
namespace cal = mw::cal::midifront;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kMaxBlock   = 512;
constexpr float  kEps        = 1.0e-5f;

bool nearly(float a, float b) noexcept { return std::abs(a - b) <= kEps; }

// Count HostEvents of a given type + (optionally) bound to a given param index.
int countType(const NormalizedEventBuffer& out, HostEventType t) noexcept {
    int n = 0;
    for (const HostEvent* e = out.begin(); e != out.end(); ++e)
        if (e->type == t) ++n;
    return n;
}

// First ParamValue event whose data0 == paramIndex, or nullptr.
const HostEvent* findParamValue(const NormalizedEventBuffer& out, std::int32_t paramIndex) noexcept {
    for (const HostEvent* e = out.begin(); e != out.end(); ++e)
        if (e->type == HostEventType::ParamValue && e->data0 == paramIndex) return e;
    return nullptr;
}

// 14-bit pitch-wheel value for a signed unit offset in [-1, +1].
int wheelForUnit(float unit) noexcept {
    return static_cast<int>(cal::kPitchWheelCentre + unit * cal::kPitchWheelHalfSpan);
}

} // namespace

// ============================================================================
// Acceptance 1: channel bend (default ±2, range 0..24) -> continuous Pre-Q offset
// [docs/design/09 §4.4; ADR-012 C8]
// ============================================================================

TEST_CASE("midifront: channel bend range defaults to plus/minus 2 semitones",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    REQUIRE(nearly(fe.channelBendRangeSemis(), 2.0f));
    REQUIRE(nearly(cal::kDefaultChannelBendSemis, 2.0f));
}

TEST_CASE("midifront: setBendRange clamps the channel range to 0..24 semitones",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    fe.setBendRange(/*channel=*/12.0f, /*mpeNote=*/48.0f, /*mpeMaster=*/48.0f);
    REQUIRE(nearly(fe.channelBendRangeSemis(), 12.0f));

    fe.setBendRange(99.0f, 48.0f, 48.0f);          // above 24 -> clamp to 24
    REQUIRE(nearly(fe.channelBendRangeSemis(), 24.0f));
    fe.setBendRange(-5.0f, 48.0f, 48.0f);          // below 0 -> clamp to 0
    REQUIRE(nearly(fe.channelBendRangeSemis(), 0.0f));

    // MPE per-note / master bend clamp to 0..96 [ADR-012 C11].
    fe.setBendRange(2.0f, 999.0f, -1.0f);
    REQUIRE(nearly(fe.mpeNoteBendRangeSemis(), 96.0f));
    REQUIRE(nearly(fe.mpeMasterBendRangeSemis(), 0.0f));
}

TEST_CASE("midifront: full-up pitch-wheel emits a continuous Pre-Q offset of plus the bend range",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    fe.setBendRange(/*channel=*/2.0f, 48.0f, 48.0f);

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    // Full-up wheel (16383) ~= +1.0 unit -> +2 semitones at the default range.
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    REQUIRE(countType(out, HostEventType::PitchBend) == 1);
    const HostEvent* bend = nullptr;
    for (const HostEvent* e = out.begin(); e != out.end(); ++e)
        if (e->type == HostEventType::PitchBend) bend = e;
    REQUIRE(bend != nullptr);
    // 16383 maps to ~+0.99988 unit; at ±2 semis that is ~+1.9998 semitones.
    REQUIRE(bend->value > 1.99f);
    REQUIRE(bend->value <= 2.0f);

    // Centre wheel = no bend.
    NormalizedEventBuffer out2;
    out2.prepare(kMaxBlock);
    juce::MidiBuffer midi2;
    midi2.addEvent(juce::MidiMessage::pitchWheel(1, 8192), 0);
    fe.processMidi(midi2, map, NoteExpressionRung::Collapsed, out2);
    const HostEvent* centre = nullptr;
    for (const HostEvent* e = out2.begin(); e != out2.end(); ++e)
        if (e->type == HostEventType::PitchBend) centre = e;
    REQUIRE(centre != nullptr);
    REQUIRE(nearly(centre->value, 0.0f));
}

TEST_CASE("midifront: bend offset scales with the configured range (24 semis = full octave-plus)",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    fe.setBendRange(/*channel=*/24.0f, 48.0f, 48.0f);

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::pitchWheel(1, wheelForUnit(-1.0f)), 0);  // full-down
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    const HostEvent* bend = nullptr;
    for (const HostEvent* e = out.begin(); e != out.end(); ++e)
        if (e->type == HostEventType::PitchBend) bend = e;
    REQUIRE(bend != nullptr);
    REQUIRE(nearly(bend->value, -24.0f));   // full-down at ±24 -> -24 semitones
}

// ============================================================================
// Acceptance 2: velocity ON -> VCA level + VCF cutoff; switch OFF disables
// [docs/design/09 §4.5; ADR-016 R-2]
// ============================================================================

TEST_CASE("midifront: velocity sensing defaults ON",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    REQUIRE(fe.velocityEnabled());
    REQUIRE(cal::kDefaultVelocityEnabled);
}

TEST_CASE("midifront: velocity ON routes a note-on to BOTH VCA level and VCF cutoff offsets",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    REQUIRE(fe.velocityEnabled());

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);  // max velocity
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    REQUIRE(countType(out, HostEventType::NoteOn) == 1);

    // BOTH documented physical nodes receive a ParamValue offset.
    const std::int32_t vcaIdx = MidiFrontEnd::vcaLevelParamIndex();
    const std::int32_t vcfIdx = MidiFrontEnd::vcfCutoffParamIndex();
    REQUIRE(vcaIdx >= 0);
    REQUIRE(vcfIdx >= 0);
    REQUIRE(vcaIdx != vcfIdx);

    const HostEvent* vca = findParamValue(out, vcaIdx);
    const HostEvent* vcf = findParamValue(out, vcfIdx);
    REQUIRE(vca != nullptr);
    REQUIRE(vcf != nullptr);

    // Max velocity (1.0) is above the centre -> a positive (additive) offset.
    REQUIRE(vca->value > 0.0f);
    REQUIRE(vcf->value > 0.0f);
}

TEST_CASE("midifront: velocity switch OFF emits NO VCA/VCF velocity offsets",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    fe.setVelocityEnabled(false);              // the one-action no-velocity switch
    REQUIRE_FALSE(fe.velocityEnabled());

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    // The note still passes; ONLY the velocity routing is suppressed.
    REQUIRE(countType(out, HostEventType::NoteOn) == 1);
    REQUIRE(findParamValue(out, MidiFrontEnd::vcaLevelParamIndex()) == nullptr);
    REQUIRE(findParamValue(out, MidiFrontEnd::vcfCutoffParamIndex()) == nullptr);
}

TEST_CASE("midifront: a soft note-on (below centre) yields a negative velocity offset; centre yields ~0",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    CcLearnMap map;

    // Soft note (velocity 1/127) -> below the centre -> negative offset.
    NormalizedEventBuffer soft;
    soft.prepare(kMaxBlock);
    juce::MidiBuffer midiSoft;
    midiSoft.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 1), 0);
    fe.processMidi(midiSoft, map, NoteExpressionRung::Collapsed, soft);
    const HostEvent* vcaSoft = findParamValue(soft, MidiFrontEnd::vcaLevelParamIndex());
    REQUIRE(vcaSoft != nullptr);
    REQUIRE(vcaSoft->value < 0.0f);
}

// ============================================================================
// Acceptance 3a: CC values resolved through the learn map, unmapped CCs dropped
// [docs/design/09 §6.2-6.3; ADR-012 C15, C20]
// ============================================================================

TEST_CASE("midifront: a CC bound in the default learn map is forwarded as a ParamValue at its param index",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    CcLearnMap map;   // seeded with the §6.2 default map (CC74 -> mw101.vcf.cutoff)
    const std::int32_t cc74Index = map.lookup(74);
    REQUIRE(cc74Index >= 0);                              // CC74 is mapped by default

    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, /*cc=*/74, /*val=*/100), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    const HostEvent* pv = findParamValue(out, cc74Index);
    REQUIRE(pv != nullptr);
    REQUIRE(pv->type == HostEventType::ParamValue);
    REQUIRE(nearly(pv->value, 100.0f / 127.0f));         // normalized 7-bit value
}

TEST_CASE("midifront: an unmapped CC is DROPPED (no event), per no-invented-control policy",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    CcLearnMap map;
    // CC3 is not in the §6.2 default map.
    REQUIRE(map.lookup(3) == CcLearnMap::kUnmapped);

    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, /*cc=*/3, /*val=*/64), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    REQUIRE(out.size() == 0);                            // dropped, never invented
}

TEST_CASE("midifront: CC64 sustain forwards the HOLD sentinel index (not a doc-06 param)",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    CcLearnMap map;
    REQUIRE(map.lookup(64) == mw::cal::cclearn::kHoldParamIndex);

    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, /*cc=*/64, /*val=*/127), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    const HostEvent* hold = findParamValue(out, mw::cal::cclearn::kHoldParamIndex);
    REQUIRE(hold != nullptr);
}

// ============================================================================
// Acceptance 3b: O(1)/sample de-zipper with NO branch on message arrival
// [docs/design/09 §6.4; ADR-012 C24]
// ============================================================================

TEST_CASE("midifront: bend de-zipper smooths toward its target over successive per-sample ticks",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    fe.setBendRange(2.0f, 48.0f, 48.0f);

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    // A single full-up bend message sets the smoother TARGET.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    const float target = fe.bendTargetSemis();
    REQUIRE(target > 1.99f);

    // The de-zipper does NOT jump instantly: after a single sample tick it is still
    // below the target (a zippered jump would already equal it).
    const float afterOne = fe.tickBendSemis();
    REQUIRE(afterOne < target);
    REQUIRE(afterOne >= 0.0f);

    // Over many per-sample ticks (NO new message; the tick is unconditional) it
    // converges to the target.
    float v = afterOne;
    for (int i = 0; i < static_cast<int>(kSampleRate); ++i)   // ~1 second of ticks
        v = fe.tickBendSemis();
    REQUIRE(nearly(v, target));
}

TEST_CASE("midifront: pressure de-zipper continues advancing on samples with NO message (no branch on arrival)",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 0);  // full pressure
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    const float target = fe.pressureTargetNorm();
    REQUIRE(nearly(target, 1.0f));

    // Tick WITHOUT processing any further MIDI — the smoother must keep moving toward
    // the target purely from the unconditional per-sample tick.
    const float a = fe.tickPressureNorm();
    const float b = fe.tickPressureNorm();
    REQUIRE(b > a);            // strictly advancing with no new message
    REQUIRE(a > 0.0f);
}

TEST_CASE("midifront: reset() snaps bend + pressure de-zippers back to rest and restores defaults",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    fe.setBendRange(20.0f, 10.0f, 30.0f);
    fe.setTuning(442.0f, 50.0f);
    fe.setModernUnquantized(true);
    fe.setVelocityEnabled(false);

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    midi.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 0);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    fe.reset();

    REQUIRE(nearly(fe.currentBendSemis(), 0.0f));
    REQUIRE(nearly(fe.currentPressureNorm(), 0.0f));
    REQUIRE(nearly(fe.channelBendRangeSemis(), cal::kDefaultChannelBendSemis));
    REQUIRE(nearly(fe.a4Hz(), cal::kDefaultA4Hz));
    REQUIRE(nearly(fe.tuneCents(), cal::kDefaultTuneCents));
    REQUIRE_FALSE(fe.modernUnquantized());
    REQUIRE(fe.velocityEnabled());     // back to the ADR-016 R-2 default ON
}

// ============================================================================
// Tuning / modern-unquantized stored params [docs/design/09 §5, §4.3; ADR-012 C21-C23]
// ============================================================================

TEST_CASE("midifront: setTuning defaults A4 to 440 Hz and clamps to 400..460; TUNE clamps to plus/minus 1 semitone",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    REQUIRE(nearly(fe.a4Hz(), 440.0f));            // default 440, never 442 [ADR-012 C21-C22]

    fe.setTuning(442.0f, 0.0f);                    // the hardware-accurate preset value
    REQUIRE(nearly(fe.a4Hz(), 442.0f));

    fe.setTuning(1000.0f, 0.0f);                   // above 460 -> clamp
    REQUIRE(nearly(fe.a4Hz(), 460.0f));
    fe.setTuning(0.0f, 0.0f);                       // below 400 -> clamp
    REQUIRE(nearly(fe.a4Hz(), 400.0f));

    fe.setTuning(440.0f, 500.0f);                   // TUNE above +100 cents (±1 semi) -> clamp
    REQUIRE(nearly(fe.tuneCents(), 100.0f));
    fe.setTuning(440.0f, -500.0f);
    REQUIRE(nearly(fe.tuneCents(), -100.0f));
}

TEST_CASE("midifront: modern-unquantized pitch flag defaults OFF and is settable",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    REQUIRE_FALSE(fe.modernUnquantized());          // OFF by default [ADR-012 C7]
    fe.setModernUnquantized(true);
    REQUIRE(fe.modernUnquantized());
}

// ============================================================================
// note/gate routing + sub-block sample offset preserved
// ============================================================================

TEST_CASE("midifront: note-on and note-off route through with the note number and sample offset preserved",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);
    fe.setVelocityEnabled(false);                   // isolate the note events

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 17);
    midi.addEvent(juce::MidiMessage::noteOff(1, 64), 200);
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    REQUIRE(out.size() == 2);
    const HostEvent* on  = out.begin();
    const HostEvent* off = out.begin() + 1;
    REQUIRE(on->type == HostEventType::NoteOn);
    REQUIRE(on->data0 == 64);
    REQUIRE(on->sampleOffset == 17);
    REQUIRE(off->type == HostEventType::NoteOff);
    REQUIRE(off->data0 == 64);
    REQUIRE(off->sampleOffset == 200);
}

// ============================================================================
// Acceptance 3c: processMidi + the per-sample ticks perform ZERO heap alloc / lock
// [docs/design/09 §6.4; ADR-012 C24; the AudioThreadGuard contract, §1.3]
// ============================================================================

TEST_CASE("midifront: processMidi + per-sample de-zipper ticks perform zero heap allocation",
          "[midifront]")
{
    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);

    // Build a dense mixed MIDI block OUTSIDE the armed window so only the hot path is
    // measured. Notes + bend + pressure + CC (mapped and unmapped).
    juce::MidiBuffer midi;
    for (int i = 0; i < 64; ++i) {
        const int off = i * 8;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36 + (i % 48), (juce::uint8)(1 + (i % 126))), off);
        midi.addEvent(juce::MidiMessage::pitchWheel(1, (i * 257) & 0x3FFF), off + 1);
        midi.addEvent(juce::MidiMessage::channelPressureChange(1, i % 128), off + 2);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, i % 128), off + 3);  // mapped
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 3,  i % 128), off + 4);  // unmapped
        midi.addEvent(juce::MidiMessage::noteOff(1, 36 + (i % 48)), off + 5);
    }

    // Warm mstats() once so lazy first-call bookkeeping is not charged to the front-end.
    (void) mstats();

    const std::size_t before = mstats().bytes_used;
    out.clear();
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);
    // Unconditional per-sample de-zipper ticks over a full block (no branch on arrival).
    float acc = 0.0f;
    for (int n = 0; n < kMaxBlock; ++n) {
        acc += fe.tickBendSemis();
        acc += fe.tickPressureNorm();
    }
    const std::size_t after = mstats().bytes_used;

    REQUIRE(after == before);                       // zero heap allocation in the hot path
    REQUIRE(std::isfinite(acc));                    // keep the loop from being optimized away
}
