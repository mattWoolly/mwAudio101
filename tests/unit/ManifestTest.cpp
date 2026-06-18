// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-3 provenance MANIFEST tests (task 046). Test-case names begin with
// "manifest" so `ctest -R manifest` selects them under the silent-pass rule
// (AGENTS.md / docs/design/11 §8.3). Tagged [manifest] (the task's subsystem tag;
// the orchestrator regenerates the label snapshot per wave).
//
// Realizes docs/design/11 §7.1, §7.3, §7.5 and ADR-013 C12-C14 + ADR-023 V7. Each
// acceptance criterion is covered with a PAIRED positive/negative control so a
// stubbed-to-constant validator (e.g. `return PASS`) fails [ADR-013 C4]:
//   - completeness: a blob hash absent from MANIFEST => FAILS; complete corpus PASSES
//   - orphan:       a MANIFEST entry with no corresponding artifact/test => FAILS
//   - honesty-label: a ledger §2-§8 claim without its label => FAILS
//   - renderVersion: artifact changed without a bump => FAILS; bump with no change => FAILS
// Plus: the parser exposes ALL §7.3 fields, proven against the real on-disk
// tests/golden/corpus/MANIFEST.toml.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../golden/Manifest.h"
#include "../golden/Sha256.h"

namespace mfst = mw::golden::manifest;

namespace {

// Resolve <repo>/tests/golden/corpus/MANIFEST.toml from this test's compile-time
// path (the established no-CMake-define convention — see FastTanhTest.cpp). __FILE__
// is <repo>/tests/unit/ManifestTest.cpp, so the repo root is two parents up.
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

// A minimal, well-formed two-entry manifest text used by the validation tests.
// One CLASS-EXACT entry deriving from a ledger fact (carries a label); one CLASS-FP
// entry with a tolerance band.
const char* kGoodManifest = R"toml(
renderVersion = 1

[[golden]]
artifactSha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
artifactRef = "corpus/seq-bytes.f32"
blesser = "tester"
isoDate = "2026-06-18"
commitSha = "1111111111111111111111111111111111111111"
blessReason = "unit fixture: sequencer bytes"
engine = "Huov"
oversampleFactor = 2
sampleRate = 48000.0
seed = 777
blockSize = 512
corpusClass = "EXACT"
compiler = "AppleClang 17"
fpFlagProof = "-fno-fast-math -ffp-contract=off"
arm64HostId = "ref-01"
renderVersion = 1
honestyLabels = ["community-disassembly"]

[[golden]]
artifactSha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
artifactRef = "corpus/ladder-selfosc.f32"
blesser = "tester"
isoDate = "2026-06-18"
commitSha = "1111111111111111111111111111111111111111"
blessReason = "unit fixture: ladder self-osc"
engine = "Huov"
oversampleFactor = 2
sampleRate = 48000.0
seed = 4242
blockSize = 512
corpusClass = "FP"
tolerance = 1e-6
compiler = "AppleClang 17"
fpFlagProof = "-fno-fast-math -ffp-contract=off"
arm64HostId = "ref-01"
renderVersion = 1
honestyLabels = ["clone-derived"]
)toml";

constexpr const char* kHashA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* kHashB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* kRefA  = "corpus/seq-bytes.f32";
constexpr const char* kRefB  = "corpus/ladder-selfosc.f32";

// A CorpusContext that makes the kGoodManifest fixture pass cleanly: both blobs
// present, both refs exist, the labelled-fact entry derives from a ledger fact.
mfst::CorpusContext goodContext() {
    mfst::CorpusContext ctx;
    ctx.corpusBlobHashes = {kHashA, kHashB};
    ctx.existingArtifactRefs = {kRefA, kRefB};
    ctx.derivesFromLedgerFact = {kRefA};   // seq bytes derive from research/13 §4.6
    return ctx;
}

} // namespace

// =========================================================================
// Parser: exposes ALL §7.3 fields, proven against the real on-disk MANIFEST.
// =========================================================================
TEST_CASE("manifest parses the real corpus ledger and exposes all 7.3 fields", "[manifest]") {
    const std::string text = readFile(manifestPath());
    REQUIRE_FALSE(text.empty());   // the corpus MANIFEST.toml exists and is non-empty

    const auto parsed = mfst::parse(text);
    INFO("parse error: " << parsed.error);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.entries.size() == 4);   // the four seed blesses

    // Find the PRNG CLASS-EXACT entry and assert every §7.3 field round-tripped.
    const mfst::ManifestEntry* prng = nullptr;
    for (const auto& e : parsed.entries) {
        if (e.artifactRef == "corpus/prng-stream-44100-s12345.f32") prng = &e;
    }
    REQUIRE(prng != nullptr);
    REQUIRE(prng->artifactSha256.size() == 64);
    REQUIRE(mfst::isWellFormedHash(prng->artifactSha256));
    REQUIRE(prng->blesser == "mwaudio101-ci");
    REQUIRE(prng->isoDate == "2026-06-18");
    REQUIRE(prng->commitSha.size() == 40);
    REQUIRE_FALSE(prng->blessReason.empty());
    REQUIRE(prng->engine == "Huov");
    REQUIRE(prng->oversampleFactor == 2);
    REQUIRE(prng->sampleRate == 44100.0);
    REQUIRE(prng->seed == 12345u);
    REQUIRE(prng->blockSize == 512);
    REQUIRE(prng->corpusClass == "EXACT");
    REQUIRE(prng->isExact());
    REQUIRE_FALSE(prng->tolerance.has_value());   // CLASS-EXACT carries no tolerance
    REQUIRE_FALSE(prng->compiler.empty());
    REQUIRE_FALSE(prng->fpFlagProof.empty());
    REQUIRE_FALSE(prng->arm64HostId.empty());
    REQUIRE(prng->renderVersion == 1);
    REQUIRE(prng->honestyLabels.empty());   // pure-integer stream: no ledger-fact claim

    // A CLASS-FP entry carries a tolerance band and a non-empty honesty label.
    const mfst::ManifestEntry* fp = nullptr;
    for (const auto& e : parsed.entries) {
        if (e.corpusClass == "FP") { fp = &e; break; }
    }
    REQUIRE(fp != nullptr);
    REQUIRE(fp->isFp());
    REQUIRE(fp->tolerance.has_value());
    REQUIRE(fp->tolerance.value() > 0.0);
    REQUIRE_FALSE(fp->honestyLabels.empty());
}

TEST_CASE("manifest honesty-label tokens round-trip through the controlled vocabulary",
          "[manifest]") {
    // Every label kind maps to a token and back (research/13 §1.2 controlled vocab).
    const mfst::LabelKind kinds[] = {
        mfst::LabelKind::CloneDerived, mfst::LabelKind::ReverseEngineered,
        mfst::LabelKind::TheoryInference, mfst::LabelKind::CommunityDisassembly,
        mfst::LabelKind::ServiceManual, mfst::LabelKind::Disputed,
        mfst::LabelKind::SoftwareEmulationArtifact};
    for (auto k : kinds) {
        const std::string tok = mfst::labelTokenOf(k);
        const auto back = mfst::labelKindFromToken(tok);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == k);
    }
    // Negative control: an unknown token is rejected (not silently mapped).
    REQUIRE_FALSE(mfst::labelKindFromToken("not-a-real-label").has_value());
}

// =========================================================================
// Baseline: the well-formed fixture + good context PASSES (the paired positive
// for every negative below). [ADR-013 C12-C14; ADR-023 V7]
// =========================================================================
TEST_CASE("manifest a complete, well-formed corpus validates clean", "[manifest]") {
    const auto parsed = mfst::parse(kGoodManifest);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.entries.size() == 2);

    const auto result = mfst::validate(parsed.entries, goodContext());
    INFO("unexpected violations: " << result.violations.size());
    REQUIRE(result.ok());
}

// =========================================================================
// Acceptance C12 (paired): a blob hash absent from MANIFEST => FAILS; complete PASSES.
// =========================================================================
TEST_CASE("manifest completeness: a blob hash absent from MANIFEST fails, complete passes",
          "[manifest]") {
    const auto parsed = mfst::parse(kGoodManifest);
    REQUIRE(parsed.ok());

    SECTION("positive: complete corpus passes the completeness check") {
        const auto r = mfst::validate(parsed.entries, goodContext());
        REQUIRE_FALSE(r.has(mfst::Failure::MissingHash));
    }
    SECTION("negative: an extra corpus blob not in the MANIFEST => MissingHash") {
        auto ctx = goodContext();
        ctx.corpusBlobHashes.insert(
            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE(r.has(mfst::Failure::MissingHash));
        REQUIRE_FALSE(r.ok());
    }
}

// =========================================================================
// Acceptance C13 (paired): a MANIFEST entry with no corresponding artifact/test => FAILS.
// =========================================================================
TEST_CASE("manifest orphan: an entry with no corresponding artifact or test fails",
          "[manifest]") {
    const auto parsed = mfst::parse(kGoodManifest);
    REQUIRE(parsed.ok());

    SECTION("positive: every entry's artifactRef exists => no orphan") {
        const auto r = mfst::validate(parsed.entries, goodContext());
        REQUIRE_FALSE(r.has(mfst::Failure::OrphanEntry));
    }
    SECTION("negative: entry B's artifact missing from the corpus => OrphanEntry") {
        auto ctx = goodContext();
        ctx.existingArtifactRefs.erase(kRefB);   // the ladder blob no longer exists
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE(r.has(mfst::Failure::OrphanEntry));
        REQUIRE_FALSE(r.ok());
    }
}

// =========================================================================
// Acceptance C14 (paired): a ledger §2-§8 claim without its honesty label => FAILS.
// =========================================================================
TEST_CASE("manifest honesty-label: a ledger fact claim without its label fails",
          "[manifest]") {
    SECTION("positive: the labelled-fact entry carries its label => passes") {
        const auto parsed = mfst::parse(kGoodManifest);
        REQUIRE(parsed.ok());
        const auto r = mfst::validate(parsed.entries, goodContext());
        REQUIRE_FALSE(r.has(mfst::Failure::MissingHonestyLabel));
    }
    SECTION("negative: a ledger-derived entry stripped of its label => MissingHonestyLabel") {
        auto parsed = mfst::parse(kGoodManifest);
        REQUIRE(parsed.ok());
        // Strip entry A's honesty labels; its claim still derives from a ledger fact.
        for (auto& e : parsed.entries)
            if (e.artifactRef == kRefA) e.honestyLabels.clear();
        const auto r = mfst::validate(parsed.entries, goodContext());
        REQUIRE(r.has(mfst::Failure::MissingHonestyLabel));
        REQUIRE_FALSE(r.ok());
    }
    SECTION("control: an entry NOT deriving from a ledger fact needs no label") {
        auto parsed = mfst::parse(kGoodManifest);
        REQUIRE(parsed.ok());
        auto ctx = goodContext();
        ctx.derivesFromLedgerFact.clear();   // neither entry derives from a ledger fact
        for (auto& e : parsed.entries) e.honestyLabels.clear();
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE_FALSE(r.has(mfst::Failure::MissingHonestyLabel));
    }
}

// =========================================================================
// Acceptance V7 (paired): artifact changed without renderVersion bump => FAILS,
// AND renderVersion bumped with no artifact change => FAILS.
// =========================================================================
TEST_CASE("manifest renderVersion: change-without-bump fails and bump-without-change fails",
          "[manifest]") {
    auto parsed = mfst::parse(kGoodManifest);
    REQUIRE(parsed.ok());

    SECTION("positive: hash changed AND renderVersion bumped => consistent, passes") {
        auto ctx = goodContext();
        ctx.priorBless[kRefA] = {/*priorHash*/ "0000000000000000000000000000000000000000000000000000000000000000",
                                 /*priorRenderVersion*/ 1};
        for (auto& e : parsed.entries)
            if (e.artifactRef == kRefA) { e.renderVersion = 2; }   // hash already differs from prior
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE_FALSE(r.has(mfst::Failure::RenderVersionNotBumped));
        REQUIRE_FALSE(r.has(mfst::Failure::RenderVersionBumpNoChange));
    }
    SECTION("positive: nothing changed and renderVersion held => consistent, passes") {
        auto ctx = goodContext();
        ctx.priorBless[kRefA] = {kHashA, 1};   // identical prior hash, same version
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE_FALSE(r.has(mfst::Failure::RenderVersionNotBumped));
        REQUIRE_FALSE(r.has(mfst::Failure::RenderVersionBumpNoChange));
    }
    SECTION("negative: blessed artifact hash changed but renderVersion NOT bumped") {
        auto ctx = goodContext();
        ctx.priorBless[kRefA] = {/*priorHash*/ "0000000000000000000000000000000000000000000000000000000000000000",
                                 /*priorRenderVersion*/ 1};
        // entry A's renderVersion stays 1 even though its hash differs from prior.
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE(r.has(mfst::Failure::RenderVersionNotBumped));
        REQUIRE_FALSE(r.ok());
    }
    SECTION("negative: renderVersion bumped with NO artifact change") {
        auto ctx = goodContext();
        ctx.priorBless[kRefA] = {kHashA, /*priorRenderVersion*/ 1};   // same hash as now
        for (auto& e : parsed.entries)
            if (e.artifactRef == kRefA) e.renderVersion = 2;          // bumped for nothing
        const auto r = mfst::validate(parsed.entries, ctx);
        REQUIRE(r.has(mfst::Failure::RenderVersionBumpNoChange));
        REQUIRE_FALSE(r.ok());
    }
}

// =========================================================================
// Parser robustness: a malformed hash and a missing CLASS-FP tolerance are caught
// (so a typo'd ledger never validates green by accident).
// =========================================================================
TEST_CASE("manifest a malformed artifact hash is flagged, not silently accepted",
          "[manifest]") {
    const char* badHash = R"toml(
[[golden]]
artifactSha256 = "deadbeef"
artifactRef = "corpus/x.f32"
corpusClass = "EXACT"
renderVersion = 1
)toml";
    const auto parsed = mfst::parse(badHash);
    REQUIRE(parsed.ok());                 // parses syntactically
    REQUIRE(parsed.entries.size() == 1);
    REQUIRE_FALSE(mfst::isWellFormedHash(parsed.entries[0].artifactSha256));

    mfst::CorpusContext ctx;
    ctx.existingArtifactRefs = {"corpus/x.f32"};
    const auto r = mfst::validate(parsed.entries, ctx);
    REQUIRE(r.has(mfst::Failure::MalformedEntry));
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("manifest a CLASS-FP entry without a tolerance band is malformed", "[manifest]") {
    const char* noTol = R"toml(
[[golden]]
artifactSha256 = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
artifactRef = "corpus/fp.f32"
corpusClass = "FP"
renderVersion = 1
honestyLabels = ["clone-derived"]
)toml";
    const auto parsed = mfst::parse(noTol);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.entries.size() == 1);
    REQUIRE_FALSE(parsed.entries[0].tolerance.has_value());

    mfst::CorpusContext ctx;
    ctx.corpusBlobHashes = {"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"};
    ctx.existingArtifactRefs = {"corpus/fp.f32"};
    const auto r = mfst::validate(parsed.entries, ctx);
    REQUIRE(r.has(mfst::Failure::MalformedEntry));
}
