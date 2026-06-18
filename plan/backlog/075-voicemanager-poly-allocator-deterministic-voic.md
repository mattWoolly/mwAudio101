<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 075
title: VoiceManager POLY allocator + deterministic voice stealing
status: todo
depends-on: [001, 006, 007, 073, 074]
component: core
estimated-size: M
stream: voice-control
tag: polyalloc
---

## Objective

Add the POLY path to VoiceManager: a per-note allocator that bypasses the KeyAssigner, re-strike reuse, the deterministic oldest-release->quietest->oldest-held steal scan with note-serial tie-break, fade-then-steal, and unison-on-poly group semantics.

## Context

- `docs/design/04-voice-and-control.md §6.4` — read first
- `docs/design/04-voice-and-control.md §6.5` — read first
- `docs/design/04-voice-and-control.md Acceptance hooks (K12-K16)` — read first
- `ADR-006 §Decision item 3 POLY` — read first
- `ADR-006 §Contract C12-C16` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `polyalloc`.

## Scope

- allocatePoly(midiNote)/releasePoly(midiNote) plus nextSerial_ stamping (§6.4)
- allocation order: idle voice -> re-strike held note within the (PI) window (no doubling) -> deterministic steal scan (§6.4 steps 1-3)
- steal order oldest-in-release -> quietest (lowest currentLevel) -> oldest-held, tie-broken by ascending noteSerial_; O(kMaxVoices) integer scan, no sort/heap/alloc (§6.4 C14)
- steal calls Voice::beginSteal (fast fade, (PI) length in Calibration.h); release tails of other voices finish in place (§6.4 C15)
- every poly note is a fresh GATE+TRIG trigger; unison-on-poly: floor(maxPoly/U) groups, hard cap kMaxVoices, steal whole unison groups (§6.4 C16, C11)

## Out of scope

- MONO/UNISON drive and the shared pool/render (voice-control-9 provides them)
- KeyAssigner (POLY bypasses it)
- the fade-ramp DSP inside Voice (voice-control-6 owns beginSteal/stealGain_)

## Acceptance criteria

- [ ] K12/K13: every poly note-on is a fresh trigger; re-striking a held key reuses that voice with no doubling (§6.4; ADR-006 C12-C13)
- [ ] K14: with no idle voice, the victim follows oldest-release->quietest->oldest-held, tie-broken by ascending integer note-serial — same input yields same victim deterministically (§6.4; ADR-006 C14)
- [ ] K15: a steal calls beginSteal (fast fade, not hard cut) and other voices' release tails finish in place (§6.4; ADR-006 C15)
- [ ] K16/K11: unison-on-poly gives floor(maxPoly/U) active groups, active count never exceeds kMaxVoices, steals remove whole unison groups (§6.4; ADR-006 C11,C16)
- [ ] allocator is O(kMaxVoices) integer scan run once per note event, noexcept/alloc-free/lock-free (§6.4; ADR-006 C17-C18)
- [ ] test names begin with the tag; verify: ctest --preset default -R polyalloc --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R polyalloc --no-tests=error
```
