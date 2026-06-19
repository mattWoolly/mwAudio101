// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the migration chain (task 022). Test-case names begin
// with "migration". Realizes docs/design/06 §7.1-§7.4 and ADR-008 C10-C12,
// ADR-018 Q8 (os.factor alias), ADR-025 (per-step accent dropped).
//
// The §7.1 contract is written against juce::ValueTree; mwcore is JUCE-free
// [ADR-001 C1]. So the chain logic is implemented over the JUCE-free abstract
// tree seam mw::state::IMutableTree (the juce::ValueTree bridge that implements
// it is a separate plugin-stream task). This test drives that logic through a
// minimal in-memory test-double tree.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "state/Migration.h"
#include "version/EngineVersion.h"

namespace {

// A minimal in-memory IMutableTree test-double. JUCE-free; models exactly the
// surface the migration chain touches: an int "schemaVersion" attribute, a
// <PARAMS> child holding string-keyed param values (for the os.factor alias),
// and a <seq> child holding per-step children that may carry a stray "accent"
// attribute (pre-ADR-025 artifact) the chain must silently drop.
class FakeTree final : public mw::state::IMutableTree {
public:
    // --- root schemaVersion --------------------------------------------------
    std::optional<int> schemaVersion;

    // --- <PARAMS> string-keyed values (canonical or alias IDs) ---------------
    std::map<std::string, double> params;

    // --- <seq> per-step accent presence (true == a stray accent attr present) -
    std::vector<bool> stepHasAccent;

    // IMutableTree -----------------------------------------------------------
    std::optional<int> getSchemaVersion() const override { return schemaVersion; }
    void setSchemaVersion(int v) override { schemaVersion = v; }

    bool hasParam(std::string_view id) const override {
        return params.find(std::string{id}) != params.end();
    }
    std::optional<double> getParam(std::string_view id) const override {
        auto it = params.find(std::string{id});
        if (it == params.end()) return std::nullopt;
        return it->second;
    }
    void setParam(std::string_view id, double value) override {
        params[std::string{id}] = value;
    }

    int getNumSeqSteps() const override {
        return static_cast<int>(stepHasAccent.size());
    }
    bool seqStepHasAttribute(int stepIndex, std::string_view attr) const override {
        if (attr != std::string_view{"accent"}) return false;
        if (stepIndex < 0 || stepIndex >= getNumSeqSteps()) return false;
        return stepHasAccent[static_cast<std::size_t>(stepIndex)];
    }
    void removeSeqStepAttribute(int stepIndex, std::string_view attr) override {
        if (attr != std::string_view{"accent"}) return;
        if (stepIndex < 0 || stepIndex >= getNumSeqSteps()) return;
        stepHasAccent[static_cast<std::size_t>(stepIndex)] = false;
    }
};

} // namespace

TEST_CASE("migration: chain is empty at the v1 baseline", "[migration]") {
    // §7.3 version table: v1 is the baseline with NO migration steps.
    REQUIRE(mw::state::migrationChain().empty());
}

TEST_CASE("migration: chain length equals CURRENT_SCHEMA_VERSION minus 1", "[migration]") {
    // The chain is contiguous and indexed by source version: a v(n)->v(n+1)
    // step per breaking change. At CURRENT==1 there are zero steps. [§7.1; §7.3]
    REQUIRE(static_cast<int>(mw::state::migrationChain().size())
            == mw101::version::kCurrentSchemaVersion - 1);
}

TEST_CASE("migration: a v1 tree is identity and gets schemaVersion set to CURRENT",
          "[migration]") {
    FakeTree t;
    t.schemaVersion = 1;
    t.params["mw101.vcf.cutoff"] = 0.42;
    t.params["mw101.quality"] = 1.0;

    const auto before = t.params;
    mw::state::migrateToCurrent(t);

    // Identity on params at v1 baseline (no steps run). [§7.3; §7.1]
    REQUIRE(t.params == before);
    // schemaVersion is set to CURRENT. [§7.1]
    REQUIRE(t.schemaVersion.has_value());
    REQUIRE(*t.schemaVersion == mw101::version::kCurrentSchemaVersion);
}

TEST_CASE("migration: a schemaVersion greater than CURRENT is left bindable as a no-op",
          "[migration]") {
    // §7.1 / §8 L3: tolerant of schemaVersion > CURRENT — no-op down-bind, no
    // crash, no throw, the newer version stamp is PRESERVED (not clamped down)
    // so the raw-newer round-trip (L6) stays possible.
    FakeTree t;
    t.schemaVersion = mw101::version::kCurrentSchemaVersion + 5;
    t.params["mw101.vcf.cutoff"] = 0.7;
    t.params["mw101.future.param"] = 0.123; // unknown future param survives

    const auto beforeParams = t.params;
    const int beforeVersion = *t.schemaVersion;

    REQUIRE_NOTHROW(mw::state::migrateToCurrent(t));

    // No-op: nothing changed, the newer version is NOT pulled back to CURRENT.
    REQUIRE(t.params == beforeParams);
    REQUIRE(t.schemaVersion.has_value());
    REQUIRE(*t.schemaVersion == beforeVersion);
}

TEST_CASE("migration: a stray pre-ADR-025 per-step accent attribute is silently dropped",
          "[migration]") {
    // §7.3 / ADR-025: a per-step accent found in a v1 artifact authored by a
    // pre-ADR-025 tool is dropped silently — it was never a contracted field.
    FakeTree t;
    t.schemaVersion = 1;
    t.stepHasAccent = { false, true, false, true, true };

    mw::state::migrateToCurrent(t);

    for (int i = 0; i < t.getNumSeqSteps(); ++i) {
        INFO("step " << i << " must have no accent attribute after migration");
        REQUIRE_FALSE(t.seqStepHasAttribute(i, "accent"));
    }
    // schemaVersion still set to CURRENT.
    REQUIRE(*t.schemaVersion == mw101::version::kCurrentSchemaVersion);
}

TEST_CASE("migration: an accent drop on a newer-than-CURRENT tree is NOT applied",
          "[migration]") {
    // The accent silent-drop is part of the v1 normalization the chain performs
    // on bind. A newer-than-CURRENT down-bind is a pure no-op (§7.1), so it must
    // NOT touch step attributes either.
    FakeTree t;
    t.schemaVersion = mw101::version::kCurrentSchemaVersion + 1;
    t.stepHasAccent = { true, true };

    mw::state::migrateToCurrent(t);

    REQUIRE(t.seqStepHasAttribute(0, "accent"));
    REQUIRE(t.seqStepHasAttribute(1, "accent"));
}

TEST_CASE("migration: os.factor alias is copied to quality when present, leaving canonical untouched",
          "[migration]") {
    // §7.4 / ADR-018 Q8: mw101.os.factor is a deprecated migration alias; if it
    // was ever minted, its value is COPIED to the canonical mw101.quality. The
    // old slot stays (a hidden no-op) — a rename never edits an ID in place.
    SECTION("alias present, canonical absent -> canonical gets the alias value") {
        FakeTree t;
        t.schemaVersion = 1;
        t.params["mw101.os.factor"] = 2.0; // legacy HQ index

        mw::state::migrateToCurrent(t);

        REQUIRE(t.hasParam("mw101.quality"));
        REQUIRE(*t.getParam("mw101.quality") == 2.0);
        // The deprecated alias slot is retained (not deleted) [§7.4].
        REQUIRE(t.hasParam("mw101.os.factor"));
    }

    SECTION("alias absent -> chain is a no-op for quality (does not invent it)") {
        FakeTree t;
        t.schemaVersion = 1;
        t.params["mw101.quality"] = 1.0;

        mw::state::migrateToCurrent(t);

        REQUIRE(t.hasParam("mw101.quality"));
        REQUIRE(*t.getParam("mw101.quality") == 1.0);
        REQUIRE_FALSE(t.hasParam("mw101.os.factor"));
    }
}

TEST_CASE("migration: runs identically for presets and sessions (same chain instance)",
          "[migration]") {
    // §7.2: presets run through the SAME chain as sessions — a single shared
    // chain object, no per-context branch. Two captures yield the same chain.
    const auto& a = mw::state::migrationChain();
    const auto& b = mw::state::migrationChain();
    REQUIRE(&a == &b);
}

TEST_CASE("migration: a missing schemaVersion is treated as the v1 baseline and bound",
          "[migration]") {
    // Forward/backward tolerance (§7.2; ADR-008 C11): a tree with no
    // schemaVersion attribute (legacy / hand-authored) is bound at the v1
    // baseline rather than crashing, and stamped to CURRENT.
    FakeTree t;
    t.schemaVersion = std::nullopt;
    t.params["mw101.vco.tune"] = 0.0;
    t.stepHasAccent = { true };

    REQUIRE_NOTHROW(mw::state::migrateToCurrent(t));

    REQUIRE(t.schemaVersion.has_value());
    REQUIRE(*t.schemaVersion == mw101::version::kCurrentSchemaVersion);
    // v1 normalization still applied (accent dropped).
    REQUIRE_FALSE(t.seqStepHasAttribute(0, "accent"));
}

TEST_CASE("migration: migrateToCurrent never throws on an empty tree", "[migration]") {
    FakeTree t; // default: no schemaVersion, no params, no steps
    REQUIRE_NOTHROW(mw::state::migrateToCurrent(t));
    REQUIRE(t.schemaVersion.has_value());
    REQUIRE(*t.schemaVersion == mw101::version::kCurrentSchemaVersion);
}
