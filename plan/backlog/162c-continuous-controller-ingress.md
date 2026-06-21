<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 162c
title: Carry pitch-bend + mod-wheel through the seam so bend->VCO/VCF and mod-wheel->LFO work
status: todo
depends-on: [162, 118, 101, 104]
component: core
estimated-size: M
stream: voice-control
tag: cc_ingress
---

## Objective

Task 162 decoded+routed pitch-bend->{VCO,VCF} (per mod.bend_dest/bend_range) and mod-wheel->LFO depth,
but the LIVE controller POSITION never reaches the engine: BlockContext carries no continuous-controller
(pitch-bend / mod-wheel) state, and the engine's note translator DROPS PitchBend/ControlChange MidiEvents.
So bend/wheel produce no audible effect. Carry the bend + mod-wheel position through the core seam and
apply it so the 162 bend/wheel routing activates.

## Context

- `core/BlockContext.h` (add continuous-controller state: pitch-bend value, mod-wheel value — or a
  per-block controller snapshot) + how the engine reads it.
- `core/Engine.cpp` renderChunk note translator (currently routes only NoteOn/NoteOff; it must also
  consume PitchBend + CC1/mod-wheel MidiEvents into the controller state).
- `plugin/midi/EventTranslator.h` / MidiFrontEnd (101/104) — confirm PitchBend/CC are translated into
  mw::MidiEvent (they may already be; the gap is the ENGINE dropping them).
- The 162 dispatch already applies bend->pitch/cutoff CV and mod-wheel->LFO depth once it has the values.

## Scope

- Carry pitch-bend (14-bit, centered) + mod-wheel (CC1) through the seam (BlockContext controller
  state, or via the existing MidiEvent stream the engine consumes per sub-block). The engine applies
  the latest bend/wheel to the per-voice CVs each control tick (bend scaled by mod.bend_range_* per
  bend_dest; mod-wheel scales the LFO depth per mod.lfo_mod_wheel). RT-safe.

## Acceptance criteria

- [ ] A pitch-bend event bends the VCO pitch (and VCF cutoff per bend_dest) by the bend_range amount — asserted via rendered fundamental shift [§ mod routing]
- [ ] A mod-wheel (CC1) move raises the LFO modulation depth per mod.lfo_mod_wheel — asserted audibly
- [ ] cc_ingress tests assert both; full core suite green; RT-safe (no alloc/lock on the audio path)

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R cc_ingress --no-tests=error --output-on-failure
```
