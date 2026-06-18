<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 013
title: docs/BUILDING.md local==CI command map + scripts/check.sh
status: todo
depends-on: [002]
component: docs
estimated-size: S
stream: infra
tag: build
---

## Objective

Write docs/BUILDING.md as the 2-column dev-command<->CI-step contract (documenting the CMake>=3.25 floor) and scripts/check.sh as the one-command host configure->build->test wrapper that CI also calls.

## Context

- `docs/design/11 §9.4` — read first
- `ADR-014 C1` — read first
- `ADR-014 Decision (local==CI mapping)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- docs/BUILDING.md: 2-column table mapping each dev command (cmake --preset X / build / ctest) to the identical CI step
- document the CMake >= 3.25 (schema v6) floor
- scripts/check.sh: host configure->build->test via the default preset; CI invokes the same script
- state that no build/test logic lives only in CI YAML

## Out of scope

- the CI YAML workflow itself (Phase 6 / out of stream)
- preset definitions (infra-2)

## Acceptance criteria

- [ ] docs/BUILDING.md maps each dev command to the identical CI step in a 2-column table; cmake --preset X is the only entrypoint [docs/design/11 §9.4; ADR-014 C1]
- [ ] the CMake >= 3.25 floor is documented [docs/design/11 §9.1, §9.4]
- [ ] scripts/check.sh runs the host configure->build->test and is the single command CI also calls; no build/test logic lives only in YAML [docs/design/11 §9.4; ADR-014 Decision]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R build --no-tests=error
```
