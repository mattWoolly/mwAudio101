// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/CcBendAlignmentTest.cpp — the PLUGIN-PATH acceptance suite for aligning the
// plugin MIDI front-end to the core continuous-controller ingress seam (task 162d, the
// plugin half of the 162c contract). Compiled into the JUCE-linked mw101_plugin_tests
// target.
//
// Every test-case display name begins with the "cc_ingress" tag so
// `ctest -R cc_ingress --no-tests=error` selects exactly this suite under the silent-pass
// rule (AGENTS.md "Tests"). The tag is "[cc_ingress]"; no literal '[' appears in any display
// name so Catch2 never mis-parses a tag out of the name.
//
// WHAT THIS PROVES (the gap 162d closes — bend/wheel were INERT in the real plugin even
// though the core test 162c proved the engine consumes them): driving the REAL
// MwAudioProcessor through processBlock with a host PitchBend / CC1 (mod-wheel) MIDI move
// now produces the 162c audible effect END-TO-END:
//   * a host pitch-bend (bend_dest=VCO, non-zero bend_range_vco) bends the rendered VCO
//     fundamental UP and DOWN — measured on the rendered output (Goertzel);
//   * the bend SCALES with the wheel position — a HALF wheel bends HALF the range (x sqrt2 at a
//     1200-cent range; +1 semitone at a +-2-semitone range), NOT the full range. This is the
//     162d regression guard for the bug QA caught: the core used to re-read the semitone-valued
//     PitchBend MidiEvent as a [-1,+1] unit, so a half wheel clamped to a FULL octave;
//   * a host pitch-bend with bend_dest=VCF opens the filter (brightness rises) WITHOUT
//     moving the VCO fundamental;
//   * a host CC1/mod-wheel move RAISES the LFO modulation depth per mod.lfo_mod_wheel —
//     measured audibly (the LFO-rate spectral line in the amplitude envelope rises with the
//     wheel);
//   * a centered bend / wheel-down render is the NEUTRAL identity (no shift / no boost);
//   * the live mod-wheel does NOT double-apply: CC1 drives the controller-ingress modWheel
//     ONLY (the 162c LFO-depth multiplier), not also a separate mod.lfo_mod_wheel ParamValue;
//   * a host PitchBend + CC1 reach BlockContext::controllers through the real processor and
//     PERSIST across blocks (the controller-ingress wiring this task adds).
//
// The absolute MIDI-note pitch is intentionally not asserted: the engine's note-ingress octave
// is owned by the core voice stream (out of this task's scope), so the bend tests assert the
// SHIFT RATIO (bent/neutral == the 1 V/oct bend factor) on whatever pitch the saw plays — the
// controller-ingress contract this task wires.
//
// Headless: a juce::ScopedJuceInitialiser_GUI brackets JUCE singletons so the render path
// runs on a CI box with no audio device (the PluginHarnessTest / ProcessorWireTest pattern).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"   // mw::plugin::MwAudioProcessor (the real assembled processor)
#include "params/ParamIDs.h"   // canonical parameter string IDs

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kBlockSize = 256;
constexpr int    kNumOut    = 2;

// 14-bit pitch-wheel value for a CENTERED signed unit [-1,+1] (0 == centered, 8192).
int wheelForUnit(float unit) noexcept {
    const float u = std::clamp(unit, -1.0f, 1.0f);
    // 8192 + u*8191 keeps full-down at 1 and full-up at 16383 (the JUCE 14-bit domain).
    return std::clamp(static_cast<int>(std::lround(8192.0f + u * 8191.0f)), 0, 16383);
}

// Set an APVTS parameter to a MODELED (engineering) value via its NormalisableRange, exactly
// as a host automation move would (convertTo0to1 + setValueNotifyingHost). Off the audio
// thread (message-thread setup), so processBlock's snapshot reads the new value.
void setParam(mw::plugin::MwAudioProcessor& proc, const char* id, float engineeringValue) {
    auto* p = proc.apvts().getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(engineeringValue));
}

std::vector<float> removeDc(const std::vector<float>& x) noexcept;   // defined below

// Render `seconds` of sustained mono audio for a held note through the REAL processor, with a
// host pitch-bend (held for the whole render) and an OPTIONAL CC1 mod-wheel move (wheel7 < 0
// => no CC1 at all). The bend + CC1 are emitted on the key-press block AND re-sent every block
// so the live controller position is held for the whole render (matching a sustained
// performance gesture). Returns the DC-removed mono (channel 0) buffer.
std::vector<float> renderHeld(mw::plugin::MwAudioProcessor& proc,
                              int midiNote, double seconds,
                              float bendUnit, int wheel7,
                              int warmupBlocks = 8) {
    const int wheelValue = wheelForUnit(bendUnit);

    auto controllerMidi = [&](bool withNoteOn) {
        juce::MidiBuffer m;
        if (withNoteOn)
            m.addEvent(juce::MidiMessage::noteOn(1, midiNote, (juce::uint8) 100), 0);
        m.addEvent(juce::MidiMessage::pitchWheel(1, wheelValue), 0);
        if (wheel7 >= 0)
            m.addEvent(juce::MidiMessage::controllerEvent(1, /*cc=*/1, wheel7), 0);
        return m;
    };

    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);

    // Key-press block (note-on + controllers).
    buffer.clear();
    {
        juce::MidiBuffer m = controllerMidi(/*withNoteOn=*/true);
        proc.processBlock(buffer, m);
    }
    // Warm-up blocks (hold the controllers; let the envelope settle).
    for (int b = 1; b < warmupBlocks; ++b) {
        buffer.clear();
        juce::MidiBuffer m = controllerMidi(/*withNoteOn=*/false);
        proc.processBlock(buffer, m);
    }

    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(total) + kBlockSize);
    int rendered = 0;
    while (rendered < total) {
        buffer.clear();
        juce::MidiBuffer m = controllerMidi(/*withNoteOn=*/false);
        proc.processBlock(buffer, m);
        const float* l = buffer.getReadPointer(0);
        for (int i = 0; i < kBlockSize && rendered < total; ++i, ++rendered)
            out.push_back(l[i]);
    }
    // DC-robust: strip the small steady offset the full plugin output path carries so the
    // fundamental/AC-energy estimators see only the AC content under test (see removeDc).
    return removeDc(out);
}

// Subtract the DC/mean from a signal. The full plugin output path carries a small steady DC
// offset (the saw source has a DC component; the open filter passes it), which a low-frequency
// Goertzel bin integrates into enormous spurious power that would swamp the true fundamental in
// a wide search. Removing the mean makes the fundamental estimate DC-robust without changing the
// audible AC content under test (bend pitch / LFO wobble are AC). Pure; allocation is the test's.
std::vector<float> removeDc(const std::vector<float>& x) noexcept {
    double mean = 0.0; for (float v : x) mean += v;
    mean /= std::max<std::size_t>(1, x.size());
    std::vector<float> out; out.reserve(x.size());
    for (float v : x) out.push_back(static_cast<float>(v - mean));
    return out;
}

double goertzelPower(const std::vector<float>& x, double f, double sr) noexcept {
    const int N = static_cast<int>(x.size());
    if (N == 0) return 0.0;
    const double w = 2.0 * 3.14159265358979323846 * f / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int n = 0; n < N; ++n) {
        const double s0 = static_cast<double>(x[static_cast<std::size_t>(n)]) + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

double estimateFundamental(const std::vector<float>& x, double sr,
                           double fLo, double fHi) noexcept {
    double bestF = fLo, bestP = -1.0;
    const int steps = 600;
    for (int i = 0; i <= steps; ++i) {
        const double f = fLo * std::pow(fHi / fLo, static_cast<double>(i) / steps);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    const double lo = bestF * 0.97, hi = bestF * 1.03;
    for (int i = 0; i <= 200; ++i) {
        const double f = lo + (hi - lo) * (static_cast<double>(i) / 200.0);
        const double p = goertzelPower(x, f, sr);
        if (p > bestP) { bestP = p; bestF = f; }
    }
    return bestF;
}

// Estimate the fundamental near an EXPECTED frequency: search a tight log-spaced band around
// `expectedHz` (a factor `band` each side, default x0.7..x1.43) so the estimator locks onto the
// right partial instead of a neighbouring harmonic when the bend moves the pitch. The bend math
// gives a precise expected Hz per wheel position, so anchoring the search there is both robust
// (no harmonic mis-lock) and a STRONGER assertion than a wide blind search.
double estimateNear(const std::vector<float>& x, double sr, double expectedHz,
                    double band = 1.43) noexcept {
    return estimateFundamental(x, sr, expectedHz / band, expectedHz * band);
}

std::vector<float> rmsEnvelope(const std::vector<float>& x, int hop) noexcept {
    std::vector<float> env;
    const int N = static_cast<int>(x.size());
    for (int start = 0; start + hop <= N; start += hop) {
        double acc = 0.0;
        for (int i = 0; i < hop; ++i) {
            const double v = x[static_cast<std::size_t>(start + i)];
            acc += v * v;
        }
        env.push_back(static_cast<float>(std::sqrt(acc / hop)));
    }
    return env;
}

// The LFO-rate spectral line in the amplitude envelope: Goertzel of the RMS envelope at the
// LFO rate, the direct measure of LFO->filter modulation DEPTH (its power scales ~depth^2, so
// it is far more sensitive to a depth change than broadband envelope variance — a doubled depth
// is ~4x this line). hop sets the envelope decimation; the envelope sample-rate is sr/hop.
double envLfoLinePower(const std::vector<float>& x, double lfoRateHz, double sr, int hop) noexcept {
    const auto env = rmsEnvelope(x, hop);
    const double envSr = sr / static_cast<double>(hop);
    return goertzelPower(env, lfoRateHz, envSr);
}

double energyAboveBand(const std::vector<float>& x, double f0, double sr,
                       int firstHarmonic, int lastHarmonic) noexcept {
    double acc = 0.0;
    for (int h = firstHarmonic; h <= lastHarmonic; ++h)
        acc += goertzelPower(x, f0 * h, sr);
    return acc;
}

// Pin the pitch-determining oscillator config to a saw at the 8' reference footage so a
// played MIDI note renders at its nominal frequency (independent of any patch/preset default
// the host layout might carry): VCO range index 1 == 8' (the reference, +0 oct), zero
// tune/fine, saw-only source mix. This isolates the BEND under test from the octave/mix
// defaults so the rendered fundamental is the note's nominal Hz.
void pinSawAtReferenceFootage(mw::plugin::MwAudioProcessor& proc) {
    using namespace mw::params::ids;
    setParam(proc, kVcoRange, 1.0f);      // choice index 1 == 8' reference footage
    setParam(proc, kVcoTune, 0.0f);
    setParam(proc, kVcoFine, 0.0f);
    setParam(proc, kSawLevel, 0.8f);      // saw is the audible source
    setParam(proc, kPulseLevel, 0.0f);
    setParam(proc, kSubLevel, 0.0f);      // no sub-oscillator octave-down content
    setParam(proc, kNoiseLevel, 0.0f);
}

// Open the filter wide and isolate the bend (no vibrato / no kbd-track / no env mod / no
// velocity perturbation; envelope held at full sustain), the same isolation the core
// CcIngressTest uses for the VCO-bend headline.
void openFilterIsolateBend(mw::plugin::MwAudioProcessor& proc) {
    using namespace mw::params::ids;
    pinSawAtReferenceFootage(proc);
    setParam(proc, kVelEnable, 0.0f);     // velocity sensing OFF (steady operating point)
    setParam(proc, kEnvSustain, 1.0f);    // hold full level (no decay sag)
    setParam(proc, kVcfCutoff, 1.0f);     // fully open
    setParam(proc, kVcfResonance, 0.0f);
    setParam(proc, kVcfEnvMod, 0.0f);
    setParam(proc, kVcfKbdTrack, 0.0f);
    setParam(proc, kLfoDepthPitch, 0.0f);
    setParam(proc, kLfoDepthCutoff, 0.0f);
}

} // namespace

// ============================================================================
// PITCH-BEND -> VCO (the headline): a HOST pitch-bend with bend_dest=VCO + a non-zero
// bend_range_vco shifts the rendered VCO fundamental UP (bend up) and DOWN (bend down) by the
// range amount, in V/oct (1200 cents == 1 octave), driven through the REAL processor. The
// acceptance is the bend SHIFT RATIO measured on the rendered output (bent / neutral == the
// 1 V/oct bend factor), which is exactly what BlockContext::controllers.pitchBend feeds the
// engine's 162c VCO-bend leg. (We assert the RATIO, not an absolute MIDI Hz: the absolute
// played pitch depends on the engine's note-ingress, owned by the core voice stream — see the
// PR note; the bend factor is the controller-ingress contract this task wires.)
// ============================================================================
TEST_CASE("cc_ingress: a host pitch-bend bends the VCO fundamental in the real plugin",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Render at a MODERATE base pitch (note 60 at the 8' reference footage, no coarse tune) so
    // both bent endpoints stay comfortably inside the estimator band (a +1-octave bend up is
    // ~523 Hz, a -1-octave bend down is ~131 Hz at the ~262 Hz base). The absolute played octave
    // is owned by the core voice stream (out of this task's scope), so we estimate the NEUTRAL
    // fundamental with a wide search and anchor the bent searches to neutral x expected-ratio —
    // octave-agnostic AND harmonic-lock-proof (the assertion is the bend RATIO, not absolute Hz).
    auto bufAtBend = [&](float bendUnit) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        using namespace mw::params::ids;
        openFilterIsolateBend(proc);
        setParam(proc, kVcoRange, 1.0f);             // 8' reference footage (+0 oct)
        setParam(proc, kVcoTune, 0.0f);
        setParam(proc, kModBendDest, 0.0f);          // VCO (choice index 0)
        setParam(proc, kModBendRangeVco, 1200.0f);   // 1 octave at full bend
        return renderHeld(proc, 60, 0.40, bendUnit, /*wheel7=*/-1);
    };

    const double neutral = estimateFundamental(bufAtBend(0.0f), kSr, 60.0, 2000.0);
    REQUIRE(neutral > 0.0);
    const double bentUp = estimateNear(bufAtBend(1.0f),  kSr, neutral * 2.0);   // +1 octave
    const double bentDn = estimateNear(bufAtBend(-1.0f), kSr, neutral * 0.5);   // -1 octave

    // Full bend up == +1 octave (x2) and full bend down == -1 octave (x0.5), measured as the
    // rendered-fundamental ratio — the 1 V/oct bend the live controller drives through the plugin.
    REQUIRE((bentUp / neutral) == Catch::Approx(2.0).epsilon(0.06));
    REQUIRE((bentDn / neutral) == Catch::Approx(0.5).epsilon(0.06));
}

// ============================================================================
// PITCH-BEND SCALES WITH THE WHEEL POSITION (the 162d regression guard — the bug QA caught).
//
// A pitch-bend is PROPORTIONAL to the wheel position: a HALF wheel bends HALF the range, not
// the full range. The earlier 162d suite only probed the FULL-bend endpoints (unit ±1), so it
// passed VACUOUSLY even with the broken core re-read — at full bend the broken value (the
// bend-range-scaled SEMITONE forwarded on the PitchBend MidiEvent, clamped to a [-1,+1] unit)
// COINCIDENTALLY lands at full bend too. A half wheel is the discriminating probe.
//
// THE BUG: core/Engine.cpp renderChunk used to re-read the forwarded PitchBend MidiEvent's
// `value` as if it were a [-1,+1] unit — but plugin/midi/MidiFrontEnd forwards `value = semis`
// (the wheel unit x the channel bend range, default ±2 semitones, for the §4.4 Pre-Q path).
// So a HALF wheel (unit 0.5) became semis = 0.5 x 2 = 1.0, which clamped to the unit 1.0 and
// rendered a FULL bend. At a 1200-cent VCO range that is a FULL OCTAVE (x2.0) for a half wheel
// instead of the correct HALF octave (x sqrt2 ~ 1.414). This test asserts the half-octave and
// would FAIL at x2.0 — the exact half-bend probe QA used to reject the vacuous suite.
//
// The fix: the bend authority is BlockContext::controllers.pitchBend (the centered [-1,+1] unit
// the processor seeds each block), NOT the semitone-valued PitchBend MidiEvent. So the wheel
// scales linearly and a half wheel is a half bend.
// ============================================================================
TEST_CASE("cc_ingress: a half host pitch-bend renders a half bend not a full octave in the real plugin",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Moderate base pitch (note 60 at 8'); at a 1200-cent range a half wheel is +half an octave
    // (~370 Hz) and a full wheel +1 octave (~523 Hz) — both well inside the band. Anchor each
    // bent search to neutral x expected-ratio (octave-agnostic; the bend RATIO is the assertion).
    auto bufAtBend = [&](float bendUnit) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        using namespace mw::params::ids;
        openFilterIsolateBend(proc);
        setParam(proc, kVcoRange, 1.0f);             // 8' reference footage (+0 oct)
        setParam(proc, kVcoTune, 0.0f);
        setParam(proc, kModBendDest, 0.0f);          // VCO (choice index 0)
        setParam(proc, kModBendRangeVco, 1200.0f);   // 1 octave at FULL bend (so half bend == sqrt2)
        return renderHeld(proc, 60, 0.40, bendUnit, /*wheel7=*/-1);
    };

    const double kHalfOctave = std::pow(2.0, 0.5);   // ~1.41421

    const double neutral = estimateFundamental(bufAtBend(0.0f), kSr, 60.0, 2000.0);
    REQUIRE(neutral > 0.0);
    const double halfUp   = estimateNear(bufAtBend(0.5f),  kSr, neutral * kHalfOctave);
    const double fullUp   = estimateNear(bufAtBend(1.0f),  kSr, neutral * 2.0);
    const double halfDown = estimateNear(bufAtBend(-0.5f), kSr, neutral / kHalfOctave);

    // A HALF wheel at a 1200-cent range bends HALF an octave: x 2^0.5 (~1.414), NOT a full
    // octave (x2.0). This is the assertion the old semitone-as-unit re-read FAILS: it would
    // render x2.0 here (the clamped-to-full-unit bug). sqrt2 vs 2.0 is a ~40% gap — far outside
    // any estimator tolerance, so this is an unambiguous negative control.
    REQUIRE((halfUp   / neutral) == Catch::Approx(kHalfOctave).epsilon(0.06));
    REQUIRE((halfDown / neutral) == Catch::Approx(1.0 / kHalfOctave).epsilon(0.06));

    // The half bend must be STRICTLY LESS than the full bend (proves proportionality, not a
    // clamp to full): the old bug collapsed both half and full onto the same full-octave value.
    REQUIRE((fullUp / neutral) == Catch::Approx(2.0).epsilon(0.06));
    REQUIRE((halfUp / neutral) < (fullUp / neutral) * 0.85);   // clearly below the full bend
}

// ============================================================================
// PITCH-BEND IS SEMITONE-ACCURATE AT A REALISTIC RANGE (the task's worked example): with the
// VCO bend range at +-2 semitones (200 cents), a FULL wheel bends +2 semitones (x2^(2/12) ~
// 1.122) and a HALF wheel bends +1 semitone (x2^(1/12) ~ 1.0595). The OLD bug rendered the half
// wheel as a FULL +2-semitone bend (x1.122) — so the correct half value (x1.0595) is the probe
// that distinguishes the fix from the bug at this realistic range too.
// ============================================================================
TEST_CASE("cc_ingress: a host pitch-bend is semitone-accurate at a plus-or-minus-two-semitone range in the real plugin",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const double kTwoSemi = std::pow(2.0, 2.0 / 12.0);   // ~1.1225
    const double kOneSemi = std::pow(2.0, 1.0 / 12.0);   // ~1.0595

    auto bufAtBend = [&](float bendUnit) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        using namespace mw::params::ids;
        openFilterIsolateBend(proc);
        setParam(proc, kVcoRange, 1.0f);            // 8' reference footage (+0 oct)
        setParam(proc, kVcoTune, 0.0f);
        setParam(proc, kModBendDest, 0.0f);         // VCO
        setParam(proc, kModBendRangeVco, 200.0f);   // +-2 semitones (the channel-bend default range)
        return renderHeld(proc, 60, 0.50, bendUnit, /*wheel7=*/-1);
    };

    const double neutral = estimateFundamental(bufAtBend(0.0f), kSr, 60.0, 2000.0);
    REQUIRE(neutral > 0.0);
    // Tight search bands around the expected fundamental (the bends are < a semitone apart at
    // this range, so a narrow band keeps the estimator on the right bin for a precise ratio).
    const double full = estimateNear(bufAtBend(1.0f), kSr, neutral * kTwoSemi, /*band=*/1.05);  // +2 semis
    const double half = estimateNear(bufAtBend(0.5f), kSr, neutral * kOneSemi, /*band=*/1.05);  // +1 semi

    // Full wheel == +2 semitones; half wheel == +1 semitone. The old bug rendered the half wheel
    // as the FULL +2-semitone bend (x1.1225), so the half-bend assertion is the discriminator.
    REQUIRE((full / neutral) == Catch::Approx(kTwoSemi).epsilon(0.025));
    REQUIRE((half / neutral) == Catch::Approx(kOneSemi).epsilon(0.025));
    // Guard the discriminator explicitly: the half-bend ratio sits CLOSER to +1 semitone than to
    // the old-bug +2-semitone value, so the test cannot pass under the semitone-as-unit re-read.
    REQUIRE(std::abs((half / neutral) - kOneSemi) < std::abs((half / neutral) - kTwoSemi));
}

// ============================================================================
// PITCH-BEND -> VCF: a host bend with bend_dest=VCF opens the filter (brightness rises) WITHOUT
// moving the VCO fundamental — proving the dest switch routes the live bend through the plugin.
// ============================================================================
TEST_CASE("cc_ingress: a host pitch-bend with bend_dest VCF opens the filter not the pitch in the real plugin",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // The actual rendered fundamental (the engine's note-ingress octave is owned by the core
    // voice stream; we measure whatever the saw plays and assert the bend does NOT move it).
    double baseFund = 0.0;

    auto measure = [&](float bendUnit, double& fundOut, double& brightOut) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        using namespace mw::params::ids;
        pinSawAtReferenceFootage(proc);
        setParam(proc, kVcoRange, 2.0f);             // 4' == +1 octave (audible harmonic content)
        setParam(proc, kVcoTune, 12.0f);
        setParam(proc, kVelEnable, 0.0f);
        setParam(proc, kEnvSustain, 1.0f);
        setParam(proc, kModBendDest, 1.0f);          // VCF (choice index 1)
        setParam(proc, kModBendRangeVcf, 1200.0f);   // 1 octave of cutoff at full bend
        setParam(proc, kModBendRangeVco, 0.0f);      // VCO range irrelevant (dest=VCF)
        setParam(proc, kVcfCutoff, 0.4f);            // partly closed so opening shows
        setParam(proc, kVcfResonance, 0.3f);
        setParam(proc, kVcfEnvMod, 0.0f);
        setParam(proc, kVcfKbdTrack, 0.0f);
        setParam(proc, kLfoDepthPitch, 0.0f);
        setParam(proc, kLfoDepthCutoff, 0.0f);
        auto buf = renderHeld(proc, 45, 0.40, bendUnit, /*wheel7=*/-1);
        const double f = estimateFundamental(buf, kSr, 30.0, 4000.0);
        if (baseFund == 0.0) baseFund = f;
        fundOut   = f;
        brightOut = energyAboveBand(buf, baseFund, kSr, 4, 16);
    };

    double fundNeutral = 0.0, brightNeutral = 0.0;
    double fundUp = 0.0, brightUp = 0.0;
    measure(0.0f, fundNeutral, brightNeutral);
    measure(1.0f, fundUp, brightUp);

    REQUIRE(fundUp == Catch::Approx(fundNeutral).epsilon(0.03));   // VCF bend does NOT move the pitch
    REQUIRE(brightNeutral > 0.0);
    REQUIRE(brightUp > brightNeutral * 1.5);                       // filter opens (brightness rises)
}

// ============================================================================
// MOD-WHEEL (CC1) -> LFO DEPTH: with mod.lfo_mod_wheel routed and lfo.dest=Filter, raising the
// HOST mod wheel RAISES the LFO->filter wobble depth (the AC energy of the amplitude envelope
// rises). Wheel down (0) is the routed-but-neutral baseline. This is the audible mod-wheel
// sweep that was INERT in the plugin before this task.
// ============================================================================
TEST_CASE("cc_ingress: a host mod-wheel move raises the LFO modulation depth in the real plugin",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double kLfoRateHz = 5.0;

    auto lfoDepthAtWheel = [&](int wheel7) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        using namespace mw::params::ids;
        pinSawAtReferenceFootage(proc);
        setParam(proc, kVcoRange, 2.0f);           // 4' == +1 octave (audible harmonic content)
        setParam(proc, kVcoTune, 12.0f);           // +12 semitones (lift into the filter's band)
        setParam(proc, kVelEnable, 0.0f);          // velocity sensing OFF: isolate the LFO wobble
        setParam(proc, kEnvSustain, 1.0f);         // hold full level (no decay sag)
        setParam(proc, kLfoDest, 1.0f);            // Filter (choice index 1)
        setParam(proc, mw::params::ids::kLfoRate, (float) kLfoRateHz);
        setParam(proc, kLfoDepthCutoff, 0.15f);    // modest base depth (headroom for the boost)
        setParam(proc, kLfoDepthPitch, 0.0f);
        setParam(proc, kLfoDelay, 0.0f);
        setParam(proc, kModLfoModWheel, 1.0f);     // full wheel->LFO routing
        setParam(proc, kVcfCutoff, 0.35f);
        setParam(proc, kVcfResonance, 0.7f);
        setParam(proc, kVcfEnvMod, 0.0f);
        setParam(proc, kVcfKbdTrack, 0.0f);
        auto buf = renderHeld(proc, 60, 1.0, /*bend=*/0.0f, wheel7);
        return envLfoLinePower(buf, kLfoRateHz, kSr, /*hop=*/64);
    };

    const double wheelDown = lfoDepthAtWheel(0);     // routed-but-neutral baseline
    const double wheelUp   = lfoDepthAtWheel(127);   // deeper LFO wobble
    REQUIRE(wheelDown > 0.0);                         // base LFO depth still wobbles
    REQUIRE(wheelUp > wheelDown * 2.0);              // the wheel deepens the modulation audibly
}

// ============================================================================
// NO DOUBLE-APPLY: with mod.lfo_mod_wheel routing == 0, the host mod wheel has NO effect on
// the LFO depth. This proves CC1 drives the controller-ingress modWheel ONLY (the 162c
// multiplier, which is gated by the routing knob) and is NOT ALSO written into the
// mod.lfo_mod_wheel ParamValue via the CcLearnMap (which would change the wobble even with
// the routing knob at zero). The wheel and the routing param BOTH gate the boost.
// ============================================================================
TEST_CASE("cc_ingress: the host mod-wheel does not double-apply when mod.lfo_mod_wheel routing is zero",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double kLfoRateHz = 5.0;

    auto lfoDepthAtWheel = [&](int wheel7) {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        using namespace mw::params::ids;
        pinSawAtReferenceFootage(proc);
        setParam(proc, kVcoRange, 2.0f);           // 4' == +1 octave (match the routed test)
        setParam(proc, kVcoTune, 12.0f);
        setParam(proc, kVelEnable, 0.0f);          // velocity sensing OFF: isolate the LFO wobble
        setParam(proc, kEnvSustain, 1.0f);
        setParam(proc, kLfoDest, 1.0f);            // Filter
        setParam(proc, mw::params::ids::kLfoRate, (float) kLfoRateHz);
        setParam(proc, kLfoDepthCutoff, 0.15f);
        setParam(proc, kLfoDepthPitch, 0.0f);
        setParam(proc, kLfoDelay, 0.0f);
        setParam(proc, kModLfoModWheel, 0.0f);     // NO wheel->LFO routing
        setParam(proc, kVcfCutoff, 0.35f);
        setParam(proc, kVcfResonance, 0.7f);
        setParam(proc, kVcfEnvMod, 0.0f);
        setParam(proc, kVcfKbdTrack, 0.0f);
        auto buf = renderHeld(proc, 60, 1.0, /*bend=*/0.0f, wheel7);
        return envLfoLinePower(buf, kLfoRateHz, kSr, /*hop=*/64);
    };

    const double wheelDown = lfoDepthAtWheel(0);
    const double wheelUp   = lfoDepthAtWheel(127);
    // No routing: the wheel does not change the LFO modulation depth — proving CC1 did NOT leak
    // into the mod.lfo_mod_wheel param (it only drives the routing-gated controller ingress, and
    // with the routing knob at zero the boost stays at the wheel-down identity). A small tolerance
    // absorbs the de-zipper/PRNG render jitter; a double-apply would land far outside it (the
    // routed test shows a >2x line, so leakage would be unmistakable here).
    REQUIRE(wheelDown > 0.0);
    REQUIRE(wheelUp == Catch::Approx(wheelDown).epsilon(0.10));
}

// ============================================================================
// NEUTRAL identity: a centered bend (0) AND a wheel-down (0) render is the no-controller
// identity — full routing set, but with the controllers at rest the rendered output equals
// the render with NO controller MIDI at all (the seam adds nothing at rest).
// ============================================================================
TEST_CASE("cc_ingress: a centered host bend and zero mod-wheel are the no-controller identity in the plugin",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    auto configure = [&](mw::plugin::MwAudioProcessor& proc) {
        using namespace mw::params::ids;
        setParam(proc, kModBendDest, 2.0f);          // Both (VCO + VCF)
        setParam(proc, kModBendRangeVco, 1200.0f);
        setParam(proc, kModBendRangeVcf, 1200.0f);
        setParam(proc, kModLfoModWheel, 1.0f);       // full routing
        setParam(proc, kLfoDest, 1.0f);
        setParam(proc, kLfoDepthCutoff, 0.5f);
    };

    // Controllers explicitly at rest (centered bend, wheel down 0).
    mw::plugin::MwAudioProcessor procA;
    procA.prepareToPlay(kSr, kBlockSize);
    configure(procA);
    auto withRest = renderHeld(procA, 62, 0.20, /*bend=*/0.0f, /*wheel7=*/0);

    // No controller MIDI at all (only the note).
    auto renderNoControllers = [&]() {
        mw::plugin::MwAudioProcessor proc;
        proc.prepareToPlay(kSr, kBlockSize);
        configure(proc);

        juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);
        buffer.clear();
        {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100), 0);
            proc.processBlock(buffer, m);
        }
        for (int b = 1; b < 8; ++b) {
            buffer.clear();
            juce::MidiBuffer m;
            proc.processBlock(buffer, m);
        }
        const int total = static_cast<int>(0.20 * kSr);
        std::vector<float> out; out.reserve(static_cast<std::size_t>(total) + kBlockSize);
        int rendered = 0;
        while (rendered < total) {
            buffer.clear();
            juce::MidiBuffer m;
            proc.processBlock(buffer, m);
            const float* l = buffer.getReadPointer(0);
            for (int i = 0; i < kBlockSize && rendered < total; ++i, ++rendered)
                out.push_back(l[i]);
        }
        return removeDc(out);   // match renderHeld's DC handling for the sample-equality compare
    };
    auto noControllers = renderNoControllers();

    REQUIRE(withRest.size() == noControllers.size());
    for (std::size_t i = 0; i < withRest.size(); ++i)
        REQUIRE(withRest[i] == Catch::Approx(noControllers[i]).margin(1.0e-6));
}

// ============================================================================
// WIRING (direct): a host PitchBend + CC1 MIDI move reaches BlockContext::controllers through
// the real processor. This is the seam this task wires — the audible bend/wheel tests above
// prove the EFFECT; this proves the controller POSITION the processor feeds the engine each
// block is the centered-unit bend [-1,+1] and the [0,1] mod-wheel (the 162c contract). It also
// guards the HOLD-across-blocks behavior: a block with no controller message keeps the last
// position (a real wheel does not snap back).
// ============================================================================
TEST_CASE("cc_ingress: a host pitch-bend and CC1 reach the controller seam through the real processor",
          "[cc_ingress]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    mw::plugin::MwAudioProcessor proc;
    proc.prepareToPlay(kSr, kBlockSize);

    juce::AudioBuffer<float> buffer(kNumOut, kBlockSize);

    // Block 1: a +0.5 bend (wheel right of center) + CC1 at 100/127.
    buffer.clear();
    {
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        m.addEvent(juce::MidiMessage::pitchWheel(1, wheelForUnit(0.5f)), 0);
        m.addEvent(juce::MidiMessage::controllerEvent(1, /*cc=*/1, 100), 0);
        proc.processBlock(buffer, m);
    }
    REQUIRE(proc.lastBendUnitForTest() == Catch::Approx(0.5f).margin(1.0e-2));
    REQUIRE(proc.lastModWheelForTest() == Catch::Approx(100.0f / 127.0f).margin(1.0e-3));

    // Block 2: NO controller messages — the held position persists (no snap-back).
    buffer.clear();
    {
        juce::MidiBuffer m;   // empty
        proc.processBlock(buffer, m);
    }
    REQUIRE(proc.lastBendUnitForTest() == Catch::Approx(0.5f).margin(1.0e-2));
    REQUIRE(proc.lastModWheelForTest() == Catch::Approx(100.0f / 127.0f).margin(1.0e-3));

    // Block 3: a full-down bend (-1) + wheel up (127) update the held position.
    buffer.clear();
    {
        juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::pitchWheel(1, wheelForUnit(-1.0f)), 0);
        m.addEvent(juce::MidiMessage::controllerEvent(1, /*cc=*/1, 127), 0);
        proc.processBlock(buffer, m);
    }
    REQUIRE(proc.lastBendUnitForTest() == Catch::Approx(-1.0f).margin(1.0e-2));
    REQUIRE(proc.lastModWheelForTest() == Catch::Approx(1.0f).margin(1.0e-3));
}

