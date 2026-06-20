<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 119
title: PresetManager in-memory bank + per-slot INIT fallback (PresetManager.h/.cpp)
status: in-review
depends-on: [001, 006, 021, 022, 024, 025, 118]
component: core
estimated-size: M
stream: params
tag: presetmgr
---

## Objective

Implement PresetManager: load the embedded factory bank from BinaryData at construction, expose name/category/index queries, apply a preset via the message-thread handoff, and resolve a missing/undecodable slot to INIT without aborting the bank.

## Context

- `docs/design/06-parameters-state-presets.md §10.1` — read first
- `§10.2` — read first
- `§10.3` — read first
- `§8.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `presetmgr`.

## Scope

- core/preset/PresetManager.h/.cpp per the §10.1 class surface (getNumPresets/getName/getCategory/loadPreset/indicesForCategory)
- Construct: decode embedded BinaryData presets via loadPresetJson (params-11) into the in-memory bank + category index (§10.2)
- A missing/undecodable embedded preset resolves that slot to INIT (params-8) and names it in the report; construction never aborts/empties the bank (§8.3 L9)
- loadPreset runs the SAME migration (params-9) + recovery (params-10) as session state and applies via the §5.3 message-thread assembly (audio-thread handoff itself owned by plugin-processor) (§10.2)
- Tests: bank loads; a deliberately-undecodable slot falls back to INIT + warns naming it without emptying the bank (L9); indicesForCategory groups correctly

## Out of scope

- authoring the 64 preset files (params-14)
- the SPSC double-buffer audio-thread swap (plugin-processor)
- the browser UI (ui-skeleton)

## Acceptance criteria

- [ ] Constructor loads the embedded bank and builds a category index via the canonical loader [§10.1; §10.2]
- [ ] A missing/undecodable embedded preset resolves that slot to INIT and warns naming it without aborting/emptying the bank [§8.3 L9]
- [ ] loadPreset runs the shared migration + recovery chain [§10.2]
- [ ] Test names begin with presetmgr and assert bank load, L9 per-slot fallback, and category indexing [§8.3 L9; §10.2]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presetmgr --no-tests=error
```
