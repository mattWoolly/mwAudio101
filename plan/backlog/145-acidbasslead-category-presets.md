<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 145
title: AcidBassLead category presets (squelchy resonant acid bass/lead)
status: in-review
depends-on: [118, 025, 144]
component: docs
estimated-size: M
stream: presets
tag: presets_acidbasslead
---

## Objective

Author the AcidBassLead preset set: self-oscillating resonant LP filter, high resonance, fast-decay zero-sustain filter envelope, portamento, overdrive-friendly, including a filter-as-sine-oscillator variant.

## Context

- `docs/research/11-cultural-influence.md §4.3,§7.1(1)` — read first
- `docs/research/11-cultural-influence.md §6.2` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C13,C14,C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- ~14 .mw101preset JSON files under presets/AcidBassLead/, each traceable to a §4.3 idiom finding in its meta.description/tags
- Acid shape: high resonance, Attack 0 / fast Decay / Sustain 0 / short Release filter env, auto portamento (§4.3)
- Include a 'filter-as-sine-oscillator' theremin/resonant-kick variant (§7.1(1))
- Bake Drive FX into the gritty variants (FX bakeable per ADR-016)
- Any preset using Sine LFO or 32'/64' sets sound_ext:true; numeric idiom values used as relative feel only, not as hardware calibration (§6.2)
- Test enumerating the folder: each file validates against the registry and round-trips through the loader

## Out of scope

- SeqArpRiff stored patterns (presets-7)
- Sub-focused voicing (presets-3)

## Acceptance criteria

- [ ] ctest --preset default -R presets_acidbasslead --no-tests=error passes; test names begin with presets_acidbasslead
- [ ] Every file: category=AcidBassLead, all IDs present, values in range, choice indices valid (008 §C18)
- [ ] No file's metadata contains a 'TB-303 filter' descriptor; artist refs are inspired-by/disputed only (008 §C16, §7.3)
- [ ] Presets using software-only features set sound_ext:true (008 §C15)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_acidbasslead --no-tests=error
```
