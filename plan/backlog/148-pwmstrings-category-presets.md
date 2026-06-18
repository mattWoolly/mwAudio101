<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 148
title: PWMStrings category presets (mono PWM stylization + chorus)
status: todo
depends-on: [118, 025, 144]
component: docs
estimated-size: M
stream: presets
tag: presets_pwmstrings
---

## Objective

Author the PWMStrings preset set: square+sub with an LFO sweeping pulse width plus baked chorus, explicitly framed as a mono PWM stylization, not true polyphonic pads.

## Context

- `docs/research/11-cultural-influence.md §4.5,§7.1(4)` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- ~8 .mw101preset JSON files under presets/PWMStrings/, each traceable to the §4.5 PWM finding
- Triangle-LFO pulse-width sweep over square+sub, with Chorus FX baked in (§4.5; FX bakeable per ADR-016)
- Every 'strings'/'pad' preset description states it is a mono PWM stylization (one VCO, true pads impossible) per §4.5 honest label
- Test enumerating the folder: each file validates and round-trips through the loader

## Out of scope

- Bright single-osc leads (presets-4)
- Stored sequences (presets-7)

## Acceptance criteria

- [ ] ctest --preset default -R presets_pwmstrings --no-tests=error passes; test names begin with presets_pwmstrings
- [ ] Every file: category=PWMStrings, IDs present, values in range, choice indices valid (008 §C18)
- [ ] Each strings/pad preset description carries the mono-PWM-stylization honest label (§4.5)
- [ ] No 'as used on track X' or 'TB-303 filter' descriptors (008 §C16, §7.3)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_pwmstrings --no-tests=error
```
