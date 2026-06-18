<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 146
title: SubBass category presets (independent sub-osc, 303-underpinning)
status: todo
depends-on: [118, 025, 144]
component: docs
estimated-size: M
stream: presets
tag: presets_subbass
---

## Objective

Author the SubBass preset set: independent-level sub-oscillator at -1/-2 oct, high mixer level under a clipped VCA, square+saw blend with optional noise, voiced to sit beneath a 303-style line.

## Context

- `docs/research/11-cultural-influence.md §4.4,§7.1(2)` — read first
- `docs/research/11-cultural-influence.md §4.8` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- ~10 .mw101preset JSON files under presets/SubBass/, each traceable to a §4.4/§4.8 finding
- Use the -1 Oct Sq / -2 Oct Sq / -2 Oct Pulse sub modes at canonical choice indices (§4.4)
- Square+saw source blend, optional noise for movement, clipped VCA (§7.1(2))
- Voodoo-Ray-style under-303 voicing framed inspired-by, never 'as used on track X' (§4.8, 008 §C16)
- Test enumerating the folder: each file validates and round-trips through the loader

## Out of scope

- Stored sequences (presets-7)
- Bright lead voicing (presets-4)

## Acceptance criteria

- [ ] ctest --preset default -R presets_subbass --no-tests=error passes; test names begin with presets_subbass
- [ ] Every file: category=SubBass, IDs present, values in range, sub-mode choice indices valid (008 §C18)
- [ ] Artist/track references are inspired-by/disputed, no 'as used on track X' (008 §C16, §7.3)
- [ ] Sub mode uses the canonical (non-sound_ext) choice indices (008 §C6)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_subbass --no-tests=error
```
