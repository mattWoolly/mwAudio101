<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 128
title: PresetBrowser thin view over processor PresetManager
status: todo
depends-on: [119, 020, 111, 006, 106, 109, 114]
component: ui
estimated-size: M
stream: ui
tag: ui_preset
---

## Objective

Implement the PresetBrowser as a thin view that lists/filters/loads presets by calling the processor-owned PresetManager on the message thread and refreshing on its change broadcaster.

## Context

- `docs/design/10-ui.md §9.1` — read first
- `docs/design/10-ui.md §9.2` — read first
- `docs/design/10-ui.md §9.3` — read first
- `ADR-015 C6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_preset`.

## Scope

- ui/PresetBrowser.h/.cpp per §9.1
- ListBox of names+categories, category filter combo over the schema taxonomy, prev/next/load buttons
- loadSelected -> manager.loadPreset(index) on the message thread; refreshList on broadcaster callback
- layoutDesignUnits()

## Out of scope

- preset serialization/migration/format (state-presets/preset-format streams)
- load-failure banner (ui-18)

## Acceptance criteria

- [ ] Loading invokes PresetManager::loadPreset on the message thread; no tree pointer/alloc crosses to audio (§9.3, ADR-015 C6/ADR-008 C19)
- [ ] List refreshes via the manager's change broadcaster, not by polling (§9.1)
- [ ] Category filter enumerates the schema taxonomy strings, not re-minted (§9.3, ADR-008 C14)
- [ ] Tests named ui_preset* assert select->loadPreset on message thread and broadcaster refresh; verify is 'ctest --preset default -R ui_preset --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_preset --no-tests=error
```
