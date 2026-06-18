<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 083
title: TriggerSource (S7): coupled note-priority + retrigger
status: todo
depends-on: [001, 006, 007, 081]
component: core
estimated-size: S
stream: mod-arp-seq
tag: trigsource
---

## Objective

Implement TriggerSource binding note priority and envelope retrigger to one S7 mode over a 32-key held bitmap, resolving a held bitmap into a monophonic TriggerDecision (selectedKey, gate, retrigger, legato).

## Context

- `docs/design/05 §4.1` — read first
- `docs/design/05 §4.2` — read first
- `docs/design/05 §4.3` — read first
- `docs/design/05 §4.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `trigsource`.

## Scope

- core/control/TriggerSource.h/.cpp per §4.2 signature; setMode/priority/mode/resolve
- Priority: LastNote for GateTrig (last-pressed tracking via fixed std::array<int8_t,32>), LowestNote for Gate/Lfo (§4.3,§4.4)
- retrigger/gate rules per §4.3 incl. Gate non-legato-only retrigger and Lfo lfoEdge re-fire
- prepare() pre-sizes last-pressed array; never resizes (§4.4)

## Out of scope

- ADSR firing and VCA gating (consumers)
- Voice/poly/unison selection (owned by voice stream)
- Clock edge generation (consumed as lfoEdge bool)

## Acceptance criteria

- [ ] trigsource test: GateTrig => last-note priority + retrigger on every justPressed (§4.3 / C4)
- [ ] trigsource test: Gate => lowest-note priority + no retrigger on legato keypress, single sustained gate (§4.3 / C5)
- [ ] trigsource test: Lfo => lowest-note priority + retrigger on each lfoEdge while held (§4.3 / C6); GATE-mode is lowest not highest (§4.1)
- [ ] trigsource test: resolve() does no heap alloc under sentinel (§4.4)
- [ ] ctest --preset default -R trigsource --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R trigsource --no-tests=error
```
