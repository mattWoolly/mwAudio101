<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 100
title: RT-safe CC/learn map (plugin/midi/CcLearnMap.h/.cpp)
status: todo
depends-on: [001, 006, 020]
component: app
estimated-size: S
stream: plugin
tag: cclearn
---

## Objective

Implement the double-buffered single-writer atomic-swap CC/learn map: editableCopy/publish on the message thread, branch-free lookup on the audio thread; seed defaults with the §6.2 CC map.

## Context

- `docs/design/09 §6.3` — read first
- `docs/design/09 §6.2` — read first
- `ADR-012 C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `cclearn`.

## Scope

- CcBinding POD and CcLearnMap with bufferA_/bufferB_ + atomic live_ pointer
- editableCopy returns inactive buffer; publish atomically stores live pointer
- lookup(cc) branch-free, returns -1 if unmapped, never locks/allocates
- Default-map seed: CC1->mod, CC7->vca.level, CC11->expression, CC74->cutoff, CC71->resonance, CC5->glide.time, CC64->HOLD per §6.2

## Out of scope

- Applying CC values / de-zipper (plugin-7)
- Param index registry (param-schema)
- Serialization of bindings (state-presets)

## Acceptance criteria

- [ ] Default bindings match the §6.2 CC table (tag 'cclearn')
- [ ] lookup on the audio thread performs zero alloc and acquires no lock (AudioThreadGuard) [§6.3; ADR-012 C16]
- [ ] editableCopy+publish swaps the live map without mutating the buffer the audio thread reads (§6.3)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R cclearn --no-tests=error
```
