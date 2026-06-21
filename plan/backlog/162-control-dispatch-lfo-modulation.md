<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 162
title: Control-dispatch — LFO (rate/shape/dest/depths) + full modulation routing (vel/bend/mod-wheel/glide-mode)
status: in-review
depends-on: [160, 161]
component: core
estimated-size: M
stream: voice-control
tag: dispatch_lfo
---

## Objective

Complete the ADR-028 per-voice modulation routing: the LFO actually modulates its destination, and
velocity / pitch-bend / mod-wheel / glide-mode are applied. Today the LFO rate is frozen, its depths
and destination are inert, and velocity/bend/mod-wheel do nothing.

## Context

- ADR-028; 160/161 (the seam + the cutoff/pitch CV sums you add the LFO term to). `core/dsp/` Lfo
  (setRateHz/shape), Glide (mode). lfo.dest (0 Pitch/1 Filter/2 PWM) × lfo.depth_{pitch,pwm,cutoff}.

## Scope

- LFO: rate (param→Hz, calibrated), shape (Tri/Sq/Random/Noise/Sine), delay; route the LFO output to pitch / PWM / cutoff per lfo.dest scaled by the matching depth (summed into the 160/161 CVs).
- Modulation: velocity→{VCA,VCF} per vel.enable/vel.depth; pitch-bend→{VCO,VCF,Both} per mod.bend_dest scaled by bend_range_*; mod-wheel→LFO depth per mod.lfo_mod_wheel; glide.mode (Off/Auto/On).

## Acceptance criteria

- [ ] LFO modulates the selected destination (vibrato when dest=Pitch, filter wobble when dest=Filter, PWM sweep when dest=PWM) scaled by its depth; rate param changes the LFO speed — asserted audibly
- [ ] Velocity affects VCA/VCF per enable/depth; pitch-bend bends VCO/VCF per dest+range; mod-wheel raises LFO depth; glide.mode Off/Auto/On behaves correctly — asserted
- [ ] RT-safe; `dispatch_lfo` audio-effect tests; full core suite green

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_lfo --no-tests=error --output-on-failure
```
