<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 074
title: VoiceManager — pool, MONO/UNISON dispatch, control-tick propagation, fixed-order render
status: todo
depends-on: [001, 006, 007, 067, 069, 073]
component: core
estimated-size: M
stream: voice-control
tag: voicemanager
---

## Objective

Implement VoiceManager ownership of the fixed Voice[kMaxVoices] pool, the MONO and UNISON drive paths (both fed by the single KeyAssigner), control-tick propagation, the active-voice list, and the fixed-index-order render/sum.

## Context

- `docs/design/04-voice-and-control.md §6.1` — read first
- `docs/design/04-voice-and-control.md §6.2` — read first
- `docs/design/04-voice-and-control.md §6.3` — read first
- `docs/design/04-voice-and-control.md §8 (RT1-RT7)` — read first
- `ADR-006 §Decision item 3 MONO/UNISON` — read first
- `ADR-019 §Contract VT-01/VT-02` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `voicemanager`.

## Scope

- core/voice/VoiceManager.{h,cpp} per §6.1 layout: pool_, keyAssigner_, active_ list, prepare/setMode/setUnisonCount/setGateTrigMode/handleNoteEvent/controlTick/render
- driveMono: exactly one voice driven verbatim by the NoteDecision (glide target, gate, retrigger) — pass-through, bit-faithful (§6.2)
- driveUnison: broadcast one NoteDecision to U voices; symmetric centered cents detune detuneCents_i and (PI) stereo-spread pan_i from Calibration.h; distinct drift seed per voice already from Voice (§6.3)
- handleNoteEvent forwards MONO/UNISON note-ons/offs to keyAssigner_; controlTick(NoteDecision) propagates to the active voice(s)
- render walks active_ and sums voices in FIXED voice-index order before FX (§8 RT2; ADR-019 VT-02); mode/unison changes only at prepare or block-boundary lock-free flag (§8 RT7)

## Out of scope

- POLY allocation and stealing, re-strike, unison-group steal (voice-control-10)
- KeyAssigner resolution internals (voice-control-3) and ControlCore tick (voice-control-8)
- Voice DSP internals (voice-control-6)
- MPE per-note routing (ADR-012/022)

## Acceptance criteria

- [ ] MONO drives exactly one voice as a verbatim pass-through of the NoteDecision (§6.2; ADR-006 §Decision item 3 MONO)
- [ ] UNISON: K9/K10 — U voices share the SAME KeyAssigner NoteDecision (mono-faithful note feel); detune symmetric/centered (0 for U==1), pan spread from Calibration.h, per-voice drift seeds distinct (§6.3; ADR-006 C9-C10)
- [ ] render sums active voices in fixed voice-index order; noexcept/alloc-free/lock-free; pool sized in prepare (§8 RT1-RT3, RT6; ADR-019 VT-02)
- [ ] mode and unison-count changes apply only at prepare or a block boundary, never mid-block (§8 RT7; ADR-006 C17)
- [ ] test names begin with the tag; verify: ctest --preset default -R voicemanager --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R voicemanager --no-tests=error
```
