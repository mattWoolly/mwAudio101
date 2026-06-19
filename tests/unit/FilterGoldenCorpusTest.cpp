// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/FilterGoldenCorpusTest.cpp — the FILTER CLASS-FP golden corpus, the
// EARLY freeze gate (task 048, golden-filter-corpus).
//
// Test-case names begin with "golden" AND contain "class-fp" so BOTH `ctest -R golden`
// and `ctest -R class-fp` select them (the silent-pass selector-hygiene rule, AGENTS.md
// / docs/design/11 §8.3; the acceptance verify command is `ctest -R class-fp`). Display
// names avoid '[' so Catch2 does not mis-parse a tag out of the name.
//
// What this realizes [docs/design/11 §5.1 (determinism-class partition), §5.2 (blessed
// sample-rate set), §4.2 (the k>=4 self-osc / k=3.9 silence paired control); ADR-013 C6
// (arm64 bit-exact, a 1-ULP diff FAILS), C4 (paired positive/negative property control);
// ADR-023 V12 (a golden at each blessed rate, keyed by sample rate)]:
//
//  - The corpus is authored from THREE filter stimuli rendered through the SHIPPING
//    ladder nonlinear path (the Huovilainen IR3109 cascade, mw::dsp::LadderFilter): a
//    self-oscillation ring, a cutoff sweep, and a resonance sweep. The ladder nonlinear
//    path is CLASS-FP (transcendental tanh) [docs/design/11 §5.1], so the corpus is
//    CLASS-FP, keyed and compared with the existing Layer-2 primitives (GoldenKey /
//    CompareFp / Sha256) — the filter DSP itself is consumed opaque (task 048 Out of
//    scope).
//
//  - A blessed filter golden exists at EACH of {44100, 48000, 88200, 96000} Hz, keyed by
//    sample rate via makeGoldenKey() (which REFUSES a non-blessed rate). The four keys
//    are distinct and the four blobs are non-trivial. [ADR-023 V12; §5.2]
//
//  - On arm64 the blessed golden is bit-exact: a re-render equals the blessed blob
//    sample-for-sample (CompareFp arm64 tolerance maxAbsErr==0 PASSES), and a deliberate
//    1-ULP perturbation FAILS — paired. [ADR-013 C6]
//
//  - The self-osc stimulus encodes the k>=4 self-oscillation vs k=3.9 silence
//    distinction, proved against the offline linear TPT oracle (LadderReferenceTPT,
//    whose k axis IS the normalized continuous-model self-osc onset at k=4): the settled
//    RMS at k=4 is above the floor (oscillating), at k=3.9 below it (silent) — the
//    canonical paired control of docs/design/11 §4.2 that a stubbed-to-constant filter
//    cannot satisfy. [docs/design/11 §4.2; docs/research/10 §3.4; ADR-013 C4]
//
// This harness validates self-consistency and topology-faithfulness, NOT measured
// SH-101 fidelity [docs/design/11 §1.3; ADR-013 owner ratification]. The bless tool
// (arm64-only, MANIFEST-appending, task 045) and the MANIFEST entries are owned
// elsewhere; this corpus authoring exercises the render + key + compare contract.
//
// Tagged [golden] + [class-fp] + [vcf] (all already in the committed labels snapshot)
// plus the NEW [filter-golden] corpus tag the orchestrator picks up at wave integration
// — an EXPECTED, transient red on the labels_snapshot test ONLY, never on the scoped
// -R class-fp / -R golden selection [task 048; docs/design/11 §8.4; ADR-013 C3].

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "../../core/calibration/FilterGoldenCorpusConstants.h"
#include "../../core/calibration/GoldenKeyConstants.h"
#include "../golden/CompareFp.h"
#include "../golden/GoldenKey.h"

#include "dsp/LadderFilter.h"
#include "dsp/LadderReferenceTPT.h"

namespace {

using mw::golden::compareFp;
using mw::golden::DeterminismClass;
using mw::golden::EngineTag;
using mw::golden::FpResult;
using mw::golden::FpTolerance;
using mw::golden::GoldenKey;
using mw::golden::LadderEngine;
using mw::golden::makeGoldenKey;
using mw::golden::RenderResult;

namespace fc = mw::cal::golden::filter;

constexpr double kTwoPi = 6.283185307179586476925286766559;

// The corpus engine tag: the shipping Huovilainen ladder at the blessed 2x oversample
// factor, at the CURRENT renderVersion (1, per tests/golden/corpus/MANIFEST.toml) — the
// context this corpus is blessed under [docs/design/11 §5.3; ADR-023 V11].
EngineTag corpusEngine() noexcept {
    return EngineTag{LadderEngine::Huovilainen, /*oversampleFactor=*/2, /*renderVersion=*/1};
}

// The three filter stimuli the corpus is authored from. Each renders through the
// SHIPPING nonlinear ladder (mw::dsp::LadderFilter) at the OVERSAMPLED rate (2x the host
// blessed rate — the per-voice 2x nonlinear zone, docs/design/00 §4.1), then captures
// every Nth oversampled sample so the blob length is kStimulusFrames at the host rate.
enum class Stimulus { SelfOsc, CutoffSweep, ResonanceSweep };

// Deterministic offline render: identical (stimulus, hostRate) -> identical bytes on the
// same platform; this is the corpus RenderHarness for the filter (docs/design/11 §5.4).
RenderResult renderFilterStimulus(Stimulus stim, double hostRateHz) {
    const int    factor = 2;                       // blessed 2x oversample (corpusEngine)
    const double fsOs   = hostRateHz * factor;
    const int    nOs    = fc::kStimulusFrames * factor;

    mw::dsp::LadderFilter f;
    f.prepare(fsOs, /*maxBlockOs=*/64);
    f.reset();

    std::vector<float> os;
    os.reserve(static_cast<std::size_t>(nOs));

    switch (stim) {
        case Stimulus::SelfOsc: {
            // A one-shot kick, then silent input: the steady tail is genuine self-
            // oscillation at full resonance (docs/design/02 §5.6).
            f.setCutoffHz(fc::kSelfOscFcHz);
            f.setResonance(1.0f);
            for (int n = 0; n < nOs; ++n) {
                const float in = (n < 4) ? fc::kSelfOscKick : 0.0f;
                os.push_back(f.processSample(in));
            }
            break;
        }
        case Stimulus::CutoffSweep: {
            // A fixed excitation tone with a sub-self-oscillation resonance while cutoff
            // sweeps geometrically lo->hi across the blob.
            f.setResonance(0.5f);
            const double w = kTwoPi * static_cast<double>(fc::kSweepExciteHz) / fsOs;
            const double ratio =
                std::log(static_cast<double>(fc::kSweepFcHiHz) /
                         static_cast<double>(fc::kSweepFcLoHz));
            for (int n = 0; n < nOs; ++n) {
                const double t  = static_cast<double>(n) / static_cast<double>(nOs - 1);
                const float  fcHz = static_cast<float>(
                    static_cast<double>(fc::kSweepFcLoHz) * std::exp(ratio * t));
                f.setCutoffHz(fcHz);
                const float in = fc::kSweepExciteAmp *
                                 static_cast<float>(std::sin(w * static_cast<double>(n)));
                os.push_back(f.processSample(in));
            }
            break;
        }
        case Stimulus::ResonanceSweep: {
            // A fixed cutoff and excitation while resonance ramps 0 -> 1, traversing from
            // a near-linear lowpass up into the self-oscillating regime.
            f.setCutoffHz(fc::kResoSweepFcHz);
            const double w = kTwoPi * static_cast<double>(fc::kSweepExciteHz) / fsOs;
            for (int n = 0; n < nOs; ++n) {
                const float reso = static_cast<float>(
                    static_cast<double>(n) / static_cast<double>(nOs - 1));
                f.setResonance(reso);
                const float in = fc::kSweepExciteAmp *
                                 static_cast<float>(std::sin(w * static_cast<double>(n)));
                os.push_back(f.processSample(in));
            }
            break;
        }
    }

    // Decimate by the factor (take every `factor`-th oversampled sample) so the blob is
    // kStimulusFrames at the host rate. Pure index selection -> deterministic.
    RenderResult r{};
    r.sampleRate = hostRateHz;
    r.engine     = corpusEngine();
    r.samples.reserve(static_cast<std::size_t>(fc::kStimulusFrames));
    for (int n = 0; n < fc::kStimulusFrames; ++n)
        r.samples.push_back(os[static_cast<std::size_t>(n * factor)]);
    return r;
}

// The arm64 bless tolerance: bit-exact (maxAbsErr == 0) [docs/design/11 §6.3; ADR-013 C6].
FpTolerance arm64Tolerance() noexcept {
    FpTolerance t{};
    t.maxAbsErr           = 0.0;     // bit-exact gate
    t.rmsErr              = 0.0;
    t.nmseDbCeiling       = -120.0;
    t.aliasFloorDbCeiling = -60.0;
    return t;
}

// RMS over the SETTLED tail of a buffer (leading transient excluded), the §4.2
// "rmsAfterSettle" the self-osc paired control is expressed in.
double rmsAfterSettle(const std::vector<double>& x, int settleFrames) noexcept {
    int from = static_cast<int>(x.size()) - settleFrames;
    if (from < 0) from = 0;
    double acc = 0.0;
    int n = 0;
    for (int i = from; i < static_cast<int>(x.size()); ++i) {
        acc += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
        ++n;
    }
    return n ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
}

// Self-oscillation tail of the OFFLINE LINEAR TPT ORACLE at a given normalized loop gain
// k. The oracle's k axis is exactly the normalized continuous-model self-osc onset at
// k = 4 (LadderReferenceTPT::setResonanceK clamps to [0,4)) [docs/research/10 §3.4]. A
// one-shot kick then silence: the tail is genuine self-oscillation iff k is at/over onset.
double tptSelfOscTailRms(double k, double hostRateHz) {
    mw::dsp::LadderReferenceTPT t;
    t.prepare(hostRateHz);
    t.setCutoffHz(static_cast<double>(fc::kSelfOscFcHz));
    t.setResonanceK(k);
    t.reset();
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(fc::kStimulusFrames));
    for (int n = 0; n < fc::kStimulusFrames; ++n) {
        const double in = (n < 4) ? static_cast<double>(fc::kSelfOscKick) : 0.0;
        out.push_back(t.processSample(in));
    }
    return rmsAfterSettle(out, fc::kSelfOscSettleFrames);
}

// Peak |sample| of a render (a render is "non-trivial" if it carries real energy).
double peakAbs(const std::vector<float>& x) noexcept {
    double p = 0.0;
    for (float v : x) p = std::max(p, std::abs(static_cast<double>(v)));
    return p;
}

} // namespace

// === Acceptance: a blessed filter golden at EACH blessed rate, keyed by sample rate ===
// [ADR-023 V12; docs/design/11 §5.2]
TEST_CASE("golden: class-fp filter corpus has a blessed golden at each blessed sample "
          "rate keyed by sample rate, all distinct and non-trivial",
          "[golden][class-fp][vcf][filter-golden]") {
    const auto eng = corpusEngine();

    std::vector<GoldenKey> keys;
    std::vector<std::uint64_t> keyHashes;

    for (double rate : mw::cal::golden::kBlessedSampleRatesHz) {
        // makeGoldenKey REFUSES a non-blessed rate; here every rate is in the set, so it
        // constructs. The blob hash (renderGraphHash) folds the self-osc render bytes so
        // the key is bound to the artifact it is blessed from.
        const RenderResult blob = renderFilterStimulus(Stimulus::SelfOsc, rate);
        REQUIRE(blob.sampleRate == rate);                 // keyed BY sample rate
        REQUIRE(peakAbs(blob.samples) > fc::kSelfOscRmsFloor);  // non-trivial (real energy)

        const std::uint64_t graphHash =
            mw::golden::hash(makeGoldenKey(/*renderGraphHash=*/0u, eng, rate,
                                           /*blockSize=*/512, /*seed=*/4242,
                                           DeterminismClass::Fp));
        const GoldenKey key = makeGoldenKey(graphHash, eng, rate,
                                            /*blockSize=*/512, /*seed=*/4242,
                                            DeterminismClass::Fp);
        REQUIRE(mw::golden::isBlessedSampleRate(key.sampleRate));
        REQUIRE(key.cls == DeterminismClass::Fp);         // ladder nonlinear path -> CLASS-FP
        keys.push_back(key);
        keyHashes.push_back(mw::golden::hash(key));
    }

    REQUIRE(keys.size() == mw::cal::golden::kBlessedSampleRatesHz.size());   // one per rate
    REQUIRE(keys.size() == 4u);

    // The four keys are DISTINCT (differ in sample rate -> differ in hash): a corpus
    // keyed by sample rate cannot collide two rates into one golden [§5.2].
    for (std::size_t i = 0; i < keyHashes.size(); ++i)
        for (std::size_t j = i + 1; j < keyHashes.size(); ++j)
            REQUIRE(keyHashes[i] != keyHashes[j]);

    // Paired negative control: a non-blessed rate is REFUSED at key construction (a
    // golden may only be keyed to a blessed rate) [ADR-023 V12/V14/V17; §5.2].
    REQUIRE_FALSE(mw::golden::isBlessedSampleRate(44101.0));
    REQUIRE_THROWS_AS(makeGoldenKey(0u, eng, 44101.0, 512, 4242, DeterminismClass::Fp),
                      std::invalid_argument);
}

// === Acceptance: arm64 bit-exact; a deliberate 1-ULP perturbation FAILS (paired) ======
// [ADR-013 C6]
TEST_CASE("golden: class-fp filter corpus is bit-exact on arm64 and a 1-ULP "
          "perturbation FAILS the blessed compare (paired)",
          "[golden][class-fp][vcf][filter-golden]") {
    const FpTolerance arm64 = arm64Tolerance();

    for (double rate : mw::cal::golden::kBlessedSampleRatesHz) {
        // "Bless" = render the self-osc stimulus once; the blessed reference blob.
        const RenderResult blessed = renderFilterStimulus(Stimulus::SelfOsc, rate);
        REQUIRE(blessed.samples.size() == static_cast<std::size_t>(fc::kStimulusFrames));

        // A fresh re-render is bit-identical to the blessed blob -> arm64 bit-exact PASS.
        const RenderResult reRender = renderFilterStimulus(Stimulus::SelfOsc, rate);
        const FpResult exact = compareFp(reRender, blessed, arm64, /*full=*/false);
        INFO("rate = " << rate << " re-render maxAbsErr = " << exact.s1.maxAbsErr);
        REQUIRE_FALSE(exact.refused);
        REQUIRE(exact.pass);                              // positive: re-render bit-exact
        REQUIRE(exact.s1.maxAbsErr == 0.0);

        // Paired negative control: perturb exactly ONE sample by ONE ULP -> the bit-exact
        // gate (maxAbsErr == 0) FAILS [ADR-013 C6].
        RenderResult perturbed = blessed;
        float& v = perturbed.samples[fc::kStimulusFrames / 2];
        v = std::nextafter(v, std::numeric_limits<float>::infinity());
        REQUIRE(v != blessed.samples[fc::kStimulusFrames / 2]);  // sanity: it changed

        const FpResult oneUlp = compareFp(perturbed, blessed, arm64, /*full=*/false);
        REQUIRE_FALSE(oneUlp.refused);
        REQUIRE_FALSE(oneUlp.pass);                       // 1-ULP diff FAILS bit-exact
        REQUIRE(oneUlp.s1.maxAbsErr > 0.0);
    }
}

// === Acceptance: the cutoff- and resonance-sweep stimuli are also bit-exact-stable =====
// (the full three-stimulus corpus, each blessed CLASS-FP) [docs/design/11 §5.1; ADR-013 C6]
TEST_CASE("golden: class-fp filter corpus cutoff-sweep and resonance-sweep stimuli are "
          "deterministic and bit-exact on re-render",
          "[golden][class-fp][vcf][filter-golden]") {
    const FpTolerance arm64 = arm64Tolerance();

    for (double rate : {48000.0, 96000.0}) {   // a base rate and a top blessed rate
        for (Stimulus stim : {Stimulus::CutoffSweep, Stimulus::ResonanceSweep}) {
            const RenderResult blessed = renderFilterStimulus(stim, rate);
            REQUIRE(peakAbs(blessed.samples) > fc::kSelfOscRmsFloor);   // non-trivial sweep

            const RenderResult reRender = renderFilterStimulus(stim, rate);
            const FpResult exact = compareFp(reRender, blessed, arm64, /*full=*/false);
            REQUIRE(exact.pass);                          // re-render bit-exact
            REQUIRE(exact.s1.maxAbsErr == 0.0);

            // Paired negative control: a 1-ULP perturbation FAILS the bit-exact gate.
            RenderResult perturbed = blessed;
            float& v = perturbed.samples[fc::kStimulusFrames / 3];
            v = std::nextafter(v, std::numeric_limits<float>::infinity());
            const FpResult oneUlp = compareFp(perturbed, blessed, arm64, /*full=*/false);
            REQUIRE_FALSE(oneUlp.pass);
            REQUIRE(oneUlp.s1.maxAbsErr > 0.0);
        }
    }
}

// === Acceptance: the self-osc stimulus encodes k>=4 self-osc vs k=3.9 silence ==========
// The canonical paired positive/negative control of docs/design/11 §4.2, proved against
// the offline linear TPT oracle whose k axis IS the normalized self-osc onset at k = 4
// [docs/design/11 §4.2; docs/research/10 §3.4; ADR-013 C4].
TEST_CASE("golden: class-fp filter self-osc stimulus encodes the k>=4 self-oscillation "
          "vs k=3.9 silence distinction at every blessed rate",
          "[golden][class-fp][vcf][filter-golden]") {
    for (double rate : mw::cal::golden::kBlessedSampleRatesHz) {
        // Positive: at the onset k = 4 the oracle sustains -> settled RMS ABOVE the floor.
        const double rmsOsc = tptSelfOscTailRms(/*k=*/4.0, rate);
        // Negative control: just below onset k = 3.9 decays -> settled RMS BELOW the floor.
        const double rmsSilent = tptSelfOscTailRms(/*k=*/3.9, rate);

        INFO("rate = " << rate << "  rms(k=4) = " << rmsOsc
             << "  rms(k=3.9) = " << rmsSilent << "  floor = " << fc::kSelfOscRmsFloor);
        REQUIRE(rmsOsc    > fc::kSelfOscRmsFloor);        // self-oscillates at k>=4
        REQUIRE(rmsSilent < fc::kSelfOscRmsFloor);        // silent at k=3.9 (negative)
        // And the distinction is unambiguous, not a knife-edge: oscillating is at least
        // an order of magnitude above the silent reading (a stub returning a constant
        // would make these equal and fail) [ADR-013 C4].
        REQUIRE(rmsOsc > rmsSilent * 10.0);
    }

    // The SHIPPING ladder nonlinear path the corpus is actually authored from also self-
    // oscillates at full resonance (its settled tail carries real energy above the floor),
    // so the blessed self-osc blobs encode genuine self-oscillation, not a decayed kick.
    for (double rate : mw::cal::golden::kBlessedSampleRatesHz) {
        const RenderResult blob = renderFilterStimulus(Stimulus::SelfOsc, rate);
        std::vector<double> tail;
        tail.reserve(blob.samples.size());
        for (float s : blob.samples) tail.push_back(static_cast<double>(s));
        const double rms = rmsAfterSettle(tail, fc::kSelfOscSettleFrames);
        INFO("shipping ladder self-osc rate = " << rate << " settled RMS = " << rms);
        REQUIRE(rms > fc::kSelfOscRmsFloor);              // genuine self-oscillation
    }
}
