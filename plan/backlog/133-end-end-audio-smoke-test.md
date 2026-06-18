<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 133
title: End-to-end audio smoke test (note-on to non-silent output)
status: todo
depends-on: [006, 118]
component: qa
estimated-size: S
stream: integration
tag: e2e_smoke
---

## Objective

Drive the assembled Engine headlessly with a BlockContext carrying a note-on + params and assert finite, non-silent, bounded mono output across a few block sizes.

## Context

- `docs/design/00 §4.1` — read first
- `docs/design/00 §5.3` — read first
- `docs/design/00 §9.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `e2e_smoke`.

## Scope

- Construct BlockContext (AudioBlockView/ParamSnapshot/TransportInfo/MidiEventView) with a note-on
- Run prepare then a sequence of process blocks at varied sizes <= maxBlockSize
- Assert output is finite (no NaN/Inf), non-silent during the note, and returns toward silence after note-off
- Links mwcore only (no JUCE, no audio device) per §9.3

## Out of scope

- Bit-exact golden comparison (golden-harness)
- Cross-format equivalence (integration-9)
- Parameter-by-parameter audit

## Acceptance criteria

- [ ] A note-on through the full graph (§4.1) yields finite, non-silent output and silence-after-release
- [ ] Test binary links mwcore only with no JUCE/plugin/audio-device dependency per §9.3
- [ ] Runs over varied valid block sizes <= maxBlockSize without tripping guards
- [ ] ctest --preset default -R e2e_smoke --no-tests=error is green; test names begin with e2e_smoke

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R e2e_smoke --no-tests=error
```
