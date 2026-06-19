<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 108
title: MwAudioLookAndFeel vector drawing parameterized by DesignTokens
status: done
depends-on: [006, 106]
component: ui
estimated-size: M
stream: ui
tag: ui_laf
---

## Objective

Implement the custom LookAndFeel_V4 that draws rotary/linear sliders, toggles, and combo boxes entirely from juce::Path/Graphics primitives parameterized by DesignTokens, with live setTokens reskin.

## Context

- `docs/design/10-ui.md §6.2` — read first
- `ADR-015 C10` — read first
- `ADR-015 C11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_laf`.

## Scope

- ui/MwAudioLookAndFeel.h/.cpp
- Override drawRotarySlider, drawLinearSlider, drawToggleButton, drawComboBox, getLabelFont
- setTokens() for live reskin/theme switch
- All drawing via vector primitives; no raster filmstrip reads

## Out of scope

- control subclasses (ui-3)
- design-token values (ui-1)
- background chrome (ui-7)

## Acceptance criteria

- [ ] Every draw* override renders from juce::Path/Graphics only, no raster asset (§6.2, ADR-015 C11)
- [ ] setTokens() restyles output without touching layout or binding code (§6.1, ADR-015 C10)
- [ ] Drawing colours/strokes are read solely from the injected DesignTokens (§6.2)
- [ ] Tests named ui_laf* assert token swap changes rendered colours/strokes; verify is 'ctest --preset default -R ui_laf --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_laf --no-tests=error
```
