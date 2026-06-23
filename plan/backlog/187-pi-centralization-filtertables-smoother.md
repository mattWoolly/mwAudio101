<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 187
title: Centralize the FilterTables 1024 + Smoother 1e-5 (PI) literals (QA-155-1/QA-155-2)
status: done
depends-on: [155]
component: dsp
estimated-size: S
stream: dsp
tag: vcf-tables
---

## Objective

Fix the two (PI)-centralization violations the task-155 ledger sweep flagged
(docs/QA-REPORT.md §5C, QA-155-1 / QA-155-2). Both are PURE value-preserving
refactors: the numeric values DO NOT change (1024 stays 1024, 1e-5 stays 1e-5),
so there is no golden / behavior change. The fix relocates the literals to their
single centralized home under `core/calibration/` and references them at the call
sites, satisfying the no-inline-literal contract (ADR-003 F-15; docs/design/11 §4.2).

## The two violations (from docs/QA-REPORT.md §5C)

- QA-155-1 (MEDIUM): the filter-table size (PI) `1024` was defined inline at
  `core/dsp/FilterTables.h:67` (`static constexpr int kTableSize = 1024;`) and
  duplicated as the bare literal `1024` at `core/dsp/FilterTables.cpp` (the
  `std::array<float, 1024>` extent, and `constexpr int n = 1024` in two loops) —
  4 sites, one value — absent from `core/calibration/FilterTablesConstants.h`
  despite that header's own no-inline-literal contract.
- QA-155-2 (LOW): `core/params/Smoother.h` inlined the (PI) `1.0e-5` at `:27`
  ("kept in sync with cal::smoothing::kSnapThreshold") and `:70`, rather than
  referencing `mw::cal::smoothing::kSnapThreshold` (Calibration.h:50). The sibling
  `core/calibration/GlideConstants.h:47-48` already does it right (derives via
  `static_cast<float>(mw::cal::smoothing::kSnapThreshold)`).

## Scope (centralize; values UNCHANGED)

- QA-155-1: add `kFilterTableSize = 1024` (PI), with its provenance comment, to
  `core/calibration/FilterTablesConstants.h` (existing `mw::cal::vcf` namespace).
  Reference it from `FilterTables.h` (the `std::array` extents) and from all bare
  `1024` sites in `FilterTables.cpp` (the loop bounds + the `lerpTable` array
  extent). Value stays exactly 1024.
- QA-155-2: make `core/params/Smoother.h` reference `mw::cal::smoothing::kSnapThreshold`
  directly (include `calibration/Calibration.h`) at both `:27` and `:70` instead of
  the inline `1.0e-5`. Remove the "kept in sync by comment" anti-pattern. Value stays
  exactly 1e-5.
- mwcore stays JUCE-free. No numeric value, other constant, or behavior changes.

## Out of scope

- Any value change, any other constant, any new ctest tag (refactor only → no
  labels_snapshot regen).

## Acceptance criteria

- [ ] `kFilterTableSize` (PI) lives once in `core/calibration/FilterTablesConstants.h`;
      no bare `1024` table-size literal remains in `FilterTables.h` / `.cpp`.
- [ ] `Smoother.h` references `mw::cal::smoothing::kSnapThreshold` at both sites; no
      inline `1.0e-5`.
- [ ] Full core suite GREEN; the filter-golden / vcf-tables / smoothing goldens are
      UNCHANGED (a value change would flip a golden RED — it must not).
- [ ] The 155 QaPiLedgerTest tripwire on `kSnapThreshold` stays green (it pins the value).

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R 'vcf|filter|smooth|glide|golden' --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
