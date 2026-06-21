<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 162b
title: Plumb per-note velocity to the voice (NoteDecision + VoiceManager + KeyAssigner) so velocity->VCA/VCF works
status: in-review
depends-on: [162, 069, 074]
component: core
estimated-size: M
stream: voice-control
tag: velocity_ingress
---

## Objective

Owner ratified velocity ON, and task 162 wired velocity->VCA/VCF in the dispatch — but the per-note
MIDI velocity NEVER reaches the voice: VoiceManager::applyDecisionToVoice hardcodes v.noteOn(note,
velocity=1.0f, ...) and NoteDecision (core/voice/VoiceTypes.h) carries no velocity field. So velocity
depth is inert today. Plumb the played note's velocity from the MIDI note event through the
KeyAssigner/NoteDecision to the Voice so the 162 velocity routing activates.

## Context

- `core/voice/VoiceManager.cpp` applyDecisionToVoice (~L207/L218: the hardcoded 1.0f velocity) +
  handleNoteEvent (where the MIDI note + its velocity arrive).
- `core/voice/VoiceTypes.h` NoteDecision (add a velocity field) and `core/voice/KeyAssigner.h`
  (the resolved active note — carry its velocity through resolve()/the decision).
- `core/voice/Voice.h` noteOn(note, velocity, ...) (already takes velocity; it's just fed 1.0 today).
- The 162 dispatch already records/uses per-voice velocity once the Voice receives it.

## Scope

- Carry velocity: NoteEvent/handleNoteEvent -> the KeyAssigner's held-key record -> NoteDecision.velocity
  (the velocity of the winning note) -> VoiceManager passes it to Voice::noteOn instead of 1.0f. MONO/
  UNISON: the active note's velocity; retrigger uses the new note's velocity. Keep RT-safe (POD field).
- vel.enable=0 forces velocity to a neutral 1.0 (the 162 dispatch already gates on vel.enable/depth);
  this task just supplies the REAL velocity when enabled.

## Acceptance criteria

- [ ] A high-velocity note vs a low-velocity note produces an audibly different VCA level (and VCF cutoff when vel->VCF depth>0), with vel.enable ON — asserted via rendered output (activates the 162 routing) [owner ratification: velocity ON]
- [ ] vel.enable OFF => velocity-neutral (no velocity effect); MONO/UNISON velocity = the resolved note's velocity
- [ ] velocity_ingress tests assert the above; full core suite green; RT-safe (no alloc/lock added)

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R velocity_ingress --no-tests=error --output-on-failure
```
