<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 153
title: KeyAssigner golden-trace conformance (K17) test battery
status: in-review
depends-on: [001, 006, 077, 069, 152]
component: qa
estimated-size: S
stream: voice-control
tag: keyassignertrace
---

## Objective

Assert the production KeyAssigner emits identical {activeNote,gate,retrigger} sequences to KeyAssignerReference over the full legato/overlap/release-order battery in all three GateTrigMode values.

## Context

- `docs/design/04-voice-and-control.md §5.4 (K17)` — read first
- `docs/design/04-voice-and-control.md Acceptance hooks` — read first
- `ADR-006 §Contract C19` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `keyassignertrace`.

## Scope

- test file driving both KeyAssigner and KeyAssignerReference over a shared event battery
- battery covers legato chains, overlapping holds, varied release order, multi-down-per-tick (§5.4 K1-K6)
- runs the battery for Gate, GateTrig, and Lfo; diffs sequences element-by-element
- fails loudly on first divergence with the diverging tick index

## Out of scope

- implementing either KeyAssigner or the reference
- poly/unison conformance (exempt per C19; tested separately in VoiceManager tasks)

## Acceptance criteria

- [ ] C++ KeyAssigner == KeyAssignerReference for {activeNote,gate,retrigger} across the full battery in all three modes (§5.4 K17; ADR-006 C19)
- [ ] battery includes legato, overlap, and release-order cases plus multi-down-per-tick (§5.4 K1-K6)
- [ ] test names begin with the tag; verify: ctest --preset default -R keyassignertrace --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R keyassignertrace --no-tests=error
```
