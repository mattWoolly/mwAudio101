<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 084
title: Arpeggiator: UP/U&D/DOWN over 32-key bitmap
status: done
depends-on: [001, 006, 007, 081]
component: core
estimated-size: S
stream: mod-arp-seq
tag: arp
---

## Objective

Implement Arpeggiator: three mutually-exclusive directions over a fixed 32-bit held-key bitmap with HOLD latch, no octave expansion, engaging on chord/legato, advancing one key per clock edge.

## Context

- `docs/design/05 §5.1` — read first
- `docs/design/05 §5.2` — read first
- `docs/design/05 §5.3` — read first
- `docs/design/05 §5.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `arp`.

## Scope

- core/control/Arpeggiator.h/.cpp per §5.4 signature; noteOn/noteOff/setMode/setHold/isEngaged/advanceOnEdge/heldBitmap/heldCount
- advanceOnEdge walks set bits ascending; UP/DOWN direction; U&D oscillation honoring UandDRepeatEndpoints (§5.1,§5.2)
- HOLD latch keeps held set after release; new chord replaces latched set (§5.1)
- setUandDRepeatEndpoints default sourced from Calibration kArpUandDRepeatEndpoints (§5.2)

## Out of scope

- Global TRANSPOSE / KEY TRANSPOSE octave math (consumed from pitch/voice layer, §5.3)
- Clock edge production (advance is driven by caller's edge)
- Pedal/panel HOLD OR-ing (caller supplies combined latch)

## Acceptance criteria

- [ ] arp test: 32 distinct held keys are all cycled; no automatic octave expansion (§5.1 / C8)
- [ ] arp test: engages on chord/legato, single non-legato note inactive; HOLD latch survives key release (§5.1 / C10)
- [ ] arp test: U&D turnaround follows UandDRepeatEndpoints for both values producing the §5.2 documented sequences (C11)
- [ ] arp test: advanceOnEdge does no heap alloc under sentinel (§5.4)
- [ ] ctest --preset default -R arp --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R arp --no-tests=error
```
