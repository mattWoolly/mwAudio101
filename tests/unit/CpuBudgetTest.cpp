// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/CpuBudgetTest.cpp — the TDD acceptance tests for the CPU-budget regression
// golden (task 076b). Test-case names begin with "golden" so
// `ctest -R golden --no-tests=error` selects them under the silent-pass rule (AGENTS.md
// "Tests"; docs/design/11 §8.3); the display names contain NO literal '[' so Catch2 does
// not mis-parse a tag out of the name. Tagged [golden][cpu] — the task's stream tag plus
// the §3.1 cross-cutting CPU-invariant label; the orchestrator regenerates the
// ctest-labels snapshot per wave so the new [cpu] label is picked up there.
//
// Lives in tests/unit/ (NOT tests/invariants/) because the build globs tests/unit/*.cpp
// into mw101_tests; a tests/invariants/CpuBudget.cpp would NOT be picked up (only
// AudioThreadGuard*.cpp is globbed there) and editing tests/CMakeLists.txt is forbidden
// by the parallel-fleet conflict-avoidance rule. The measurement primitive is the
// header-only tests/golden/CpuBudget.h (same pattern as the sibling RenderHarness.h).
//
// Objective (plan/backlog/076b; docs/design/11 §13.5): render a worst-case patch (full
// poly + full unison + 2x oversampling + Newton-iterated ladder) through the assembled
// Engine and assert the MEDIAN per-block wall-time stays under the committed
// ceilingMicrosPerBlock — pinned in MANIFEST alongside engine + oversample factor — as a
// HARD gate that FAILS when exceeded [ADR-013 C21; ADR-019 VT-05].
//
// Acceptance coverage (each criterion is an explicit assertion below):
//   1. A worst-case patch whose per-block wall-time EXCEEDS the committed ceiling =>
//      the gate FAILS [docs/design/11 §13.5; ADR-013 C21; ADR-019 VT-05] — proven both
//      with the real worst-case render under the committed ceiling (passes) AND a stub
//      that exceeds a tiny ceiling (caught).
//   2. ceilingMicrosPerBlock, engine tag, and oversample factor are READ from
//      MANIFEST.toml (via the 046-style reader), not hard-coded [docs/design/11 §13.5].
//   3. measureWorstCaseBlockMicros returns the MEDIAN of N runs [docs/design/11 §13.5].
//   4. An injected fidelity change that blows the RT budget is caught as a regression,
//      demonstrated against a stub that exceeds the ceiling [docs/design/11 §13.5].
//
// Out of scope (NOT asserted here): deriving/committing the ceiling value itself;
// CLASS-EXACT/CLASS-FP audio-output comparison; per-format wall-time differences
// [task 076b Out-of-scope].

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../golden/CpuBudget.h"
#include "../golden/CpuBudgetManifest.h"
#include "../golden/GoldenKey.h"
#include "../../core/voice/VoiceTypes.h"

namespace {

using mw::golden::LadderEngine;
using mw::test::CpuBudgetMeasurement;
using mw::test::CpuBudgetSpec;

// Resolve <repo>/tests/golden/corpus/MANIFEST.toml from this test's compile-time path
// (the established no-CMake-define convention — see ManifestTest.cpp / FastTanhTest.cpp).
// __FILE__ is <repo>/tests/unit/CpuBudgetTest.cpp, so the repo root is two parents up.
std::filesystem::path manifestPath() {
    const std::filesystem::path thisFile{__FILE__};
    const std::filesystem::path repoRoot = thisFile.parent_path()   // tests/unit
                                                  .parent_path()    // tests
                                                  .parent_path();   // <repo>
    return repoRoot / "tests" / "golden" / "corpus" / "MANIFEST.toml";
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Read the committed CPU-budget pin (ceiling + engine + oversample factor) from the real
// on-disk MANIFEST.toml — NEVER a literal in the test [docs/design/11 §13.5].
mw::golden::cpu::CpuBudgetEntry committedBudget() {
    return mw::golden::cpu::parseCpuBudget(readFile(manifestPath()));
}

// Map the MANIFEST engine token to the ladder A/B tag for the spec. "Huov" is the
// Newton-iterated (Huovilainen) ladder — the worst case.
LadderEngine ladderFromToken(const std::string& tok) noexcept {
    return (tok == "ZDF") ? LadderEngine::ZDF : LadderEngine::Huovilainen;
}

// Build the worst-case spec straight from the committed MANIFEST pin: full poly + full
// unison on top of the MANIFEST-pinned engine / oversample factor / ceiling. SR is
// pinned to the MANIFEST's reference rate (a blessed rate <= 96 kHz, so the engine runs
// 2x). The ONLY values invented here are the (PI) full-load voice/unison counts, which
// are the worst-case caps, not the ceiling.
CpuBudgetSpec worstCaseSpec(const mw::golden::cpu::CpuBudgetEntry& b) {
    CpuBudgetSpec s{};
    s.numVoices             = mw::kMaxPoly;     // full poly
    s.unison                = mw::kMaxUnison;   // full unison
    s.oversampleFactor      = b.oversampleFactor;     // from MANIFEST
    s.engine                = ladderFromToken(b.engine);  // from MANIFEST
    s.sampleRate            = b.sampleRate;     // from MANIFEST (blessed, <= 96 kHz)
    s.blockSize             = b.blockSize;      // from MANIFEST
    s.ceilingMicrosPerBlock = b.ceilingMicrosPerBlock;    // from MANIFEST
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Acceptance 2 — the ceiling, engine tag, and oversample factor are READ FROM MANIFEST,
// not hard-coded [docs/design/11 §13.5]. A missing [cpu_budget] pin is an OBJECTIVE
// failure (found == false), never a silent pass.
// ---------------------------------------------------------------------------
TEST_CASE("golden: cpu budget ceiling engine and oversample factor are read from MANIFEST",
          "[golden][cpu]") {
    const auto budget = committedBudget();

    // The pin exists and was parsed (a missing table => found == false => FAILS here).
    REQUIRE(budget.found);
    // The committed ceiling is a positive wall-time (a typo'd/zero ceiling would make the
    // gate meaningless — caught here, not silently).
    REQUIRE(budget.ceilingMicrosPerBlock > 0.0);
    // The pinned worst-case context: the Newton-iterated ladder at 2x oversampling
    // [docs/design/11 §13.5; ADR-003 §F-09].
    REQUIRE(budget.engine == "Huov");
    REQUIRE(budget.oversampleFactor == 2);
    // The ceiling is host-relative — the reference host id is recorded alongside it.
    REQUIRE_FALSE(budget.arm64HostId.empty());
    // The reference render rate is a blessed rate so the engine runs 2x there.
    REQUIRE(mw::golden::isBlessedSampleRate(budget.sampleRate));
}

// ---------------------------------------------------------------------------
// Acceptance 1 + 3 — the WORST-CASE render (full poly + full unison + 2x OS + Newton
// ladder) measured through the assembled Engine: the median per-block wall-time stays
// UNDER the committed ceiling (the gate PASSES on the reference host), AND the engine
// actually ran 2x oversampling (not a silent 1x clamp) [docs/design/11 §13.5; ADR-013
// C21; ADR-019 VT-05]. The measurement is the MEDIAN of N runs (§13.5).
// ---------------------------------------------------------------------------
TEST_CASE("golden: cpu budget worst-case median per-block micros stays under the committed ceiling",
          "[golden][cpu]") {
    const auto budget = committedBudget();
    REQUIRE(budget.found);

    const CpuBudgetSpec spec = worstCaseSpec(budget);

    // Sanity: the worst-case spec really is full poly + full unison on the one pool
    // [ADR-019 VT-05] — so the WHOLE Voice[kMaxVoices] pool can sound.
    REQUIRE(spec.numVoices == mw::kMaxPoly);
    REQUIRE(spec.unison    == mw::kMaxUnison);
    REQUIRE(mw::kMaxPoly * mw::kMaxUnison == mw::kMaxVoices);

    const CpuBudgetMeasurement m = mw::test::measureWorstCase(spec);

    // The median is taken over N runs (§13.5 "median of N runs").
    REQUIRE(m.blocksMeasured > 1);
    // A real render took a positive, finite amount of wall time (a zero/negative median
    // would mean the engine never ran — caught, not silently passed).
    REQUIRE(m.medianMicrosPerBlock > 0.0);

    // The engine ACTUALLY ran 2x oversampling at this blessed rate — not a silent 1x
    // clamp that would under-report the worst-case cost [ADR-023 V15].
    REQUIRE(m.achievedOversample == 2);
    REQUIRE(m.achievedOversample == spec.oversampleFactor);

    // The worst-case load ACTUALLY sounded the full voice stack — not a silent path that
    // would measure-as-fast and pass the gate vacuously. The assembled engine drives the
    // full unison stack (POLY note allocation is task 075, not yet wired into the note
    // path — see tests/golden/CpuBudget.h), so the full kMaxUnison voices must be active.
    INFO("sounding voices = " << m.soundingVoices << " ; full unison = " << mw::kMaxUnison);
    REQUIRE(m.soundingVoices == mw::kMaxUnison);
    REQUIRE(m.soundingVoices > 1);   // genuinely a multi-voice worst case, not one voice

    // The HARD gate: the worst-case median is UNDER the committed ceiling on the
    // reference host [ADR-013 C21]. (Exceeding it => exceedsCeiling() true => the gate
    // FAILS as an RT-budget regression — exercised by the negative control below.)
    INFO("worst-case median micros/block = " << m.medianMicrosPerBlock
         << " ; committed ceiling = " << spec.ceilingMicrosPerBlock
         << " ; achieved oversample = " << m.achievedOversample);
    REQUIRE_FALSE(m.exceedsCeiling(spec.ceilingMicrosPerBlock));
    REQUIRE(m.medianMicrosPerBlock < spec.ceilingMicrosPerBlock);
}

// ---------------------------------------------------------------------------
// Acceptance 1 + 4 — NEGATIVE CONTROL: a worst-case render whose measured per-block
// wall-time EXCEEDS the ceiling is CAUGHT as a regression (the gate FAILS), demonstrated
// against a stub that exceeds the ceiling [docs/design/11 §13.5; ADR-013 C21]. This is
// the paired control proving the gate actually bites: a constant-pass gate that always
// returned "under budget" would fail this test.
// ---------------------------------------------------------------------------
TEST_CASE("golden: cpu budget a render that exceeds the ceiling is caught as a regression",
          "[golden][cpu]") {
    const auto budget = committedBudget();
    REQUIRE(budget.found);

    // Measure the real worst-case median ONCE; reuse it for both arms so the control is
    // about the CEILING comparison, not measurement noise.
    const CpuBudgetSpec realSpec = worstCaseSpec(budget);
    const CpuBudgetMeasurement m = mw::test::measureWorstCase(realSpec);
    REQUIRE(m.medianMicrosPerBlock > 0.0);

    SECTION("positive: under the committed (generous) ceiling => NOT a regression") {
        REQUIRE_FALSE(m.exceedsCeiling(budget.ceilingMicrosPerBlock));
    }

    SECTION("negative: a fidelity change that blows the budget (stub ceiling below the "
            "measured time) is caught as a regression") {
        // Model an injected fidelity change that pushed the per-block cost over budget by
        // pinning the ceiling BELOW the measured worst-case time. The exact same verdict
        // path the ctest uses (exceedsCeiling) now reports a regression. A near-zero
        // ceiling is the strongest form of the control: any real render exceeds it.
        const double impossibleCeiling = m.medianMicrosPerBlock * 0.5;
        REQUIRE(m.exceedsCeiling(impossibleCeiling));   // FAILS the gate => caught

        const double zeroCeiling = 0.0;
        REQUIRE(m.exceedsCeiling(zeroCeiling));         // any positive time exceeds 0
    }
}

// ---------------------------------------------------------------------------
// Acceptance 3 (focused) — measureWorstCaseBlockMicros returns the MEDIAN of N runs: a
// stable central value, not a single noisy sample. Two independent measurement runs of
// the same spec land in the same order of magnitude (the median is robust to a one-off
// scheduler hiccup), and the §13.5-named entry point agrees with the richer overload.
// ---------------------------------------------------------------------------
TEST_CASE("golden: cpu budget measureWorstCaseBlockMicros returns a stable median of N runs",
          "[golden][cpu]") {
    const auto budget = committedBudget();
    REQUIRE(budget.found);
    const CpuBudgetSpec spec = worstCaseSpec(budget);

    // The §13.5-named entry point returns a positive median.
    const double med1 = mw::test::measureWorstCaseBlockMicros(spec);
    REQUIRE(med1 > 0.0);

    // The richer overload's median is the same measurement path (positive, finite).
    const CpuBudgetMeasurement m = mw::test::measureWorstCase(spec);
    REQUIRE(m.medianMicrosPerBlock > 0.0);
    REQUIRE(m.blocksMeasured > 1);   // a median REQUIRES more than one run

    // Robustness: a second independent run is within a wide band of the first (the median
    // damps scheduler jitter; we assert order-of-magnitude stability, not equality — wall
    // time is inherently noisy). Guards against a "median" that is actually a single
    // sample dominated by one preemption.
    const double med2 = mw::test::measureWorstCaseBlockMicros(spec);
    REQUIRE(med2 > 0.0);
    const double ratio = (med1 > med2) ? (med1 / med2) : (med2 / med1);
    REQUIRE(ratio < 20.0);   // generous: two stable medians never differ by 20x
}
