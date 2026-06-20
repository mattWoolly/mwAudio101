<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 116
title: BackgroundLayer cached static chrome + patch lines + labels
status: done
depends-on: [006, 106, 114]
component: ui
estimated-size: M
stream: ui
tag: ui_background
---

## Objective

Render panel chrome, module outlines, signal-flow patch lines, and static labels once into a cached juce::Image regenerated only on resize, blitting it on paint with no per-frame path work.

## Context

- `docs/design/10-ui.md §7.1` — read first
- `docs/design/10-ui.md §7.2` — read first
- `ADR-015 C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_background`.

## Scope

- ui/BackgroundLayer.h/.cpp with regenerate(pixelBounds, designToPixels, tokens) and paint (blit only)
- Vector patch lines MODULATOR->VCO->SOURCE MIXER->VCF->VCA drawn into the cached image
- Static module outlines and labels parameterized by DesignTokens
- Regeneration only on resize

## Out of scope

- control drawing (ui-2/ui-3)
- telemetry-driven visuals (ui-15)

## Acceptance criteria

- [ ] paint() blits the cached image with no juce::Path stroking per frame (§7.1, ADR-015 C7)
- [ ] Patch lines and chrome regenerate only on resize, not on the Timer (§7.2, ADR-015 C7)
- [ ] All colours/strokes read from DesignTokens (§7.1)
- [ ] Tests named ui_background* assert regenerate-only-on-resize via paint/regen count probe; verify is 'ctest --preset default -R ui_background --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_background --no-tests=error
```
