<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 160
title: Control-dispatch seam + VCO/source-mixer — Engine reads ctx.params and the oscillator+mixer respond (KEYSTONE)
status: todo
depends-on: [118, 118c, 070, 071, 073, 102b]
component: core
estimated-size: L
stream: voice-control
tag: dispatch_vco
---

## Objective

THE KEYSTONE of ADR-028. Today the Engine receives BlockContext::params (the full 91-param
mw::ParamSnapshot) but NEVER reads it (`grep ctx.params core/` = 0 hits) — so the synth ignores every
knob. Establish the control-dispatch SEAM: each control tick, the Engine reads ctx.params and applies
the per-voice values to the DSP setters; then wire the VCO + source mixer end-to-end as the first
section + the pattern the rest of the cluster (161/162) follows. This makes DIFFERENT NOTES PLAY
DIFFERENT PITCHES (folds in the superseded 073b) and makes the oscillator/mixer respond.

## Context (read first)

- ADR-028 (the seam decision); ADR-005 (ControlCore = circuit-accurate VCO pitch authority, count-domain).
- `core/Engine.cpp` (process/renderChunk — add the dispatch; control_.advance has the voices + tick).
- `core/params/ParamSnapshot.h` + `core/params/ParamDefs.h` (the registry-indexed POD + the 91 IDs/ranges/skews) + `core/BlockContext.h` (ctx.params).
- `core/voice/Voice.{h,cpp}` (the per-voice DSP + the `glide_.nextValue()` DISCARD bug at render) + `core/dsp/Oscillator.h` (OscillatorSection::setControls / OscControls fields: pitchCvVolts, footage, pwmCvNorm, subShape) + `core/control/ControlCore.h` (assemblePitchCounts/blendedPitchVolts).
- The audit: Voice::render mixes ONLY `src.saw` — pulse/sub/noise are DROPPED. The source mixer must sum all four by their levels.

## Scope

- Add the dispatch seam: Engine reads ctx.params each control tick and applies per-voice control; keep it RT-safe (POD read + arithmetic + setters; no heap/lock; noexcept). Structural params stay off-thread.
- VCO: pitch (the ControlCore count-domain CV per ADR-005, applied to osc pitchCvVolts so note 48 vs 72 ~ 4 octaves), tune (param), fine (param), range/footage (choice 0-5: 16'/8'/4'/2'/32'/64'), pw, sub mode. Reconcile the duplicate glide (ControlCore count-domain owns it in MONO; apply once — fixes the glide_ discard).
- Source mixer: sum saw + pulse + sub + noise by mw101.{saw,pulse,sub,noise}.level (the mixer the §4.1 comment promised but never implemented).

## Out of scope

- VCF/Env/VCA dispatch (161); LFO + modulation routing (162); FX (163); analog character/tune.a4/velocity/bend/drift (164). Build the seam so those plug in cleanly.

## Acceptance criteria

- [ ] Different MIDI notes produce different VCO fundamentals at the correct 1V/oct ratio (note 48 vs 72 ~16x), asserted by a real frequency measurement (zero-crossing/FFT) — the headline fix [ADR-005; §7.3]
- [ ] vco.tune/fine shift pitch; range switches octave/footage; pw changes the pulse duty; sub.mode selects the sub octave/shape — each asserted audibly
- [ ] The source mixer sums saw+pulse+sub+noise by their level params (e.g. saw=0/pulse=1 yields a pulse-dominant spectrum; noise level raises broadband content) — asserted
- [ ] Glide applied exactly once (no double-glide); MONO pitch flows from the single KeyAssigner note [ADR-006]
- [ ] RT-safe (AudioThreadGuard-clean dispatch); `dispatch_vco`-tagged audio-effect tests; full core suite green

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_vco --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
