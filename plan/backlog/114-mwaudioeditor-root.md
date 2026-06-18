<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 114
title: MwAudioEditor root: AffineTransform scaling, constrainer, resize/DPI
status: todo
depends-on: [111, 020, 006, 106, 108]
component: ui
estimated-size: M
stream: ui
tag: ui_editor
---

## Objective

Implement the editor root that lays out over the 1000x640 logical design space, scales to pixels via a single AffineTransform recomputed in resized(), and enforces a fixed-aspect resizable constrainer with scale presets and persisted size.

## Context

- `docs/design/10-ui.md §4` — read first
- `docs/design/10-ui.md §5.2` — read first
- `ADR-015 C1` — read first
- `ADR-015 C2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_editor`.

## Scope

- ui/MwAudioEditor.h/.cpp constructor, paint (cached background only), resized (recompute AffineTransform + layout)
- ComponentBoundsConstrainer with fixed aspect ratio and min/max from Calibration.h
- Scale presets (75/100/150/200%) snapping; size persisted via processor getStoredEditorSize/setStoredEditorSize
- getDesignToPixels()/getScaleFactor() test hooks
- Own and lay out module members in design units

## Out of scope

- module internals (ui-8..ui-14)
- background regen internals (ui-7)
- telemetry timer logic (ui-6)

## Acceptance criteria

- [ ] Layout is entirely in design units; resize changes only the single AffineTransform (§4, ADR-015 C1)
- [ ] Constrainer holds the fixed aspect ratio across resizes (§4.3, ADR-015 C1)
- [ ] Scale presets snap the window and size round-trips via the <extras> UI node accessor (§4.4, ADR-015 C2)
- [ ] TDD: tests named ui_editor* assert getDesignToPixels maps known design points within tolerance and aspect held; verify is 'ctest --preset default -R ui_editor --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_editor --no-tests=error
```
