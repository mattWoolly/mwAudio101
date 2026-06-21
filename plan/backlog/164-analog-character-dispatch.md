<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 164
title: Control-dispatch — analog character + tuning + expression (drift/vintage/variance/tune.a4/warmup/expression/MPE)
status: done
depends-on: [160, 161, 162]
component: core
estimated-size: M
stream: voice-control
tag: dispatch_character
---

## Objective

Apply the remaining params the dispatch doesn't yet read: the analog-character set (drift, vintage
age/enable/cal_spread/detune, the per-voice var.* variance), tuning (tune.a4 reference, tune.slop),
warmup.time, amp.expression (CC11), velocity enable/depth (if not done in 162), and the MPE params.

## Context

- ADR-028; 160/161/162 (the seam + CV sums). The drift/vintage model (core/dsp or core/voice drift),
  the tuning conversion (midi->Hz reference = tune.a4), MPE per-note state.

## Scope

- vintage.{enable,age,cal_spread,detune} + drift.{depth,rate} -> the drift/age model perturbs pitch/cal as designed; var.{cutoff,env_time,pw,glide} -> per-voice variance; tune.a4 -> the pitch reference (440 default, 442 'hardware-accurate'); tune.slop -> tuning variance; warmup.time -> launch smoothing; amp.expression -> VCA scaler; mpe.{enable,bend_range,pressure_dest} -> MPE routing.

## Acceptance criteria

- [ ] vintage/drift enabled perturbs pitch over time (disabled = stable); tune.a4=442 shifts the reference; var.* adds per-voice spread; amp.expression scales the VCA; MPE per-note bend/pressure work when enabled — each asserted (audibly or via the drift/pitch model)
- [ ] RT-safe; `dispatch_character` tests; full core suite green

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_character --no-tests=error --output-on-failure
```
