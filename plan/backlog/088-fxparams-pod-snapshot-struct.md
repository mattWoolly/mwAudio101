<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 088
title: FxParams POD snapshot struct
status: todo
depends-on: [001, 006]
component: core
estimated-size: S
stream: fx
tag: fxparams
---

## Objective

Define the trivially-copyable POD FxParams snapshot (Drive/Chorus/Delay nested structs plus master flags) consumed lock-free by the FX audio thread.

## Context

- `07-fx-section.md §7` — read first
- `07-fx-section.md §3.1` — read first
- `ADR-010 FX-13` — read first
- `ADR-010 FX-9` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fxparams`.

## Scope

- core/dsp/fx/FxParams.h with struct FxParams and nested DriveP/ChorusP/DelayP per §7
- Plain decoded values only (no parameter IDs, ranges, or APVTS types)
- Engine-default field initializers: masterBypass=true, monoOutput=false, drive.on=false, chorus.mode=Off(0), delay.on=false, hostBpm=120.0
- Static assertion that FxParams is trivially copyable

## Out of scope

- Mapping APVTS/doc-06 parameter IDs into FxParams (owned by plugin/param-schema)
- Any DSP processing

## Acceptance criteria

- [ ] fxparams test: default-constructed FxParams has masterBypass==true, monoOutput==false, drive.on==false, chorus.mode==0, delay.on==false per §7 and ADR-010 FX-13
- [ ] fxparams test: static_assert / runtime check that std::is_trivially_copyable_v<FxParams> per §7
- [ ] fxparams test: DelayP/ChorusP/DriveP field set matches §7 layout (DriveP{on,amount,tone,output}; ChorusP{mode,rate,depth,width,mix}; DelayP{on,sync,pingpong,division,timeMs,feedback,damp,width,mix})
- [ ] Verify: ctest --preset default -R fxparams --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fxparams --no-tests=error
```
