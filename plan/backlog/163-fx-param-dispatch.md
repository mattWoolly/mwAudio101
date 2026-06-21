<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 163
title: Control-dispatch — FX params (bypass + drive/chorus/delay) decoded from the snapshot into FxChain
status: done
depends-on: [160, 118]
component: core
estimated-size: M
stream: voice-control
tag: dispatch_fx
---

## Objective

The FxChain IS run by the Engine (task 118) but its params are NEVER fed from ctx.params, so FX are
permanently at defaults/bypassed regardless of the knobs/presets. Decode the FX params and apply.

## Context

- ADR-028; `core/dsp/fx/FxChain.h` (setParams + FxParams: drive/chorus/delay sub-structs + bypass) +
  the §4.1 FX site in Engine::process (a SEPARATE site from the per-voice path — parallelizable with 161/162).
- Params: fx.bypass, fx.drive_{enable,amount,tone,output}, fx.chorus_{enable,mode,rate,depth,width,mix}, fx.delay_{enable,time,feedback,damp,width,mix,sync,division,pingpong}, out.mono.

## Scope

- Decode ctx.params[FX range] into FxParams (the enables -> on flags; the continuous/choice values -> the sub-struct fields; fx.bypass -> master bypass; out.mono -> mono collapse) and call fx_.setParams() once per block.

## Acceptance criteria

- [ ] Enabling Drive adds harmonics/saturation; enabling Chorus adds modulated detune; enabling Delay adds echoes at the set time/feedback; fx.bypass mutes all FX (FX-off stays bit-exact per task 141) — each asserted audibly
- [ ] out.mono collapses to mono; FX respond to their continuous params (e.g. delay time changes echo spacing)
- [ ] RT-safe; `dispatch_fx` audio-effect tests; full core suite green

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_fx --no-tests=error --output-on-failure
```
