<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 182
title: Sequencer run-state seam + free-run gate + editor run/hold wiring (ADR-030 part 2)
status: todo
depends-on: [181, 114c, 125, 022]
component: app
estimated-size: M
stream: plugin
tag: seq_runstate
---

## Objective

Make pressing RUN actually play the sequencer in the shipped plugin, end-to-end, including Standalone
(no host transport) per ADR-022's free-run rung. Part 2 of the ADR-030 sequencer/arp integration. This
closes the integration ship-blocker (the editor→processor→engine run path that no test covered).

## Context

- `plan/decisions/030-sequencer-arp-engine-integration.md` (read first) — breaks Q1, Q3, Q4 + the fix scope.
- `plan/decisions/022-*.md` §Decision — the transport ladder: Internal clock FREE-RUNS at the RATE
  knob when no transport is reported (Standalone/stopped/headless); HostSync holds when stopped.
- Task 181 (must be merged first): the seq/arp dispatch + pattern-buffer load — without it a "playing"
  sequencer has no params/pattern.
- `core/Engine.{h,cpp}` — add `setSequencerPlaying`; the `sequencerOwnsIngress` gate (`Engine.cpp:332`
  `transportRunning && (seqPlaying || arpEnabled)`); `transportRunning = ctx.transport.isPlaying`
  (`Engine.cpp:285`). `setGateTrigMode` (`Engine.h:100`) is the lock-free-flag-forward template.
- `core/BlockContext.h` — `TransportInfo` (bpm/ppq/isPlaying); the place to plumb the resolved rung if needed.
- `core/caps/Capabilities.h` / `CapabilityShim.h` — the `TransportRung`/`FreeRun` resolution that is
  currently DISCARDED at `plugin/PluginProcessor.cpp:232 ignoreUnused(caps)`.
- `plugin/PluginProcessor.{h,cpp}` — `processBlock` (`:263` engine_.process; `:222-232` the playhead +
  the discarded caps); add the run-state atomic + seam.
- `plugin/ui/MwAudioEditor.cpp:124-131` — the documented task-114c run/hold stub
  (`lastTransportRunning_ = running;`) to replace; `ui/modules/TransportModeBar.h:92-94` `onRunStateChanged`.

## Scope

RECONCILIATION (per the 181 QA — CRITICAL): task 181's `dispatchSeqArp` is the SINGLE writer of
`setSeqPlay`, mapping `setSeqPlay(seq.mode==Play)` (correct per §6.1/§6.3). Do NOT add a competing
`setSequencerPlaying`/`setSeqPlay` writer — it would fight `dispatchSeqArp` every block (last-writer-
wins). Instead route run/hold into the **`transportRunning` clock/ingress gate**, leaving `setSeqPlay`
to 181. The existing gate `sequencerOwnsIngress = transportRunning && (seqPlaying || arpEnabled)` then
composes: seqPlaying = seq.mode==Play (181); transportRunning = run/hold ∧ free-run-rung (182).

- core/BlockContext.h: add a transient `bool runHeld` (default false) POD field (in `TransportInfo` or
  alongside it) — the run/hold transport state the processor fills each block.
- core/Engine.{h,cpp}: replace `Engine.cpp:285` `transportRunning = ctx.transport.isPlaying` with
  `transportRunning = runHeld && (clockSource==Internal ? true : ctx.transport.isPlaying)` — Internal
  free-runs when RUN is held (ADR-022); HostSync still requires the host playing. Get clockSource from
  the dispatched snapshot (181) or the resolved TransportRung. Do NOT add a setSeqPlay writer.
- plugin/PluginProcessor.{h,cpp}: `std::atomic<bool> transportRunning_{false}` + `setTransportRunning(bool)
  noexcept` (message-thread RELEASE); `processBlock` loads it (ACQUIRE) and fills `ctx`'s `runHeld` each
  block (also stop discarding `caps` if the rung is needed for clockSource). RT-safe: one atomic read.
  Run-state is transient — NOT persisted in <extras>.
- plugin/ui/MwAudioEditor.cpp: `onRunStateChanged` → `processor_.setTransportRunning(running)` (replace
  the stub; the test member may remain).

## Out of scope

- The seq/arp param dispatch + pattern-buffer load (task 181 — depended on, merged first).
- The LFO multi-dest fix (task 180). Preset re-validation (downstream).
- Persisting run-state (it must NOT survive reload).

## TDD — failing tests first (tests/, names begin 'seq_runstate', tag seq_runstate)

THE MISSING PLUGIN-LEVEL INTEGRATION TEST (the coverage whose absence let the dead path ship):
- Construct a real `MwAudioProcessor`; load a ≥2-distinct-pitch seq pattern through the public
  pattern path (setSeqPattern); set **seq.mode=Play** (the play state, via the APVTS param) AND
  `setTransportRunning(true)` (the transport — or simulate the editor's onRunStateChanged(true) via a
  constructed MwAudioEditor); run `processBlock` for several Internal-clock periods with
  `isPlaying == false` (the STANDALONE case); assert the rendered output is the STEPPED pattern (pitch
  changes per step / measured) AND `engine.currentSeqStep()` ADVANCES across steps.
- THE GATE COMPOSES (both conditions required): `setTransportRunning(false)` → NO advance (transport
  off); seq.mode≠Play with RUN held → NO advance (not in play mode). Only Play ∧ run-held advances.
- A HostSync-vs-Internal case: under HostSync with the host stopped (isPlaying=false) + RUN held, the
  seq does NOT advance (the rung distinction holds); under Internal + RUN held it DOES.
- Non-vacuity: each assertion must fail against the pre-fix code (host gate blocks Standalone / run/hold inert).

## Acceptance criteria

- [ ] Editor run/hold → processor.setTransportRunning → the `runHeld` transport gate (NOT a competing setSeqPlay writer — 181 owns setSeqPlay); the 114c stub is replaced [ADR-030 Q1/Q4]
- [ ] The Internal clock free-runs in Standalone/stopped hosts (isPlaying=false) when RUN held, per ADR-022; HostSync still holds when stopped; `caps` is no longer discarded [ADR-030 Q3; ADR-022]
- [ ] The plugin-level seq_runstate integration test proves (seq.mode=Play ∧ RUN held) → stepped output + step-advance end-to-end (Standalone); and the gate composes (transport off → no advance; not-Play → no advance); non-vacuous; RT-safe (atomic, no lock/alloc)
- [ ] Run-state is NOT persisted across getState/setState
- [ ] Full core suite green + full PLUGIN suite green as the merge gate (paste real output)

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default && ctest --preset default --no-tests=error
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R seq_runstate --no-tests=error --output-on-failure
ctest --test-dir build/plugin --no-tests=error
```
