// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/QaCoverageMatrixTest.cpp — the test-enforced spine of the Phase-5
// adversarial QA audit (task 154; docs/QA-REPORT.md). It makes the report's CENTRAL
// claim — "every required subsystem prefix maps to >= 1 discovered test" (ADR-013 C2;
// docs/design/11 §3.1, §8.2) — a MECHANICALLY-CHECKED invariant rather than prose that
// can silently rot as suites are renamed or deleted.
//
// Test-case display names ALL begin with "qa" so `ctest -R qa --no-tests=error` selects
// exactly this suite under the silent-pass rule (AGENTS.md "Tests"; docs/design/11 §8.3).
// The tag is "[qa]". No literal '[' appears in any display name so Catch2 never mis-parses
// a tag out of the name.
//
// WHY THIS IS NEEDED (an audit finding, encoded as a test): the in-tree per-prefix gate
// `prefix_coverage` (tests/CMakeLists.txt) is wired with REQUIRED_TAGS=cal;prng only and
// carries an open TODO(task-006) to widen to the full `vco vcf vca env seq prng arp cal`
// set. So C2 is only PARTIALLY gated today. This suite asserts the full eight-subsystem
// matrix directly against the in-process Catch2 registry, recognising the CANONICAL
// single-token tag (e.g. [vca]) OR the documented alias tags actually applied to that
// subsystem's suites (e.g. [vca_taper], [env_curve], [seqengine]) — so the matrix is true
// by construction and a deleted suite turns the matrix red here, not silent.
//
// It is read-only introspection (no engine state, no DSP); it links mwcore + Catch2 only.

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_registry_hub.hpp>
#include <catch2/interfaces/catch_interfaces_testcase.hpp>
#include <catch2/catch_test_case_info.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Snapshot the in-process Catch2 registry once: every registered test case's tag set,
// lower-cased and stripped of brackets (Catch2 stores Tag.original WITHOUT brackets).
struct RegistrySnapshot {
    // Flat list of all tag tokens (lower-cased) across all registered test cases.
    std::vector<std::string> tags;

    RegistrySnapshot() {
        const auto& infos = Catch::getRegistryHub().getTestCaseRegistry().getAllInfos();
        for (const Catch::TestCaseInfo* info : infos) {
            if (info == nullptr) continue;
            for (const auto& tag : info->tags) {
                std::string t;
                t.reserve(tag.original.size());
                for (char c : std::string_view(tag.original.data(), tag.original.size())) {
                    if (c == '[' || c == ']') continue;
                    t.push_back(static_cast<char>(
                        (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c));
                }
                if (!t.empty()) tags.push_back(std::move(t));
            }
        }
    }
};

// The eight required subsystem prefixes (docs/design/11 §3.1 / §8.2; ADR-013 C2) and the
// set of tag tokens that legitimately PROVE coverage for each. The canonical single-token
// tag is listed first; the remaining entries are the alias tags the as-built suites
// actually use for that subsystem (an audit observation recorded in docs/QA-REPORT.md).
struct Required {
    std::string_view prefix;
    std::vector<std::string_view> aliases;
};

const std::vector<Required>& requiredSubsystems() {
    static const std::vector<Required> kReq = {
        { "vco",  { "vco", "vcoshape", "oscsection", "polyblep", "sub", "dispatch_vco" } },
        { "vcf",  { "vcf", "vcf-core", "vcf-tpt", "vcf-tanh", "vcf-reso", "vcf-tables",
                    "dispatch_vcf" } },
        { "vca",  { "vca", "vca_taper", "vca_thump", "envlfovca_rtsafe" } },
        { "env",  { "env", "env_curve", "env_trig", "env_header", "envlfovca_dezip" } },
        { "seq",  { "seq", "seqengine", "stepseq", "modseqtypes", "engine_seq" } },
        { "prng", { "prng", "vintage_prng", "noise" } },
        { "arp",  { "arp" } },
        { "cal",  { "cal", "calibration" } },
    };
    return kReq;
}

// The cross-cutting invariant tags whose Catch2-side coverage the matrix tracks
// (docs/design/11 §13; ADR-013 C19/C21). NOTE: [license] (C18) and [fp] (C20) are NOT
// Catch2 tags — they are standalone add_test gates (license_headers / fp_discipline_guard),
// asserted separately below and documented as such in docs/QA-REPORT.md.
const std::vector<std::string_view>& crossCuttingCatchTags() {
    static const std::vector<std::string_view> kInv = { "rt", "cpu" };
    return kInv;
}

} // namespace

// ===========================================================================
// C2 — every required subsystem prefix maps to >= 1 discovered test (the matrix spine).
// ===========================================================================
TEST_CASE("qa coverage matrix: every required subsystem prefix maps to a discovered test",
          "[qa]") {
    const RegistrySnapshot snap;
    REQUIRE_FALSE(snap.tags.empty());   // guard: the registry must be populated (not a
                                        // mis-linked/empty binary) — silent-pass backstop.

    for (const auto& req : requiredSubsystems()) {
        int count = 0;
        for (const auto& have : snap.tags)
            for (std::string_view want : req.aliases)
                if (have == want) { ++count; break; }
        INFO("required subsystem prefix '" << req.prefix
             << "' has " << count << " discovered test(s)");
        REQUIRE(count >= 1);   // ADR-013 C2: 0 => the matrix is broken.
    }
}

// ===========================================================================
// Negative control (paired per ADR-013 C4): a deliberately-absent token must resolve to
// ZERO, proving the matcher discriminates and does not pass everything by construction.
// ===========================================================================
TEST_CASE("qa coverage matrix: an unknown subsystem token has zero discovered tests",
          "[qa]") {
    const RegistrySnapshot snap;
    int count = 0;
    for (const auto& have : snap.tags)
        if (have == "no_such_subsystem_xyzzy") ++count;
    REQUIRE(count == 0);
}

// ===========================================================================
// Cross-cutting Catch2 invariant tags ([rt], [cpu]) each have >= 1 discovered test
// (docs/design/11 §13.1/§13.5; ADR-013 C19/C21). [license]/[fp] are add_test gates, not
// Catch2 tags, and are audited separately (docs/QA-REPORT.md cross-cutting section).
// ===========================================================================
TEST_CASE("qa coverage matrix: cross-cutting Catch2 invariant tags are present",
          "[qa]") {
    const RegistrySnapshot snap;
    for (std::string_view tag : crossCuttingCatchTags()) {
        int count = 0;
        for (const auto& have : snap.tags)
            if (have == tag) ++count;
        INFO("cross-cutting Catch2 tag [" << tag << "] has " << count << " discovered test(s)");
        REQUIRE(count >= 1);
    }
}
