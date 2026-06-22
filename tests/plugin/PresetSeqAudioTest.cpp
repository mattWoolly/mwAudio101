// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetSeqAudioTest.cpp — the MISSING "do the headline sequencer/arp
// presets actually PLAY?" coverage (task 183; closes ADR-030's downstream gap). The
// existing PresetSeqArpRiffTest validates only schema/loader round-trip — NOTHING renders
// these presets with the sequencer RUNNING. Now that 181 (seq/arp control dispatch +
// pattern-buffer load) and 182 (run/hold transport gate + free-run) wired the subsystem
// end-to-end, this drives the REAL processor render path over the bundled factory corpus
// and proves the live presets actually step + produce healthy, time-varying audio (the
// same "loads but does it play?" class the integration epic addressed).
//
// DRIVE PATTERN (the production path, NOT a publishSnapshot back door):
//   1. preset/program path  — PresetManager::loadPreset(index, apvts, extras, report)
//      applies the preset's 91 params into the live APVTS (incl. seq.mode / seq.tempo_sync /
//      arp.*) AND fills the <extras> 100-step pattern POD. This is the SAME message-thread
//      assembly setCurrentProgram() routes through (PluginProcessor.cpp setCurrentProgram ->
//      presetManager_.loadPreset). The processor's setCurrentProgram discards the recovered
//      <extras> (it does not publish the pattern to the audio thread), so we publish the
//      preset's pattern through the SAME public production accessor the editor's
//      SequencerGrid uses — MwAudioProcessor::setSeqPattern(extras) — which crosses the
//      RT-safe SPSC handoff the audio thread adopts each block (the production seam, task
//      111c/181). [Recorded as a finding in the PR: setCurrentProgram itself never publishes
//      the preset <extras>, so a Program-Change recall of a seq preset would not play its
//      pattern; this test publishes it explicitly via the editor's accessor.]
//   2. transport            — every shipped SeqArpRiff preset ships seq.tempo_sync == HostSync
//      (and arp.tempo_sync == HostSync), so the sequencer/arp clock follows the HOST transport
//      (docs/design/05 §7.4; ADR-022 — run/hold free-runs only the INTERNAL clock). The faithful
//      "running" render for these presets therefore supplies a PLAYING host playhead with an
//      advancing PPQ (the natural DAW case); setTransportRunning(true) is also held. This is the
//      documented deviation from task 183's "Internal-clock / isPlaying==false" wording: the
//      ACTUAL corpus is HostSync, so the test drives the host transport that genuinely clocks it.
//
// ASSERTIONS (objective health + the sequencer ALIVE), per preset class:
//   * ALL seq-using presets, ALIVE render: no NaN/Inf; non-silent (RMS > floor); no hard
//     clip (peak <= kPeakCeil).
//   * seq.mode==Play with stepCount>0: engine.currentSeqStep() ADVANCES across the render
//     (the pattern steps), AND the windowed-RMS contour VARIES (a stepped contour, not a
//     drone) — both far exceed the STATIC baseline (host stopped: step stuck at -1, silent).
//   * arp ENGAGED (arp.latch on — the only arp the as-built ControlSnapshot can switch on,
//     Engine.cpp arpHold gate): a held chord arpeggiates — the engine's arp().isEngaged()
//     latches true (the arp owns ingress) and the ALIVE render materially DIFFERS from a
//     STATIC (host-stopped, same held chord) render.
//   * seq.mode==Off with the arp NOT engaged (arp.mode set but no latch -> the engine plays
//     the held keys as a plain chord, by design): KEEP the health check, SKIP stepping/variance.
//
// NON-VACUITY (recorded in the PR): for every Play preset the test renders the SAME preset
// with the host STOPPED (no clock edges) and REQUIRES that baseline FAILS the alive criteria
// — currentSeqStep never leaves -1 and the output is silent / static — so the alive assertion
// genuinely distinguishes a live-sequenced preset from a frozen one (it cannot pass on a
// static render). A dedicated test case asserts this distinction explicitly.
//
// Test-case display names begin with the `preset_seqaudio` tag and avoid the '[' character
// so `ctest -R preset_seqaudio --no-tests=error` selects exactly these (AGENTS.md silent-pass
// rule). Data-driven over the embedded BinaryData bank via the processor's PresetManager, so
// any new seq/arp preset is auto-covered on the next configure.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"           // mw::plugin::MwAudioProcessor
#include "Engine.h"                    // currentSeqStep / sequencer().arp().isEngaged()
#include "preset/PresetManager.h"      // PresetManager::loadPreset (the production preset path)
#include "state/Extras.h"              // mw::state::Extras (the <extras> seq pattern POD)
#include "state/LoadFailure.h"         // RecoveryReport
#include "params/ParamIDs.h"           // seq/arp param IDs
#include "control/SequencerEngine.h"   // arp() accessor type

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 4096;
constexpr int    kBlock     = 1024;    // host block; PPQ advances per block at the host BPM
constexpr double kBpm       = 120.0;
constexpr int    kBlocks    = 48;      // ~1 s of render — many clock periods at 120 BPM / 16th sync,
                                       // ample windows for the contour-variance + stepping checks

// Health bounds.
constexpr double kRmsFloor  = 5.0e-4;  // "non-silent": well above the static (silent) baseline
constexpr float  kPeakCeil  = 1.2f;    // "no hard clip": bounded headroom

// seq.mode choices { Off=0, Play=1, Record=2 } (core/params/ParamDefs.h kSeqMode).
constexpr int kSeqModePlay = 1;

// A host playhead that PLAYS (or is stopped) with a settable, advancing PPQ — so the
// SequencerEngine's HOST-SYNC clock receives edges. Returning a populated PositionInfo with
// isPlaying==true models a DAW transport rolling; isPlaying==false (frozen PPQ) models the
// stopped host (the static, no-edges baseline) [tests/plugin/CapabilityShimTest FakePlayHead;
// docs/design/05 §7.4]. getPosition() is allocation-free (an inline value type).
class HostHead final : public juce::AudioPlayHead {
public:
    void setPlaying(bool p) noexcept { playing_ = p; }
    void setPpq(double p)  noexcept  { ppq_ = p; }
    static double bpm() noexcept { return kBpm; }
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(playing_);
        info.setPpqPosition(ppq_);
        info.setBpm(kBpm);
        return info;
    }
private:
    bool   playing_ = true;
    double ppq_     = 0.0;
};

double rms(const std::vector<float>& x) noexcept {
    double acc = 0.0;
    for (float v : x) acc += static_cast<double>(v) * v;
    return std::sqrt(acc / std::max<std::size_t>(1, x.size()));
}

float peakAbs(const std::vector<float>& x) noexcept {
    float p = 0.0f;
    for (float v : x) p = std::max(p, std::fabs(v));
    return p;
}

bool allFinite(const std::vector<float>& x) noexcept {
    for (float v : x) if (! std::isfinite(v)) return false;
    return true;
}

// Coefficient of variation (stddev/mean) of per-window RMS — a stepped/arpeggiated contour
// modulates the windowed level over time (high CV), a static drone does not (CV ~ 0).
double windowedRmsCV(const std::vector<float>& x, int windowSamples) noexcept {
    std::vector<double> wr;
    for (std::size_t i = 0; i + static_cast<std::size_t>(windowSamples) <= x.size();
         i += static_cast<std::size_t>(windowSamples)) {
        double acc = 0.0;
        for (int j = 0; j < windowSamples; ++j) {
            const double v = x[i + static_cast<std::size_t>(j)];
            acc += v * v;
        }
        wr.push_back(std::sqrt(acc / windowSamples));
    }
    if (wr.size() < 2) return 0.0;
    double mean = 0.0; for (double v : wr) mean += v; mean /= static_cast<double>(wr.size());
    if (mean < 1.0e-9) return 0.0;
    double var = 0.0; for (double v : wr) var += (v - mean) * (v - mean);
    var /= static_cast<double>(wr.size());
    return std::sqrt(var) / mean;
}

// Sum of absolute per-sample difference — a coarse "do these two renders differ at all?"
// metric used to prove the arp's ingress genuinely changes the held-chord output.
double sampleDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    double d = 0.0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) d += std::fabs(a[i] - b[i]);
    return d;
}

// One rendered preset run: the captured mono (L) output + the max live seq step observed +
// whether the engine's arp engaged at any point during the render.
struct Render {
    std::vector<float> mono;
    int  maxStep   = -1;
    bool arpEngaged = false;
};

// Read an APVTS choice param's 0-based engineering index (== its choice index).
int choiceParam(mw::plugin::MwAudioProcessor& proc, const char* id) {
    auto* p = proc.apvts().getParameter(id);
    REQUIRE(p != nullptr);
    return static_cast<int>(std::lround(
        p->getNormalisableRange().convertFrom0to1(p->getValue())));
}

// Render a preset through the REAL production path: loadPreset (params + <extras>) ->
// setSeqPattern (publish the pattern to the audio thread) -> setTransportRunning(true) ->
// processBlock for kBlocks, advancing a PLAYING (or stopped) host playhead each block.
// `holdNote` presses a 3-note chord on block 0 (the held-key ingress the arp needs).
Render renderPreset(int index, bool hostPlaying, bool holdNote) {
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    HostHead head;
    head.setPlaying(hostPlaying);
    proc.setPlayHead(&head);

    // (1) The production preset/program path: params -> APVTS, pattern -> <extras> POD.
    mw::state::Extras extras{};
    mw::plugin::state::RecoveryReport report{};
    proc.presetManager().loadPreset(index, proc.apvts(), extras, report);

    // (1b) Publish the preset's pattern to the audio thread via the editor's public accessor
    //      (setCurrentProgram does not do this itself — see the file header finding).
    proc.setSeqPattern(extras);

    // (2) Hold the run/hold transport (belt-and-suspenders; HostSync follows isPlaying).
    proc.setTransportRunning(true);

    Render out;
    out.mono.reserve(static_cast<std::size_t>(kBlocks) * kBlock);
    const double ppqPerBlock = (HostHead::bpm() / 60.0) * (static_cast<double>(kBlock) / kSr);
    double ppq = 0.0;

    for (int b = 0; b < kBlocks; ++b) {
        head.setPpq(ppq);
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        buffer.clear();
        if (holdNote && b == 0) {
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, static_cast<juce::uint8>(100)), 1);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 2);
        }
        proc.processBlock(buffer, midi);
        const float* l = buffer.getReadPointer(0);
        for (int i = 0; i < kBlock; ++i) out.mono.push_back(l[i]);
        out.maxStep = std::max(out.maxStep, proc.engineForTest().currentSeqStep());
        if (proc.engineForTest().sequencer().arp().isEngaged()) out.arpEngaged = true;
        if (hostPlaying) ppq += ppqPerBlock;   // stopped host => PPQ frozen => no edges
    }

    proc.setPlayHead(nullptr);
    proc.releaseResources();
    return out;
}

// One preset's classification, read off the live APVTS + <extras> after loadPreset.
struct SeqPresetInfo {
    int index = -1;
    juce::String name;
    juce::String category;
    int  seqMode   = 0;
    int  stepCount = 0;
    int  arpMode   = 0;
    bool arpLatch  = false;
    bool isPlay()  const noexcept { return seqMode == kSeqModePlay && stepCount > 0; }
    bool arpOn()   const noexcept { return arpLatch; }   // the engine's expressible "arp on"
    bool usesSeqOrArp() const noexcept { return isPlay() || arpMode > 0 || arpLatch; }
};

// Enumerate the embedded bank once (one JUCE init), classifying every preset that uses the
// seq or arp. Data-driven: any new seq/arp preset shipped into the corpus is auto-covered.
std::vector<SeqPresetInfo> enumerateSeqPresets() {
    std::vector<SeqPresetInfo> out;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);
    auto& pm = proc.presetManager();
    for (int i = 0; i < pm.getNumPresets(); ++i) {
        mw::state::Extras extras{};
        mw::plugin::state::RecoveryReport report{};
        pm.loadPreset(i, proc.apvts(), extras, report);

        SeqPresetInfo info;
        info.index     = i;
        info.name      = pm.getName(i);
        info.category  = pm.getCategory(i);
        info.seqMode   = choiceParam(proc, mw::params::ids::kSeqMode);
        info.stepCount = extras.stepCount;
        info.arpMode   = choiceParam(proc, "mw101.arp.mode");
        info.arpLatch  = choiceParam(proc, "mw101.arp.latch") != 0;
        if (info.usesSeqOrArp())
            out.push_back(info);
    }
    proc.releaseResources();
    return out;
}

} // namespace

// ===========================================================================
// (1) THE HEADLINE COVERAGE: every seq/arp preset in the bundled corpus renders with the
//     sequencer RUNNING and produces healthy audio; the Play presets STEP + VARY, the
//     engaged arp OWNS INGRESS. Data-driven over the embedded bank.
// ===========================================================================
TEST_CASE("preset_seqaudio every bundled seq/arp preset renders healthy running audio",
          "[preset_seqaudio]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto presets = enumerateSeqPresets();

    // The corpus ships 12 SeqArpRiff presets (9 seq.mode=Play + 3 arp); require a healthy
    // non-trivial set so this selector can never silently pass on an empty/half-loaded bank.
    INFO("seq/arp presets discovered in the embedded bank: " << presets.size());
    REQUIRE(presets.size() >= 10);

    int playCount = 0;   // seq.mode=Play, stepCount>0
    int arpCount  = 0;   // arp engaged (latch)

    for (const auto& p : presets) {
        INFO("preset_seqaudio [" << p.index << "] '" << p.name << "' cat=" << p.category
             << " seqMode=" << p.seqMode << " stepCount=" << p.stepCount
             << " arpMode=" << p.arpMode << " arpLatch=" << (p.arpLatch ? 1 : 0));

        // The step sequencer SELF-TRIGGERS from its stored pattern (a Play preset is rendered
        // WITHOUT a held key). An arp preset — engaged (latch) OR not (arp.mode set, no latch,
        // which the engine plays as a plain held chord) — needs a HELD CHORD to make any sound,
        // so we press one for every non-Play preset.
        const bool holdNote = ! p.isPlay();
        const Render alive = renderPreset(p.index, /*hostPlaying=*/true, holdNote);

        // --- Objective health (ALL seq/arp presets) -------------------------------
        REQUIRE(allFinite(alive.mono));                 // no NaN / Inf
        REQUIRE(rms(alive.mono) > kRmsFloor);           // non-silent
        REQUIRE(peakAbs(alive.mono) <= kPeakCeil);      // no hard clip

        if (p.isPlay()) {
            ++playCount;
            // The pattern actually STEPS: the live playhead advanced past slot 0.
            REQUIRE(alive.maxStep >= 1);
            // The rendered contour VARIES over the pattern (a stepped contour, not a drone).
            const double cvAlive = windowedRmsCV(alive.mono, 2 * kBlock);

            // The STATIC baseline (host stopped: no clock edges) must NOT step and must be
            // silent — proving the alive criteria distinguish a live-sequenced preset.
            const Render stat = renderPreset(p.index, /*hostPlaying=*/false, /*holdNote=*/false);
            REQUIRE(stat.maxStep == -1);                       // never stepped
            REQUIRE(rms(stat.mono) < kRmsFloor);               // silent (no self-triggered notes)
            const double cvStatic = windowedRmsCV(stat.mono, 2 * kBlock);

            // Alive varies; static does not. (cvStatic is ~0 on a silent render.)
            REQUIRE(cvAlive > 0.1);
            REQUIRE(cvAlive > cvStatic + 0.05);
            // The renders are materially different (the live pattern is audible motion).
            REQUIRE(sampleDiff(alive.mono, stat.mono) > 1.0);
        }
        else if (p.arpOn()) {
            ++arpCount;
            // The arp ENGAGED — the held chord handed ingress to the arpeggiator (the arp
            // owns ingress only when arpHold/latch is set, Engine.cpp gate).
            REQUIRE(alive.arpEngaged);
            // The arp's stepping materially changes the held-chord output vs the STATIC
            // (host-stopped, same held chord) render — where the chord sounds directly,
            // un-arpeggiated. The two must differ (the arp is doing something).
            const Render stat = renderPreset(p.index, /*hostPlaying=*/false, /*holdNote=*/true);
            REQUIRE(allFinite(stat.mono));
            REQUIRE(sampleDiff(alive.mono, stat.mono) > 1.0);
        }
        // else: seq.mode=Off with the arp NOT engaged (arp.mode set, no latch) — the engine
        // plays the held keys as a plain chord. Health check above already covered it; the
        // stepping/variance assertion is (correctly) skipped.
    }

    // The headline corpus is the seq.mode=Play SeqArpRiff bank (9) + at least one engaged arp.
    INFO("Play presets exercised: " << playCount << "; engaged-arp presets: " << arpCount);
    REQUIRE(playCount >= 8);
    REQUIRE(arpCount  >= 1);
}

// ===========================================================================
// (2) NON-VACUITY, made explicit: pick a known Play preset and prove the ALIVE render steps
//     + sounds while the STATIC (host-stopped) render does NOT — so the stepping/variance
//     assertion in case (1) genuinely fails on a static render (it cannot silent-pass).
// ===========================================================================
TEST_CASE("preset_seqaudio a static (stopped-host) render fails the stepping and variance test",
          "[preset_seqaudio]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto presets = enumerateSeqPresets();
    REQUIRE_FALSE(presets.empty());

    // The first seq.mode=Play preset in the bank.
    const SeqPresetInfo* play = nullptr;
    for (const auto& p : presets) if (p.isPlay()) { play = &p; break; }
    REQUIRE(play != nullptr);
    INFO("non-vacuity probe on Play preset [" << play->index << "] '" << play->name << "'");

    const Render alive = renderPreset(play->index, /*hostPlaying=*/true,  /*holdNote=*/false);
    const Render stat  = renderPreset(play->index, /*hostPlaying=*/false, /*holdNote=*/false);

    // ALIVE: steps + non-silent + varying.
    REQUIRE(alive.maxStep >= 1);
    REQUIRE(rms(alive.mono) > kRmsFloor);
    REQUIRE(windowedRmsCV(alive.mono, 2 * kBlock) > 0.1);

    // STATIC: the SAME preset, host stopped — the stepping assertion FAILS (never steps) and
    // the output is silent/static. This is the mutation that flips case (1)'s assertions red,
    // proving the test distinguishes a live-sequenced preset from a frozen one.
    REQUIRE(stat.maxStep == -1);                                   // would fail `maxStep >= 1`
    REQUIRE(rms(stat.mono) < kRmsFloor);                           // would fail the RMS floor
    REQUIRE(windowedRmsCV(stat.mono, 2 * kBlock) <= 0.1);          // would fail the variance test

    // And the two renders are unmistakably different.
    REQUIRE(sampleDiff(alive.mono, stat.mono) > 1.0);
}
