// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-3 guarded bless-tool tests (task 045). Test-case names begin with
// "manifest" so `ctest -R manifest` selects them under the silent-pass rule
// (AGENTS.md / docs/design/11 §8.3). Tagged [manifest] (the task's subsystem tag;
// already present in the committed label snapshot — task 046).
//
// Realizes docs/design/11 §7.2 (the guarded bless tool: arm64-only, BLESS_REASON-
// gated, CLASS-FP-tolerance-required, honesty-label-required), §7.3 (the appended
// ManifestEntry field set), §7.6 / ADR-023 V6 (renderVersion governed and recorded
// next to BLESS_REASON), and ADR-013 C10/C11/C14.
//
// Each acceptance criterion is covered with a PAIRED positive/negative control so a
// stubbed-to-constant tool (e.g. always-Ok / always-Refuse) fails [ADR-013 C4]:
//   - C10 arm64 guard:        non-arm64 host => NotArm64; arm64 + valid => a ManifestEntry
//   - C11 reason guard:       empty BLESS_REASON => EmptyReason; non-empty + valid => Ok
//   - §7.2 tolerance guard:   CLASS-FP without tolerance => MissingTolerance; with => Ok
//   - C14 honesty-label guard: ledger-derived without a label => MissingHonestyLabel
//   - V6 renderVersion oracle: a tolerance-exceeding change records a BUMPED
//                              renderVersion next to BLESS_REASON; an in-band change does NOT

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "../../tools/bless/Bless.h"

namespace bl  = mw::bless;
namespace gld = mw::golden;

namespace {

// A blessed-set GoldenKey for a CLASS-FP artifact (the nonlinear ladder path).
gld::GoldenKey fpKey() {
    const gld::EngineTag eng{gld::LadderEngine::Huovilainen, /*os*/ 2, /*renderVersion*/ 1};
    return gld::makeGoldenKey(/*renderGraphHash*/ 0xABCDEF01u, eng,
                              /*sampleRate*/ 48000.0, /*blockSize*/ 512,
                              /*seed*/ 4242u, gld::DeterminismClass::Fp);
}

// A blessed-set GoldenKey for a CLASS-EXACT artifact (the integer PRNG path).
gld::GoldenKey exactKey() {
    const gld::EngineTag eng{gld::LadderEngine::Huovilainen, /*os*/ 2, /*renderVersion*/ 1};
    return gld::makeGoldenKey(/*renderGraphHash*/ 0x12345678u, eng,
                              /*sampleRate*/ 44100.0, /*blockSize*/ 512,
                              /*seed*/ 12345u, gld::DeterminismClass::Exact);
}

// A fully-valid CLASS-FP request: arm64 host, non-empty reason, a tolerance band, a
// honesty label. Each negative test below knocks out exactly one field.
bl::BlessRequest validFpRequest() {
    bl::BlessRequest req;
    req.key            = fpKey();
    req.blessReason    = "unit fixture: ladder self-osc bless";
    req.honestyLabels  = {bl::makeLabel(bl::LabelKind::CloneDerived)};
    req.tolerance      = gld::FpTolerance{};   // present (CLASS-FP requires one)
    req.tolerance->maxAbsErr = 1e-6;
    req.artifactSha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    req.artifactRef    = "corpus/ladder-selfosc.f32";
    req.blesser        = "tester";
    req.isoDate        = "2026-06-18";
    req.commitSha      = "1111111111111111111111111111111111111111";
    req.compiler       = "AppleClang 17";
    req.fpFlagProof    = "-fno-fast-math -ffp-contract=off";
    req.arm64HostId    = "ref-01";
    req.derivesFromLedgerFact = true;
    req.simulatedHostIsArm64  = true;   // force the arm64 branch deterministically
    return req;
}

} // namespace

// =========================================================================
// C10 (paired): a simulated non-arm64 host returns NotArm64; arm64 + valid
// inputs returns a ManifestEntry. [ADR-013 C10; docs/design/11 §7.2]
// =========================================================================
TEST_CASE("manifest bless refuses a non-arm64 host and accepts a valid arm64 bless",
          "[manifest]") {
    SECTION("negative: a simulated non-arm64 host => NotArm64") {
        bl::BlessRequest req = validFpRequest();
        req.simulatedHostIsArm64 = false;
        const auto r = bl::bless(req);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == bl::BlessRefusal::NotArm64);
    }
    SECTION("positive: arm64 + valid inputs => a ManifestEntry") {
        const auto r = bl::bless(validFpRequest());
        REQUIRE(r.has_value());
        // The returned entry carries the request's identity and render parameters.
        REQUIRE(r.value().artifactSha256 ==
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        REQUIRE(r.value().corpusClass == "FP");
        REQUIRE(r.value().sampleRate == 48000.0);
        REQUIRE(r.value().seed == 4242u);
    }
}

// =========================================================================
// C11 (paired): empty BLESS_REASON returns EmptyReason; a non-empty reason with
// otherwise-valid inputs is accepted. [ADR-013 C11; docs/design/11 §7.2]
// =========================================================================
TEST_CASE("manifest bless refuses an empty BLESS_REASON", "[manifest]") {
    SECTION("negative: empty reason => EmptyReason") {
        bl::BlessRequest req = validFpRequest();
        req.blessReason = "";
        const auto r = bl::bless(req);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == bl::BlessRefusal::EmptyReason);
    }
    SECTION("negative: whitespace-only reason is treated as empty => EmptyReason") {
        bl::BlessRequest req = validFpRequest();
        req.blessReason = "   \t  ";
        const auto r = bl::bless(req);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == bl::BlessRefusal::EmptyReason);
    }
    SECTION("positive: a non-empty reason is accepted and recorded verbatim") {
        const auto r = bl::bless(validFpRequest());
        REQUIRE(r.has_value());
        REQUIRE(r.value().blessReason == "unit fixture: ladder self-osc bless");
    }
}

// =========================================================================
// §7.2 (paired): a CLASS-FP request without a tolerance returns MissingTolerance;
// with a tolerance it is accepted. A CLASS-EXACT request needs NO tolerance.
// [docs/design/11 §7.2; ADR-013 C11 context — tolerance is per-corpus, CLASS-FP only]
// =========================================================================
TEST_CASE("manifest bless requires a tolerance band for CLASS-FP", "[manifest]") {
    SECTION("negative: a CLASS-FP request with no tolerance => MissingTolerance") {
        bl::BlessRequest req = validFpRequest();
        req.tolerance.reset();   // CLASS-FP but no band
        const auto r = bl::bless(req);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == bl::BlessRefusal::MissingTolerance);
    }
    SECTION("positive: a CLASS-FP request WITH a tolerance is accepted and records it") {
        const auto r = bl::bless(validFpRequest());
        REQUIRE(r.has_value());
        REQUIRE(r.value().tolerance.has_value());
        REQUIRE(r.value().tolerance.value() == 1e-6);
    }
    SECTION("control: a CLASS-EXACT request needs NO tolerance and records none") {
        bl::BlessRequest req = validFpRequest();
        req.key = exactKey();          // now CLASS-EXACT
        req.tolerance.reset();         // EXACT carries no band
        req.honestyLabels.clear();     // pure-integer stream: no ledger-fact claim
        req.derivesFromLedgerFact = false;
        req.artifactSha256 =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        const auto r = bl::bless(req);
        REQUIRE(r.has_value());
        REQUIRE(r.value().corpusClass == "EXACT");
        REQUIRE_FALSE(r.value().tolerance.has_value());
    }
}

// =========================================================================
// C14 (paired): a ledger-derived artifact without a honesty label returns
// MissingHonestyLabel; one carrying its label is accepted; a NON-ledger-derived
// artifact needs no label. [ADR-013 C14; docs/design/11 §7.4]
// =========================================================================
TEST_CASE("manifest bless requires a honesty label where ledger-derived", "[manifest]") {
    SECTION("negative: ledger-derived but no honesty label => MissingHonestyLabel") {
        bl::BlessRequest req = validFpRequest();
        req.derivesFromLedgerFact = true;
        req.honestyLabels.clear();
        const auto r = bl::bless(req);
        REQUIRE_FALSE(r.has_value());
        REQUIRE(r.error() == bl::BlessRefusal::MissingHonestyLabel);
    }
    SECTION("positive: ledger-derived WITH its label is accepted and records it") {
        const auto r = bl::bless(validFpRequest());
        REQUIRE(r.has_value());
        REQUIRE(r.value().honestyLabels.size() == 1);
        REQUIRE(r.value().honestyLabels.front().kind == gld::manifest::LabelKind::CloneDerived);
    }
    SECTION("control: a non-ledger-derived artifact needs NO label") {
        bl::BlessRequest req = validFpRequest();
        req.derivesFromLedgerFact = false;
        req.honestyLabels.clear();
        const auto r = bl::bless(req);
        REQUIRE(r.has_value());
        REQUIRE(r.value().honestyLabels.empty());
    }
}

// =========================================================================
// §7.6 / ADR-023 V6 (paired oracle): on a tolerance-exceeding change the appended
// entry records a BUMPED renderVersion next to BLESS_REASON; an in-band change (or
// a CLASS-EXACT with an unchanged hash) records the SAME renderVersion.
// =========================================================================
TEST_CASE("manifest bless bumps and records renderVersion on a tolerance-exceeding change",
          "[manifest]") {
    SECTION("negative: a CLASS-FP render moved OUTSIDE its band => renderVersion bumped") {
        bl::BlessRequest req = validFpRequest();
        req.tolerance->maxAbsErr = 1e-6;
        req.priorRenderVersion   = 1;
        req.measuredMaxAbsDelta  = 2e-6;   // strictly exceeds the 1e-6 band [ADR-023 V5]
        const auto r = bl::bless(req);
        REQUIRE(r.has_value());
        // The recorded renderVersion sits next to BLESS_REASON and is bumped [V6].
        REQUIRE(r.value().renderVersion == 2);
        REQUIRE(r.value().blessReason == req.blessReason);
    }
    SECTION("positive: an in-band CLASS-FP change holds the renderVersion") {
        bl::BlessRequest req = validFpRequest();
        req.tolerance->maxAbsErr = 1e-6;
        req.priorRenderVersion   = 1;
        req.measuredMaxAbsDelta  = 5e-7;   // within the band => no bump
        const auto r = bl::bless(req);
        REQUIRE(r.has_value());
        REQUIRE(r.value().renderVersion == 1);
    }
    SECTION("negative: a CLASS-EXACT hash change => renderVersion bumped") {
        bl::BlessRequest req = validFpRequest();
        req.key = exactKey();
        req.tolerance.reset();
        req.honestyLabels.clear();
        req.derivesFromLedgerFact = false;
        req.artifactSha256 =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        req.priorArtifactSha256 =
            "0000000000000000000000000000000000000000000000000000000000000000";  // differs
        req.priorRenderVersion = 1;
        const auto r = bl::bless(req);
        REQUIRE(r.has_value());
        REQUIRE(r.value().renderVersion == 2);
    }
    SECTION("positive: a CLASS-EXACT bless with an unchanged hash holds the renderVersion") {
        bl::BlessRequest req = validFpRequest();
        req.key = exactKey();
        req.tolerance.reset();
        req.honestyLabels.clear();
        req.derivesFromLedgerFact = false;
        req.artifactSha256 =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        req.priorArtifactSha256 =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";  // identical
        req.priorRenderVersion = 1;
        const auto r = bl::bless(req);
        REQUIRE(r.has_value());
        REQUIRE(r.value().renderVersion == 1);
    }
}

// =========================================================================
// §7.3: the appended entry carries the FULL §7.3 field set, including fpFlagProof,
// arm64HostId and renderVersion. The MANIFEST validator (task 046) must find a
// well-formed entry. [docs/design/11 §7.3]
// =========================================================================
TEST_CASE("manifest bless emits a complete 7.3 ManifestEntry that validates clean",
          "[manifest]") {
    const auto r = bl::bless(validFpRequest());
    REQUIRE(r.has_value());
    const gld::manifest::ManifestEntry& e = r.value();

    // Every §7.3 field is populated (fpFlagProof, arm64HostId, renderVersion included).
    REQUIRE(gld::manifest::isWellFormedHash(e.artifactSha256));
    REQUIRE(e.blesser == "tester");
    REQUIRE(e.isoDate == "2026-06-18");
    REQUIRE(e.commitSha.size() == 40);
    REQUIRE_FALSE(e.blessReason.empty());
    REQUIRE(e.engine == "Huov");           // LadderEngine::Huovilainen -> "Huov"
    REQUIRE(e.oversampleFactor == 2);
    REQUIRE(e.blockSize == 512);
    REQUIRE(e.corpusClass == "FP");
    REQUIRE(e.tolerance.has_value());
    REQUIRE_FALSE(e.compiler.empty());
    REQUIRE(e.fpFlagProof == "-fno-fast-math -ffp-contract=off");
    REQUIRE(e.arm64HostId == "ref-01");
    REQUIRE(e.renderVersion >= 1);
    REQUIRE(e.artifactRef == "corpus/ladder-selfosc.f32");

    // The validator (task 046) accepts it as a complete, non-orphan, labelled entry.
    gld::manifest::CorpusContext ctx;
    ctx.corpusBlobHashes      = {e.artifactSha256};
    ctx.existingArtifactRefs  = {e.artifactRef};
    ctx.derivesFromLedgerFact = {e.artifactRef};   // CLASS-FP self-osc derives from §4.1
    const auto v = gld::manifest::validate({e}, ctx);
    INFO("unexpected violations: " << v.violations.size());
    REQUIRE(v.ok());
}

// =========================================================================
// §7.2 helper: the env reader treats an unset / empty BLESS_REASON as empty so the
// CLI refuses; a set, non-empty value is read verbatim. (Paired.)
// =========================================================================
TEST_CASE("manifest bless reads BLESS_REASON from the environment", "[manifest]") {
    // Use a uniquely-named env var to avoid clobbering a real BLESS_REASON.
    const char* kVar = "MW_TEST_BLESS_REASON_045";

    bl::unsetEnv(kVar);
    REQUIRE(bl::readEnvNonEmpty(kVar).empty());          // unset => empty

    bl::setEnv(kVar, "");
    REQUIRE(bl::readEnvNonEmpty(kVar).empty());          // set-but-empty => empty

    bl::setEnv(kVar, "re-bless: tightened Linux band");
    REQUIRE(bl::readEnvNonEmpty(kVar) == "re-bless: tightened Linux band");

    bl::unsetEnv(kVar);
}
