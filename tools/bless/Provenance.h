// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tools/bless/Provenance.h — the honesty-label controlled vocabulary and the
// renderVersion-bump governor (task 044).
//
// Realizes docs/design/11 §7.4 (honesty-label provenance binding — the LabelKind /
// HonestyLabel types and their controlled ledger references) and §7.6 (renderVersion
// governance — the bless tool is where the bump decision is made). Normative
// contracts: ADR-013 C14 (a blessed artifact whose claim derives from a ledger §2-§8
// fact MUST carry its label) and ADR-023 V5/V6 (renderVersion increments IFF a bless
// changes a CLASS-EXACT artifact hash or moves a CLASS-FP artifact outside its
// manifest tolerance band; the bless tool governs and records the bump).
//
// Scope (plan/backlog/044): the LabelKind enum + HonestyLabel{kind, ledgerRef} type,
// the LabelKind -> controlled ledger-reference map, and the pure governor that, given
// old-vs-new artifact (hash for EXACT, tolerance-band move for FP), decides whether a
// renderVersion bump is REQUIRED. OUT of scope: MANIFEST file I/O (golden-8,
// tests/golden/Manifest.h) and the bless CLI / refusal flow (golden-10).
//
// This is OFFLINE bless-tool / harness code, not real-time anything [§2.2]: it is
// JUCE-free and exception-free on these paths, but the RT invariants (no-alloc / no-
// lock) are NOT in play. It is header-only and `inline`: the design tree lists
// tools/bless/Provenance.{h,cpp}, but a header-only realization keeps the primitive
// self-contained and avoids touching the shared tests/CMakeLists glob set (which
// compiles tests/unit/*.cpp — a tools/bless/*.cpp would not be picked up, and the
// shared CMakeLists is a do-not-edit file for the parallel fleet). This is the same
// pattern the sibling harness headers use (tests/golden/Sha256.h, GoldenKey.h,
// CompareFp.h, Manifest.h). The bless CLI (golden-10) links this header.
//
// What this can NEVER prove: that any artifact matches a real SH-101. renderVersion
// communicates "we changed the DSP," never "we got closer to hardware" [ADR-023
// Context]; every check here is self-consistency / provenance discipline.

#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace mw::bless {

// ---------------------------------------------------------------------------
// Honesty-label controlled vocabulary (docs/design/11 §7.4; research/13 §1.2).
// The label set is the ledger's; a blessed artifact whose claim derives from a
// ledger §2-§8 fact MUST carry its label [ADR-013 C14]. Enum order matches the
// §7.4 table.
// ---------------------------------------------------------------------------
enum class LabelKind {
    CloneDerived,             // IR3109 electrical figures: AMSynths/Alfa AS3109 [research/13 §4.1]
    ReverseEngineered,        // BA662 VCA internals: Open Music Labs chip probing [research/13 §4.2]
    TheoryInference,          // ADSR curve law: topology inference, unmeasured [research/13 §5.1]
    CommunityDisassembly,     // sequencer byte format: joebritt, partly inferred [research/13 §4.6]
    ServiceManual,            // gate ON = 12V per 1982 Service Notes [research/13 §2.2]
    Disputed,                 // gate ON 10V vs 12V; ship as range [research/13 §3.1]
    SoftwareEmulationArtifact // no sine LFO / no 32'-64' on 1982 hardware [research/13 §7]
};

// docs/design/11 §7.4 HonestyLabel: a label kind + the ledger section it cites.
struct HonestyLabel {
    LabelKind   kind{};
    std::string ledgerRef;   // e.g. "research/13 §4.1"
    bool operator==(const HonestyLabel& o) const noexcept {
        return kind == o.kind && ledgerRef == o.ledgerRef;
    }
};

// ---------------------------------------------------------------------------
// Controlled token <-> LabelKind (the tokens are the ledger's controlled
// vocabulary as written in MANIFEST.toml) [docs/design/11 §7.4; research/13 §1.2].
// ---------------------------------------------------------------------------
inline std::string labelToken(LabelKind k) {
    switch (k) {
        case LabelKind::CloneDerived:              return "clone-derived";
        case LabelKind::ReverseEngineered:         return "reverse-engineered";
        case LabelKind::TheoryInference:           return "theory/inference";
        case LabelKind::CommunityDisassembly:      return "community-disassembly";
        case LabelKind::ServiceManual:             return "service-manual";
        case LabelKind::Disputed:                  return "disputed";
        case LabelKind::SoftwareEmulationArtifact: return "software-emulation-artifact";
    }
    return {};
}

inline std::optional<LabelKind> labelKindFromToken(std::string_view t) noexcept {
    if (t == "clone-derived")               return LabelKind::CloneDerived;
    if (t == "reverse-engineered")          return LabelKind::ReverseEngineered;
    if (t == "theory/inference")            return LabelKind::TheoryInference;
    if (t == "community-disassembly")       return LabelKind::CommunityDisassembly;
    if (t == "service-manual")              return LabelKind::ServiceManual;
    if (t == "disputed")                    return LabelKind::Disputed;
    if (t == "software-emulation-artifact") return LabelKind::SoftwareEmulationArtifact;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// LabelKind -> controlled ledger reference (the §2-§8 anchor the §7.4 table cites
// for each label's example artifact claim). These strings are the validation
// targets: a MANIFEST honesty label is checked against the controlled reference so
// a typo'd or out-of-range citation cannot silently validate green [ADR-013 C14].
// ---------------------------------------------------------------------------
inline std::string ledgerRefOf(LabelKind k) {
    switch (k) {
        case LabelKind::CloneDerived:              return "research/13 §4.1";
        case LabelKind::ReverseEngineered:         return "research/13 §4.2";
        case LabelKind::TheoryInference:           return "research/13 §5.1";
        case LabelKind::CommunityDisassembly:      return "research/13 §4.6";
        case LabelKind::ServiceManual:             return "research/13 §2.2";
        case LabelKind::Disputed:                  return "research/13 §3.1";
        case LabelKind::SoftwareEmulationArtifact: return "research/13 §7";
    }
    return {};
}

// Build a HonestyLabel from a kind, defaulting its ledgerRef to the controlled
// reference so the label always carries a valid §2-§8 citation [docs/design/11 §7.4].
inline HonestyLabel makeLabel(LabelKind k) {
    return HonestyLabel{k, ledgerRefOf(k)};
}

// ---------------------------------------------------------------------------
// Ledger-reference validation: a controlled honesty label cites a fact in the
// ledger's §2-§8 range (the labelled-fact range: §2 frozen resolutions, §3 standing
// disputes, §4 clone/reverse-engineered figures, §5 measurement gaps, §6 critic
// findings, §7 software-emulation artifacts, §8 cultural cautions). §1 (purpose) is
// not a labelled fact, and there is no §9+. The reference MUST point at the ledger
// doc "research/13" and carry a parseable section number in [2, 8].
// ---------------------------------------------------------------------------
inline bool isLedgerSectionInRange(std::string_view ref) noexcept {
    constexpr std::string_view kDoc = "research/13";
    if (ref.substr(0, kDoc.size()) != kDoc) return false;

    // Find the section marker '§' (UTF-8 0xC2 0xA7) and read the leading top-level
    // section integer that follows it (e.g. "§4.1" -> 4, "§7" -> 7).
    const std::size_t s = ref.find("§");
    if (s == std::string_view::npos) return false;
    std::size_t i = s + 2;   // '§' is two UTF-8 bytes
    if (i >= ref.size() || ref[i] < '0' || ref[i] > '9') return false;

    int section = 0;
    for (; i < ref.size() && ref[i] >= '0' && ref[i] <= '9'; ++i)
        section = section * 10 + (ref[i] - '0');

    return section >= 2 && section <= 8;
}

// ===========================================================================
// renderVersion-bump governor (docs/design/11 §7.6; ADR-023 V5/V6).
//
// renderVersion increments IFF a bless changes any CLASS-EXACT artifact hash or
// moves a CLASS-FP artifact OUTSIDE its manifest tolerance band [ADR-023 V5]. These
// are the two — and only two — triggers; the governor is a pure function of the
// old-vs-new artifact so the bless tool (golden-10) can call it and record the
// decision next to BLESS_REASON [ADR-023 V6]. The two classes are deliberately NOT
// conflated: an EXACT hash change is the trigger for EXACT (bit-exact corpora), and
// a band-exceeding delta is the trigger for FP (FP hashes legitimately differ across
// platforms, so a hash change alone must NOT force an FP bump) [§7.6; ADR-013 C6/C7].
// ===========================================================================
enum class CorpusClass { Exact, Fp };

// A CLASS-EXACT artifact is identified by its SHA-256 hash; the band is irrelevant.
struct ExactArtifact {
    std::string sha256;
};

// A CLASS-FP artifact carries its manifest tolerance band (max-abs-error). The move
// is measured as the max-abs delta between the old and new render.
struct FpArtifact {
    double tolerance = 0.0;   // the manifest tolerance band [docs/design/11 §6.3/§6.4]
};

// CLASS-EXACT trigger: the hash changed [ADR-023 V5].
inline bool bumpRequiredExact(const ExactArtifact& oldArt,
                              const ExactArtifact& newArt) noexcept {
    return oldArt.sha256 != newArt.sha256;
}

// CLASS-FP trigger: the new render moved OUTSIDE the artifact's tolerance band, i.e.
// the measured max-abs delta strictly exceeds the band. A delta AT the band edge is
// in-band (|delta| <= band is a pass, matching the comparer's tolerance semantics)
// [ADR-023 V5; docs/design/11 §6.3]. NaN delta is treated as out-of-band (a render
// that produced NaN is unquestionably a change).
inline bool bumpRequiredFp(const FpArtifact& oldArt, double maxAbsDelta) noexcept {
    if (std::isnan(maxAbsDelta)) return true;
    return std::abs(maxAbsDelta) > oldArt.tolerance;
}

// Unified governor: dispatch by corpus class so the bless tool has one entry point.
// For EXACT, only (oldHash, newHash) decide; for FP, only (band, maxAbsDelta) decide.
inline bool bumpRequired(CorpusClass cls,
                         std::string_view oldHash, std::string_view newHash,
                         double band, double maxAbsDelta) noexcept {
    switch (cls) {
        case CorpusClass::Exact: return oldHash != newHash;
        case CorpusClass::Fp:
            return std::isnan(maxAbsDelta) || std::abs(maxAbsDelta) > band;
    }
    return false;
}

} // namespace mw::bless
