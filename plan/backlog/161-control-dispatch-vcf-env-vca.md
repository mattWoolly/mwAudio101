<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 161
title: Control-dispatch — VCF (cutoff/res + env_mod/kbd_track) + Envelope (ADSR) + VCA (level/mode) respond
status: done
depends-on: [160]
component: core
estimated-size: M
stream: voice-control
tag: dispatch_vcf
---

## Objective

Extend the ADR-028 dispatch seam (built by 160) so the FILTER, ENVELOPE, and VCA respond to their
params + the env/keyboard modulation of cutoff. Today cutoff/resonance are static, the ADSR times are
frozen at INIT defaults, and the VCA ignores level + the env/gate select.

## Context

- ADR-028; 160 (the seam you extend). `core/dsp/` filter (setCutoffCv/setResonance), Env (setParams),
  Vca/VcaGate (level + ENV-vs-GATE mode). `core/params/ParamDefs.h` (skews: kEnvTime, cutoff calibration).
- The cutoff CV is a SUM: base cutoff param + env_mod×env.level + kbd_track×note-pitch (LFO→cutoff is 162).

## Scope

- VCF: cutoff (param → calibrated volts), resonance (param), env amount (env_mod × env output summed into cutoff CV), keyboard track (kbd_track × current note pitch summed into cutoff CV). Self-oscillation at high resonance.
- Envelope: attack/decay/sustain/release params → env_.setParams each control tick (calibrated times).
- VCA: level param scales output; vca.mode selects ENV vs GATE source.

## Acceptance criteria

- [ ] cutoff param sweeps the filter; high resonance self-oscillates; env_mod opens the filter with the envelope; kbd_track raises cutoff with note pitch — each asserted audibly [§ filter design]
- [ ] ADSR param changes change the envelope times (e.g. longer attack -> slower onset; sustain level audible); VCA level scales amplitude; vca.mode ENV vs GATE changes the amp contour
- [ ] RT-safe; `dispatch_vcf` audio-effect tests; full core suite green

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_vcf --no-tests=error --output-on-failure
```

## Note (from 160 QA LOW)

160 reached the voice via `const_cast<Voice&>(voices_.voice(vi))` (legal — Voice is non-const — but a smell). While you are in this dispatch path, add a non-const `VoiceManager::voiceMutable(int)` accessor (a one-line VoiceManager.h add) and replace 160's const_cast at the call site with it. This is a small allowed cleanup within the dispatch work.
