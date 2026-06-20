<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 142
title: Cross-format capability ladder integration test (note-expression + transport rungs)
status: in-review
depends-on: [113, 104, 087, 114, 136]
component: qa
estimated-size: M
stream: integration
tag: capability_matrix
---

## Objective

Assert the per-format capability matrix: note-expression rungs (Native/MPE-over-MIDI/Collapsed) and transport rungs (Sample-accurate/Block-quantized/Free-run) all feed the same engine path, resolve RT-safely, and publish to the UI.

## Context

- `docs/design/09 §7.2` — read first
- `docs/design/09 §8.1` — read first
- `plan/decisions/022 Contract C1-C12` — read first
- `docs/design/09 §7.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `capability_matrix`.

## Scope

- Drive each format's resolved NoteExpressionRung into the same per-voice offset and compare engine output
- Drive each transport rung; assert block-quantized edge count/order matches sample-accurate within <=1 block jitter; free-run falls back to INTERNAL clock
- Assert HOST-SYNC-without-transport behaves as INTERNAL then re-locks from absolute PPQ with no allocation
- Assert both rungs are published to the UI via the lock-free atomic-pointer path and rung resolution allocates/locks nowhere on the audio thread

## Out of scope

- CapabilityShim/MpeReconstructor internals (format-wrappers/midi-frontend)
- Arp/seq DSP (mod-arp-seq)
- UI rendering of the rung indicator (ui)

## Acceptance criteria

- [ ] Native/MPE-over-MIDI/Collapsed rungs all feed the SAME per-voice offset; Collapsed is bit-identical to running without MPE per ADR-022 C1-C4
- [ ] Block-quantized edges match sample-accurate count/order within <=1 block; Free-run uses INTERNAL clock per ADR-022 C5-C7
- [ ] Rung resolution is RT-safe and both rungs publish to the UI per §7.4 / ADR-022 C11-C12
- [ ] ctest --preset default -R capability_matrix --no-tests=error is green; test names begin with capability_matrix

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R capability_matrix --no-tests=error
```
