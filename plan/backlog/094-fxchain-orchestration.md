<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 094
title: FxChain orchestration: bypass, dry-pad, mono collapse, latency
status: in-review
depends-on: [001, 006, 007, 088, 089, 091, 092, 093]
component: core
estimated-size: M
stream: fx
tag: fxchain
---

## Objective

Implement FxChain: own Drive/Chorus/Delay in fixed order, the dry-pad alignment delay, master/per-block bypass early-outs, global Mono Output collapse, and the constant FX latency report.

## Context

- `07-fx-section.md §3` — read first
- `07-fx-section.md §3.3` — read first
- `07-fx-section.md §3.4` — read first
- `07-fx-section.md §6.1` — read first
- `07-fx-section.md §6.3` — read first
- `ADR-010 FX-1` — read first
- `ADR-010 FX-2` — read first
- `ADR-010 FX-3` — read first
- `ADR-010 FX-4` — read first
- `ADR-010 FX-9` — read first
- `ADR-017 L2` — read first
- `ADR-017 L5` — read first
- `ADR-017 L6` — read first
- `ADR-017 L8` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fxchain`.

## Scope

- core/dsp/fx/FxChain.h/.cpp per §3.1 signature; prepare/reset/setParams (atomic double-buffer snapshot, acquire read) / process / getLatencySamples
- process order per §3.4: dryPad (always) -> master/all-off early-out -> Drive(if on) -> sum to L/R -> Chorus(if !=Off) -> Delay(if on) -> Mono Output collapse
- Bypass rules per §3.3: master bypass and all-blocks-off copy padded dry equally to L/R (~0 cost); per-block bypass skips that stage's process entirely
- dryPad_ length = getLatencySamples() (Drive 2x group delay only), applied every block so FX-off sits at the declared worst-case offset per §6.3 / ADR-017 L5/L6
- getLatencySamples() == Drive::latencySamples() only, invariant to bypass per §6.1 / ADR-017 L2/L8; Mono Output: m=0.5*(L+R); L=R=m per FX-9

## Out of scope

- Summing FX latency with per-voice zone delay and the setLatencySamples host call (owned by plugin/)
- Individual stage DSP internals (owned by fx-4/fx-5/fx-6)
- FX-off golden capture/bless (owned by golden-harness)

## Acceptance criteria

- [ ] fxchain test: masterBypass ON (and separately all three blocks bypassed) gives out[L][n]==out[R][n] equal to the padded mono dry, FX DSP skipped per §3.3 / ADR-010 FX-1
- [ ] fxchain test: chain order is fixed Drive->Chorus->Delay with no path applying Chorus/Delay before Drive per §3.4 / ADR-010 FX-2
- [ ] fxchain test: Drive on but Chorus Off and Delay off gives out[L]==out[R] (stereo only from Chorus/Delay) per ADR-010 FX-4
- [ ] fxchain test: monoOutput=ON with any Chorus/Delay width yields out[L]==out[R] at chain output per §3.3 / ADR-010 FX-9
- [ ] fxchain test: getLatencySamples() equals Drive 2x group delay and is invariant to drive.on, masterBypass, and per-block bypass; dryPad keeps FX-off at the constant offset per §6.1/§6.3 / ADR-017 L2/L5/L8
- [ ] fxchain test: prepare/reset/process/setParams/getLatencySamples perform no heap allocation and take no locks per §3.2 / ADR-010 FX-10
- [ ] Verify: ctest --preset default -R fxchain --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fxchain --no-tests=error
```
