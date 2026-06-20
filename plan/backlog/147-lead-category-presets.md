<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 147
title: Lead category presets (bright saw/square leads, vibrato)
status: in-review
depends-on: [118, 025, 144]
component: docs
estimated-size: M
stream: presets
tag: presets_lead
---

## Objective

Author the Lead preset set: bright saw/square with resonance for expressiveness at higher registers, vibrato/PWM motion.

## Context

- `docs/research/11-cultural-influence.md §4.6,§7.1(3)` — read first
- `docs/research/11-cultural-influence.md §6.2` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C15,C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- ~10 .mw101preset JSON files under presets/Lead/, each traceable to a §4.6 lead finding
- Bright saw/square sources, moderate-high resonance, vibrato via LFO (§4.6)
- Squarepusher 'Theme from Ernest Borgnine' main-melody homage may be cited with care as the one documented exception (§3.1, §7.3)
- Chiptune/game-music style presets, if any, labelled homage-only and not historical SH-101 usage (§4.10)
- Hardware-only register set; 32'/64' or Sine LFO leads set sound_ext:true (§6.2)
- Test enumerating the folder: each file validates and round-trips through the loader

## Out of scope

- Acid envelope shapes (presets-2)
- PWM/strings stylization (presets-5)

## Acceptance criteria

- [ ] ctest --preset default -R presets_lead --no-tests=error passes; test names begin with presets_lead
- [ ] Every file: category=Lead, IDs present, values in range, choice indices valid (008 §C18)
- [ ] Any homage/game-music preset labelled inspired-by/homage only (§4.10, §7.3, 008 §C16)
- [ ] Software-only-feature presets set sound_ext:true (008 §C15)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_lead --no-tests=error
```
