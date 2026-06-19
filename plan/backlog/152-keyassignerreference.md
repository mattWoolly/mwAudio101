<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 152
title: KeyAssignerReference.{h,cpp} — disassembly-semantics golden reference
status: done
depends-on: [001, 006, 007, 067, 077]
component: qa
estimated-size: M
stream: voice-control
tag: keyassignerref
---

## Objective

Implement the independent disassembly-semantics reference model of the firmware keyboard_read/play priority+retrigger logic (low-to-high scan for lowest-note, XOR-of-changed-down for last-note) used to lock KeyAssigner conformance.

## Context

- `docs/design/04-voice-and-control.md §2 (module map)` — read first
- `docs/design/04-voice-and-control.md §5.3` — read first
- `docs/design/04-voice-and-control.md §5.4` — read first
- `ADR-006 §Decision item 2` — read first
- `ADR-006 §Contract C19` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `keyassignerref`.

## Scope

- test/golden/KeyAssignerReference.h/.cpp — a self-contained reference that emits {activeNote,gate,retrigger} per tick for a sequence of note events in a given GateTrigMode
- independently coded from the §5.3/§5.4 firmware semantics, NOT by calling KeyAssigner (it is the oracle)
- supports all three GateTrigMode values and batched multi-down-per-tick events (§5.4 K4)
- deterministic, allocation-not-required (test code, not audio thread)

## Out of scope

- the conformance test driver itself (voice-control-5)
- production KeyAssigner code (voice-control-3) — must remain independent
- POLY/unison (exempt from golden trace per C19)

## Acceptance criteria

- [ ] reference emits {activeNote,gate,retrigger} sequences implementing low->high lowest-note and XOR-changed-down last-note exactly per §5.3 (ADR-006 §Decision item 2)
- [ ] reference is implemented independently of the production KeyAssigner (no include of KeyAssigner.h)
- [ ] handles the legato/overlap/release-order battery shapes (§5.4 K1-K6) across all three modes
- [ ] test names begin with the tag (self-check tests of the reference's own invariants); verify: ctest --preset default -R keyassignerref --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R keyassignerref --no-tests=error
```
