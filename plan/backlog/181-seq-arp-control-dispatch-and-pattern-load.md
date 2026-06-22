<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 181
title: Seq/arp control-dispatch + pattern-buffer load into the engine (ADR-030 part 1)
status: done
depends-on: [154, 160, 161, 162, 165]
component: dsp
estimated-size: M
stream: dsp
tag: dispatch_seqarp
---

## Objective

Wire the seq/arp APVTS params and the edited/preset pattern buffer INTO the live Engine control path
so the SequencerEngine/Arp stop running on INIT defaults with an empty buffer. Part 1 of the ADR-030
sequencer/arp integration (the dead-subsystem ship-blocker).

## Context

- `plan/decisions/030-sequencer-arp-engine-integration.md` (read first) — esp. break Q2.
- `docs/QA-REPORT.md` (the integration finding) and the 2026-06-21 investigation.
- `plan/decisions/028-control-dispatch-seam.md` — the existing ParamSnapshot→DSP dispatch this extends.
- `core/Engine.cpp` — `applyParamSnapshot` (the control-tick dispatch; today it never reads seq/arp
  params) + how `sequencer_` (SequencerEngine) is owned; `Engine.cpp:329-332` (the seqPlaying/arpEnabled gate).
- `core/control/SequencerEngine.{h,cpp}` — `applySnapshot`, `publishSnapshot`/`ControlSnapshot`,
  `arp()`/`setMode`, `seq()`; `core/control/ControlTypes.h` — `ControlSnapshot` fields (arpMode, arpHold, seq config).
- `core/control/StepSequencer.{h,cpp}` — `loadBuffer`/`buffer()`/`count()` (the pattern buffer API).
- `plugin/PluginProcessor.cpp` — `seqPatternHandoff_.adopt()`→`lastAdoptedSeq_` (today read only by
  `adoptedSeqPatternForTest()`); `core/state/Extras.h` (the seq buffer POD).
- `core/params/ParamDefs.h` / `core/params/ParamIDs.h` — kSeqMode, kSeqTempoSync, kSeqSyncDiv,
  kArpMode, kArpRange, kArpTempoSync, kArpSyncDiv, kArpLatch.

## Scope

- Extend the control-tick dispatch (Engine::applyParamSnapshot or a sibling) to read the seq.*/arp.*
  params and build + publish a SequencerEngine ControlSnapshot each tick — so arp.mode/range/latch/
  tempo_sync/sync_div and seq.mode/tempo_sync/sync_div actually drive the SequencerEngine + Arp.
- Add an Engine seam to load the adopted seq pattern buffer (Extras/SeqPatternHandoff → StepSequencer)
  so SequencerGrid (126) edits + preset patterns reach the engine (count_>0). RT-safe (the buffer
  handoff is already lock-free SPSC; the engine-side adopt is a POD copy on the audio thread — no lock/alloc).
- Wire the processor's adopted pattern (lastAdoptedSeq_) into that engine seam (replace the
  test-only dead end) — minimal, RT-safe.

## Out of scope

- Run-state (setSequencerPlaying) + the editor run/hold wiring + the free-run gate — that is task 182.
- The LFO multi-dest fix (task 180).
- Changing param decls or the 91-count.

## TDD — failing test first (tests/, names begin 'dispatch_seqarp', tag dispatch_seqarp)

- Core test: dispatch a snapshot with seq.mode set + arp.mode/latch set through the REAL
  applyParamSnapshot path; assert the engine's SequencerEngine snapshot reflects them (arp mode/hold,
  seq mode) — failing against the current no-dispatch code (non-vacuous).
- Pattern load: feed a ≥2-step pattern through the processor/engine load seam; assert the engine's
  StepSequencer count() and buffer() match (was 0). Prove the SequencerGrid→engine path is live.

## Acceptance criteria

- [ ] seq.*/arp.* params drive the SequencerEngine/Arp through the live control-tick dispatch (not INIT defaults) [ADR-030 Q2; ADR-028]
- [ ] The adopted seq pattern buffer loads into the engine's StepSequencer (count_>0); SequencerGrid/preset patterns reach the engine, RT-safe (no lock/alloc on the audio thread)
- [ ] dispatch_seqarp tests prove both, non-vacuously (fail against the pre-fix no-dispatch/no-load code)
- [ ] Full core suite green; verify 'ctest --preset default -R dispatch_seqarp --no-tests=error' + full suite as the merge gate. New core tag dispatch_seqarp -> labels_snapshot regen on merge.

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
ctest --preset default -R dispatch_seqarp --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
