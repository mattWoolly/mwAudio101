<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 107
title: Telemetry SPSC types (Snapshot POD + Producer/Consumer, lock-free, pre-allocated)
status: done
depends-on: [006, 006]
component: ui
estimated-size: M
stream: ui
tag: ui_telemetry
---

## Objective

Implement the fixed-capacity lock-free single-producer/single-consumer telemetry path: the trivially-copyable Snapshot POD, a noexcept non-allocating Producer, and a coalescing Consumer.

## Context

- `docs/design/10-ui.md §8.3` — read first
- `ADR-015 C5` — read first
- `ADR-015 C12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_telemetry`.

## Scope

- ui/Telemetry.h (and .cpp if needed) per §8.3
- Snapshot POD with vca levels, vcfCutoffDisplay, scope array, lfoPhase, seqStep
- Producer::prepare/push (noexcept, no alloc, no lock; overrun overwrites oldest)
- Consumer::pull coalescing to most-recent
- kScopePoints/kFifoCapacity/decimation sourced from Calibration.h

## Out of scope

- wiring producer into the processor (cross-stream)
- scope rendering (ui-15)

## Acceptance criteria

- [ ] Snapshot is trivially copyable and fixed size per §8.3
- [ ] push() performs no allocation and takes no lock; overrun drops oldest, never blocks (§8.3, ADR-015 C5/C12)
- [ ] pull() returns most-recent and false when empty (§8.3)
- [ ] TDD: tests named ui_telemetry* assert push no-alloc/no-lock and overrun-drop via instrumented allocator; verify is 'ctest --preset default -R ui_telemetry --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_telemetry --no-tests=error
```
