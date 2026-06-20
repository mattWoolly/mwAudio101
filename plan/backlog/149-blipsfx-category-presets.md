<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 149
title: BlipsFX category presets (percussive blips, noise FX)
status: done
depends-on: [118, 025, 144]
component: docs
estimated-size: M
stream: presets
tag: presets_blipsfx
---

## Objective

Author the BlipsFX preset set: very short A/D envelopes on a resonant filter plus noise-source effects, framed as general-practice character rather than a sourced SH-101 idiom.

## Context

- `docs/research/11-cultural-influence.md §4.6,§7.1(5)` — read first
- `docs/research/11-cultural-influence.md §3.1` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- ~8 .mw101preset JSON files under presets/BlipsFX/, each labelled general-practice/theory character (§4.6, §7.1(5))
- Short attack/decay percussive blips on a resonant filter; noise-modulated sweeps and zaps
- May reference Squarepusher 'Dimotane CO' noise->VCO-pitch-mod as the one documented inspired-by exception (§3.1, §7.3)
- Test enumerating the folder: each file validates and round-trips through the loader

## Out of scope

- Sustained leads (presets-4)
- Stored sequences (presets-7)

## Acceptance criteria

- [ ] ctest --preset default -R presets_blipsfx --no-tests=error passes; test names begin with presets_blipsfx
- [ ] Every file: category=BlipsFX, IDs present, values in range, choice indices valid (008 §C18)
- [ ] Blip presets labelled general-practice/theory character, not a sourced SH-101 idiom (§4.6)
- [ ] Artist references are inspired-by/disputed only (008 §C16, §7.3)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_blipsfx --no-tests=error
```
