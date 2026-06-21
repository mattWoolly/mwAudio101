<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 127
title: ScopeMeterOverlay (telemetry-driven, reduce-motion gated)
status: in-review
depends-on: [006, 106, 107, 115, 111c]
component: ui
estimated-size: S
stream: ui
tag: ui_scope
---

## Objective

Implement the scope/meter overlay that paints the most-recent telemetry Snapshot (levels, scope points, modulated cutoff) on targeted repaints and renders a static idle state when reduce-motion is on.

## Context

- `docs/design/10-ui.md §5.1` — read first
- `docs/design/10-ui.md §8.4` — read first
- `docs/design/10-ui.md §10` — read first
- `ADR-015 C5` — read first
- `ADR-015 C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_scope`.

## Scope

- ui/ScopeMeterOverlay.h/.cpp
- Render vcaLevelL/R meters, decimated scope array, vcfCutoffDisplay indicator from a Snapshot
- Targeted repaint only (own bounds) driven by the editor Timer
- Static/idle paint path when reduce-motion is enabled

## Out of scope

- Timer drain ownership (ui-6)
- telemetry types (ui-4)
- background chrome (ui-7)

## Acceptance criteria

- [ ] Renders solely from a Telemetry Snapshot; holds no audio-domain state (§8.3, ADR-015 C5/C12)
- [ ] Repaints only its own bounds on Timer-driven updates, never whole-editor (§7.3/§8.4, ADR-015 C7)
- [ ] Reduce-motion ON renders a static/idle state (§10, ADR-015 C8)
- [ ] Tests named ui_scope* assert snapshot-driven render and idle state; verify is 'ctest --preset default -R ui_scope --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_scope --no-tests=error
```
