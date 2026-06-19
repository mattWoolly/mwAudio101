<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 085
title: StepSequencer: 100-slot note/rest/tie record & play
status: in-review
depends-on: [001, 006, 007, 081]
component: core
estimated-size: M
stream: mod-arp-seq
tag: stepseq
---

## Objective

Implement StepSequencer: a fixed 100-slot one-event-per-slot model with keyboard-only LOAD recording (auto-exit at 100), wrap-around PLAY advancing one slot per clock edge, and REST/TIE articulation.

## Context

- `docs/design/05 §6.1` — read first
- `docs/design/05 §6.3` — read first
- `docs/design/05 §6.4` — read first
- `docs/design/05 §6.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `stepseq`.

## Scope

- core/control/StepSequencer.h/.cpp per §6.5 signature; record path (setRecord/recordNote/recordRest/recordTie/clear) and play path (setPlay/resetToStart/advanceOnEdge)
- advanceOnEdge decodes SeqStep flags into SeqPlayResult and wraps playPos % count_ (no-op when count_==0) (§6.5)
- RecordNote/Rest/Tie append at count_; auto-clear recording_ at kMaxSteps (§6.3)
- Articulation: REST drops gate, TIE sustains envelope + portamento + no re-gate (§6.4); loadBuffer/buffer for preset restore (§6.5)

## Out of scope

- Per-step accent and per-step gate-time (explicitly removed, ADR-025)
- Clock edge production (driven by caller)
- Snapshot publish/swap (mod-arp-seq-7)

## Acceptance criteria

- [ ] stepseq test: note/REST/each tie-extension each consume exactly one slot; payload is note/rest/tie only with NO accent field in the type (§6.1 / C12,C13; ADR-025)
- [ ] stepseq test: LOAD records keyboard-only and auto-exits at 100; PLAY loops wrapping last->first; one slot per edge (§6.3 / C14,C15)
- [ ] stepseq test: REST drops gate; TIE sets tie/sustain + suppresses retrigger (§6.4 / C16)
- [ ] stepseq test: advanceOnEdge/record do no heap alloc under sentinel (§6.5)
- [ ] ctest --preset default -R stepseq --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R stepseq --no-tests=error
```
