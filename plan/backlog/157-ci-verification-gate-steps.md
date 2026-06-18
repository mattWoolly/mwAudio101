<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 157
title: CI verification gate steps: discovery, label-snapshot, MANIFEST, no-network
status: todo
depends-on: [001, 006, 077, 156]
component: infra
estimated-size: M
stream: ci
tag: ci
---

## Objective

Add the CI-only orchestration steps layered on the Linux x64 / macOS arm64 hard-gate jobs that fail the build on missing test prefixes, label-snapshot drift, MANIFEST gaps, and broken offline builds.

## Context

- `docs/design/11 §8.2` — read first
- `docs/design/11 §8.4` — read first
- `docs/design/11 §7.5` — read first
- `docs/design/11 §10` — read first
- `ADR-013 C2` — read first
- `ADR-013 C3` — read first
- `ADR-013 C12` — read first
- `ADR-013 C13` — read first
- `ADR-014 C10` — read first
- `ADR-023 V7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- Add a per-prefix discovery step that enumerates discovered ctest tests and fails if any required prefix (vco vcf vca env seq prng arp cal) has 0 tests (§8.2, ADR-013 C2).
- Add a label-snapshot diff step comparing ctest --print-labels against tests/golden/corpus/ctest-labels.snapshot; any diff fails (§8.4, ADR-013 C3).
- Invoke the MANIFEST completeness/orphan/honesty-label/renderVersion checks and fail on any violation (§7.5, ADR-013 C12/C13, ADR-023 V7).
- Add a no-network build verification step using CPM_SOURCE_CACHE + FETCHCONTENT_FULLY_DISCONNECTED; failure to build offline fails CI (§10, ADR-014 C10).
- Run these gate steps on the hard-gate platforms; selectors carry --no-tests=error (§8.3).

## Out of scope

- Implementing the discovery assertion / label snapshot / MANIFEST / offline tooling logic (test-harness, golden-harness streams).
- The build/test matrix itself (ci-1).

## Acceptance criteria

- [ ] The discovery step fails CI when any of vco vcf vca env seq prng arp cal has 0 discovered tests per ADR-013 C2 / §8.2.
- [ ] The label-snapshot diff fails CI on any divergence from the checked-in snapshot per ADR-013 C3 / §8.4.
- [ ] A golden hash absent from MANIFEST.toml, an orphan MANIFEST entry, or a renderVersion mismatch fails CI per ADR-013 C12/C13 and ADR-023 V7 / §7.5.
- [ ] A no-network build succeeds via CPM_SOURCE_CACHE + FETCHCONTENT_FULLY_DISCONNECTED, and a failure to build offline fails CI per ADR-014 C10 / §10.

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ci --no-tests=error
```
