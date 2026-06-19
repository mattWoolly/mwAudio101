<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 073
title: Voice.h/.cpp — circuit-accurate signal-path assembly + drift seed
status: done
depends-on: [001, 006, 007, 067, 068, 032, 047, 062]
component: core
estimated-size: M
stream: voice-control
tag: voice
---

## Objective

Assemble the per-voice signal path (one VCO+sub+noise, one VCF, one VCA, one ADSR, one LFO, one Glide) plus the inline VoiceDrift block, and implement the note lifecycle + state-machine render contract with a deterministic per-voice drift seed.

## Context

- `docs/design/04-voice-and-control.md §4.1` — read first
- `docs/design/04-voice-and-control.md §4.2` — read first
- `docs/design/04-voice-and-control.md §4.3` — read first
- `docs/design/04-voice-and-control.md §4.4` — read first
- `ADR-006 §Decision item 1` — read first
- `ADR-019 §Contract VT-01` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `voice`.

## Scope

- core/voice/Voice.h/.cpp matching the §4.2 data layout and method signatures
- prepare(sampleRate,oversampleFactor,voiceIndex,instanceSeed); noteOn/noteOff/setGlideTarget/setDetuneCents/setStereoPan
- render(outL,outR,numSamples) accumulating; Idle skipped, Releasing self-transitions to Idle at silence, Stealing applies stealGain_ fade then Idle (§4.3)
- beginSteal() flips state to Stealing; currentLevel()/noteSerial()/currentNote()/isActive() accessors (§4.2)
- VoiceDrift seed = hashCombine(instanceSeed, voiceIndex); rng.reseed; one-pole tune/pw/cutoff walk smoothers wired (seed derivation only, §4.4)

## Out of scope

- the drift DSP law / walk coefficients / vintage.age scaling (ADR-009)
- internal DSP of VCO/VCF/VCA/ADSR/LFO (their own modules/docs)
- polyphony, note priority, unison fan-out (VoiceManager)
- 6-bit pitch quantization (ControlCore); Voice consumes a glide target in Hz

## Acceptance criteria

- [ ] Voice owns exactly one ADSR and one LFO; flat value type, no virtual dispatch in render (§4.1-§4.2; ADR-006 §Decision item 1)
- [ ] render is noexcept/alloc-free/lock-free; Idle costs nothing, Releasing finishes its tail then goes Idle, Stealing fades via stealGain_ then Idle (§4.3; ADR-019 VT-01)
- [ ] TDD: drift seed is hashCombine(instanceSeed,voiceIndex) and is byte-stable across runs; distinct voiceIndex => distinct seed (§4.4; ADR-006 C18)
- [ ] beginSteal sets state to Stealing and a subsequent completed fade transitions to Idle (§4.3)
- [ ] test names begin with the tag; verify: ctest --preset default -R voice --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R voice --no-tests=error
```
