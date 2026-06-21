<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118e
title: CRITICAL — toNoteEvent must read data0 (the note number), not noteId, so MIDI notes play the right pitch
status: todo
depends-on: [118, 160, 162e]
component: core
estimated-size: S
stream: voice-control
tag: note_ingress
---

## Objective

CRITICAL pitch bug (found by 162d): core/Engine.cpp toNoteEvent() sets the played note from `e.noteId`
(`out.note = clamp(e.noteId,0,127)`), but per docs/design/09 §3.3 + the field docs, `noteId` is the
CLAP note-id (`-1` for MIDI-derived events, used for note-expression VOICE matching) and `data0` is the
NOTE NUMBER (pitch). So a MIDI note from a DAW (noteId=-1) resolves to note 0 / wrong pitch — the synth
plays the WRONG NOTE for its primary input. (160's core pitch test passed only because it set noteId as
the pitch, encoding the bug's assumption.) Fix toNoteEvent to read data0 for the pitch.

## Context

- `core/Engine.cpp` toNoteEvent (~L99-116): `out.note = clamp<int>(e.noteId,0,127)` — WRONG; must be
  `out.note = clamp<int>(static_cast<int>(e.data0),0,127)` (round/truncate the float note number).
  This also fixes the seq/arp path (~L351 ke.pitch = ne.note - kSeqVoiceBaseMidi uses ne.note).
- `core/BlockContext.h` MidiEvent {noteId (int16, -1 MIDI), data0 (float note/CC/param)}; design §3.3
  (the seam contract): data0 = note number; noteId = CLAP id / -1, for note-expression matching only.
- `plugin/midi/EventTranslator.h`: confirms "data0 -> note/CC/param index", "noteId -1 preserved for MIDI".

## Scope

- Engine.cpp toNoteEvent: derive the note number from `data0` (the seam's note-number field), NOT noteId.
  Keep noteId for note-expression voice matching where used (it is NOT the pitch). Velocity ingress
  (162b) + the NoteOn/Off type logic stay as-is.
- Reconcile any test that previously fed noteId-as-pitch: tests must drive a MIDI-realistic event
  (data0 = note number, noteId = -1) and assert the CORRECT pitch. Where 160's DispatchVcoTest used
  noteId for pitch, either it already round-trips (if it set both) or update it to data0 so it tests
  the REAL path. Do NOT weaken any pitch-ratio assertion.

## Acceptance criteria

- [ ] A MIDI-derived NoteOn (data0 = note number, noteId = -1) plays the note's CORRECT pitch (note 48 vs 72 = 4x, at the right absolute frequency for the 1V/oct reference), asserted via rendered fundamental — proving real-MIDI input is correct [§3.3; ADR-005]
- [ ] The seq/arp note path (ke.pitch from ne.note) also gets the correct note number
- [ ] note_ingress tests cover a MIDI-realistic event end-to-end; existing pitch tests stay green; full core suite green
- [ ] RT-safe; no behavior change for the velocity/type/noteId-expression paths

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R 'note_ingress|dispatch_vco' --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
