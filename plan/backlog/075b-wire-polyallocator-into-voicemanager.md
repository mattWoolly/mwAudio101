<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 075b
title: Wire PolyAllocator into the VoiceManager/Engine POLY path (POLY mode must sound)
status: in-review
depends-on: [075, 074, 118, 069]
component: core
estimated-size: S
stream: voice-control
tag: polywire
---

## Objective

POLY voice mode actually allocates and sounds voices through the assembled Engine: note events
in `VoiceMode::Poly` route through `PolyAllocator` (075) to assign/steal voices, instead of the
current early-return that makes POLY silent. Closes the wave-16 QA finding (PR #101/076b): the
CPU-budget gate had to drive UNISON because full-POLY sounds ZERO voices.

## Context

- Originating finding: 076b QA — `VoiceManager`/`Engine` POLY path early-returns (handleNoteEvent/
  controlTick skip POLY), so `PolyAllocator` (075, merged but uncalled) never runs; POLY renders silence.
- Verified facts: `core/voice/VoiceManager.{h,cpp}` (the POLY early-return), `core/voice/PolyAllocator.{h,cpp}`
  (075 — allocatePoly/releasePoly/steal), `core/Engine.cpp` (note routing). Design: `docs/design/04 §6.4/§6.5`; ADR-006.
- TDD: write the failing test first (`tests/unit/`, names begin `polywire`): in POLY mode, N note-ons
  sound N voices; the (N+1)th steals deterministically (PolyAllocator order); release frees voices.

## Scope

- Route POLY note events through `PolyAllocator` in `VoiceManager` (allocate on note-on, release on
  note-off, deterministic steal at capacity), so the Engine sounds POLY voices. Keep MONO/UNISON paths
  intact. RT-safe (noexcept, no alloc/lock on the audio thread).
- Update the CPU-budget golden (076b) to drive full poly groups × unison once POLY sounds (broaden the
  worst-case load), if straightforward.

## Out of scope

- PolyAllocator internals (075, done); JUCE/plugin marshalling (processor 111).

## Acceptance criteria

- [ ] In `VoiceMode::Poly`, K simultaneous note-ons produce K sounding voices through `Engine::process`;
      the (K+1)th triggers a deterministic steal (PolyAllocator order) [§6.4; ADR-006 C14].
- [ ] note-off releases the matching voice; mono/unison behavior unchanged.
- [ ] RT invariants preserved (no alloc/lock under AudioThreadGuard).

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R polywire --no-tests=error
```
