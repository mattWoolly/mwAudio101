<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 162d
title: Align the plugin MIDI path to feed pitch-bend + mod-wheel into the core controller-ingress seam (so bend/wheel work IN the plugin)
status: todo
depends-on: [162c, 104, 101, 111]
component: app
estimated-size: M
stream: plugin
tag: cc_ingress
---

## Objective

162c built the CORE controller-ingress contract (the engine consumes raw PitchBend + CC1
ControlChange MidiEvents + BlockContext::controllers, applying bend->{VCO,VCF} and mod-wheel->LFO
depth). But the PLUGIN side does not yet feed it: PluginProcessor builds BlockContext WITHOUT setting
ctx.controllers (162c's waiver was core-only), and the MidiFrontEnd/EventTranslator may route CC1 via
the CcLearnMap to a ParamValue and PitchBend to semitones rather than emitting the raw PitchBend/CC1
MidiEvents the engine now consumes. RESULT: bend + mod-wheel work in the core tests but are INERT in
the real plugin/DAW. Align the plugin so bend/wheel actually work end-to-end.

## Context

- 162c: core/BlockContext.h `ContinuousControllers{pitchBend[-1,1], modWheel[0,1]}`; core/Engine.cpp
  process() seeds from ctx.controllers + renderChunk() consumes PitchBend/CC1 MidiEvents.
- plugin/PluginProcessor.cpp processBlock (builds BlockContext — must set ctx.controllers and/or pass
  the PitchBend/CC1 MidiEvents through to the engine).
- plugin/midi/MidiFrontEnd.{h,cpp} + EventTranslator.h (101/104 — how it currently translates
  PitchBend/CC; confirm whether it emits raw PitchBend/CC1 mw::MidiEvents or routes them elsewhere).
- plugin/midi/CcLearnMap (CC1 may be a learnable target — reconcile: the mod-wheel both drives the
  mapped param AND the LFO-depth-via-controllers; decide the canonical routing without double-applying).

## Scope

- Make the processor populate BlockContext::controllers (pitchBend, modWheel) each block from the live
  host pitch-bend + CC1, AND/OR ensure MidiFrontEnd emits the raw PitchBend/CC1 mw::MidiEvents the
  engine (162c) consumes — whichever matches the 162c contract; reconcile the CcLearnMap CC1 path so
  bend/wheel are applied EXACTLY once (no double-application). RT-safe (POD writes; no audio-thread alloc).
- A plugin-level test (tag cc_ingress) driving the real processor with a host PitchBend + a CC1 move and
  asserting the rendered output bends pitch + the LFO depth rises (the 162c effect, now via the plugin path).

## Out of scope

- The core seam (162c, done); MPE per-note bend/pressure (separate); the dispatch legs (162).

## Acceptance criteria

- [ ] A host pitch-bend bends the VCO (and VCF per bend_dest) in the REAL plugin (processor-driven render), not just the core test [162c contract]
- [ ] A CC1/mod-wheel move raises the LFO mod depth in the real plugin; the CcLearnMap CC1 path does not double-apply
- [ ] cc_ingress plugin test asserts both via rendered output; no audio-thread alloc/lock; full suite green

## Verification commands

```
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R cc_ingress --no-tests=error
```
