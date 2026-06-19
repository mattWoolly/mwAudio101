// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/CpuBudgetManifest.h — read the committed CPU-budget ceiling (plus its
// pinned engine + oversample factor) from the provenance MANIFEST.toml (task 076b).
//
// Realizes docs/design/11 §13.5: "ceilingMicrosPerBlock is (PI) — a committed wall-time
// ceiling whose value is pinned in the MANIFEST alongside engine + oversample factor."
// The CPU-budget gate reads the ceiling FROM MANIFEST (not hard-coded in the test) so a
// re-tune of the ceiling is a reviewed MANIFEST diff, exactly like every other blessed
// provenance fact [§13.5; ADR-013 C21]. Deriving/committing the ceiling value itself is
// out of scope for the gate [task 076b Out-of-scope].
//
// The ceiling lives in a dedicated `[cpu_budget]` table in MANIFEST.toml — NOT a
// `[[golden]]` array-of-tables entry — so it does not perturb the golden-entry count the
// sibling Manifest.h (046) parser and its ManifestTest see (that parser only reads
// `[[golden]]` tables and ignores other sections + top-level keys, so the two readers
// coexist on the one file without either touching the other's grammar).
//
// Header-only, JUCE-free, OFFLINE harness code [docs/design/11 §2.2]. It carries its own
// tiny `[cpu_budget]`-table reader rather than extending Manifest.h (a sibling task's
// file): there is no TOML library wired into the build (cmake/Dependencies.cmake pins
// only Catch2 + the lazily-fetched JUCE), and the grammar here is a single flat table of
// string/int/float values. Same self-contained pattern as the sibling tests/golden/
// Sha256.h / GoldenKey.h / Stimulus.h / Manifest.h.

#pragma once

#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace mw::golden::cpu {

// ---------------------------------------------------------------------------
// CpuBudgetEntry — the §13.5 provenance fields for the CPU-budget gate: the committed
// wall-time ceiling and the engine + oversample factor it is pinned alongside. `found`
// is false if no `[cpu_budget]` table is present (so a missing pin is an OBJECTIVE
// failure, never a silent pass).
// ---------------------------------------------------------------------------
struct CpuBudgetEntry {
    bool   found = false;                  // a [cpu_budget] table was present + parsed
    double ceilingMicrosPerBlock = 0.0;    // the committed (PI) ceiling [§13.5]
    std::string engine;                    // pinned ladder tag ("ZDF" | "Huov")
    int    oversampleFactor = 0;           // pinned oversample factor (2 == worst case)
    double sampleRate = 0.0;               // the reference render rate the ceiling is at
    int    blockSize  = 0;                 // the reference block size the ceiling is at
    std::string arm64HostId;               // reference host id (the ceiling is host-relative)
};

namespace detail {

inline std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

inline std::string unquote(std::string_view s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    return std::string(s);
}

// Strip an unquoted, end-of-line `# comment` (no '#' appears inside the values here).
inline std::string_view stripComment(std::string_view line) noexcept {
    bool inStr = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inStr = !inStr;
        else if (line[i] == '#' && !inStr) return line.substr(0, i);
    }
    return line;
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
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        acc = acc * 10 + (c - '0');
    }
    out = sign * acc;
    return true;
}

inline bool parseDouble(std::string_view v, double& out) noexcept {
    const std::string s(std::string(trim(v)));
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        out = std::stod(s, &pos);
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// parseCpuBudget — read the single `[cpu_budget]` table from a MANIFEST.toml body.
//
// Grammar: ignore everything until a `[cpu_budget]` header line; then read `key = value`
// lines (string / int / float) until the next bracketed section header (`[` or `[[`) or
// end of file. Returns found == false if no `[cpu_budget]` table is present, so a caller
// can turn a missing pin into a hard failure (never a silent pass) [ADR-013 C21].
// ---------------------------------------------------------------------------
[[nodiscard]] inline CpuBudgetEntry parseCpuBudget(std::string_view text) {
    CpuBudgetEntry e{};
    bool inSection = false;

    std::string line;
    std::istringstream in{std::string(text)};
    while (std::getline(in, line)) {
        std::string_view t = detail::trim(detail::stripComment(line));
        if (t.empty()) continue;

        if (t.front() == '[') {
            // Entering/leaving a table. The CPU-budget pin is the flat `[cpu_budget]`
            // table; any other header ends it.
            inSection = (t == "[cpu_budget]");
            if (inSection) e.found = true;
            continue;
        }
        if (!inSection) continue;

        const std::size_t eq = t.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = detail::trim(t.substr(0, eq));
        const std::string_view val = detail::trim(t.substr(eq + 1));

        if (key == "ceilingMicrosPerBlock") {
            double v{};
            if (detail::parseDouble(val, v)) e.ceilingMicrosPerBlock = v;
        } else if (key == "engine") {
            e.engine = detail::unquote(val);
        } else if (key == "oversampleFactor") {
            long long v{};
            if (detail::parseInt(val, v)) e.oversampleFactor = static_cast<int>(v);
        } else if (key == "sampleRate") {
            double v{};
            if (detail::parseDouble(val, v)) e.sampleRate = v;
        } else if (key == "blockSize") {
            long long v{};
            if (detail::parseInt(val, v)) e.blockSize = static_cast<int>(v);
        } else if (key == "arm64HostId") {
            e.arm64HostId = detail::unquote(val);
        }
        // unknown keys ignored (forward-compatible)
    }
    return e;
}

} // namespace mw::golden::cpu
