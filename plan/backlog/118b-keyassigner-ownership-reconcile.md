<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118b
title: Reconcile KeyAssigner ownership — single note-priority authority, GateTrigMode reachable through Engine
status: in-review
depends-on: [118, 069, 071, 074]
component: core
estimated-size: S
stream: voice-control
tag: engine_s7
---

## Objective

A single KeyAssigner is the note-priority authority in the assembled engine, and the S7
GateTrigMode (last-note vs lowest-note priority + LFO/clock-reset trigger) is settable through
`Engine` and audibly takes effect — closing the wave-11 QA MEDIUM (PR #71) where the Engine's
adapter KeyAssigner bypassed VoiceManager's own and left S7 unreachable.

## Context

- Originating finding: wave-11 QA on PR #71 (task 118) — the Engine owns a second `KeyAssigner keys_`
  and routes notes to it via the ControlTickBridge, never calling `VoiceManager::handleNoteEvent`,
  so `VoiceManager::keyAssigner_` (doc 04 §5.1 / §9, ADR-006 C12 — the documented sole authority) is
  dead and `setGateTrigMode` is unreachable through the engine.
- Verified facts (read first): `core/Engine.cpp` (the `keys_` + `ControlTickBridge` path),
  `core/voice/VoiceManager.h/.cpp` (`handleNoteEvent`, `controlTick(NoteDecision)`, `keyAssigner_`,
  `setGateTrigMode`), `core/control/ControlCore` (`advance()` duck-typed no-arg `controlTick()`),
  `core/dsp/.../KeyAssigner` (069). Design: `docs/design/04-voice-and-control.md §5.1, §6.1, §9`;
  ADR-006 C12/C17.
- TDD: write the failing test(s) first under `tests/`; test names begin with `engine_s7`.

## Scope

- Reconcile the tick/note surface so exactly ONE KeyAssigner resolves note priority for the
  assembled engine (prefer routing note events through `VoiceManager::handleNoteEvent` and driving
  its own `keyAssigner_`; remove the Engine's duplicate `keys_`/ControlTickBridge OR make it the
  single shared authority — no dead duplicate).
- Expose `Engine::setGateTrigMode(GateTrigMode)` (and any minimal sibling-surface change needed,
  e.g. a `VoiceManager`/`ControlCore` resolve()/note-ingress reconciliation) so S7 is settable.
- Keep `Engine` prepare/process/reset seam + RT invariants (noexcept, no alloc/lock) intact.

## Out of scope

- JUCE/plugin marshalling of the S7 parameter (plugin-processor task); UI control (ui stream).
- Voice DSP internals (their streams).

## Acceptance criteria

- [ ] `Engine::setGateTrigMode` exists; switching GATE (lowest-note) vs GATE+TRIG (last-note)
      changes which held note sounds through `Engine::process`, proven by an oracle test [§5.1; ADR-006 C12].
- [ ] LFO/clock-reset trigger mode is reachable through the engine [doc 04 §5; ADR-006 C17].
- [ ] No dead/duplicate KeyAssigner remains; exactly one is the authority [doc 04 §9].
- [ ] RT invariants preserved (noexcept process/reset; no alloc/lock under AudioThreadGuard).

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R engine_s7 --no-tests=error
```
