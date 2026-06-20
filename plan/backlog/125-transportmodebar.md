<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 125
title: TransportModeBar (arp/seq mode, tempo-sync, run/hold, scale + reduce-motion toggles)
status: done
depends-on: [020, 111, 087, 006, 109, 117]
component: ui
estimated-size: M
stream: ui
tag: ui_transport
---

## Objective

Implement the transport/mode bar binding arp mode, tempo-sync subdivisions, and run/hold via APVTS attachments, plus hosting the scale-preset selector and the reduce-motion toggle surfaced to the editor.

## Context

- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §4.4` — read first
- `docs/design/10-ui.md §10` — read first
- `ADR-015 C2` — read first
- `ADR-015 C8` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_transport`.

## Scope

- ui/modules/TransportModeBar.h/.cpp
- Arp mode, tempo-sync subdivisions, run/hold APVTS attachments from ParamIds
- Scale-preset selector (75/100/150/200%) signalling the editor to snap window
- Reduce-motion/low-CPU toggle surfaced to the editor's Timer logic
- layoutDesignUnits()

## Out of scope

- sequencer step grid (ui-15... see ui-16)
- actual Timer drain (ui-6)
- editor snap implementation (ui-5)

## Acceptance criteria

- [ ] Arp/seq mode, tempo-sync, run/hold bind via APVTS attachments using schema ParamIds (§8.1, ADR-015 C3)
- [ ] Scale-preset selector exposes 75/100/150/200% options driving the editor snap (§4.4, ADR-015 C2)
- [ ] Reduce-motion toggle is surfaced without affecting any control binding (§10, ADR-015 C8)
- [ ] Tests named ui_transport* assert mode binding and scale-preset options; verify is 'ctest --preset default -R ui_transport --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_transport --no-tests=error
```
