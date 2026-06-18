<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 106
title: DesignTokens table (palette/stroke/radius/typography, single reskin knob)
status: in-review
depends-on: [006]
component: ui
estimated-size: S
stream: ui
tag: ui_tokens
---

## Objective

Define the single DesignTokens struct holding palette, stroke weights, corner radii, and typography plus defaultTheme()/highContrast() factories, sourcing concrete values from Calibration.h.

## Context

- `docs/design/10-ui.md §6.1` — read first
- `ADR-015 C10` — read first
- `docs/design/10-ui.md §2.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_tokens`.

## Scope

- ui/DesignTokens.h with the DesignTokens struct fields per §6.1
- defaultTheme() factory returning the shipped non-Roland palette
- highContrast() accessibility variant factory
- Pull concrete hex/stroke/radius/font defaults from core/calibration/Calibration.h
- Header-only; no LookAndFeel or layout code

## Out of scope

- drawing logic (ui-2)
- layout math
- minting parameter IDs

## Acceptance criteria

- [ ] Struct exposes all fields listed in §6.1 (palette + stroke + geometry + typography + extensionTag)
- [ ] defaultTheme() palette is provably never Roland grey/red/blue per §6.1 / ADR-015 C10
- [ ] highContrast() returns a distinct higher-contrast variant per §6.1
- [ ] Tests named ui_tokens* verify both factories construct and differ; verify is 'ctest --preset default -R ui_tokens --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_tokens --no-tests=error
```
