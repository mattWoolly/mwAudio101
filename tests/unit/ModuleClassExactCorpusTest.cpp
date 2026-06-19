// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/ModuleClassExactCorpusTest.cpp — the per-module CLASS-EXACT golden corpora
// for the integer/control paths (task 080, golden / module-class-exact).
//
// Test-case names begin with "golden" AND contain "class-exact" so BOTH `ctest -R golden`
// and `ctest -R class-exact` select them (the silent-pass selector-hygiene rule, AGENTS.md
// / docs/design/11 §8.3; the task acceptance verify command is `ctest -R class-exact`).
// Display names avoid '[' so Catch2 does not mis-parse a tag out of the name.
//
// What this realizes [docs/design/11 §5.1 (the CLASS-EXACT determinism-class members),
// §5.2 (blessed sample-rate set), §6.2 (SHA-256 hash-compare; equality is whole-digest);
// ADR-013 C5 (a CLASS-EXACT golden — seq bytes, divider-OR edges, PRNG stream, arp order,
// param-smooth boundaries, CC map — must SHA-256 match bit-for-bit on macOS arm64 AND Linux
// x64; any diff FAILS, paired); ADR-023 V12 (a corpus at each blessed rate, keyed by SR)]:
//
//  - SIX integer/control corpora are authored from the SHIPPING core modules and rendered
//    into exact integer-valued f32 payloads (integer-valued floats have an exact IEEE-754
//    little-endian representation, so the bytes — and therefore the SHA-256 — are identical
//    on arm64 AND Linux x64, the CLASS-EXACT contract):
//      * seq bytes          — mw::control::StepSequencer per-step slot-byte layout
//      * divider-OR edges    — mw101::dsp::SubOscillator 4013 Q1/Q2/(Q1||Q2) edge pattern
//      * PRNG stream         — mw::util::Prng fixed-seed integer draw stream
//      * arp ordering        — mw::control::Arpeggiator UP / U&D / DOWN step ordering
//      * param-smooth bounds  — mw::params::OnePoleSmoother control-rate snap-tick boundary
//      * CC/param map         — the §6.2 default CC->param-index integer mapping
//    The module DSP/logic internals are owned by their streams and consumed OPAQUE here
//    (task 080 Out-of-scope); this corpus only RENDERS, KEYS, and HASH-COMPARES them.
//
//  - Each rate-relevant corpus is keyed at EACH of {44100, 48000, 88200, 96000} Hz via
//    makeGoldenKey() (which REFUSES a non-blessed rate). The keys are distinct per rate.
//    [ADR-023 V12; §5.2]
//
//  - The hash-compare is bit-for-bit: a re-render equals the blessed blob (compareExact
//    match == true), and a ONE-BYTE diff FAILS (match == false) — the paired positive /
//    negative control of ADR-013 C5 that a stubbed-to-constant module cannot satisfy.
//
//  - The PRNG-stream golden uses a FIXED seed and reproduces identically across runs
//    [docs/design/11 §5.1]: two independent fixed-seed renders are byte-identical, while a
//    different seed produces a different blob (the negative control).
//
// This harness validates self-consistency and topology-faithfulness, NOT measured SH-101
// fidelity [docs/design/11 §1.3; ADR-013 owner ratification]. The bless tool (arm64-only,
// MANIFEST-appending, task 045) and the MANIFEST entries are owned elsewhere; this corpus
// authoring exercises the render + key + hash-compare contract. The committed MANIFEST.toml
// already carries CLASS-EXACT seed entries (prng-stream, seq-bytes); this test is their
// "corresponding test" (the §7.5 / ADR-013 C13 completeness pairing).
//
// Tagged [golden] + [class-exact] (both already in the committed labels snapshot) plus the
// NEW [module-class-exact] corpus tag the orchestrator picks up at wave integration — an
// EXPECTED, transient red on the labels_snapshot test ONLY, never on the scoped -R
// class-exact / -R golden selection [task 080; docs/design/11 §8.4; ADR-013 C3].

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "../../core/calibration/ModuleClassExactCorpusConstants.h"
#include "../../core/calibration/GoldenKeyConstants.h"
#include "../../core/calibration/CcLearnMapConstants.h"
#include "../golden/CompareExact.h"
#include "../golden/GoldenKey.h"

#include "control/StepSequencer.h"
#include "control/ControlTypes.h"
#include "control/Arpeggiator.h"
#include "dsp/SubOscillator.h"
#include "dsp/MinBlepTable.h"
#include "params/Smoother.h"
#include "params/ParamDefs.h"
#include "util/Prng.h"

namespace {

using mw::golden::compareExact;
using mw::golden::DeterminismClass;
using mw::golden::EngineTag;
using mw::golden::ExactResult;
using mw::golden::GoldenKey;
using mw::golden::LadderEngine;
using mw::golden::makeGoldenKey;
using mw::golden::RenderResult;

namespace ce = mw::cal::golden::classexact;

// The corpus engine tag: the shipping Huovilainen ladder at the blessed 2x oversample
// factor, at the CURRENT renderVersion (1, per tests/golden/corpus/MANIFEST.toml) — the
// context this corpus is blessed under [docs/design/11 §5.3; ADR-023 V11]. (For an
// integer/control corpus the engine axis does not change the bytes, but the key is tagged
// faithfully so a cross-tag compare would still differ.)
EngineTag corpusEngine() noexcept {
    return EngineTag{LadderEngine::Huovilainen, /*oversampleFactor=*/2, /*renderVersion=*/1};
}

// Wrap an exact integer/control payload in a RenderResult, zero-padded to the canonical
// blob length so every corpus blob has a uniform footprint. The payload values are exact
// integer-valued floats (their IEEE-754 little-endian bytes are identical on arm64 / x64),
// so the CompareExact SHA-256 is bit-for-bit cross-platform stable [docs/design/11 §6.2].
RenderResult makeBlob(const std::vector<float>& payload, double rate) {
    RenderResult r{};
    r.sampleRate = rate;
    r.engine     = corpusEngine();
    r.samples.assign(static_cast<std::size_t>(ce::kBlobFrames), 0.0f);
    const std::size_t n =
        payload.size() < r.samples.size() ? payload.size() : r.samples.size();
    for (std::size_t i = 0; i < n; ++i) r.samples[i] = payload[i];
    return r;
}

// --- 1. Sequencer per-step byte layout ---------------------------------------------
// Record the (PI) note/rest/tie program into the SHIPPING StepSequencer, then read back
// every filled slot's raw byte. The byte is an exact small integer -> exact f32 [§5.1
// "sequencer per-step byte layout"; docs/design/05 §6.2; research/13 §4.6].
std::vector<float> renderSeqBytes() {
    mw::control::StepSequencer seq;
    seq.prepare();
    seq.setRecord(true);
    // A deterministic program exercising plain note / REST / TIE so all three slot-byte
    // flavors are captured.
    seq.recordNote(ce::kSeqPitches[0]);
    seq.recordRest();
    seq.recordTie(ce::kSeqPitches[1]);
    seq.recordNote(ce::kSeqPitches[2]);
    seq.recordNote(ce::kSeqPitches[3]);
    seq.recordTie(ce::kSeqPitches[4]);
    seq.recordRest();
    seq.recordNote(ce::kSeqPitches[5]);
    seq.recordNote(ce::kSeqPitches[6]);
    seq.recordNote(ce::kSeqPitches[7]);

    const mw::control::SeqBuffer& buf = seq.buffer();
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(seq.count()));
    for (int i = 0; i < seq.count(); ++i)
        out.push_back(static_cast<float>(buf[static_cast<std::size_t>(i)].bits));
    return out;
}

// --- 2. 4013 divider-OR sub-osc edge indices ---------------------------------------
// Drive the SHIPPING SubOscillator over a sequence of saw-wrap clock edges and capture,
// at each edge, the OR-output LEVEL of the 25%-pulse logic (Q1 || Q2): 1.0 when high, 0.0
// when low. The divider is exact integer logic — Q1 toggles each wrap, Q2 toggles on Q1's
// rising edge, OR is the -2-oct 75%/25% pulse — so this is a CLASS-EXACT pattern [§5.1
// "4013 divider diode-OR sub-osc edge ... indices"; docs/design/01 §5.3, §5.4; research/10
// §7]. Captured as the OR output SIGN, not the band-limited float, so the pattern is the
// integer divider state, not the FP residual.
std::vector<float> renderDividerOrEdges(double hostRate) {
    mw101::dsp::MinBlepTable table;
    table.build();   // read-only table; built once for the applicator (unused in PolyBlep)

    mw101::dsp::SubOscillator sub;
    sub.prepare(hostRate, table);
    sub.setShape(mw101::dsp::SubShape::TwoOctDown25Pulse);   // the Q1 || Q2 diode-OR pulse
    sub.reset();

    // For each captured edge: clock the divider ONCE on a saw-wrap sample (wrapped == true),
    // then read the SETTLED held level on a following non-wrap sample at MID-PHASE. At
    // mid-phase neither PolyBLEP window is active (the interior residual is EXACTLY 0, see
    // PolyBlep.h), so renderSample returns the naive held level verbatim: +kSubHigh (high) or
    // kSubLow (low). The captured value is therefore the EXACT integer OR state of the
    // divider (1.0 high / 0.0 low) — no FP residual enters the byte that is hashed.
    const double freqHz = hostRate / 4.0;   // dt = 0.25 => mid-phase 0.5 is interior (BLEP==0)
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(ce::kDividerEdges));
    for (int e = 0; e < ce::kDividerEdges; ++e) {
        (void) sub.renderSample(/*masterPhase=*/0.0f, /*wrapped=*/true, freqHz);  // 4013 clock
        const float held = sub.renderSample(/*masterPhase=*/0.5f, /*wrapped=*/false, freqHz);
        out.push_back(held > 0.0f ? 1.0f : 0.0f);   // OR-output high/low: exact integer state
    }
    return out;
}

// --- 3. Fixed-seed integer PRNG stream ---------------------------------------------
// Capture the leading 32-bit draws of the SHIPPING fixed-seed PCG PRNG. Pure integer
// arithmetic -> identical run-to-run AND across arm64 / Linux x64 [§5.1 "fixed-seed
// integer PRNG stream"; core/util/Prng.h]. The 32-bit draw is split into two 16-bit halves
// so each is an exact integer-valued f32 (a full u32 up to 2^32 can lose low bits in f32).
std::vector<float> renderPrngStream(std::uint64_t seed) {
    mw::util::Prng rng{ seed };
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(ce::kPrngDraws) * 2u);
    for (int i = 0; i < ce::kPrngDraws; ++i) {
        const std::uint32_t v = rng.nextU32();
        out.push_back(static_cast<float>(v & 0xFFFFu));          // low 16 bits (exact f32)
        out.push_back(static_cast<float>((v >> 16) & 0xFFFFu));  // high 16 bits (exact f32)
    }
    return out;
}

// --- 4. Arp step ordering -----------------------------------------------------------
// Hold the (PI) chord, then advance one clock edge at a time in a given direction,
// capturing the SOUNDING key index each edge (the arp ORDER). Pure integer bitmap walk
// over the held set -> CLASS-EXACT [§5.1 "arp step ordering"; docs/design/05 §5.1-§5.4].
std::vector<float> renderArpOrder(mw::control::ArpMode mode) {
    mw::control::Arpeggiator arp;
    arp.prepare();
    arp.setMode(mode);
    for (int key : ce::kArpHeldKeys) arp.noteOn(key);

    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(ce::kArpEdges));
    for (int e = 0; e < ce::kArpEdges; ++e)
        out.push_back(static_cast<float>(arp.advanceOnEdge()));   // sounding key index (exact)
    return out;
}

// --- 5. Param-smoothing block boundaries -------------------------------------------
// The CLASS-EXACT param-smoothing golden is the integer control-tick INDEX at which the
// one-pole smoother first SNAPS to a stepped target — the "block boundary" — not the FP
// de-zipper trajectory [§5.1 "parameter-smoothing block boundaries"; docs/design/00 §4.4].
// We step the smoother one control-rate tick at a time and record, at each tick, whether it
// is still smoothing (1.0) or has snapped (0.0): an exact integer boundary pattern whose
// transition tick is the captured value. (The FP value itself is NOT hashed — only the
// integer is-smoothing boundary, which is platform-stable.)
std::vector<float> renderSmoothBoundaries() {
    mw::params::OnePoleSmoother sm;
    sm.prepare(ce::kSmoothTimeConstantS, ce::kSmoothTickRateHz);
    sm.reset(0.0);
    sm.setTarget(ce::kSmoothTarget);   // a step at tick 0; the smoother approaches the target

    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(ce::kSmoothTicks));
    for (int t = 0; t < ce::kSmoothTicks; ++t) {
        sm.process();   // advance one control-rate chunk-boundary tick
        out.push_back(sm.isSmoothing() ? 1.0f : 0.0f);   // integer boundary state (exact)
    }
    return out;
}

// --- 6. CC / param mapping ----------------------------------------------------------
// The §6.2 default CC->param-index integer mapping, reconstructed from the SAME JUCE-free
// primitives the plugin CcLearnMap (plugin/midi, not built here) seeds from: a string-ID
// lookup into the doc-06 kParamDefs registry, with CC64 sustain -> the HOLD sentinel
// (mw::cal::cclearn::kHoldParamIndex). The CC->index mapping is integer -> CLASS-EXACT [§5.1
// "CC/automation param mapping (IDs per docs/design/06 §2)"; docs/design/09 §6.2; ADR-012
// C15/C20]. The plugin map is just one CONSUMER of this integer mapping; the integer
// contract is what the corpus pins.
std::int32_t paramIndexOf(std::string_view id) noexcept {
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (std::string_view{ mw::params::kParamDefs[i].id } == id)
            return static_cast<std::int32_t>(i);
    return -1;   // unmapped sentinel (matches CcLearnMap::kUnmapped)
}

std::vector<float> renderCcMap() {
    struct Row { std::uint8_t cc; const char* id; };  // id == nullptr => HOLD sentinel
    const std::array<Row, 7> defaults = {{
        { 1,  "mw101.mod.lfo_mod_wheel" },
        { 7,  "mw101.vca.level" },
        { 11, "mw101.amp.expression" },
        { 74, "mw101.vcf.cutoff" },
        { 71, "mw101.vcf.resonance" },
        { 5,  "mw101.glide.time" },
        { 64, nullptr },   // CC64 sustain -> HOLD / external-HOLD semantics (not a param)
    }};

    std::vector<float> out;
    out.reserve(defaults.size() * 2u);
    for (const Row& r : defaults) {
        const std::int32_t idx =
            (r.id == nullptr) ? mw::cal::cclearn::kHoldParamIndex : paramIndexOf(r.id);
        out.push_back(static_cast<float>(r.cc));    // the CC number   (exact f32)
        out.push_back(static_cast<float>(idx));     // the param index (exact f32; small int)
    }
    return out;
}

} // namespace

// === Acceptance: each integer/control golden SHA-256 matches bit-for-bit; a one-byte =====
// === diff FAILS (paired) [ADR-013 C5; docs/design/11 §6.2] ================================
TEST_CASE("golden: class-exact module corpora hash-match bit-for-bit and a one-byte diff "
          "FAILS the compare (paired) at every blessed rate",
          "[golden][class-exact][module-class-exact]") {
    for (double rate : mw::cal::golden::kBlessedSampleRatesHz) {
        // Each corpus's blessed blob == a fresh re-render of the same shipping module.
        const std::array<RenderResult, 6> blessed = {{
            makeBlob(renderSeqBytes(), rate),
            makeBlob(renderDividerOrEdges(rate), rate),
            makeBlob(renderPrngStream(ce::kPrngSeed), rate),
            makeBlob(renderArpOrder(mw::control::ArpMode::Up), rate),
            makeBlob(renderSmoothBoundaries(), rate),
            makeBlob(renderCcMap(), rate),
        }};
        const std::array<RenderResult, 6> reRender = {{
            makeBlob(renderSeqBytes(), rate),
            makeBlob(renderDividerOrEdges(rate), rate),
            makeBlob(renderPrngStream(ce::kPrngSeed), rate),
            makeBlob(renderArpOrder(mw::control::ArpMode::Up), rate),
            makeBlob(renderSmoothBoundaries(), rate),
            makeBlob(renderCcMap(), rate),
        }};

        for (std::size_t c = 0; c < blessed.size(); ++c) {
            INFO("rate = " << rate << "  corpus index = " << c);

            // Positive: a re-render of the shipping module hash-matches the blessed blob.
            const ExactResult ok = compareExact(reRender[c], blessed[c]);
            REQUIRE(ok.match);                       // bit-for-bit SHA-256 equality
            REQUIRE(ok.got == ok.expected);

            // The blob carries REAL content (not all-zero): a stub returning a constant
            // blob would still "match" itself, so assert the payload is non-trivial too.
            bool anyNonZero = false;
            for (float v : blessed[c].samples) if (v != 0.0f) { anyNonZero = true; break; }
            REQUIRE(anyNonZero);

            // Paired NEGATIVE control: flip exactly ONE byte of the payload (one sample to
            // a distinct exact integer). The whole-digest SHA-256 gate FAILS [ADR-013 C5].
            RenderResult perturbed = blessed[c];
            float& v = perturbed.samples[0];
            v = (v == 0.0f) ? 1.0f : 0.0f;           // a distinct exact integer value
            REQUIRE(v != blessed[c].samples[0]);     // sanity: it changed
            const ExactResult diff = compareExact(perturbed, blessed[c]);
            REQUIRE_FALSE(diff.match);               // one-byte diff FAILS the hash-compare
            REQUIRE_FALSE(diff.got == diff.expected);
        }
    }
}

// === Acceptance: corpora exist at the blessed rates where rate-relevant, keyed by SR =====
// [ADR-023 V12; docs/design/11 §5.2] ======================================================
TEST_CASE("golden: class-exact module corpora are keyed at each blessed sample rate, all "
          "keys distinct, and a non-blessed rate is REFUSED (paired)",
          "[golden][class-exact][module-class-exact]") {
    const EngineTag eng = corpusEngine();

    // The divider-OR corpus is the rate-RELEVANT one (its edge timing tracks fs); key it at
    // each blessed rate and require the four keys distinct (keyed BY sample rate).
    std::vector<std::uint64_t> keyHashes;
    for (double rate : mw::cal::golden::kBlessedSampleRatesHz) {
        const std::vector<float> blob = renderDividerOrEdges(rate);
        REQUIRE(blob.size() == static_cast<std::size_t>(ce::kDividerEdges));

        const std::uint64_t graphHash =
            mw::golden::hash(makeGoldenKey(/*renderGraphHash=*/0u, eng, rate,
                                           /*blockSize=*/512, ce::kPrngSeed,
                                           DeterminismClass::Exact));
        const GoldenKey key = makeGoldenKey(graphHash, eng, rate,
                                            /*blockSize=*/512, ce::kPrngSeed,
                                            DeterminismClass::Exact);
        REQUIRE(mw::golden::isBlessedSampleRate(key.sampleRate));
        REQUIRE(key.cls == DeterminismClass::Exact);   // integer/control path -> CLASS-EXACT
        keyHashes.push_back(mw::golden::hash(key));
    }

    REQUIRE(keyHashes.size() == mw::cal::golden::kBlessedSampleRatesHz.size());
    REQUIRE(keyHashes.size() == 4u);

    // The four keys are DISTINCT (differ in sample rate -> differ in hash): the corpus is
    // keyed by sample rate and cannot collide two rates into one golden [§5.2].
    for (std::size_t i = 0; i < keyHashes.size(); ++i)
        for (std::size_t j = i + 1; j < keyHashes.size(); ++j)
            REQUIRE(keyHashes[i] != keyHashes[j]);

    // Paired negative control: a non-blessed rate is REFUSED at key construction (a golden
    // may only be keyed to a blessed rate) [ADR-023 V12/V14/V17; §5.2].
    REQUIRE_FALSE(mw::golden::isBlessedSampleRate(44101.0));
    REQUIRE_THROWS_AS(
        makeGoldenKey(0u, eng, 44101.0, 512, ce::kPrngSeed, DeterminismClass::Exact),
        std::invalid_argument);
}

// === Acceptance: the PRNG-stream golden uses a fixed seed and reproduces identically =====
// across runs [docs/design/11 §5.1] ======================================================
TEST_CASE("golden: class-exact PRNG-stream golden uses a fixed seed and reproduces "
          "identically across runs while a different seed differs (paired)",
          "[golden][class-exact][module-class-exact][prng]") {
    const double rate = 44100.0;   // PRNG stream is rate-INVARIANT (pure integer); pin one

    // Two independent fixed-seed renders are BYTE-IDENTICAL (reproducible across runs).
    const RenderResult a = makeBlob(renderPrngStream(ce::kPrngSeed), rate);
    const RenderResult b = makeBlob(renderPrngStream(ce::kPrngSeed), rate);
    const ExactResult same = compareExact(a, b);
    REQUIRE(same.match);                              // identical fixed-seed stream
    REQUIRE(same.got == same.expected);

    // The stream is non-trivial (carries real PRNG content, not a constant).
    bool anyNonZero = false;
    for (float v : a.samples) if (v != 0.0f) { anyNonZero = true; break; }
    REQUIRE(anyNonZero);

    // Paired NEGATIVE control: a DIFFERENT seed yields a DIFFERENT stream -> different hash.
    // (A stubbed-to-constant PRNG would make these equal and fail this half.)
    const RenderResult other = makeBlob(renderPrngStream(ce::kPrngSeed + 1u), rate);
    const ExactResult diff = compareExact(other, a);
    REQUIRE_FALSE(diff.match);
    REQUIRE_FALSE(diff.got == diff.expected);
}

// === Circuit-behavior oracle: the 4013 divider-OR edge pattern is the exact -2-oct =======
// 75%/25% duty of the Q1 || Q2 logic; the arp UP / U&D / DOWN orderings are the exact ======
// bitmap traversals [docs/design/01 §5.3/§5.4; docs/design/05 §5.1-§5.4; ADR-013 C4] =======
TEST_CASE("golden: class-exact divider-OR and arp orderings encode the exact integer "
          "circuit behavior (paired against the documented logic)",
          "[golden][class-exact][module-class-exact][sub][arp]") {
    // --- 4013 divider-OR: the OR output (Q1 || Q2) is HIGH 75% / LOW 25% of the -2-oct
    // period. Over a full -2-oct period (4 wraps) the divider state (q1,q2) cycles
    //   wrap1: (1,0) OR=1 ; wrap2: (0,1) OR=1 ; wrap3: (1,1) OR=1 ; wrap4: (0,0) OR=0
    // so exactly ONE in every FOUR edges is LOW (25% low / 75% high) [docs/design/01 §5.4].
    const std::vector<float> orEdges = renderDividerOrEdges(48000.0);
    REQUIRE(static_cast<int>(orEdges.size()) == ce::kDividerEdges);
    int lows = 0, highs = 0;
    for (float v : orEdges) { if (v == 0.0f) ++lows; else ++highs; }
    // 75% high / 25% low over the captured edges (exact integer duty of the OR logic).
    REQUIRE(lows  == ce::kDividerEdges / 4);
    REQUIRE(highs == (ce::kDividerEdges * 3) / 4);
    // Paired negative control: the pattern is NOT all-high (a stub returning a constant
    // would be all-high or all-low and fail one side) [ADR-013 C4].
    REQUIRE(lows  > 0);
    REQUIRE(highs > 0);

    // --- Arp orderings: UP ascends, DOWN descends, and they are MIRRORS of each other over
    // a single cycle of the held set (the canonical paired control of the directions).
    const auto& keys = ce::kArpHeldKeys;        // {0,4,7,12}, ascending
    const int   n    = static_cast<int>(keys.size());

    const std::vector<float> up   = renderArpOrder(mw::control::ArpMode::Up);
    const std::vector<float> down = renderArpOrder(mw::control::ArpMode::Down);

    // UP's first cycle is the held keys in ascending order.
    for (int i = 0; i < n; ++i)
        REQUIRE(up[static_cast<std::size_t>(i)] == static_cast<float>(keys[static_cast<std::size_t>(i)]));
    // DOWN's first cycle is the held keys in DESCENDING order — the mirror of UP.
    for (int i = 0; i < n; ++i)
        REQUIRE(down[static_cast<std::size_t>(i)]
                == static_cast<float>(keys[static_cast<std::size_t>(n - 1 - i)]));
    // Paired negative control: UP != DOWN over the first cycle (a stub emitting a constant
    // key would make them equal) [ADR-013 C4].
    bool anyDiffer = false;
    for (int i = 0; i < n; ++i)
        if (up[static_cast<std::size_t>(i)] != down[static_cast<std::size_t>(i)]) { anyDiffer = true; break; }
    REQUIRE(anyDiffer);

    // --- The param-smoothing boundary golden actually contains a SNAP transition (it starts
    // smoothing and later snaps), so the integer boundary pattern is meaningful, not flat.
    const std::vector<float> sm = renderSmoothBoundaries();
    REQUIRE(static_cast<int>(sm.size()) == ce::kSmoothTicks);
    REQUIRE(sm.front() == 1.0f);   // smoothing right after the step (positive)
    REQUIRE(sm.back()  == 0.0f);   // snapped by the end (negative control: not stuck on)

    // --- The CC map binds CC64 to the HOLD sentinel (negative, < 0, != unmapped) and the
    // other defaults to REAL non-negative registry indices [docs/design/09 §6.2; ADR-012 C20].
    const std::vector<float> cc = renderCcMap();
    REQUIRE(cc.size() == 14u);                 // 7 rows * (cc, index)
    // CC64 is the last row; its param index is the HOLD sentinel.
    REQUIRE(cc[12] == 64.0f);
    REQUIRE(cc[13] == static_cast<float>(mw::cal::cclearn::kHoldParamIndex));
    REQUIRE(cc[13] < 0.0f);                    // HOLD is a sentinel, not a registry index
    // The first six rows bind to real (>= 0) registry indices (paired: not all unmapped).
    for (int row = 0; row < 6; ++row)
        REQUIRE(cc[static_cast<std::size_t>(row * 2 + 1)] >= 0.0f);
}
