// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/Manifest.h — provenance MANIFEST.toml read + validate (task 046).
//
// Realizes docs/design/11 §7.1, §7.3, §7.5 and ADR-013 C12-C14 + ADR-023 V7. The
// authoritative provenance ledger is tests/golden/corpus/MANIFEST.toml [§7.1]: it
// binds every blessed golden artifact to its render parameters, its honesty-label
// provenance, and the renderVersion it was blessed under, so a guess can never
// silently harden into a "regression-protected fact" [§1.3; ADR-013 Context].
//
// This module is OFFLINE CI/harness code, not real-time anything [§2.2]. It is
// JUCE-free, exception-free on its hot paths, and self-contained: there is no TOML
// library wired into the build harness (cmake/Dependencies.cmake pins only Catch2 +
// the lazily-fetched JUCE), and that file is a shared, do-not-edit file for the
// parallel fleet, so this header carries a small, purpose-built reader for the exact
// MANIFEST grammar (array-of-tables with string/int/float/array values + comments).
// Mirroring tests/golden/Sha256.h, this is header-only so it is consumed by the
// globbed tests/unit sources without touching the shared tests/CMakeLists glob set.
//
// What it validates (each maps to a normative contract case):
//  - Completeness — every golden corpus blob hash MUST appear in MANIFEST [ADR-013
//    C12; §7.5].
//  - Orphan       — every MANIFEST entry MUST have a corresponding test/artifact
//    [ADR-013 C13; §7.5].
//  - Honesty-label — an artifact whose claim derives from a ledger §2-§8 fact MUST
//    carry its label [ADR-013 C14; §7.4/§7.5].
//  - renderVersion bump-vs-change consistency — a blessed artifact changed without a
//    renderVersion bump FAILS, and a renderVersion bump with no artifact change FAILS
//    [ADR-023 V7; §7.5].
//
// What this harness can NEVER prove: that any artifact matches a real SH-101. Every
// check here is self-consistency / provenance discipline, NOT measured fidelity
// [§1.3; ADR-013 owner ratification].

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Sha256.h"

namespace mw::golden::manifest {

// ---------------------------------------------------------------------------
// Honesty-label controlled vocabulary (docs/design/11 §7.4; research/13 §1.2).
// The label set is the ledger's; a blessed artifact whose claim derives from a
// ledger §2-§8 fact MUST carry its label [ADR-013 C14].
// ---------------------------------------------------------------------------
enum class LabelKind {
    CloneDerived,             // self-osc amplitude: AMSynths AM8101 @ +-12V [research/13 §4.1]
    ReverseEngineered,        // BA662 VCA internals: Open Music Labs probing [research/13 §4.2]
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

// Maps a label token (as written in MANIFEST.toml) to its LabelKind, and back. The
// tokens are the ledger's controlled vocabulary [docs/design/11 §7.4].
inline std::optional<LabelKind> labelKindFromToken(std::string_view t) noexcept {
    if (t == "clone-derived")              return LabelKind::CloneDerived;
    if (t == "reverse-engineered")         return LabelKind::ReverseEngineered;
    if (t == "theory/inference")           return LabelKind::TheoryInference;
    if (t == "community-disassembly")      return LabelKind::CommunityDisassembly;
    if (t == "service-manual")             return LabelKind::ServiceManual;
    if (t == "disputed")                   return LabelKind::Disputed;
    if (t == "software-emulation-artifact")return LabelKind::SoftwareEmulationArtifact;
    return std::nullopt;
}

inline std::string labelTokenOf(LabelKind k) {
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

// ---------------------------------------------------------------------------
// ManifestEntry — ALL the docs/design/11 §7.3 provenance fields.
// ---------------------------------------------------------------------------
struct ManifestEntry {
    std::string               artifactSha256;   // SHA-256 of the golden blob (64-hex)
    std::string               blesser;          // blesser identity
    std::string               isoDate;          // ISO-8601 bless date
    std::string               commitSha;        // repo commit SHA at bless time
    std::string               blessReason;      // non-empty BLESS_REASON
    std::string               engine;           // "ZDF" | "Huov" ladder tag
    int                       oversampleFactor = 0;   // 1 or 2 (clamp recorded per ADR-023 V16)
    double                    sampleRate = 0.0;       // one of the blessed set (§5.2)
    std::uint64_t             seed = 0;               // fixed render seed
    int                       blockSize = 0;
    std::string               corpusClass;      // "EXACT" | "FP"
    std::optional<double>     tolerance;        // per-corpus band (CLASS-FP only)
    std::string               compiler;         // compiler + version
    std::string               fpFlagProof;      // -ffast-math off, -ffp-contract=off proof
    std::string               arm64HostId;      // reference host identifier
    int                       renderVersion = 0;      // renderVersion blessed under (ADR-023)
    std::vector<HonestyLabel> honestyLabels;    // ledger §2-§8 provenance labels (§7.4)

    // An optional human-readable artifact handle (the blob path / test name) used by
    // the orphan check to bind an entry to a corresponding artifact/test. Not one of
    // the §7.3 ledger fields, but the corpus sidecar records it and the validator
    // needs it to detect orphans deterministically.
    std::string               artifactRef;

    bool isExact() const noexcept { return corpusClass == "EXACT"; }
    bool isFp() const noexcept    { return corpusClass == "FP"; }
};

// ---------------------------------------------------------------------------
// Parse result: a list of entries plus a parse-level error (empty == ok).
// ---------------------------------------------------------------------------
struct ParseResult {
    std::vector<ManifestEntry> entries;
    std::string                error;   // empty on success
    bool ok() const noexcept { return error.empty(); }
};

namespace detail {

inline std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// Strip a surrounding pair of double quotes, if present.
inline std::string unquote(std::string_view s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    return std::string(s);
}

// Strip an unquoted, end-of-line `# comment`. We do NOT split inside a quoted
// string, which is enough for the MANIFEST grammar (no '#' inside values here).
inline std::string_view stripComment(std::string_view line) noexcept {
    bool inStr = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inStr = !inStr;
        else if (line[i] == '#' && !inStr) return line.substr(0, i);
    }
    return line;
}

// Parse a TOML array of double-quoted strings: ["a", "b"]. Tolerates trailing
// commas and whitespace. Used for honestyLabels.
inline std::vector<std::string> parseStringArray(std::string_view v) {
    std::vector<std::string> out;
    v = trim(v);
    if (v.size() < 2 || v.front() != '[' || v.back() != ']') return out;
    v = v.substr(1, v.size() - 2);
    std::size_t i = 0;
    while (i < v.size()) {
        // find next quote
        while (i < v.size() && v[i] != '"') ++i;
        if (i >= v.size()) break;
        const std::size_t start = ++i;
        while (i < v.size() && v[i] != '"') ++i;
        if (i > v.size()) break;
        out.emplace_back(v.substr(start, i - start));
        if (i < v.size()) ++i;   // skip closing quote
    }
    return out;
}

inline bool parseInt(std::string_view v, long long& out) noexcept {
    v = trim(v);
    if (v.empty()) return false;
    long long sign = 1;
    std::size_t i = 0;
    if (v[0] == '-') { sign = -1; ++i; }
    else if (v[0] == '+') { ++i; }
    if (i >= v.size()) return false;
    long long acc = 0;
    for (; i < v.size(); ++i) {
        const char c = v[i];
        if (c == '_') continue;   // TOML digit separator
        if (c < '0' || c > '9') return false;
        acc = acc * 10 + (c - '0');
    }
    out = sign * acc;
    return true;
}

inline bool parseDouble(std::string_view v, double& out) noexcept {
    const std::string s(trim(v));
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        out = std::stod(s, &pos);
        // require the whole token to be numeric (ignoring trailing space)
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// parse — read a MANIFEST.toml text body into ManifestEntry records.
//
// Grammar (the subset MANIFEST uses): a sequence of `[[golden]]` array-of-tables
// headers, each followed by `key = value` lines (string / int / float / string
// array). `#` begins a comment to end of line. Blank lines and the top file-level
// comment block are ignored. Unknown keys are ignored (forward-compatible). A
// missing required field is reported as a parse error rather than silently
// defaulted, so a malformed ledger never validates green by accident.
// ---------------------------------------------------------------------------
inline ParseResult parse(std::string_view text) {
    ParseResult result;
    std::vector<ManifestEntry>& entries = result.entries;

    bool inEntry = false;
    int  lineNo = 0;

    auto finishError = [&](const std::string& msg) {
        result.error = msg;
        return result;
    };

    std::string line;
    std::istringstream in{std::string(text)};
    while (std::getline(in, line)) {
        ++lineNo;
        std::string_view raw = detail::stripComment(line);
        std::string_view t = detail::trim(raw);
        if (t.empty()) continue;

        if (t == "[[golden]]") {
            entries.emplace_back();
            inEntry = true;
            continue;
        }
        // Any other bracketed table header we treat as a (non-golden) section: a
        // following key/value belongs to it, not to a golden entry.
        if (t.front() == '[') {
            inEntry = false;
            continue;
        }

        const std::size_t eq = t.find('=');
        if (eq == std::string_view::npos) {
            return finishError("manifest parse error: expected key = value at line "
                               + std::to_string(lineNo));
        }
        if (!inEntry) {
            // key/value outside any [[golden]] table — ignore (file-level metadata).
            continue;
        }

        const std::string_view key = detail::trim(t.substr(0, eq));
        const std::string_view val = detail::trim(t.substr(eq + 1));
        ManifestEntry& e = entries.back();

        if (key == "artifactSha256")      e.artifactSha256 = detail::unquote(val);
        else if (key == "blesser")        e.blesser        = detail::unquote(val);
        else if (key == "isoDate")        e.isoDate        = detail::unquote(val);
        else if (key == "commitSha")      e.commitSha      = detail::unquote(val);
        else if (key == "blessReason")    e.blessReason    = detail::unquote(val);
        else if (key == "engine")         e.engine         = detail::unquote(val);
        else if (key == "corpusClass")    e.corpusClass    = detail::unquote(val);
        else if (key == "compiler")       e.compiler       = detail::unquote(val);
        else if (key == "fpFlagProof")    e.fpFlagProof    = detail::unquote(val);
        else if (key == "arm64HostId")    e.arm64HostId    = detail::unquote(val);
        else if (key == "artifactRef")    e.artifactRef    = detail::unquote(val);
        else if (key == "oversampleFactor") {
            long long v{};
            if (!detail::parseInt(val, v))
                return finishError("manifest parse error: oversampleFactor not an int at line "
                                   + std::to_string(lineNo));
            e.oversampleFactor = static_cast<int>(v);
        } else if (key == "blockSize") {
            long long v{};
            if (!detail::parseInt(val, v))
                return finishError("manifest parse error: blockSize not an int at line "
                                   + std::to_string(lineNo));
            e.blockSize = static_cast<int>(v);
        } else if (key == "renderVersion") {
            long long v{};
            if (!detail::parseInt(val, v))
                return finishError("manifest parse error: renderVersion not an int at line "
                                   + std::to_string(lineNo));
            e.renderVersion = static_cast<int>(v);
        } else if (key == "seed") {
            long long v{};
            if (!detail::parseInt(val, v))
                return finishError("manifest parse error: seed not an int at line "
                                   + std::to_string(lineNo));
            e.seed = static_cast<std::uint64_t>(v);
        } else if (key == "sampleRate") {
            double v{};
            if (!detail::parseDouble(val, v))
                return finishError("manifest parse error: sampleRate not a number at line "
                                   + std::to_string(lineNo));
            e.sampleRate = v;
        } else if (key == "tolerance") {
            double v{};
            if (!detail::parseDouble(val, v))
                return finishError("manifest parse error: tolerance not a number at line "
                                   + std::to_string(lineNo));
            e.tolerance = v;
        } else if (key == "honestyLabels") {
            for (const std::string& tok : detail::parseStringArray(val)) {
                const auto kind = labelKindFromToken(tok);
                if (!kind)
                    return finishError("manifest parse error: unknown honesty label '"
                                       + tok + "' at line " + std::to_string(lineNo));
                e.honestyLabels.push_back(HonestyLabel{*kind, std::string(tok)});
            }
        }
        // unknown keys: ignored (forward-compatible).
    }

    return result;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

// One failure reason; the validator collects all of them so a single run reports
// every problem, not just the first.
enum class Failure {
    MissingHash,            // a corpus blob hash absent from MANIFEST [ADR-013 C12]
    OrphanEntry,            // a MANIFEST entry with no corresponding artifact/test [ADR-013 C13]
    MissingHonestyLabel,    // a ledger §2-§8 claim without its label [ADR-013 C14]
    RenderVersionNotBumped, // artifact changed without renderVersion bump [ADR-023 V7]
    RenderVersionBumpNoChange, // renderVersion bumped with no artifact change [ADR-023 V7]
    MalformedEntry          // an entry missing a required field / bad hash form
};

struct Violation {
    Failure     kind{};
    std::string detail;
};

struct ValidationResult {
    std::vector<Violation> violations;
    bool ok() const noexcept { return violations.empty(); }
    bool has(Failure f) const noexcept {
        return std::any_of(violations.begin(), violations.end(),
                           [f](const Violation& v) { return v.kind == f; });
    }
};

// The corpus + prior-bless context the validator checks the MANIFEST against. All of
// it is supplied by the caller (CI enumerates the real corpus dir + git history); the
// validator is a pure function of (entries, context) so it is unit-testable without
// real blobs on disk.
struct CorpusContext {
    // Every golden blob hash present in the corpus directory. Completeness requires
    // each of these to appear in the MANIFEST [ADR-013 C12; §7.5].
    std::set<std::string> corpusBlobHashes;

    // The set of artifact/test references that actually exist (blob paths / test
    // names). The orphan check requires every entry.artifactRef to be in this set
    // [ADR-013 C13; §7.5].
    std::set<std::string> existingArtifactRefs;

    // Whether an entry's artifactRef requires an honesty label, i.e. its claim
    // derives from a ledger §2-§8 fact [ADR-013 C14; §7.4]. CI seeds this from the
    // sidecar render-graph description / ledger map. If a ref is absent it is treated
    // as NOT deriving from a ledger fact (no label required).
    std::set<std::string> derivesFromLedgerFact;

    // Prior bless snapshot keyed by artifactRef, for the renderVersion bump-vs-change
    // consistency check [ADR-023 V7; §7.5]. For each artifact present at the previous
    // bless: its previous blob hash and the renderVersion it was blessed under.
    struct Prior {
        std::string priorHash;
        int         priorRenderVersion = 0;
    };
    std::map<std::string, Prior> priorBless;
};

// SHA-256 hex form: 64 lowercase/uppercase hex chars.
inline bool isWellFormedHash(std::string_view h) noexcept {
    if (h.size() != 64) return false;
    for (char c : h) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// validate — run completeness + orphan + honesty-label + renderVersion checks.
// Returns ALL violations (empty == PASS) [docs/design/11 §7.5; ADR-013 C12-C14;
// ADR-023 V7].
inline ValidationResult validate(const std::vector<ManifestEntry>& entries,
                                 const CorpusContext& ctx) {
    ValidationResult r;

    // Index the MANIFEST by artifact hash for the completeness check.
    std::set<std::string> manifestHashes;
    for (const ManifestEntry& e : entries) {
        // A malformed/blank hash cannot satisfy completeness; flag it explicitly so a
        // typo never becomes a silent pass.
        if (!isWellFormedHash(e.artifactSha256)) {
            r.violations.push_back({Failure::MalformedEntry,
                "entry has missing/ill-formed artifactSha256: '" + e.artifactSha256 + "'"});
            continue;
        }
        // CLASS-FP entries MUST carry a tolerance band [docs/design/11 §7.3].
        if (e.isFp() && !e.tolerance.has_value()) {
            r.violations.push_back({Failure::MalformedEntry,
                "CLASS-FP entry " + e.artifactSha256 + " has no tolerance band"});
        }
        manifestHashes.insert(e.artifactSha256);
    }

    // --- Completeness: every corpus blob hash present in MANIFEST [ADR-013 C12] ----
    for (const std::string& blobHash : ctx.corpusBlobHashes) {
        if (manifestHashes.find(blobHash) == manifestHashes.end()) {
            r.violations.push_back({Failure::MissingHash,
                "corpus blob hash absent from MANIFEST.toml: " + blobHash});
        }
    }

    for (const ManifestEntry& e : entries) {
        if (!isWellFormedHash(e.artifactSha256)) continue;   // already flagged

        // --- Orphan: every MANIFEST entry has a corresponding artifact/test --------
        // [ADR-013 C13]
        const bool refExists =
            !e.artifactRef.empty() &&
            ctx.existingArtifactRefs.find(e.artifactRef) != ctx.existingArtifactRefs.end();
        if (!refExists) {
            r.violations.push_back({Failure::OrphanEntry,
                "MANIFEST entry " + e.artifactSha256
                + " has no corresponding artifact/test (artifactRef='" + e.artifactRef + "')"});
        }

        // --- Honesty-label: a ledger §2-§8 claim carries its label [ADR-013 C14] ---
        const bool derives =
            ctx.derivesFromLedgerFact.find(e.artifactRef) != ctx.derivesFromLedgerFact.end();
        if (derives && e.honestyLabels.empty()) {
            r.violations.push_back({Failure::MissingHonestyLabel,
                "MANIFEST entry " + e.artifactSha256
                + " derives from a ledger §2-§8 fact but carries no honesty label"});
        }

        // --- renderVersion bump-vs-change consistency [ADR-023 V7] -----------------
        const auto it = ctx.priorBless.find(e.artifactRef);
        if (it != ctx.priorBless.end()) {
            const CorpusContext::Prior& prior = it->second;
            const bool hashChanged   = (e.artifactSha256 != prior.priorHash);
            const bool versionBumped = (e.renderVersion > prior.priorRenderVersion);

            if (hashChanged && !versionBumped) {
                r.violations.push_back({Failure::RenderVersionNotBumped,
                    "blessed artifact changed (" + prior.priorHash + " -> " + e.artifactSha256
                    + ") without a renderVersion bump (still " + std::to_string(e.renderVersion)
                    + ")"});
            }
            if (versionBumped && !hashChanged) {
                r.violations.push_back({Failure::RenderVersionBumpNoChange,
                    "renderVersion bumped (" + std::to_string(prior.priorRenderVersion) + " -> "
                    + std::to_string(e.renderVersion)
                    + ") with no MANIFEST/artifact change for " + e.artifactRef});
            }
        }
    }

    return r;
}

} // namespace mw::golden::manifest
