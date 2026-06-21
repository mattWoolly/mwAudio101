<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118f
title: Fix dispatch_complete #181/#185 — complete 118e's data0 reconciliation (inline NoteOff events) + diagnose the FX case
status: in-review
depends-on: [118e, 165, 163]
component: qa
estimated-size: S
stream: voice-control
tag: dispatch_complete
---

## Objective

The full core suite has 2 failures (993/995): dispatch_complete #181 (env vca/glide) + #185 (every FX
param). They are TEST-reconciliation fallout from 118e (which made toNoteEvent read data0 as the note
number): 118e updated the noteOn HELPER to set data0=note but MISSED inline NoteOff/note events, so
post-118e a NoteOff with data0=0 targets note 0 and never releases the held note. Fix the failures
and COMPLETE 118e's reconciliation (sweep for other inline note events missing data0).

## Context (diagnosed)

- #181 env.release (tests/unit/DispatchCompleteTest.cpp ~L862-865): the inline NoteOff is
  `mw::MidiEvent{ NoteOff, 0, (int16)note, 0.0f /*data0*/, 0.0f, 0 }` — data0=0, so post-118e the
  note-off resolves to note 0 and never releases note 60 -> the voice stays active to the 2000-block
  cap for BOTH releases -> the ratio (activeBlocks(1.0) > activeBlocks(0.01)*4) FAILS. FIX: set the
  NoteOff's data0 = note (matching the noteOn helper).
- #185 (every FX param): uses renderHeld/renderHeldStereo helpers (note 48/60); the failure
  (~0.182 vs required >0.431) is a DIFFERENT cause — DIAGNOSE: is it (a) a note-resolution/test-setup
  issue (a helper or inline event missing data0), or (b) a GENUINE FX-param weakness (a delay/chorus/
  drive param whose low-vs-high effect no longer clears the audit threshold)? If (a), fix the test
  setup; if (b), it is a REAL finding — flag it loudly and (if in scope) fix the FX dispatch, else
  spawn a follow-up. Do NOT paper over a real FX gap by loosening the threshold.

## Scope

- Fix the #181 inline NoteOff to carry data0=note. Diagnose + fix #185 per the above.
- SWEEP tests/ for OTHER inline note On/Off MidiEvent constructions (not via the noteOn helper) that
  set data0=0 / omit data0 — post-118e they target note 0. Update them to set data0=note so 118e's
  reconciliation is COMPLETE (even where they don't currently fail, they're latently wrong).
- If #185 reveals a real FX-dispatch gap, document it; fixing the FX dispatch (core) is in scope only
  if small + clearly the right fix, else flag a follow-up.

## Acceptance criteria

- [ ] dispatch_complete is 14/14 green again; the full core suite is GREEN (was 993/995)
- [ ] #181's env.release assertion passes because the NoteOff now releases the correct note (data0=note), proving env.release genuinely shortens the voice-active duration (NOT by loosening the threshold)
- [ ] #185 passes either by a correct test-setup fix OR a real FX fix — and if it was a real FX-param weakness, that is documented (not silently thresholded away)
- [ ] No OTHER inline note event in tests/ is left with data0=0 (the 118e reconciliation is complete)

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_complete --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
