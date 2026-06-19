// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tools/bless/Bless.h — the guarded bless tool (task 045).
//
// Realizes docs/design/11 §7.2 (the arm64-only, BLESS_REASON-gated guarded writer:
// the BlessRequest aggregate, the BlessRefusal enum, and bless(request) ->
// expected<ManifestEntry, BlessRefusal>), §7.3 (the appended ManifestEntry field
// set, including fpFlagProof, arm64HostId and renderVersion), and §7.6 (renderVersion
// governance: the bless tool is where the bump decision is made and recorded next to
// BLESS_REASON). Normative contracts: ADR-013 C10 (bless runs ONLY on macOS arm64 —
// Linux/Windows blesses are REFUSED), C11 (refuses unless BLESS_REASON is non-empty),
// C14 (a blessed artifact whose claim derives from a ledger §2-§8 fact MUST carry its
// label), and ADR-023 V5/V6 (renderVersion increments IFF a bless changes a
// CLASS-EXACT hash or moves a CLASS-FP artifact outside its manifest tolerance band;
// the bless tool governs and records the bump).
//
// This module is OFFLINE bless-tool code, NEVER a test side-effect and NEVER on the
// audio thread [§7.2; ADR-013 Layer 3]. It is JUCE-free. It runs on the message
// thread / from the command line, so the RT no-alloc / no-lock invariants are NOT in
// play (it allocates std::string freely) [docs/design/11 §2.2].
//
// Header-only realization (vs the design tree's tools/bless/bless.cpp): a tools/bless/
// *.cpp is NOT picked up by the shared tests/CMakeLists glob (it compiles only
// tests/unit/*.cpp + core/**), and that CMakeLists is a do-not-edit file for the
// parallel fleet. A header-only writer keeps the bless logic self-contained, unit-
// testable from a globbed tests/unit/ TU, and free of any shared-file edit — the same
// pattern the sibling harness primitives use (tools/bless/Provenance.h, task 044;
// tests/golden/{Sha256,GoldenKey,CompareFp,Manifest}.h). The eventual command-line
// entrypoint (a thin main() built by the JUCE/tooling phase) links this header; it
// reads BLESS_REASON via readEnvNonEmpty() and the host arch via hostIsArm64(), then
// calls bless().
//
// What this can NEVER prove: that any artifact matches a real SH-101. A bless records
// "we changed/froze the DSP under this provenance," never "we got closer to hardware"
// [§1.3; ADR-013 owner ratification]. Every check here is self-consistency /
// provenance discipline.
//
// C++20 NOTE (deliberate, documented deviation from the §7.2 signature): the design
// names std::expected<ManifestEntry, BlessRefusal>, a C++23 library type. The build is
// locked to C++20 with extensions OFF (top CMakeLists CMAKE_CXX_STANDARD 20; the
// toolchain's libc++ does not ship <expected> under C++20), and bumping the project
// standard would touch a shared, do-not-edit file. We therefore use a minimal,
// expected-shaped BlessResult with the same value/error vocabulary (has_value(),
// value(), error(), operator bool); the public surface and behavior match the design.
// When the project moves to C++23 this is a drop-in alias for std::expected.

#pragma once

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "../../tests/golden/CompareFp.h"   // mw::golden::FpTolerance
#include "../../tests/golden/GoldenKey.h"    // mw::golden::GoldenKey / EngineTag / classes
#include "../../tests/golden/Manifest.h"     // mw::golden::manifest::ManifestEntry / HonestyLabel
#include "Provenance.h"                      // mw::bless::LabelKind / makeLabel / bumpRequired*

namespace mw::bless {

// The §7.3 provenance entry is the canonical manifest type (task 046). The bless tool
// produces one; the MANIFEST validator consumes it. Re-export it under the bless
// namespace so the §7.2 signature reads as written (bless -> ManifestEntry).
using ManifestEntry = mw::golden::manifest::ManifestEntry;

// ---------------------------------------------------------------------------
// BlessRefusal — the four guarded-writer refusal reasons [docs/design/11 §7.2].
// Order matches the §7.2 enum.
// ---------------------------------------------------------------------------
enum class BlessRefusal {
    NotArm64,            // host is not macOS arm64 [ADR-013 C10]
    EmptyReason,         // BLESS_REASON is unset / empty / whitespace-only [ADR-013 C11]
    MissingTolerance,    // a CLASS-FP request carries no per-corpus tolerance band [§7.2]
    MissingHonestyLabel  // a ledger §2-§8 derived artifact carries no honesty label [ADR-013 C14]
};

inline std::string_view refusalName(BlessRefusal r) noexcept {
    switch (r) {
        case BlessRefusal::NotArm64:            return "NotArm64";
        case BlessRefusal::EmptyReason:         return "EmptyReason";
        case BlessRefusal::MissingTolerance:    return "MissingTolerance";
        case BlessRefusal::MissingHonestyLabel: return "MissingHonestyLabel";
    }
    return "";
}

// ---------------------------------------------------------------------------
// BlessResult<T> — a minimal C++20 stand-in for std::expected<T, BlessRefusal>
// (see the C++20 NOTE in the file header). Same value/error vocabulary.
// ---------------------------------------------------------------------------
template <typename T>
class BlessResult {
public:
    BlessResult(T value) : store_(std::move(value)) {}            // success
    BlessResult(BlessRefusal err) : store_(err) {}               // refusal

    bool has_value() const noexcept { return std::holds_alternative<T>(store_); }
    explicit operator bool() const noexcept { return has_value(); }

    const T& value() const& { return std::get<T>(store_); }
    T&       value() &       { return std::get<T>(store_); }

    BlessRefusal error() const { return std::get<BlessRefusal>(store_); }

private:
    std::variant<T, BlessRefusal> store_;
};

// ---------------------------------------------------------------------------
// BlessRequest — the guarded-writer input [docs/design/11 §7.2 + §7.3].
//
// The §7.2 design names {key, blessReason, honestyLabels, tolerance}; this aggregate
// adds the remaining §7.3 provenance fields the appended ManifestEntry must record
// (blesser / isoDate / commitSha / compiler / fpFlagProof / arm64HostId / hashes),
// plus the inputs the renderVersion governor needs (prior hash + prior version +
// measured FP delta), and the honesty-binding flag (does this claim derive from a
// ledger §2-§8 fact). The CLI fills these from env / git / the render before calling.
// ---------------------------------------------------------------------------
struct BlessRequest {
    // --- §7.2 core inputs ---------------------------------------------------
    mw::golden::GoldenKey                  key{};            // engine/SR/seed/class etc.
    std::string                            blessReason;      // env BLESS_REASON, must be non-empty
    std::vector<HonestyLabel>              honestyLabels;    // ledger §2-§8 provenance (§7.4)
    std::optional<mw::golden::FpTolerance> tolerance;        // REQUIRED for CLASS-FP

    // --- §7.3 provenance the appended entry records -------------------------
    std::string artifactSha256;   // SHA-256 of the golden blob (64-hex)
    std::string artifactRef;      // blob path / test handle (orphan binding)
    std::string blesser;          // blesser identity
    std::string isoDate;          // ISO-8601 bless date
    std::string commitSha;        // repo commit SHA at bless time
    std::string compiler;         // compiler + version
    std::string fpFlagProof;      // -ffast-math off / -ffp-contract=off proof (§13.4)
    std::string arm64HostId;      // reference host identifier

    // --- honesty binding ----------------------------------------------------
    // True iff this artifact's claim derives from a ledger §2-§8 labelled fact; when
    // true, a honesty label is REQUIRED [ADR-013 C14; §7.4].
    bool derivesFromLedgerFact = false;

    // --- renderVersion governance inputs (§7.6; ADR-023 V5/V6) -------------
    int                  priorRenderVersion = 1;   // renderVersion the corpus is at
    std::optional<std::string> priorArtifactSha256;  // prior blob hash (CLASS-EXACT trigger)
    std::optional<double>      measuredMaxAbsDelta;   // max-abs delta vs prior (CLASS-FP trigger)

    // --- arm64 guard override (TEST ONLY) -----------------------------------
    // The host-arch guard reads this when set, so the arm64 / non-arm64 branches are
    // both exercisable deterministically on any CI host [ADR-013 C10]. Unset => the
    // real compile-time host arch (hostIsArm64()).
    std::optional<bool> simulatedHostIsArm64;
};

// ---------------------------------------------------------------------------
// Host-arch detection. macOS arm64 is the sole bless platform [ADR-013 C10]. Detected
// at compile time from the target arch macros (Apple silicon defines __aarch64__ /
// __arm64__). A bless on any non-arm64 host is REFUSED. The CLI passes this to the
// guard; a test can override via BlessRequest::simulatedHostIsArm64.
// ---------------------------------------------------------------------------
inline constexpr bool hostIsArm64() noexcept {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Engine-tag -> the §7.3 `engine` string ("ZDF" | "Huov"). Mirrors the controlled
// strings the MANIFEST parser reads (tests/golden/Manifest.h) [docs/design/11 §7.3].
// ---------------------------------------------------------------------------
inline std::string engineString(mw::golden::LadderEngine e) {
    switch (e) {
        case mw::golden::LadderEngine::ZDF:          return "ZDF";
        case mw::golden::LadderEngine::Huovilainen:  return "Huov";
    }
    return "";
}

// ---------------------------------------------------------------------------
// Environment helpers for the CLI (§7.2 reads BLESS_REASON from the env). Kept here so
// the bless tool's env contract is unit-testable. readEnvNonEmpty trims whitespace and
// returns "" for an unset / empty / whitespace-only variable, so the EmptyReason guard
// fires consistently whether the env var is missing or blank [ADR-013 C11].
// ---------------------------------------------------------------------------
inline bool isAllWhitespace(std::string_view s) noexcept {
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    return true;
}

inline std::string readEnvNonEmpty(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr) return {};
    std::string s(v);
    if (isAllWhitespace(s)) return {};
    return s;
}

inline void setEnv(const char* name, const char* value) noexcept {
    ::setenv(name, value, /*overwrite*/ 1);
}
inline void unsetEnv(const char* name) noexcept { ::unsetenv(name); }

// ===========================================================================
// bless — the guarded writer [docs/design/11 §7.2].
//
// Refusal order (each maps to a normative contract case):
//   1. NotArm64           — host is not arm64 [ADR-013 C10]
//   2. EmptyReason        — BLESS_REASON empty / whitespace [ADR-013 C11]
//   3. MissingTolerance   — CLASS-FP request with no tolerance band [§7.2]
//   4. MissingHonestyLabel— ledger §2-§8 derived artifact with no label [ADR-013 C14]
//
// On success it builds and returns the §7.3 ManifestEntry (the caller appends it to
// MANIFEST.toml via the Manifest module — file I/O is out of THIS task's scope per
// plan/backlog/045 Out-of-scope), governing renderVersion per §7.6 / ADR-023 V5/V6:
// the recorded renderVersion is bumped past priorRenderVersion IFF the bless changes a
// CLASS-EXACT hash or moves a CLASS-FP artifact outside its band; otherwise it holds.
// ===========================================================================
inline BlessResult<ManifestEntry> bless(const BlessRequest& req) {
    // (1) arm64-only guard. Linux/Windows blesses are REFUSED [ADR-013 C10].
    const bool onArm64 = req.simulatedHostIsArm64.value_or(hostIsArm64());
    if (!onArm64) return BlessRefusal::NotArm64;

    // (2) non-empty BLESS_REASON guard [ADR-013 C11]. A whitespace-only reason is
    // treated as empty so it cannot launder a blank justification past review.
    if (req.blessReason.empty() || isAllWhitespace(req.blessReason))
        return BlessRefusal::EmptyReason;

    const bool isFp = (req.key.cls == mw::golden::DeterminismClass::Fp);

    // (3) CLASS-FP requires a per-corpus tolerance band [§7.2; §7.3]. CLASS-EXACT
    // carries none (it is SHA-256 hash-compared).
    if (isFp && !req.tolerance.has_value())
        return BlessRefusal::MissingTolerance;

    // (4) a ledger §2-§8 derived claim MUST carry its honesty label [ADR-013 C14].
    if (req.derivesFromLedgerFact && req.honestyLabels.empty())
        return BlessRefusal::MissingHonestyLabel;

    // --- renderVersion governance [§7.6; ADR-023 V5/V6] --------------------
    // Decide the bump from the class-appropriate trigger ONLY (the two are never
    // conflated): EXACT bumps on a changed hash; FP bumps on a band-exceeding delta.
    bool bump = false;
    if (isFp) {
        const double band  = req.tolerance->maxAbsErr;
        const double delta = req.measuredMaxAbsDelta.value_or(0.0);
        bump = bumpRequired(CorpusClass::Fp, /*oldHash*/ "", /*newHash*/ "", band, delta);
    } else {
        const std::string oldHash = req.priorArtifactSha256.value_or(req.artifactSha256);
        bump = bumpRequired(CorpusClass::Exact, oldHash, req.artifactSha256,
                            /*band*/ 0.0, /*delta*/ 0.0);
    }
    const int recordedRenderVersion =
        bump ? (req.priorRenderVersion + 1) : req.priorRenderVersion;

    // --- Build the §7.3 ManifestEntry --------------------------------------
    ManifestEntry e{};
    e.artifactSha256   = req.artifactSha256;
    e.blesser          = req.blesser;
    e.isoDate          = req.isoDate;
    e.commitSha        = req.commitSha;
    e.blessReason      = req.blessReason;                       // recorded next to renderVersion [V6]
    e.engine           = engineString(req.key.engine.ladder);
    e.oversampleFactor = req.key.engine.oversampleFactor;
    e.sampleRate       = req.key.sampleRate;
    e.seed             = req.key.seed;
    e.blockSize        = req.key.blockSize;
    e.corpusClass      = isFp ? "FP" : "EXACT";
    e.tolerance        = isFp ? std::optional<double>(req.tolerance->maxAbsErr)
                              : std::nullopt;                   // EXACT carries no band
    e.compiler         = req.compiler;
    e.fpFlagProof      = req.fpFlagProof;
    e.arm64HostId      = req.arm64HostId;
    e.renderVersion    = recordedRenderVersion;                 // governed bump [§7.6; V6]
    e.artifactRef      = req.artifactRef;

    // Carry the honesty labels into the entry's controlled vocabulary. The
    // tools/bless Provenance label vocabulary and the manifest label vocabulary are
    // the SAME ledger set (research/13 §1.2); translate by token so the entry's labels
    // round-trip through the MANIFEST parser/validator [§7.4].
    for (const HonestyLabel& lbl : req.honestyLabels) {
        const std::string tok = labelToken(lbl.kind);
        if (const auto k = mw::golden::manifest::labelKindFromToken(tok))
            e.honestyLabels.push_back(
                mw::golden::manifest::HonestyLabel{*k,
                    lbl.ledgerRef.empty() ? ledgerRefOf(lbl.kind) : lbl.ledgerRef});
    }

    return e;
}

} // namespace mw::bless
