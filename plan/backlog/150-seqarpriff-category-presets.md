<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 150
title: SeqArpRiff category presets (stored 100-step patterns + arp settings)
status: done
depends-on: [118, 025, 144]
component: docs
estimated-size: M
stream: presets
tag: presets_seqarpriff
---

## Objective

Author the SeqArpRiff preset set: presets carrying a stored 100-step sequencer pattern and/or arp settings, capturing the riff-as-identity idiom (Voodoo Ray as canonical inspired-by example).

## Context

- `docs/research/11-cultural-influence.md §4.7,§4.8,§7.1(6)` — read first
- `plan/decisions/008-parameter-state-preset-schema.md C8,C13,C14,C20` — read first
- `plan/decisions/016-owner-ratifications-2026-06-18.md (sequencer = saved state)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- ~12 .mw101preset JSON files under presets/SeqArpRiff/, each with a seq and/or arp section in addition to params (008 §C13)
- Stored patterns use per-step note/gate/tie/rest/accent within the fixed 100-step capacity (008 §C8/§C20)
- Arp presets use up/down/up-down modes at canonical choice indices; host-synced (§4.7)
- Voodoo-Ray-style two-101-under-808 sub+riff homage framed inspired-by, never 'as used on track X' (§4.8, 008 §C16)
- Test enumerating the folder: each file validates, round-trips the seq/arp sections through the loader, and stored step count <= 100

## Out of scope

- The sequencer/arp DSP engine (full-engine)
- Non-sequenced timbre presets (presets-2..6)

## Acceptance criteria

- [ ] ctest --preset default -R presets_seqarpriff --no-tests=error passes; test names begin with presets_seqarpriff
- [ ] Every file: category=SeqArpRiff, params+seq/arp present, all values in range, choice indices valid (008 §C18)
- [ ] Stored sequences fit the fixed 100-step capacity and round-trip exactly (008 §C8/§C20)
- [ ] Riff/track references are inspired-by/disputed, no 'as used on track X' (008 §C16, §4.8/§7.3)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R presets_seqarpriff --no-tests=error
```
