<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 104
title: MidiFrontEnd note/gate/bend/pressure/CC translation (plugin/midi/MidiFrontEnd.h/.cpp)
status: todo
depends-on: [001, 006, 007, 020, 073, 099, 100, 098]
component: app
estimated-size: M
stream: plugin
tag: midifront
---

## Objective

Implement MidiFrontEnd: drain a juce::MidiBuffer + capability rung into normalized HostEvents, route note/gate to the assigner, apply continuous pre-Q bend and pressure offsets via fixed-cost one-pole de-zippers, set tuning/bend-range/modern-unquantized, and apply velocity routing (default ON).

## Context

- `docs/design/09 §4.1-4.5` — read first
- `docs/design/09 §6.4` — read first
- `ADR-012 C1-C9` — read first
- `ADR-012 C24` — read first
- `ADR-016 R-2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `midifront`.

## Scope

- prepare(sr,maxBlock) sizes bend/pressure smoothers; reset; processMidi(midi, map, neRung, out)
- Bend/pressure as continuous offsets; CC values resolved through the CcLearnMap to a param index then pushed as ParamValue HostEvents
- setTuning/setBendRange/setModernUnquantized store params (quantizer + CV scaling live in core; this passes assembled offsets)
- Velocity-ON default: emit per-voice level + cutoff offsets; no-velocity switch disables (§4.5; ADR-016 R-2)
- O(1)/sample de-zipper, no branch on message arrival (§6.4)

## Out of scope

- 6-bit quantizer / CV scaling (osc/voice in core)
- MPE per-channel reconstruction (plugin-9)
- Key-assigner DSP (voice)
- HostEvent->MidiEvent (plugin-8)

## Acceptance criteria

- [ ] Channel bend (default ±2, range 0..24) emitted as a continuous pre-Q offset (§4.4; ADR-012 C8)
- [ ] Velocity ON routes to VCA level + VCF cutoff amount; switch OFF disables (§4.5; ADR-016 R-2)
- [ ] CC values resolved through the learn map and de-zippered O(1)/sample with no branch on arrival; processMidi performs zero alloc/lock (tag 'midifront', AudioThreadGuard) [§6.4; ADR-012 C24]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R midifront --no-tests=error
```
