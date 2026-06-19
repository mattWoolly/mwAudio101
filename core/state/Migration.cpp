// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/state/Migration.cpp — implementation of the migration chain (task 022).
// Realizes docs/design/06 §7.1-§7.4; ADR-008 C10-C12; ADR-018 Q8; ADR-025.

#include "state/Migration.h"

#include "params/ParamIDs.h"      // kDeprecatedOsFactor ("mw101.os.factor")
#include "version/EngineVersion.h" // kCurrentSchemaVersion

#include <string_view>

namespace mw::state {

namespace {

// The canonical Quality ID is "mw101.quality" [docs/design/06 §3.0, §3.7]. Its
// constexpr constant is owned by core/params/ParamIDs.h (task 014, not yet
// landed there); referenced by the literal here to avoid editing that shared
// header. ParamIDs.h already owns the deprecated alias slot kDeprecatedOsFactor.
inline constexpr const char* kQualityId = "mw101.quality";

// §7.4 / ADR-018 Q8: if the deprecated mw101.os.factor alias was ever minted,
// COPY its value to the canonical mw101.quality (never overwriting an already
// present canonical value), and LEAVE the old slot in place (a rename never
// edits an ID in place — the alias becomes a hidden no-op). This is part of the
// v1 bind normalization, not a versioned v(n)->v(n+1) step.
void applyOsFactorAlias(IMutableTree& tree) {
    if (!tree.hasParam(mw::params::ids::kDeprecatedOsFactor))
        return;
    if (tree.hasParam(kQualityId))
        return; // canonical already authoritative; do not clobber
    if (const auto v = tree.getParam(mw::params::ids::kDeprecatedOsFactor))
        tree.setParam(kQualityId, *v);
}

// §7.3 / ADR-025: a per-step accent attribute found in a v1 artifact authored by
// a pre-ADR-025 tool is dropped silently — it was never a contracted field.
void dropStrayAccentAttributes(IMutableTree& tree) {
    const int n = tree.getNumSeqSteps();
    for (int i = 0; i < n; ++i)
        if (tree.seqStepHasAttribute(i, std::string_view{"accent"}))
            tree.removeSeqStepAttribute(i, std::string_view{"accent"});
}

} // namespace

const std::vector<MigrationStep>& migrationChain() {
    // §7.3 version table: schemaVersion 1 is the initial contract with NO
    // migration. The chain is therefore EMPTY at the v1 baseline. A single
    // function-local static so presets and sessions share ONE chain [§7.2].
    //
    // Every future breaking change (new param, re-skew of a shipped range,
    // rename via alias, structural change) appends exactly one ordered
    // v(n)->v(n+1) step here (plus its frozen golden fixture, ADR-008 C12). A
    // pure DSP re-tune does NOT bump schemaVersion and MUST NOT add a no-op step
    // — it bumps renderVersion instead [ADR-023 V4].
    static const std::vector<MigrationStep> kChain{};
    return kChain;
}

void migrateToCurrent(IMutableTree& canonical) {
    constexpr int kCurrent = mw101::version::kCurrentSchemaVersion;

    // A missing schemaVersion is treated as the v1 baseline (legacy /
    // hand-authored tree) rather than crashing [§7.2; ADR-008 C11].
    const int from = canonical.getSchemaVersion().value_or(1);

    // §7.1 / §8 L3: tolerant of schemaVersion > CURRENT — a pure no-op
    // down-bind. The newer stamp is PRESERVED (not pulled back to CURRENT) so
    // the raw-newer round-trip (L6) stays possible, and NO v1 normalization is
    // applied (the newer schema may legitimately carry fields v1 would strip).
    if (from > kCurrent)
        return;

    // Run the ordered, contiguous chain for [from, CURRENT). At the v1 baseline
    // the chain is empty, so this loop is a no-op (identity on params).
    const auto& chain = migrationChain();
    for (int v = from; v < kCurrent; ++v) {
        const auto idx = static_cast<std::size_t>(v - 1); // v(n)->v(n+1) at index n-1
        if (idx < chain.size())
            chain[idx](canonical);
    }

    // v1 bind normalization (idempotent; always safe to apply once at/under
    // CURRENT): drop any stray pre-ADR-025 per-step accent attribute, and copy
    // the deprecated os.factor alias to the canonical quality ID. [§7.3; §7.4]
    dropStrayAccentAttributes(canonical);
    applyOsFactorAlias(canonical);

    // Stamp the tree to CURRENT. [§7.1]
    canonical.setSchemaVersion(kCurrent);
}

} // namespace mw::state
