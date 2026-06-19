<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 042
title: Stimulus and PatchSnapshot render-input types
status: in-review
depends-on: [001, 006, 007, 041, 020]
component: qa
estimated-size: S
stream: golden
tag: golden
---

## Objective

Define the offline render-input POD types (Stimulus, and the harness-side view of PatchSnapshot) that RenderHarness consumes, plus a small library of deterministic stimuli.

## Context

- `docs/design/11 §5.4` — read first
- `docs/design/11 §2.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/Stimulus.h/.cpp: a deterministic stimulus descriptor (note/gate/CC events + duration + seed) and a few canonical builders (sustained note, gate burst, sweep)
- Harness-side PatchSnapshot adapter referencing core ParamSnapshot
- All inputs are plain data, no audio-thread concerns (offline only)

## Out of scope

- Running the engine (golden-4)
- Param ID definitions (owned by param-schema; reference only)

## Acceptance criteria

- [ ] mw101.unit.golden each canonical stimulus builder produces identical event sequences for a fixed seed [docs/design/11 §5.4]
- [ ] Negative control: two different seeds produce different stimulus streams for the randomized builder [docs/design/11 §5.4]
- [ ] Stimulus serializes to a stable byte form for inclusion in renderGraphHash [docs/design/11 §5.3]
- [ ] verify: ctest --preset default -R golden --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
