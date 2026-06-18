<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 067
title: VoiceTypes.h — shared voice/control PODs, enums, and pool constants
status: done
depends-on: [001, 006, 007]
component: core
estimated-size: S
stream: voice-control
tag: voicetypes
---

## Objective

Create the header-only shared types for the voice/control subsystem: pool-size constants, the VoiceMode / GateTrigMode / VoiceState enums, and the NoteEvent / NoteDecision PODs that the whole stream consumes.

## Context

- `docs/design/04-voice-and-control.md §3.1` — read first
- `docs/design/04-voice-and-control.md §3.2` — read first
- `docs/design/04-voice-and-control.md §3.3` — read first
- `docs/design/04-voice-and-control.md §3.4` — read first
- `ADR-006 §Decision item 3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `voicetypes`.

## Scope

- core/voice/VoiceTypes.h with namespace mw, no .cpp
- kMaxUnison=8, kMaxPoly=8, kMaxVoices=kMaxPoly*kMaxUnison constants referencing Calibration.h for the (PI) caps (§3.1)
- enum class VoiceMode {Mono,Unison,Poly}; GateTrigMode {Gate,GateTrig,Lfo}; VoiceState {Idle,Active,Releasing,Stealing} (§3.2)
- struct NoteEvent {Type,note,velocity,sampleOffset} and struct NoteDecision {activeNote,gate,retrigger,clockReset} (§3.3)
- all PODs trivially copyable, uint8_t-backed enums as shown

## Out of scope

- any behavioral logic, classes Voice/KeyAssigner/VoiceManager/ControlCore
- parameter-ID strings or APVTS (owned by doc 06)
- the numeric values of (PI) pool caps if they must live in Calibration.h — reference, do not redefine the calibration table

## Acceptance criteria

- [ ] kMaxVoices == kMaxPoly * kMaxUnison compile-time static_assert (§3.1; ADR-006 §Decision item 3)
- [ ] enum underlying types and member order match §3.2 exactly (Mono=0, Gate=0, Idle=0)
- [ ] NoteEvent and NoteDecision field names/types/defaults match §3.3 verbatim
- [ ] test names begin with the tag; verify: ctest --preset default -R voicetypes --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R voicetypes --no-tests=error
```
