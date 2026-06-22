// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/PresetRecallSeqTest.cpp — the PRODUCTION-PATH preset-recall coverage for
// the seq pattern (task 185; closes the gap task 183 found and worked around).
//
// THE DEFECT THIS PINS (confirmed): MwAudioProcessor::setCurrentProgram(index) recovers a
// preset's 91 params into APVTS AND fills its local <extras> POD with the preset's 100-step
// <seq> pattern (via PresetManager::loadPreset), then DISCARDS that pattern — it never
// assigns seqPattern_ nor calls seqPatternHandoff_.publish(). So a host MIDI Program-Change
// recall (handleAsyncUpdate -> setCurrentProgram) OR a PresetBrowser selection of a
// SeqArpRiff preset applies seq.mode=Play but leaves the engine's StepSequencer pattern
// EMPTY (count_==0) — the headline riff DOES NOT PLAY. setStateInformation DOES publish
// (it parses the recovered tree via readSeqPattern and calls seqPatternHandoff_.publish),
// so session reload works; Program-Change / browser recall does not.
//
// THE PRODUCTION PATH UNDER TEST (NO setSeqPattern workaround — that is the whole point):
//   setCurrentProgram(index)  — the SAME message-thread recall the host's Program-Change
//   handoff (handleAsyncUpdate) and the editor's PresetBrowser route through. After the fix
//   it must, BY ITSELF, load the preset's pattern into the audio thread's sequencer. The
//   sibling PresetSeqAudioTest deliberately publishes the pattern via the editor's accessor
//   (proc.setSeqPattern(extras)) to isolate the render; THIS test calls ONLY the recall API.
//
// TRANSPORT: every shipped SeqArpRiff preset ships seq.tempo_sync == HostSync, so the
// sequencer clock follows the HOST transport (docs/design/05 §7.4; ADR-022). The faithful
// running render supplies a PLAYING host playhead with advancing PPQ (the natural DAW case);
// setTransportRunning(true) is also held (belt-and-suspenders for any Internal preset).
//
// ASSERTIONS: after the recall ALONE the engine sequencer pattern loaded (it STEPS:
// currentSeqStep advances past slot 0 — only possible if count_ > 0) AND the audio is
// healthy (no NaN/Inf, non-silent, no hard clip) and TIME-VARYING (a stepped contour, not a
// drone). NON-VACUITY: the same recall drives a STATIC (host-stopped, no clock edges) render
// that must NOT step and must be silent — and, critically, this whole test FAILS against the
// pre-fix setCurrentProgram (pattern discarded -> count_==0 -> never steps, silent).
//
// Case display names begin with the `preset_recall_seq` tag and avoid '[' so
// `ctest -R preset_recall_seq --no-tests=error` selects exactly these (AGENTS.md silent-pass).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"           // mw::plugin::MwAudioProcessor
#include "Engine.h"                    // currentSeqStep
#include "preset/PresetManager.h"      // PresetManager::loadPreset (preset classification)
#include "state/Extras.h"              // mw::state::Extras (the <extras> seq pattern POD)
#include "state/LoadFailure.h"         // RecoveryReport
#include "params/ParamIDs.h"           // seq param IDs

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 4096;
constexpr int    kBlock     = 1024;    // host block; PPQ advances per block at the host BPM
constexpr double kBpm       = 120.0;
constexpr int    kBlocks    = 48;      // ~1 s of render — many clock periods at 120 BPM / 16th sync

// Health bounds (mirror PresetSeqAudioTest).
constexpr double kRmsFloor  = 5.0e-4;  // "non-silent": well above the static (silent) baseline
constexpr float  kPeakCeil  = 1.2f;    // "no hard clip": bounded headroom

// seq.mode choices { Off=0, Play=1, Record=2 } (core/params/ParamDefs.h kSeqMode).
constexpr int kSeqModePlay = 1;

// A host playhead that PLAYS (or is stopped) with a settable, advancing PPQ so the
// SequencerEngine's HOST-SYNC clock receives edges. isPlaying==false (frozen PPQ) models the
// stopped host — the static, no-edges baseline [docs/design/05 §7.4].
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

// Coefficient of variation (stddev/mean) of per-window RMS — a stepped contour modulates the
// windowed level over time (high CV), a static drone does not (CV ~ 0).
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

double sampleDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    double d = 0.0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) d += std::fabs(a[i] - b[i]);
    return d;
}

// One rendered preset run: captured mono (L) + max live seq step observed.
struct Render {
    std::vector<float> mono;
    int  maxStep = -1;
};

// Read an APVTS choice param's 0-based engineering index (== its choice index).
int choiceParam(mw::plugin::MwAudioProcessor& proc, const char* id) {
    auto* p = proc.apvts().getParameter(id);
    REQUIRE(p != nullptr);
    return static_cast<int>(std::lround(
        p->getNormalisableRange().convertFrom0to1(p->getValue())));
}

// THE PRODUCTION-PATH render: recall the preset via setCurrentProgram(index) ALONE — NO
// setSeqPattern workaround — then render the real processBlock over kBlocks, advancing a
// PLAYING (or stopped) host playhead. After the fix, setCurrentProgram must have published
// the recovered pattern to the audio thread; pre-fix it discards it (count_==0 -> no steps).
Render renderRecall(int index, bool hostPlaying) {
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    HostHead head;
    head.setPlaying(hostPlaying);
    proc.setPlayHead(&head);

    // THE RECALL — the sole pattern-loading call. setCurrentProgram routes to
    // PresetManager::loadPreset (params -> APVTS, pattern -> <extras>) and (post-fix)
    // publishes the recovered <extras> pattern to the audio thread.
    proc.setCurrentProgram(index);

    // Hold the run/hold transport (HostSync follows isPlaying; belt-and-suspenders).
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
        proc.processBlock(buffer, midi);
        const float* l = buffer.getReadPointer(0);
        for (int i = 0; i < kBlock; ++i) out.mono.push_back(l[i]);
        out.maxStep = std::max(out.maxStep, proc.engineForTest().currentSeqStep());
        if (hostPlaying) ppq += ppqPerBlock;   // stopped host => PPQ frozen => no edges
    }

    proc.setPlayHead(nullptr);
    proc.releaseResources();
    return out;
}

// One preset's classification, read off the live APVTS + <extras> after loadPreset (does NOT
// publish — pure inspection of what the preset ships).
struct SeqPlayPreset {
    int index = -1;
    juce::String name;
    int  seqMode   = 0;
    int  stepCount = 0;
    bool isPlay() const noexcept { return seqMode == kSeqModePlay && stepCount > 0; }
};

// Enumerate the embedded bank, returning every seq.mode=Play preset with a non-empty pattern
// (the headline riffs whose recall must load + play the stored sequence).
std::vector<SeqPlayPreset> enumeratePlayPresets() {
    std::vector<SeqPlayPreset> out;
    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);
    auto& pm = proc.presetManager();
    for (int i = 0; i < pm.getNumPresets(); ++i) {
        mw::state::Extras extras{};
        mw::plugin::state::RecoveryReport report{};
        pm.loadPreset(i, proc.apvts(), extras, report);

        SeqPlayPreset info;
        info.index     = i;
        info.name      = pm.getName(i);
        info.seqMode   = choiceParam(proc, mw::params::ids::kSeqMode);
        info.stepCount = extras.stepCount;
        if (info.isPlay())
            out.push_back(info);
    }
    proc.releaseResources();
    return out;
}

} // namespace

// ===========================================================================
// (1) THE HEADLINE FIX: a Play preset RECALLED via setCurrentProgram ALONE (no setSeqPattern
//     workaround) loads its stored pattern onto the audio thread and PLAYS — it steps + the
//     contour varies + the audio is healthy. PRE-FIX this FAILS: setCurrentProgram discards
//     the recovered <extras>, so the engine sequencer stays empty (count_==0), never steps,
//     and is silent. Data-driven over every Play preset in the embedded bank.
// ===========================================================================
TEST_CASE("preset_recall_seq setCurrentProgram alone loads and plays a Play preset's stored pattern",
          "[preset_recall_seq]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto plays = enumeratePlayPresets();
    INFO("seq.mode=Play presets discovered in the embedded bank: " << plays.size());
    REQUIRE(plays.size() >= 8);   // the SeqArpRiff Play bank; never silently pass on an empty bank

    for (const auto& p : plays) {
        INFO("preset_recall_seq [" << p.index << "] '" << p.name
             << "' seqMode=" << p.seqMode << " stepCount=" << p.stepCount);

        // Recall via the PRODUCTION API ALONE, then render with the host PLAYING.
        const Render alive = renderRecall(p.index, /*hostPlaying=*/true);

        // The pattern actually LOADED + STEPS: the live playhead advanced past slot 0. This
        // is ONLY possible if setCurrentProgram published the recovered pattern (count_ > 0).
        REQUIRE(alive.maxStep >= 1);

        // Objective health.
        REQUIRE(allFinite(alive.mono));                 // no NaN / Inf
        REQUIRE(rms(alive.mono) > kRmsFloor);           // non-silent (the riff sounds)
        REQUIRE(peakAbs(alive.mono) <= kPeakCeil);      // no hard clip

        // The rendered contour VARIES over the pattern (a stepped riff, not a drone).
        const double cvAlive = windowedRmsCV(alive.mono, 2 * kBlock);

        // The STATIC baseline (SAME recall, host stopped: no clock edges) must NOT step and
        // must be silent — proving the alive criteria distinguish a live-sequenced recall.
        const Render stat = renderRecall(p.index, /*hostPlaying=*/false);
        REQUIRE(stat.maxStep == -1);                    // never stepped (no edges)
        REQUIRE(rms(stat.mono) < kRmsFloor);            // silent (no self-triggered notes)
        const double cvStatic = windowedRmsCV(stat.mono, 2 * kBlock);

        // Alive varies; static does not; the two renders are materially different.
        REQUIRE(cvAlive > 0.1);
        REQUIRE(cvAlive > cvStatic + 0.05);
        REQUIRE(sampleDiff(alive.mono, stat.mono) > 1.0);
    }
}

// ===========================================================================
// (2) THE MIDI PROGRAM-CHANGE ROUTE: a Program Change in the processBlock MIDI stream fires
//     the AsyncUpdater; the message-thread consumer (handleAsyncUpdate, test-driven here)
//     applies the recall via setCurrentProgram — so the SAME fix loads the pattern for a host
//     PC recall. Proves the headline riff plays end-to-end through the documented host path.
// ===========================================================================
TEST_CASE("preset_recall_seq a MIDI ProgramChange recall loads and plays the preset's pattern",
          "[preset_recall_seq]") {
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto plays = enumeratePlayPresets();
    REQUIRE_FALSE(plays.empty());
    const SeqPlayPreset& play = plays.front();
    INFO("PC-recall probe on Play preset [" << play.index << "] '" << play.name << "'");

    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kMaxBlock);

    HostHead head;
    head.setPlaying(true);
    proc.setPlayHead(&head);
    proc.setTransportRunning(true);

    // (a) A Program Change for the Play preset arrives in the audio-thread MIDI stream; the
    //     processor captures it and fires triggerAsyncUpdate (no recall on the audio thread).
    const double ppqPerBlock = (HostHead::bpm() / 60.0) * (static_cast<double>(kBlock) / kSr);
    double ppq = 0.0;
    {
        head.setPpq(ppq);
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        buffer.clear();
        midi.addEvent(juce::MidiMessage::programChange(1, play.index), 0);
        proc.processBlock(buffer, midi);
        ppq += ppqPerBlock;
    }
    REQUIRE(proc.lastProgramChangeForTest() == play.index);

    // (b) The message-thread consumer applies the recall (this is what JUCE calls when the
    //     AsyncUpdate fires). It routes through setCurrentProgram -> loadPreset + (post-fix)
    //     publish of the recovered pattern.
    const int recallsBefore = proc.programRecallCountForTest();
    proc.handleAsyncUpdate();
    REQUIRE(proc.programRecallCountForTest() == recallsBefore + 1);
    REQUIRE(proc.lastRecalledProgramForTest() == play.index);

    // (c) Render with the host PLAYING — the recalled pattern must now step + sound.
    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(kBlocks) * kBlock);
    int maxStep = -1;
    for (int b = 0; b < kBlocks; ++b) {
        head.setPpq(ppq);
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        buffer.clear();
        proc.processBlock(buffer, midi);
        const float* l = buffer.getReadPointer(0);
        for (int i = 0; i < kBlock; ++i) mono.push_back(l[i]);
        maxStep = std::max(maxStep, proc.engineForTest().currentSeqStep());
        ppq += ppqPerBlock;
    }
    proc.setPlayHead(nullptr);
    proc.releaseResources();

    // The PC recall alone loaded the pattern (count_ > 0 -> the playhead advanced) and the
    // riff plays: stepped, healthy, time-varying.
    REQUIRE(maxStep >= 1);
    REQUIRE(allFinite(mono));
    REQUIRE(rms(mono) > kRmsFloor);
    REQUIRE(peakAbs(mono) <= kPeakCeil);
    REQUIRE(windowedRmsCV(mono, 2 * kBlock) > 0.1);
}
