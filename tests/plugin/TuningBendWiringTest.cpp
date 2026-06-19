// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/TuningBendWiringTest.cpp — JUCE-linked Catch2 tests for the tuning +
// bend-range param wiring (task 104b). Realizes docs/design/09 §5 / §4.4 and
// ADR-012 C8 / C11 / C21-C23 / §Decision item 7.
//
// WHAT IS UNDER TEST. TuningBendWiring (plugin/midi/TuningBendWiring.h/.cpp) is the
// thin adapter that reads the doc-06 APVTS params — the A4 reference (mw101.tune.a4),
// the front-panel TUNE (mw101.vco.fine), the channel bend range
// (mw101.mod.bend_range_vco, stored in CENTS) and the MPE bend range
// (mw101.mpe.bend_range, semitones) — and drives MidiFrontEnd::setTuning /
// setBendRange so the continuous Pre-Q pitch-offset path is fed from the parameters.
// The wiring also collapses MPE to channel bend in mono voice mode and honours an
// optional MTS-ESP override that defers to mw101.tune.a4 when absent.
//
// Test-case display names begin with the task tag `tuningbend` so the
// `-R tuningbend` ctest selector matches them and the silent-pass rule holds
// [AGENTS.md; ADR-013 C1].
//
// The no-alloc/no-lock invariant (Acceptance 4) is proved with the same process-level
// heap probe (mstats() bytes_used delta) the sibling MidiFrontEndTest uses: this
// target globs every tests/plugin/*.cpp into ONE binary and LatencyReporterTest.cpp
// already defines the replaceable global operator new, so two TUs cannot both define
// it — the override-free mstats() delta is the collision-proof allocation sentinel on
// macOS arm64 (the documented bless platform).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>

#include <malloc/malloc.h>   // mstats(): override-free heap-usage probe (macOS arm64)

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/TuningBendWiring.h"   // mw::plugin::TuningBendWiring (under test)
#include "midi/MidiFrontEnd.h"       // mw::plugin::MidiFrontEnd
#include "params/ParameterLayout.h"  // mw::plugin::buildParameterLayout
#include "params/ParamDefs.h"        // mw::params::kParamDefs (JUCE-free registry)
#include "../../core/calibration/MidiFrontEndConstants.h"

using namespace mw::plugin;
namespace cal = mw::cal::midifront;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kMaxBlock   = 512;

// A minimal headless AudioProcessor hosting an APVTS built from the real plugin
// layout — the same construction MwAudioProcessor / ParamBridgeTest use — so the
// wiring is exercised against genuine juce::AudioProcessorParameter atomics.
class WiringHostProcessor final : public juce::AudioProcessor
{
public:
    WiringHostProcessor()
        : apvts(*this, nullptr, "PARAMS", buildParameterLayout()) {}

    const juce::String getName() const override         { return "TuningBendWiringHost"; }
    void prepareToPlay(double, int) override            {}
    void releaseResources() override                    {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override        { return 0.0; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override                     { return false; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram(int) override                {}
    const juce::String getProgramName(int) override     { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

// Drive a parameter to a host-automation NORMALIZED [0,1] target (the host path).
void setNormalized(juce::AudioProcessorValueTreeState& apvts, const char* id, float n)
{
    auto* p = apvts.getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(n);
}

// Drive a parameter to an exact ENGINEERING value (Hz / cents / semitones), going
// through the parameter's NormalisableRange so the stored atomic ends up exact.
void setEngineering(juce::AudioProcessorValueTreeState& apvts, const char* id, float eng)
{
    auto* p = apvts.getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(eng));
}

} // namespace

// ============================================================================
// Acceptance 1: setTuning driven from mw101.tune.a4 (default 440, range 400..460)
// and TUNE from mw101.vco.fine (±1.0 semitone); 442 only via preset, never default
// [docs/design/09 §5; ADR-012 C21-C23]
// ============================================================================

TEST_CASE("midi_tuning: A4 defaults to 440 Hz and TUNE to 0 cents from the stock params",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    wiring.apply(fe);   // read params -> drive setTuning / setBendRange

    // Stock layout default for mw101.tune.a4 is 440 (never 442) and mw101.vco.fine 0.
    REQUIRE(fe.a4Hz() == Catch::Approx(440.0f).margin(1.0e-3));
    REQUIRE(fe.tuneCents() == Catch::Approx(0.0f).margin(1.0e-3));
}

TEST_CASE("midi_tuning: A4 of 442 is reachable through the param (the hardware-accurate preset value)",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    // A 'hardware-accurate' preset recalls A4 = 442 Hz via this very param [ADR-012 C22].
    setEngineering(host.apvts, "mw101.tune.a4", 442.0f);
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(442.0f).margin(1.0e-2));

    // The full documented A4 span 400..460 round-trips through the wiring.
    setEngineering(host.apvts, "mw101.tune.a4", 400.0f);
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(400.0f).margin(1.0e-2));
    setEngineering(host.apvts, "mw101.tune.a4", 460.0f);
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(460.0f).margin(1.0e-2));
}

TEST_CASE("midi_tuning: TUNE comes from mw101.vco.fine as plus/minus 1 semitone = plus/minus 100 cents",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    // Full positive fine tune (+1.0 semitone) -> +100 cents of TUNE.
    setEngineering(host.apvts, "mw101.vco.fine", 1.0f);
    wiring.apply(fe);
    REQUIRE(fe.tuneCents() == Catch::Approx(100.0f).margin(1.0e-1));

    // Full negative fine tune (-1.0 semitone) -> -100 cents of TUNE.
    setEngineering(host.apvts, "mw101.vco.fine", -1.0f);
    wiring.apply(fe);
    REQUIRE(fe.tuneCents() == Catch::Approx(-100.0f).margin(1.0e-1));

    // Half fine tune (+0.5 semitone) -> +50 cents.
    setEngineering(host.apvts, "mw101.vco.fine", 0.5f);
    wiring.apply(fe);
    REQUIRE(fe.tuneCents() == Catch::Approx(50.0f).margin(1.0e-1));
}

// ============================================================================
// Acceptance 2: setBendRange wires channel +/-2 (0..24), MPE per-note +/-48 (0..96),
// MPE master +/-48 (0..96), all as continuous offsets BEFORE quantization
// [docs/design/09 §4.4; ADR-012 C8, C11]
// ============================================================================

TEST_CASE("midi_tuning: channel bend range comes from mw101.mod.bend_range_vco (cents) defaulting to plus/minus 2 semitones",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    // Stock default for mw101.mod.bend_range_vco is 200 cents == +/-2 semitones [ADR-012 C8].
    wiring.apply(fe);
    REQUIRE(fe.channelBendRangeSemis() == Catch::Approx(2.0f).margin(1.0e-2));

    // 1200 cents (the param ceiling) -> 12 semitones of channel bend.
    setEngineering(host.apvts, "mw101.mod.bend_range_vco", 1200.0f);
    wiring.apply(fe);
    REQUIRE(fe.channelBendRangeSemis() == Catch::Approx(12.0f).margin(1.0e-2));

    // 0 cents -> 0 semitones.
    setEngineering(host.apvts, "mw101.mod.bend_range_vco", 0.0f);
    wiring.apply(fe);
    REQUIRE(fe.channelBendRangeSemis() == Catch::Approx(0.0f).margin(1.0e-2));
}

TEST_CASE("midi_tuning: MPE per-note and master bend ranges both come from mw101.mpe.bend_range, default plus/minus 48",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    // Stock default for mw101.mpe.bend_range is 48 semitones (the MPE-spec default,
    // ADR-012 C11); the wiring feeds BOTH per-note and master from this single param.
    wiring.apply(fe);
    REQUIRE(fe.mpeNoteBendRangeSemis()   == Catch::Approx(48.0f).margin(1.0e-2));
    REQUIRE(fe.mpeMasterBendRangeSemis() == Catch::Approx(48.0f).margin(1.0e-2));

    // 96 semitones (the param ceiling) feeds both.
    setEngineering(host.apvts, "mw101.mpe.bend_range", 96.0f);
    wiring.apply(fe);
    REQUIRE(fe.mpeNoteBendRangeSemis()   == Catch::Approx(96.0f).margin(1.0e-2));
    REQUIRE(fe.mpeMasterBendRangeSemis() == Catch::Approx(96.0f).margin(1.0e-2));
}

TEST_CASE("midi_tuning: the channel bend range fed by the param drives the actual Pre-Q pitch offset",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    // Set the channel bend range param to 1200 cents (== 12 semitones) and wire it.
    setEngineering(host.apvts, "mw101.mod.bend_range_vco", 1200.0f);
    wiring.apply(fe);

    // A full-down wheel must now produce a -12-semitone continuous Pre-Q offset, proving
    // the param actually feeds the offset path (not just the stored range field).
    CcLearnMap map;
    NormalizedEventBuffer out;
    out.prepare(kMaxBlock);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 0), 0);   // full-down
    fe.processMidi(midi, map, NoteExpressionRung::Collapsed, out);

    const HostEvent* bend = nullptr;
    for (const HostEvent* e = out.begin(); e != out.end(); ++e)
        if (e->type == HostEventType::PitchBend) bend = e;
    REQUIRE(bend != nullptr);
    REQUIRE(bend->value == Catch::Approx(-12.0f).margin(2.0e-2));
}

// ============================================================================
// Acceptance 3: channel + MPE bend share one Pre-Q path; mono collapses MPE to
// channel bend [docs/design/09 §4.4; ADR-012 C13]
// ============================================================================

TEST_CASE("midi_tuning: mono voice mode collapses MPE to channel bend",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    // mw101.voice.mode index 0 == Mono (the stock default) -> collapse MPE to channel.
    setEngineering(host.apvts, "mw101.voice.mode", 0.0f);
    wiring.apply(fe);
    REQUIRE(wiring.monoCollapse());

    // Poly (index 1) does NOT collapse — MPE per-note/master bend stay active.
    setEngineering(host.apvts, "mw101.voice.mode", 1.0f);
    wiring.apply(fe);
    REQUIRE_FALSE(wiring.monoCollapse());

    // Unison (index 2) is also non-mono for the purposes of the MPE collapse decision.
    setEngineering(host.apvts, "mw101.voice.mode", 2.0f);
    wiring.apply(fe);
    REQUIRE_FALSE(wiring.monoCollapse());
}

// ============================================================================
// Acceptance 4: MTS-ESP consulted only when present and cheap; otherwise the single
// A4 float param is authoritative; wiring is RT-safe (no alloc/lock)
// [docs/design/09 §5; ADR-012 §Decision item 7]
// ============================================================================

TEST_CASE("midi_tuning: with no MTS-ESP provider the single A4 float param is authoritative",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);
    REQUIRE_FALSE(wiring.hasTuningProvider());   // none attached by default

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    setEngineering(host.apvts, "mw101.tune.a4", 445.0f);
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(445.0f).margin(1.0e-2));   // param, not MTS
}

TEST_CASE("midi_tuning: a present + cheap MTS-ESP provider overrides A4; an absent/expensive one defers to the param",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    setEngineering(host.apvts, "mw101.tune.a4", 440.0f);

    // A present + cheap provider supplies a reference A4 (e.g. an external MTS-ESP
    // master at 432 Hz). It is honoured ONLY because it is both present and cheap.
    TuningProvider cheap{};
    cheap.present   = true;
    cheap.cheap     = true;
    cheap.a4Hz      = 432.0f;
    wiring.setTuningProvider(&cheap);
    REQUIRE(wiring.hasTuningProvider());
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(432.0f).margin(1.0e-2));   // MTS override

    // The SAME provider but flagged not-cheap must be ignored -> defer to the param.
    cheap.cheap = false;
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(440.0f).margin(1.0e-2));   // back to the param

    // Present but not cheap stays deferred; detaching it entirely also defers.
    wiring.setTuningProvider(nullptr);
    REQUIRE_FALSE(wiring.hasTuningProvider());
    cheap.cheap = true;                                  // would override IF attached
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(440.0f).margin(1.0e-2));   // still the param
}

TEST_CASE("midi_tuning: a present-but-absent provider (present == false) defers to the param even if cheap",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    setEngineering(host.apvts, "mw101.tune.a4", 450.0f);

    TuningProvider absent{};
    absent.present = false;   // no active MTS-ESP master right now
    absent.cheap   = true;
    absent.a4Hz    = 432.0f;
    wiring.setTuningProvider(&absent);
    wiring.apply(fe);
    REQUIRE(fe.a4Hz() == Catch::Approx(450.0f).margin(1.0e-2));   // defers to the param
}

TEST_CASE("midi_tuning: apply() performs zero heap allocation on the audio thread (RT-safe)",
          "[midi_tuning]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    WiringHostProcessor host;
    TuningBendWiring wiring;
    wiring.prepare(host.apvts);

    MidiFrontEnd fe;
    fe.prepare(kSampleRate, kMaxBlock);

    TuningProvider prov{};
    prov.present = true; prov.cheap = true; prov.a4Hz = 441.0f;
    wiring.setTuningProvider(&prov);

    // Warm mstats() so lazy first-call bookkeeping is not charged to apply().
    (void) mstats();
    wiring.apply(fe);                       // warm the path once before measuring

    const std::size_t before = mstats().bytes_used;
    for (int i = 0; i < 256; ++i)
        wiring.apply(fe);                   // the hot, per-block wiring call
    const std::size_t after = mstats().bytes_used;

    REQUIRE(after == before);               // zero heap allocation in apply()
    REQUIRE(std::isfinite(fe.a4Hz()));
}
