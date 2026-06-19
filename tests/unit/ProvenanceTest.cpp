// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Provenance honesty-label + renderVersion-governor tests (task 044). Test-case
// names begin with "provenance" so `ctest -R provenance` selects them under the
// silent-pass rule (AGENTS.md / docs/design/11 §8.3). Tagged [provenance] (the
// task's subsystem tag; the orchestrator regenerates the label snapshot per wave).
//
// Realizes docs/design/11 §7.4 / §7.6 and ADR-013 C14 + ADR-023 V5/V6. Each
// acceptance criterion is covered with a PAIRED positive/negative control so a
// stubbed-to-constant governor (e.g. `return false`) fails:
//   - V5 EXACT:  a changed CLASS-EXACT hash requires a bump; unchanged does not
//   - V5 FP:     an FP artifact moved OUTSIDE its tolerance band requires a bump;
//                inside-band (or exactly at the band edge) does not
//   - §7.4:      each LabelKind resolves to a valid ledger §2-§8 reference string
//                AND round-trips through the controlled-vocabulary token table

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "../../tools/bless/Provenance.h"

namespace bl = mw::bless;

// =========================================================================
// §7.4 — controlled honesty-label vocabulary: every LabelKind resolves to a
// valid ledger §2-§8 reference string, and the token round-trips.
// =========================================================================
TEST_CASE("provenance: every LabelKind resolves to a ledger 2-8 reference string",
          "[provenance]") {
    const bl::LabelKind kinds[] = {
        bl::LabelKind::CloneDerived,      bl::LabelKind::ReverseEngineered,
        bl::LabelKind::TheoryInference,   bl::LabelKind::CommunityDisassembly,
        bl::LabelKind::ServiceManual,     bl::LabelKind::Disputed,
        bl::LabelKind::SoftwareEmulationArtifact};

    for (const auto k : kinds) {
        const std::string ref = bl::ledgerRefOf(k);
        INFO("LabelKind index " << static_cast<int>(k) << " -> '" << ref << "'");
        // A controlled reference is non-empty and points at the ledger doc.
        REQUIRE_FALSE(ref.empty());
        REQUIRE(ref.rfind("research/13", 0) == 0);   // starts with "research/13"
        // It cites a section in §2-§8 (the labelled-fact range of the ledger).
        REQUIRE(bl::isLedgerSectionInRange(ref));
    }
}

TEST_CASE("provenance: an out-of-range or malformed ledger reference is rejected",
          "[provenance]") {
    // Negative controls: a §1 (purpose) ref and a §9 ref are outside §2-§8; a
    // reference with no section anchor at all is malformed. None may validate green.
    REQUIRE_FALSE(bl::isLedgerSectionInRange("research/13 §1.2"));
    REQUIRE_FALSE(bl::isLedgerSectionInRange("research/13 §9.1"));
    REQUIRE_FALSE(bl::isLedgerSectionInRange("research/13"));
    REQUIRE_FALSE(bl::isLedgerSectionInRange("research/07 §4.1"));   // wrong doc
}

TEST_CASE("provenance: HonestyLabel carries its kind and a defaulted controlled ref",
          "[provenance]") {
    // Constructing a label from a kind fills in the controlled ledger reference, so
    // the label always carries a valid §2-§8 citation for MANIFEST validation.
    const bl::HonestyLabel lbl = bl::makeLabel(bl::LabelKind::ReverseEngineered);
    REQUIRE(lbl.kind == bl::LabelKind::ReverseEngineered);
    REQUIRE(lbl.ledgerRef == bl::ledgerRefOf(bl::LabelKind::ReverseEngineered));
    REQUIRE(bl::isLedgerSectionInRange(lbl.ledgerRef));

    // The aggregate is value-typed and equality-comparable.
    const bl::HonestyLabel same{bl::LabelKind::ReverseEngineered, lbl.ledgerRef};
    REQUIRE(lbl == same);
}

TEST_CASE("provenance: label kind round-trips through its controlled vocabulary token",
          "[provenance]") {
    const bl::LabelKind kinds[] = {
        bl::LabelKind::CloneDerived,      bl::LabelKind::ReverseEngineered,
        bl::LabelKind::TheoryInference,   bl::LabelKind::CommunityDisassembly,
        bl::LabelKind::ServiceManual,     bl::LabelKind::Disputed,
        bl::LabelKind::SoftwareEmulationArtifact};
    for (const auto k : kinds) {
        const std::string tok = bl::labelToken(k);
        const auto back = bl::labelKindFromToken(tok);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == k);
    }
    // Negative control: an unknown token is rejected, not silently mapped.
    REQUIRE_FALSE(bl::labelKindFromToken("not-a-real-label").has_value());
}

// =========================================================================
// §7.6 / ADR-023 V5 (paired) — CLASS-EXACT: a changed artifact hash requires a
// renderVersion bump; an unchanged hash does not.
// =========================================================================
TEST_CASE("provenance: a changed CLASS-EXACT hash requires a renderVersion bump",
          "[provenance]") {
    const std::string oldHash =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const std::string newHash =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    bl::ExactArtifact oldArt{oldHash};

    SECTION("negative: hash changed => bump REQUIRED") {
        const bl::ExactArtifact newArt{newHash};
        REQUIRE(bl::bumpRequiredExact(oldArt, newArt));
    }
    SECTION("positive: hash unchanged => bump NOT required") {
        const bl::ExactArtifact newArt{oldHash};
        REQUIRE_FALSE(bl::bumpRequiredExact(oldArt, newArt));
    }
}

// =========================================================================
// §7.6 / ADR-023 V5 (paired) — CLASS-FP: an artifact moved OUTSIDE its tolerance
// band requires a bump; inside-band (or exactly at the band edge) does not.
// =========================================================================
TEST_CASE("provenance: a CLASS-FP artifact moved outside its tolerance band requires a bump",
          "[provenance]") {
    // The band is the artifact's manifest tolerance (max-abs-error). The governor
    // compares the measured max-abs delta between old and new render to that band.
    const double tolerance = 1e-6;
    const bl::FpArtifact oldArt{tolerance};

    SECTION("negative: delta strictly OUTSIDE the band => bump REQUIRED") {
        const double maxAbsDelta = 2e-6;   // exceeds the 1e-6 band
        REQUIRE(bl::bumpRequiredFp(oldArt, maxAbsDelta));
    }
    SECTION("positive: delta strictly INSIDE the band => bump NOT required") {
        const double maxAbsDelta = 5e-7;   // within the 1e-6 band
        REQUIRE_FALSE(bl::bumpRequiredFp(oldArt, maxAbsDelta));
    }
    SECTION("positive: delta exactly AT the band edge stays inside => no bump") {
        const double maxAbsDelta = tolerance;   // |delta| <= band is in-band
        REQUIRE_FALSE(bl::bumpRequiredFp(oldArt, maxAbsDelta));
    }
    SECTION("positive: an identical FP render (zero delta) => no bump") {
        REQUIRE_FALSE(bl::bumpRequiredFp(oldArt, 0.0));
    }
}

// =========================================================================
// V5/V6 oracle — the unified governor dispatches by corpus class: only the
// class-appropriate trigger fires (an EXACT hash change is invisible to the FP
// band test and vice-versa), proving the two triggers are not conflated.
// =========================================================================
TEST_CASE("provenance: the governor dispatches EXACT vs FP triggers independently",
          "[provenance]") {
    const std::string h0 =
        "1111111111111111111111111111111111111111111111111111111111111111";
    const std::string h1 =
        "2222222222222222222222222222222222222222222222222222222222222222";

    // EXACT: only the hash decides; the FP band is irrelevant.
    REQUIRE(bl::bumpRequired(bl::CorpusClass::Exact, h0, h1, /*band*/ 1.0, /*delta*/ 0.0));
    REQUIRE_FALSE(
        bl::bumpRequired(bl::CorpusClass::Exact, h0, h0, /*band*/ 0.0, /*delta*/ 99.0));

    // FP: only the band/delta decide; the hash is irrelevant (FP hashes legitimately
    // differ between platforms, so a hash change alone MUST NOT force a bump).
    REQUIRE(bl::bumpRequired(bl::CorpusClass::Fp, h0, h1, /*band*/ 1e-6, /*delta*/ 1e-3));
    REQUIRE_FALSE(
        bl::bumpRequired(bl::CorpusClass::Fp, h0, h1, /*band*/ 1e-6, /*delta*/ 1e-9));
}
