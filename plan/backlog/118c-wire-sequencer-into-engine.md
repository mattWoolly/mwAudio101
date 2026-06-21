<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118c
title: Wire SequencerEngine (arp/seq/clock) into the Engine audio path — make the sequencer + arpeggiator actually run
status: in-review
depends-on: [087, 118, 071, 074, 075b, 069]
component: core
estimated-size: L
stream: integration
tag: engine_seq
---

## Objective

CRITICAL INTEGRATION GAP (found by audit): the audio Engine (core/Engine.{h,cpp}) assembles ONLY
VoiceManager + ControlCore + FxChain. The SequencerEngine (core/control/SequencerEngine, task 087)
and its hosted Arpeggiator/StepSequencer/Clock/TriggerSource/ModRouter are NEVER instantiated or
advanced in the audio path — so the 100-step sequencer, the arpeggiator, clock-sync, swing, and the
RANDOM-reload are DEAD CODE. The entire SeqArpRiff preset category (150) stores patterns that never
play. Task 118 wired voices+control+FX but NOT the sequencer (it doesn't even depend on 087); no
task owns this. Wire SequencerEngine into the Engine so the sequencer + arp actually drive the voice.

## Context (verified by audit)

- `core/Engine.h` members today: `VoiceManager voices_`, `ControlCore control_`, `fx::FxChain fx_`.
  No SequencerEngine. `core/Engine.cpp renderChunk` routes MIDI NoteEvents -> voices_.handleNoteEvent,
  then control_.advance(len, voices_), then renders. The sequencer is absent.
- `core/control/SequencerEngine.h` (087, done): the fixed-order state machine hosting
  TriggerSource/Arpeggiator/StepSequencer/Clock/ModRouter; publishes a POD ControlSnapshot;
  has prepare/reset and a processBlock-style tick (READ ITS ACTUAL public surface — the audit noted
  signatures may differ from the design sketch; use the real API, trace to source).
- Design: `docs/design/05 §2.1/§2.2/§2.3/§9.1/§9.2` (the state-machine flow + ControlEvent data flow +
  snapshot model). `docs/design/04 §9` (arp boundary) and §5 (KeyAssigner is the SOLE note authority
  in MONO/UNISON; POLY bypasses). `docs/design/00 §4.1/§4.2`.
- **DESIGN-DOC GAP / routing decision (trace-or-deviate):** docs/design/00 §4.1/§4.2 do NOT specify
  how arp/seq selected-notes reach the voice. The CORRECT seam, per SH-101 behavior + ADR-006
  (KeyAssigner = sole MONO/UNISON note authority): the sequencer/arp OUTPUT drives the SAME single
  mono/unison voice path as a keyboard key — i.e. when the transport is RUNNING, the arp/seq
  selected-note + gate + trigger enter the VoiceManager/KeyAssigner note path (sourced from the
  sequencer instead of live MIDI); when stopped, live keyboard notes flow as today. POLY is out of
  scope for arp/seq note-routing (SH-101 is mono; arp/seq is a mono feature). Record this seam as a
  short note in the task PR + propose a one-paragraph design-doc/ADR addendum (do NOT invent silently).

## Scope

- Add a `seq::SequencerEngine sequencer_` member to Engine; prepare()/reset() it alongside the others.
- In the render/advance path: on each control tick (the SAME clock edge that drives control_), advance
  the SequencerEngine with the transport (BlockContext transport/PPQ/BPM) + the held-key set, and when
  RUNNING route its emitted note/gate/trigger into the VoiceManager note path (through the KeyAssigner
  for MONO/UNISON, per ADR-006) so the voice actually plays the arp/seq. Keep keyboard-direct play when
  the sequencer is stopped. Phase-consistency: one H->L clock edge advances arp + seq + RANDOM together
  (§2.1 C17). RT-safe: no heap/alloc/lock on the audio thread; the SequencerEngine + its handoff are
  already RT-safe (087) — only wire them.
- Expose `Engine::currentSeqStep()` (or surface the live step in the control snapshot) so the processor
  can publish the REAL playhead step to telemetry — this CLOSES the 111c QA MEDIUM (Snapshot.seqStep is
  currently a monotonic display counter, not the live step). (The 1-line processor publish update to use
  it is a tiny follow-up, noted below — keep THIS task core-only.)
- Consume the `<extras>` seq pattern the processor hands off (SeqPatternHandoff, 111c) as the
  StepSequencer's pattern source where the design wires it (the pattern POD already crosses RT-safely).

## Out of scope

- The processor's 1-line telemetry-publish change to read Engine::currentSeqStep() (tiny follow-up 118d).
- The SequencerEngine internals (087); UI (126 SequencerGrid); POLY arp/seq note routing.
- FX / voice DSP (already wired by 118).

## Acceptance criteria

- [ ] Engine owns + prepares/resets a SequencerEngine; on play, the StepSequencer advances through its stored 100-step pattern on clock edges and the selected note+gate+trigger reach the voice (audible/observable in a render) [§2.1; §2.3]
- [ ] The Arpeggiator (Up/Down/Up-Down, hold, range) drives the voice from held keys when ARP is active; one clock H->L edge advances arp + seq + RANDOM on the same edge (phase-consistent) [§2.1 C17]
- [ ] When the transport is stopped, keyboard-direct play is unchanged (no regression to the 118 voice path); MONO/UNISON note selection still flows through the single KeyAssigner [ADR-006]
- [ ] Engine exposes the live current seq step (accessor or snapshot field) for telemetry [closes 111c MEDIUM]
- [ ] RT-safe: an AudioThreadGuard/no-alloc test confirms the wired sequencer adds no heap/lock on the audio path
- [ ] `engine_seq`-tagged tests prove: pattern playback advances + reaches the voice; arp note selection; phase-consistent single-edge advance; stopped-transport keyboard regression-free; deterministic (same seed+pattern -> same output). Full core suite green.
- [ ] PR documents the arp/seq->voice routing seam + proposes the design-doc/ADR addendum for the §4.1/§4.2 gap

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R engine_seq --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
