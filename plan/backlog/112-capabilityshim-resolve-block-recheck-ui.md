<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 112
title: CapabilityShim resolve + per-block recheck + UI publish (plugin/host/CapabilityShim.h/.cpp)
status: in-review
depends-on: [001, 006, 098, 103, 087]
component: app
estimated-size: M
stream: plugin
tag: capshim
---

## Objective

Implement CapabilityShim: resolve NoteExpressionRung + TransportRung once at prepare from format+MPE flag+playhead, branch-free per-block transport recheck via cached pointer, and publish both rungs to the UI via the lock-free atomic-pointer path.

## Context

- `docs/design/09 §7.2-7.4` — read first
- `docs/design/09 §8.1-8.2` — read first
- `ADR-022 C1-C12` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `capshim`.

## Scope

- resolve(fmt, mpeLiteEnabled, playhead) per the §8.1 / ADR-022 per-format matrix
- recheckPerBlock(playhead): branch-free cached-pointer read; transition to/from FreeRun without alloc/lock
- publishToUi via the §6.3 atomic-pointer pattern
- HOST-SYNC-without-transport behaves as INTERNAL then re-locks from absolute PPQ (C8)

## Out of scope

- MPE parser internals (plugin-9)
- Edge detector / clock DSP (mod-arp-seq)
- UI rendering of the rung (ui-skeleton)

## Acceptance criteria

- [ ] Per-format rungs match the §8.1 / ADR-022 capability matrix (tag 'capshim')
- [ ] recheckPerBlock is branch-free, performs zero alloc/lock across a FreeRun<->transport transition (AudioThreadGuard) [§8.2; ADR-022 C11]
- [ ] Both resolved rungs are published via the atomic-pointer path so Collapsed/Free-run are user-visible (§7.4; ADR-022 C12)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R capshim --no-tests=error
```
