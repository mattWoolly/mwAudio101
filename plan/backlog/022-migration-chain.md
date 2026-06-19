<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 022
title: Migration chain (Migration.h/.cpp)
status: done
depends-on: [016, 017, 007]
component: core
estimated-size: S
stream: params
tag: migration
---

## Objective

Implement the ordered pure migration chain and migrateToCurrent, with the v1 baseline (no steps) version table and tolerance for schemaVersion > CURRENT.

## Context

- `docs/design/06-parameters-state-presets.md §7.1` — read first
- `§7.2` — read first
- `§7.3` — read first
- `§7.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `migration`.

## Scope

- core/state/Migration.h/.cpp: MigrationStep type, migrationChain() (empty/baseline at v1 per §7.3), migrateToCurrent() running steps for [schemaVersion, CURRENT) and setting schemaVersion=CURRENT (§7.1)
- migrateToCurrent never throws, is pure per input tree, and is a no-op down-bind when schemaVersion > CURRENT (§7.1; §7.2)
- Silent-drop of any stray pre-ADR-025 per-step accent attribute encountered (§7.3)
- Alias mechanism stub honoring §7.4 (os.factor -> quality copy if present); chain runs identically for presets and sessions (§7.2)
- Test: v1 baseline has zero steps; migrateToCurrent on a v1 tree is identity + sets schemaVersion; a schemaVersion>CURRENT tree is left bindable (no crash); a stray accent attribute is dropped

## Out of scope

- the recovery ladder / clamping (params-10)
- binding to APVTS (plugin-processor)
- future v2+ steps (none exist at v1)

## Acceptance criteria

- [ ] migrationChain() is empty at v1 baseline; migrateToCurrent on a v1 tree is identity and sets schemaVersion=CURRENT [§7.3; §7.1]
- [ ] migrateToCurrent never throws and tolerates schemaVersion>CURRENT as a no-op down-bind [§7.1; §7.2]
- [ ] A stray per-step accent attribute is silently dropped [§7.3; ADR-025]
- [ ] Test names begin with migration and assert baseline-empty, identity-at-current, newer-tolerance, accent-drop [§7]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R migration --no-tests=error
```
