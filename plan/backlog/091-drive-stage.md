<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 091
title: Drive stage: oversampled asymmetric waveshaper + tilt + DC block
status: todo
depends-on: [001, 006, 007, 088, 090]
component: core
estimated-size: M
stream: fx
tag: fxdrive
---

## Objective

Implement the Drive stage: 2x-oversampled asymmetric tanh waveshaper with pre/de-emphasis tilt and post-downsample DC blocker, mono-in/mono-out.

## Context

- `07-fx-section.md §4` — read first
- `07-fx-section.md §4.3` — read first
- `07-fx-section.md §4.4` — read first
- `07-fx-section.md §4.5` — read first
- `ADR-010 FX-5` — read first
- `ADR-017 L2` — read first
- `ADR-017 L8` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `fxdrive`.

## Scope

- core/dsp/fx/Drive.h/.cpp per §4.2 signature; flow upsample->pre-emphasis->input gain->asymmetric shaper->de-emphasis->downsample->DC blocker->makeup per §4.1
- Asymmetric shaper y=tanh(kDrivePreGain*(x+kDriveBias))-tanh(kDrivePreGain*kDriveBias) per §4.3
- One-pole pre/de-emphasis tilt (tone=0.5 unity) per §4.4; DC blocker y=x-x1+R*y1 after downsample per §4.5
- latencySamples() == FxOversampler2x group delay; add PI constants (kDriveBias, kDrivePreGain, kDriveTiltHz, kDcBlockR, kDriveAliasFloorDb) to Calibration.h
- Smoothed Drive/Output gains; in-place process on caller mono buffer

## Out of scope

- Per-block bypass dispatch / chain wiring (owned by fx-7 FxChain)
- Mono-to-stereo widening (Drive never widens, §4.1)
- Parameter ID/range registration (owned by param-schema)

## Acceptance criteria

- [ ] fxdrive test: tone=0.5 yields flat pre/de-emphasis (low drive amount is near-linear pass through) per §4.4
- [ ] fxdrive test: nonzero bias produces even harmonics (asymmetry) and the DC blocker removes the standing offset per §4.3/§4.5
- [ ] fxdrive test: a full-scale sine into hot Drive has in-band aliasing below kDriveAliasFloorDb, and 2x path beats the same shaper at 1x per ADR-017 L2 / §9
- [ ] fxdrive test: latencySamples() equals the FxOversampler2x group delay and is invariant to drive amount/on per ADR-017 L2/L8
- [ ] fxdrive test: prepare/reset/process/setParams perform no heap allocation and take no locks per §4.7
- [ ] Verify: ctest --preset default -R fxdrive --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R fxdrive --no-tests=error
```
