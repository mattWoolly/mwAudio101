<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 021
title: INIT patch builder (out-of-box defaults, ADR-016)
status: todo
depends-on: [019, 017, 007]
component: core
estimated-size: S
stream: params
tag: initpatch
---

## Objective

Provide the INIT patch as a ParamDefs-defaults canonical tree with the ADR-016 pole overlays applied (modern control, velocity on low-mid, mono, subtle drift+low Age, FX off, A4=440, MPE off, renderVersion=CURRENT) and an empty <extras> sequence.

## Context

- `docs/design/06-parameters-state-presets.md §11` — read first
- `§3.10` — read first
- `§8.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `initpatch`.

## Scope

- core/state/InitPatch.h/.cpp building a canonical ValueTree from kParamDefs defaults with the §11 overlay table applied as a patch (param defaults unchanged)
- Overlays: control.vintage=Modern, vel.enable=true + vel.depth low-mid (PI), voice.mode=Mono, vintage.enable=true + vintage.age low (PI), fx.bypass=true + *_enable false + chorus_mode Off, tune.a4=440, mpe.enable=false, pitch.modern_unquantized=false, renderVersion=CURRENT (§11)
- (PI) overlay values referenced from Calibration.h, not inlined (§3.10; §11)
- Empty <extras> sequence by default (§11)
- Test asserting the overlayed surfaces match §11 and that vintage.age/fx.bypass PARAMETER defaults are unchanged in kParamDefs

## Out of scope

- using INIT as the load-failure fallback wiring (params-10)
- factory presets (params-13)
- calibration numeric values (core-types Calibration.h)

## Acceptance criteria

- [ ] INIT applies exactly the §11 overlay poles over kParamDefs defaults; param defaultValues are NOT mutated [§11]
- [ ] (PI) INIT values are read from Calibration.h [§3.10; §11]
- [ ] renderVersion in INIT == kCurrentRenderVersion; <extras> sequence empty [§11; §9.2]
- [ ] Test names begin with initpatch and assert overlay correctness and unchanged param defaults [§11]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R initpatch --no-tests=error
```
