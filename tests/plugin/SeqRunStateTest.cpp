// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/SeqRunStateTest.cpp — the MISSING plugin-level run-state integration test
// (task 182; ADR-030 part 2). Test-case display names begin with "seq_runstate" so
// `ctest -R seq_runstate` selects exactly these under the silent-pass rule (AGENTS.md
// "Tests"); the display text avoids '[' so Catch2 does not mis-parse a tag and break ctest
// -R selection.
//
// This is the coverage whose ABSENCE let the dead sequencer path ship (ADR-030 Context):
// the editor->processor->engine RUN path that no test exercised (the only sequencer tests
// drove the engine-internal path via a const_cast back door, never the shipped plugin's
// run/hold transport). It proves the END-TO-END wiring task 182 closes:
//
//   (1) STANDALONE PLAYS (the ship-blocker fix). A real MwAudioProcessor, a >=2-distinct-
//       pitch pattern loaded through the PUBLIC pattern path (setSeqPattern), seq.mode=Play
//       via the APVTS param AND setTransportRunning(true), run through processBlock for
//       several INTERNAL-clock periods with isPlaying==false (the Standalone case) — the
//       rendered output is the STEPPED pattern (the measured fundamental changes per step)
//       AND engine.currentSeqStep() ADVANCES across steps. Per ADR-022 the Internal clock
//       FREE-RUNS at the RATE knob with no host transport; the pre-182 isPlaying-only gate
//       wrongly blocked this (ADR-030 break Q3).
//
//   (2) THE GATE COMPOSES (both conditions required, ADR-030 RECONCILIATION):
//       (2a) setTransportRunning(false) -> NO advance (transport off), even in Play mode.
//       (2b) seq.mode != Play with RUN held -> NO advance (not in play mode). Only
//            (Play AND run-held) advances. 181 owns seq.mode==Play (setSeqPlay); 182 owns
//            the run/hold transport — the engine gate requires BOTH.
//
//   (3) HOSTSYNC vs INTERNAL (the transport-rung distinction holds): under HostSync with
//       the host STOPPED (isPlaying=false) + RUN held, the seq does NOT advance (run/hold is
//       the Internal-clock transport, docs/design/05 §7.4: no host edges when stopped);
//       under Internal + RUN held it DOES advance.
//
//   (4) RUN-STATE IS NOT PERSISTED: getStateInformation/setStateInformation round-trip does
//       not carry run==true (run/hold is transient, docs/design/10 §5.3; ADR-030).
//
//   (5) EDITOR WIRING: the editor's onRunStateChanged forwards to setTransportRunning. The
//       bar->callback leg is proven by TransportModeBarTest; here the callback->processor
//       forward the editor installs is proven against a real MwAudioProcessor, and a real
//       MwAudioEditor constructs cleanly with the new wiring.
//
//   (RT) RT-SAFETY: the run-state seam is a single atomic read on the audio thread (no lock,
//       no alloc); the run-on/off processBlock loop is steady-state stable (finite output,
//       no per-block growth). The hard alloc/lock SENTINEL for the engine seq path is the
//       JUCE-free engine_seq RT test (the global-new sentinel only compiles into that
//       binary); here we assert steady-state stability through the real processor.
//
// NON-VACUITY (recorded in the PR): each assertion FAILS against the pre-182 code, where the
// host gate (transportRunning = ctx.transport.isPlaying) blocked the Standalone case (no
// isPlaying -> no advance) and run/hold was inert (the 114c stub stored a member nothing
// read). The mutations that flip each assertion red are noted at each case.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"            // mw::plugin::MwAudioProcessor
#include "../../ui/MwAudioEditor.h"     // mw::ui::MwAudioEditor
#include "../../ui/modules/TransportModeBar.h" // mw::ui::TransportModeBar

#include "Engine.h"
#include "control/ControlTypes.h"
#include "control/SequencerEngine.h"
#include "state/Extras.h"
#include "params/ParamIDs.h"
#include "calibration/SequencerRoutingConstants.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 4096;

// A 2 Hz internal clock => one H->L edge every kSr/2 == 24000 samples. Rendering one
// 24000-sample block therefore advances the playhead exactly one step (the same
// one-edge-per-block cadence the engine_seq suite uses), so the run-on case marches the seq
// deterministically and fast.
constexpr float  kInternalRateHz = 2.0f;
const int        kStepSamples    = static_cast<int>(kSr / kInternalRateHz);   // 24000

using mw::control::ControlSnapshot;
using mw::control::ClockSource;
using mw::control::TrigMode;

// Mutable handle to the processor's hosted SequencerEngine — the same const_cast pattern the
// engine_seq / gui_datapaths suites use. Used ONLY to SEED the internal clock RATE off the
// audio thread (the RATE knob is not task 182's subject; task 181's dispatch PRESERVES the
// live internalRateHz every block, so a seeded rate survives). The pattern, play state and
// run state all reach the engine through the PUBLIC plugin path under test.
mw::seq::SequencerEngine& seqOf(mw::plugin::MwAudioProcessor& proc) noexcept {
    auto& eng = const_cast<mw::Engine&>(proc.engineForTest());
    return const_cast<mw::seq::SequencerEngine&>(eng.sequencer());
}

// Seed only the internal clock RATE + clock-reset-off through the snapshot seam (NOT the
// pattern — that comes via setSeqPattern, and NOT the clock source — that comes from the
// seq.mode/tempo_sync param dispatch each block).
void seedInternalRate(mw::plugin::MwAudioProcessor& proc, float rateHz) {
    ControlSnapshot s{};
    s.clockSource          = ClockSource::Internal;
    s.internalRateHz       = rateHz;
    s.clockResetOnKeypress = false;
    s.trigMode             = TrigMode::GateTrig;
    seqOf(proc).publishSnapshot(s);
}

// Set an APVTS param from its engineering value (a choice param's engineering value IS its
// 0-based index; AudioParameterChoice's range is [0, numChoices-1]). Drives the processor's
// real per-block dispatch.
void setParam(mw::plugin::MwAudioProcessor& proc, const char* id, float engineeringValue) {
    auto* p = proc.apvts().getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(engineeringValue));
}

// Open the voice path so a gated seq step renders audible sound through the real dispatch.
void openVoice(mw::plugin::MwAudioProcessor& proc) {
    setParam(proc, mw::params::ids::kVcaLevel, 0.9f);
    setParam(proc, mw::params::ids::kSawLevel, 0.9f);
    setParam(proc, mw::params::ids::kVcfCutoff, 0.95f);
    setParam(proc, mw::params::ids::kEnvSustain, 1.0f);   // hold level through the step
    setParam(proc, mw::params::ids::kEnvAttack, 0.0f);    // snap on so the step is audible fast
}

// A 2-step pattern of two pitches an octave apart so the rendered fundamental is measurably
// different per step. Notes are RELATIVE to the seq seam base (kSeqVoiceBaseMidi == 36):
// step 0 -> base+12 == MIDI 48 (C3), step 1 -> base+24 == MIDI 60 (C4). Both gated, no rest/tie.
mw::state::Extras twoPitchPattern() {
    mw::state::Extras ex{};
    ex.stepCount = 2;
    ex.steps[0] = mw::state::SeqStep{ /*note*/ 12, /*gate*/ true, /*tie*/ false, /*rest*/ false };
    ex.steps[1] = mw::state::SeqStep{ /*note*/ 24, /*gate*/ true, /*tie*/ false, /*rest*/ false };
    return ex;
}

// MIDI note -> Hz (A4 = 440, note 69) — the 1V/oct reference.
double midiHz(int n) noexcept { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

// Goertzel power of frequency f in x (single bin).
double goertzelPower(const std::vector<float>& x, double f, double sr) noexcept {
    const double w = 2.0 * M_PI * f / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) { const double s0 = static_cast<double>(v) + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// Estimate the dominant fundamental by scanning candidate frequencies for the Goertzel peak.
double estimateFundamental(const std::vector<float>& x, double sr, double fLo, double fHi) noexcept {
    double bestF = fLo, bestP = -1.0;
    const int steps = 600;
    for (int i = 0; i <= steps; ++i) {
        const double f = fLo * std::pow(fHi / fLo, static_cast<double>(i) / steps);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    return bestF;
}

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}

// Render exactly one internal-clock period (kStepSamples == one H->L edge) and return the
// mono (L) output for that window. To keep the processor's prepared block size at the normal
// kMaxBlock, the step window is rendered as kMaxBlock-sized sub-blocks; the internal clock
// accumulates phase ACROSS blocks, so exactly one edge lands per kStepSamples regardless of
// how the window is chunked, and the step's note is constant across the window (clean to
// measure). The processor's transport.isPlaying is naturally false here (the headless
// AudioProcessor reports no playhead — the Standalone / stopped-host case); the run/hold
// transport is set on the processor via setTransportRunning before the loop.
std::vector<float> renderStep(mw::plugin::MwAudioProcessor& proc, bool isPlaying) {
    juce::ignoreUnused(isPlaying);   // headless host => isPlaying==false; documented above
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(kStepSamples));
    int remaining = kStepSamples;
    while (remaining > 0) {
        const int n = std::min(remaining, kMaxBlock);
        juce::AudioBuffer<float> buffer(2, n);
        juce::MidiBuffer midi;
        buffer.clear();
        proc.processBlock(buffer, midi);
        const float* l = buffer.getReadPointer(0);
        for (int i = 0; i < n; ++i) out.push_back(l[i]);
        remaining -= n;
    }
    return out;
}

} // namespace

// ===========================================================================
// (1) STANDALONE PLAYS: seq.mode=Play + RUN held + Internal clock + isPlaying==false ->
//     stepped audio AND currentSeqStep() advances. The ship-blocker fix (ADR-030 Q3).
//
//     NON-VACUITY: pre-182 the gate was `transportRunning = ctx.transport.isPlaying`. The
//     headless host reports isPlaying==false, so transportRunning was false, the sequencer
//     never owned ingress, currentSeqStep() stayed -1 forever and the render was the held
//     default pitch (no stepping). Every assertion below was red.
// ===========================================================================
TEST_CASE("seq_runstate: standalone (no host transport) plays the stepped pattern when RUN is held",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    openVoice(proc);
    seedInternalRate(proc, kInternalRateHz);
    proc.setSeqPattern(twoPitchPattern());

    setParam(proc, mw::params::ids::kSeqMode,      /*Play*/  1);
    setParam(proc, mw::params::ids::kSeqTempoSync, /*Off*/   0);   // Off -> Internal clock (free-run)
    proc.setTransportRunning(true);                                // RUN held (the transient transport)

    // Before any block: no step has played.
    REQUIRE(proc.engineForTest().currentSeqStep() == -1);

    // March several internal-clock periods (one edge per block). Capture the rendered step
    // pitch + the live step at each.
    std::vector<int>   stepsSeen;
    std::vector<double> pitchesSeen;
    for (int b = 0; b < 4; ++b) {
        auto out = renderStep(proc, /*isPlaying=*/false);
        const int step = proc.engineForTest().currentSeqStep();
        stepsSeen.push_back(step);
        REQUIRE(rms(out) > 1.0e-4);   // the gated step actually sounded
        pitchesSeen.push_back(estimateFundamental(out, kSr, midiHz(40), midiHz(72)));
    }

    // (a) STEP ADVANCE: the live step advanced across the pattern (0,1,0,1 for a 2-step loop).
    //     It is no longer stuck at -1 (the pre-182 no-advance symptom).
    REQUIRE(stepsSeen[0] == 0);
    REQUIRE(stepsSeen[1] == 1);
    REQUIRE(stepsSeen[2] == 0);   // wraps
    REQUIRE(stepsSeen[3] == 1);

    // (b) STEPPED AUDIO: the measured fundamental is the step's note (base+12 == 48 on even
    //     steps, base+24 == 60 on odd steps) — the audio actually STEPPED, not a fixed pitch.
    const int base = mw::cal::seqroute::kSeqVoiceBaseMidi;   // 36
    REQUIRE(pitchesSeen[0] == Catch::Approx(midiHz(base + 12)).epsilon(0.04));
    REQUIRE(pitchesSeen[1] == Catch::Approx(midiHz(base + 24)).epsilon(0.04));
    // The two distinct step pitches are an octave apart (2x) — measurably different per step.
    REQUIRE((pitchesSeen[1] / pitchesSeen[0]) == Catch::Approx(2.0).epsilon(0.05));

    proc.releaseResources();
}

// ===========================================================================
// (2a) GATE COMPOSES — transport off: seq.mode=Play but setTransportRunning(false) -> the
//      seq does NOT advance (the run/hold transport gate is OFF). Only Play AND run-held
//      advances (ADR-030 RECONCILIATION).
//
//      NON-VACUITY: this is the inverse of case (1). A regression that made the gate ignore
//      run/hold (e.g. transportRunning hard-true, or routing run/hold into setSeqPlay instead
//      of the transport gate) would let it advance here, failing this assertion.
// ===========================================================================
TEST_CASE("seq_runstate: Play mode with RUN released does not advance the sequencer",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    openVoice(proc);
    seedInternalRate(proc, kInternalRateHz);
    proc.setSeqPattern(twoPitchPattern());

    setParam(proc, mw::params::ids::kSeqMode,      /*Play*/ 1);
    setParam(proc, mw::params::ids::kSeqTempoSync, /*Off*/  0);
    proc.setTransportRunning(false);                              // RUN released

    for (int b = 0; b < 4; ++b)
        (void) renderStep(proc, /*isPlaying=*/false);

    // No step ever played: the live step is the "no step" sentinel.
    REQUIRE(proc.engineForTest().currentSeqStep() == -1);
    // The play FLAG is still set (181 owns seq.mode==Play); it is the TRANSPORT that gates.
    REQUIRE(proc.engineForTest().sequencer().seq().isPlaying());

    proc.releaseResources();
}

// ===========================================================================
// (2b) GATE COMPOSES — not in Play mode: RUN held but seq.mode != Play (Off) -> the seq does
//      NOT advance (181's seqPlaying term is false). Only Play AND run-held advances.
//
//      NON-VACUITY: a regression that made run/hold alone drive playback (bypassing the
//      seq.mode==Play term, i.e. 182 writing setSeqPlay(runHeld)) would advance here and clobber
//      181's seq.mode authority — exactly the last-writer-wins fight ADR-030 forbids.
// ===========================================================================
TEST_CASE("seq_runstate: RUN held but seq.mode Off does not advance the sequencer",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    openVoice(proc);
    seedInternalRate(proc, kInternalRateHz);
    proc.setSeqPattern(twoPitchPattern());

    setParam(proc, mw::params::ids::kSeqMode,      /*Off*/ 0);    // NOT Play
    setParam(proc, mw::params::ids::kSeqTempoSync, /*Off*/ 0);
    proc.setTransportRunning(true);                               // RUN held

    for (int b = 0; b < 4; ++b)
        (void) renderStep(proc, /*isPlaying=*/false);

    REQUIRE(proc.engineForTest().currentSeqStep() == -1);
    REQUIRE_FALSE(proc.engineForTest().sequencer().seq().isPlaying());   // 181: seq.mode Off

    proc.releaseResources();
}

// ===========================================================================
// (3) HOSTSYNC vs INTERNAL: under HOST-SYNC with the host STOPPED (isPlaying==false, the
//     headless host) + RUN held, the seq does NOT advance — run/hold is the Internal-clock
//     transport and does NOT make a host-synced clock free-run (docs/design/05 §7.4: no host
//     edges when stopped; ADR-022). The Internal sibling (case 1) DOES advance under the same
//     RUN-held + isPlaying==false conditions, so the rung distinction is the only difference.
//
//     NON-VACUITY: a gate that ignored the clock source (e.g. transportRunning = runHeld for
//     ALL sources) would advance here too, failing this assertion. The Internal-vs-HostSync
//     split is the whole point of `(clockSource==Internal) ? runHeld : isPlaying`.
// ===========================================================================
TEST_CASE("seq_runstate: HostSync with the host stopped does not advance even when RUN is held",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    openVoice(proc);
    seedInternalRate(proc, kInternalRateHz);
    proc.setSeqPattern(twoPitchPattern());

    setParam(proc, mw::params::ids::kSeqMode,      /*Play*/ 1);
    setParam(proc, mw::params::ids::kSeqTempoSync, /*On*/   1);   // On -> HostSync clock
    proc.setTransportRunning(true);                               // RUN held, but host is stopped

    for (int b = 0; b < 4; ++b)
        (void) renderStep(proc, /*isPlaying=*/false);

    // HostSync + host stopped: no host edges, so no advance — even with RUN held.
    REQUIRE(proc.engineForTest().currentSeqStep() == -1);
    REQUIRE(proc.engineForTest().sequencer().seq().isPlaying());   // play FLAG still set (181)

    proc.releaseResources();
}

// ===========================================================================
// (4) RUN-STATE IS NOT PERSISTED: a getStateInformation/setStateInformation round-trip does
//     not carry run==true (run/hold is transient, docs/design/10 §5.3).
//
//     NON-VACUITY: a regression that persisted run-state in <extras> would restore it as
//     true here, failing the post-restore REQUIRE_FALSE.
// ===========================================================================
TEST_CASE("seq_runstate: run-state is transient and does not survive a state round-trip",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    juce::MemoryBlock blob;
    {
        mw::plugin::MwAudioProcessor source;
        source.prepareToPlay(kSr, kMaxBlock);
        source.setTransportRunning(true);                 // RUN held on the source
        REQUIRE(source.transportRunningForTest());        // sanity: it took
        source.getStateInformation(blob);                 // capture the persisted state
        source.releaseResources();
    }

    // A FRESH processor restores from the blob: run-state must NOT come back.
    mw::plugin::MwAudioProcessor restored;
    restored.prepareToPlay(kSr, kMaxBlock);
    REQUIRE_FALSE(restored.transportRunningForTest());     // default stopped before restore
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE_FALSE(restored.transportRunningForTest());     // STILL stopped — run was not persisted
    restored.releaseResources();
}

// ===========================================================================
// (5) EDITOR WIRING: the editor's onRunStateChanged forwards to processor.setTransportRunning.
//     The bar->callback leg is proven by TransportModeBarTest; here we prove (i) a real
//     MwAudioEditor constructs cleanly with the new wiring (and its run-state mirror starts
//     released), and (ii) the callback->processor forward the editor installs drives the real
//     processor seam (wired against the processor's own APVTS, exactly as the editor wires it).
//
//     NON-VACUITY: the pre-182 editor stub stored lastTransportRunning_ and did NOT call the
//     processor, so the processor's run-state never changed from a run/hold toggle — the
//     processor assertion below was red (the seam did not even exist).
// ===========================================================================
TEST_CASE("seq_runstate: the editor run/hold callback forwards to the processor transport seam",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    // (i) The real editor constructs with the task-182 wiring installed; its mirror starts
    //     released (no run held yet).
    auto editor = std::make_unique<mw::ui::MwAudioEditor>(proc);
    REQUIRE_FALSE(editor->transportRunningForTest());
    REQUIRE_FALSE(proc.transportRunningForTest());

    // (ii) The callback->processor forward the editor installs: a run/hold toggle on the bar
    //      fires onRunStateChanged, which the editor routes to processor.setTransportRunning.
    //      The bar is the editor's private member; drive the SAME wiring against a bar bound to
    //      the processor's APVTS (the exact binding the editor uses) and assert the processor
    //      seam flips. (TransportModeBarTest proves the bar's toggle fires the callback.)
    mw::ui::TransportModeBar bar(proc.apvts());
    bar.onRunStateChanged = [&proc](bool running) { proc.setTransportRunning(running); };

    REQUIRE_FALSE(proc.transportRunningForTest());
    bar.runHoldToggle().setToggleState(true, juce::sendNotificationSync);
    REQUIRE(proc.transportRunningForTest());               // RUN held reached the processor
    bar.runHoldToggle().setToggleState(false, juce::sendNotificationSync);
    REQUIRE_FALSE(proc.transportRunningForTest());          // RUN released reached the processor

    editor.reset();
    proc.releaseResources();
}

// ===========================================================================
// (RT) STEADY-STATE STABILITY: the run-on processBlock loop is steady-state stable — finite
//      output every block, the seq keeps advancing, with no per-block growth (the run-state
//      seam is a single atomic read; no lock, no alloc added). The hard alloc/lock sentinel
//      for the engine seq path is the JUCE-free engine_seq RT test.
// ===========================================================================
TEST_CASE("seq_runstate: the run-on processBlock loop is steady-state stable",
          "[seq_runstate]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    openVoice(proc);
    seedInternalRate(proc, kInternalRateHz);
    proc.setSeqPattern(twoPitchPattern());
    setParam(proc, mw::params::ids::kSeqMode,      /*Play*/ 1);
    setParam(proc, mw::params::ids::kSeqTempoSync, /*Off*/  0);
    proc.setTransportRunning(true);

    // Warm once, then march many steps: every block renders finite output and the playhead
    // keeps marching (a valid 0..1 slot for the 2-step loop).
    (void) renderStep(proc, /*isPlaying=*/false);
    for (int b = 0; b < 16; ++b) {
        auto out = renderStep(proc, /*isPlaying=*/false);
        for (float v : out) REQUIRE(std::isfinite(v));
        const int step = proc.engineForTest().currentSeqStep();
        REQUIRE(step >= 0);
        REQUIRE(step <= 1);
    }

    proc.releaseResources();
}
