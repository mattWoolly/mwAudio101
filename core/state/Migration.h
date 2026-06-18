// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/state/Migration.h — the ordered pure migration chain + migrateToCurrent
// (task 022). Realizes docs/design/06 §7.1-§7.4 and ADR-008 C10-C12, ADR-018 Q8
// (os.factor alias), ADR-025 (per-step accent dropped).
//
// JUCE-FREE NOTE. The §7.1 contract is written against juce::ValueTree:
//
//     using MigrationStep = std::function<void(juce::ValueTree&)>;
//     const std::vector<MigrationStep>& migrationChain();
//     void migrateToCurrent(juce::ValueTree& canonical);
//
// mwcore is JUCE-free [ADR-001 C1; docs/design/00 §5.2] — no juce::* type may
// appear in a core header or cross the seam. So the migration LOGIC (the ordered
// chain, the version table, the [schemaVersion, CURRENT) loop, the >CURRENT
// no-op down-bind, the accent silent-drop, the os.factor->quality alias copy) is
// expressed here over a JUCE-free abstract tree seam, IMutableTree, modelling
// exactly the operations a migration step performs on a canonical state tree.
//
// The thin juce::ValueTree adapter that IMPLEMENTS IMutableTree (so the §7.1
// juce::ValueTree overloads can simply wrap-and-delegate) is a separate
// plugin-stream task; it carries the JUCE dependency, not this module. This is
// the standard core/plugin seam: PODs/abstractions in core, JUCE in the shell
// [docs/design/00 §5.2-§5.4].

#pragma once

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace mw::state {

// The JUCE-free abstract canonical-state tree a migration step operates on.
// A concrete juce::ValueTree-backed implementation lives in the plugin stream;
// the unit tests supply an in-memory test double. Implementations need not be
// real-time safe: ALL migration runs on the message thread only [§7.2;
// ADR-008 C19; ADR-021 L13].
class IMutableTree {
public:
    virtual ~IMutableTree() = default;

    // Root "schemaVersion" attribute (int). nullopt == attribute absent. [§5.1]
    virtual std::optional<int> getSchemaVersion() const = 0;
    virtual void setSchemaVersion(int v) = 0;

    // <PARAMS> string-keyed values (the APVTS subtree, §5.1). Values are the
    // stored normalized/choice-index numbers; double is the widest carrier for
    // both. Used by the §7.4 rename/alias mechanism.
    virtual bool hasParam(std::string_view id) const = 0;
    virtual std::optional<double> getParam(std::string_view id) const = 0;
    virtual void setParam(std::string_view id, double value) = 0;

    // <seq> per-step attribute surface, used only by the ADR-025 accent drop.
    // getNumSeqSteps() == 0 when there is no <seq> subtree.
    virtual int getNumSeqSteps() const = 0;
    virtual bool seqStepHasAttribute(int stepIndex, std::string_view attr) const = 0;
    virtual void removeSeqStepAttribute(int stepIndex, std::string_view attr) = 0;
};

// A pure transform: in-place upgrade of a canonical tree from version n to n+1.
// MUST NOT throw; MUST be a pure function of its input tree. [§7.1]
using MigrationStep = std::function<void(IMutableTree& canonical)>;

// The ordered, contiguous chain indexed by source version (v1->v2 at index 0,
// v2->v3 at index 1, ...). EMPTY at the v1 baseline [§7.3]. The returned
// reference is to a single shared chain so presets and sessions run the SAME
// chain instance [§7.2].
const std::vector<MigrationStep>& migrationChain();

// Runs the chain steps for [schemaVersion, CURRENT) and sets
// schemaVersion = CURRENT_SCHEMA_VERSION. A missing schemaVersion is treated as
// the v1 baseline. Tolerant of schemaVersion > CURRENT: a pure no-op down-bind
// that PRESERVES the newer stamp (so the raw-newer round-trip stays possible,
// §8 L3/L6). Always also applies the v1 normalization invariants (the ADR-025
// accent silent-drop and the §7.4 os.factor->quality alias copy) when binding a
// tree at or below CURRENT. NEVER throws. [§7.1; §7.2; §7.3; §7.4]
void migrateToCurrent(IMutableTree& canonical);

} // namespace mw::state
