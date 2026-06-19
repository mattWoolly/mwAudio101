<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 058
title: Envelope trigger state machine: GateTrig/Gate/Lfo retrigger
status: done
depends-on: [006, 054]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: env_trig
---

## Objective

Implement noteOn(legato)/noteOff/clockTrigger semantics for the three trigger modes, re-attacking from the current level on GateTrig retrigger per the v1 (PI) choice.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §2.3` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §2.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `env_trig`.

## Scope

- GateTrig: every noteOn (incl legato) restarts Attack (§2.5)
- Gate: noteOn(legato=true) ignored while non-Idle; only initial gate starts Attack (§2.5)
- Lfo: clockTrigger() restarts Attack while held, independent of new presses (§2.5)
- noteOff -> Release in all modes (§2.2)
- GateTrig retrigger re-attacks from current level (no snap-to-0); comment as (PI) open gap (§2.5)

## Out of scope

- LFO cycle-edge generation (task 7 supplies the edge)
- Contour math (task 3)

## Acceptance criteria

- [ ] GateTrig retriggers on legato; Gate does not; Lfo retriggers on clockTrigger() while held (§2.3 acceptance hook)
- [ ] GateTrig retrigger continues from current level (no discontinuity) (§2.5)
- [ ] ctest --preset default -R env_trig --no-tests=error passes; tests env_trig_* cover all three modes positive+negative

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R env_trig --no-tests=error
```
