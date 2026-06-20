<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 129
title: StatusBanner (non-modal load-failure + disclaimer surface)
status: done
depends-on: [111, 119, 006, 106, 114]
component: ui
estimated-size: S
stream: ui
tag: ui_banner
---

## Objective

Implement the non-modal StatusBanner driven by the processor's load-failure path via a thread-safe AsyncUpdater/ChangeBroadcaster, also hosting the static non-affiliation disclaimer string.

## Context

- `docs/design/10-ui.md §9.4` — read first
- `docs/design/10-ui.md §5.1` — read first
- `ADR-021` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_banner`.

## Scope

- ui/StatusBanner.h/.cpp per §9.4
- Non-modal warning surface updated on the message thread via AsyncUpdater/ChangeBroadcaster
- Severity + dismissal handling, message-thread only, never blocking
- Hosts the legal-track-authored disclaimer string (string injected, not authored here)

## Out of scope

- load-failure runtime decision logic (ADR-021 owner)
- authoring legal text (naming/legal track)

## Acceptance criteria

- [ ] Banner is non-modal and updates on the message thread, never a modal dialog during host load (§9.4, ADR-021)
- [ ] Update path is thread-safe via AsyncUpdater/ChangeBroadcaster and never blocks (§9.4)
- [ ] Surfaces the injected disclaimer string without authoring legal posture (§9.4)
- [ ] Tests named ui_banner* assert non-modal async update and dismissal; verify is 'ctest --preset default -R ui_banner --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_banner --no-tests=error
```
