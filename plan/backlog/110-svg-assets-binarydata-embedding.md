<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 110
title: SVG assets + BinaryData embedding (logo + static decoration)
status: todo
depends-on: [001, 006]
component: ui
estimated-size: S
stream: ui
tag: ui_assets
---

## Objective

Add the only bundled art as static SVG (logo + decorative glyphs) under ui/assets, loaded via juce::Drawable::createFromSVG through BinaryData, with zero raster faceplate bitmaps.

## Context

- `docs/design/10-ui.md §12` — read first
- `ADR-015 C11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_assets`.

## Scope

- ui/assets/*.svg (logo + static decorative art)
- BinaryData embedding wiring per §12 (build doc owns the CMake mechanics)
- createFromSVG loader usage in chrome/background where art is genuinely static
- No @2x/@3x raster matrix introduced

## Out of scope

- coded vector controls/chrome (ui-2/ui-7)
- CMake BinaryData target internals (build-skeleton)

## Acceptance criteria

- [ ] Only static SVG art and the logo are present; no raster faceplate bitmaps and no @2x/@3x matrix (§12, ADR-015 C11)
- [ ] SVGs load via juce::Drawable::createFromSVG through BinaryData (§12)
- [ ] Tests named ui_assets* assert SVG load succeeds and no raster matrix exists; verify is 'ctest --preset default -R ui_assets --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_assets --no-tests=error
```
