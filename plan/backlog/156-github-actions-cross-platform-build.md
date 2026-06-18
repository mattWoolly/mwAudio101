<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 156
title: GitHub Actions cross-platform build+test matrix workflow (preset dispatcher)
status: todo
depends-on: [001, 006, 113, 077]
component: infra
estimated-size: M
stream: ci
tag: ci
---

## Objective

Add the GitHub Actions workflow that mirrors the local preset commands 1:1 across the three platforms, running configure/build/test as a thin dispatcher of preset names only.

## Context

- `docs/design/11 §9.2` — read first
- `docs/design/11 §9.3` — read first
- `docs/design/11 §9.4` — read first
- `ADR-014 C1` — read first
- `ADR-014 C6` — read first
- `ADR-014 C12` — read first
- `ADR-024 C4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- Create .github/workflows/ci.yml with a matrix over macos-arm64, linux-x64, windows-x64.
- Each job runs exactly cmake --preset <p>, cmake --build --preset <p>, ctest --preset <p> (or scripts/check.sh) — no build/test logic in YAML (§9.4, ADR-014 C1).
- Set continue-on-error: true on the windows-x64 job (goal tier); macOS arm64 + Linux x64 are hard gates (ADR-014 C12).
- Per-platform format scoping is inherited from the platform presets, not duplicated in YAML (§9.3, ADR-014 C6; AAX never appears, ADR-024 C4).
- Wire CPM_SOURCE_CACHE checkout dir caching so cold JUCE builds are bounded (§10).

## Out of scope

- Defining any CMakePresets.json content, scripts/check.sh, or BUILDING.md (build-skeleton stream).
- The ctest graph, fp_discipline_guard, or validators themselves (test-harness / other streams).
- CI-only verification gates beyond the matrix (ci-2).

## Acceptance criteria

- [ ] ci.yml invokes only preset names / scripts/check.sh with zero inline build-test logic, verifiable by inspection per §9.4 and ADR-014 C1.
- [ ] macOS arm64 and Linux x64 jobs are hard gates and Windows x64 carries continue-on-error: true per ADR-014 C12 and §9.3.
- [ ] Each platform job uses its inheriting preset so formats are scoped per platform (no format built without a wired validator) per §9.3 / ADR-014 C6.
- [ ] No AAX cell or target appears in any job per ADR-024 C4.

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ci --no-tests=error
```
