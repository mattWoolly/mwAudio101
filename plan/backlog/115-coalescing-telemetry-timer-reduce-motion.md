<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 115
title: Coalescing telemetry Timer + reduce-motion toggle in editor
status: todo
depends-on: [020, 006, 107, 114, 111c]
component: ui
estimated-size: S
stream: ui
tag: ui_timer
---

## Objective

Drive a single 30-60 Hz Timer that drains the Telemetry Consumer to the most-recent Snapshot and triggers targeted repaints, plus a reduce-motion/low-CPU toggle that suppresses/downsamples animation without affecting bindings.

## Context

- `docs/design/10-ui.md §8.4` — read first
- `docs/design/10-ui.md §10` — read first
- `ADR-015 C5` — read first
- `ADR-015 C8` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_timer`.

## Scope

- timerCallback() in MwAudioEditor: pull most-recent snapshot, targeted repaint of scope/indicators only
- Timer rate default 60 Hz (floor 30) from Calibration.h
- Reduce-motion toggle: stop/downsample Timer and idle the scope; persist state in <extras> UI subtree
- No control attachment affected by the toggle

## Out of scope

- Snapshot/Consumer types (ui-4)
- scope painting (ui-15)
- AffineTransform layout (ui-5)

## Acceptance criteria

- [ ] Timer drains SPSC at 30-60 Hz and triggers only targeted repaints, never whole-editor (§8.4, ADR-015 C5/C7)
- [ ] Reduce-motion ON suppresses/downsamples animation while bindings/automation stay functional (§10, ADR-015 C8)
- [ ] Toggle state persists in the <extras> UI subtree (§10, ADR-008 §4/§5/C8)
- [ ] Tests named ui_timer* assert rate bounds and reduce-motion behavior; verify is 'ctest --preset default -R ui_timer --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_timer --no-tests=error
```
