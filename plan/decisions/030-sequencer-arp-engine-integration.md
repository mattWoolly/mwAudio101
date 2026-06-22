<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR-030 — Sequencer/arp engine integration (dispatch + run-state + free-run)

- Status: accepted
- Date: 2026-06-21
- Deciders: orchestrator (integration-defect correction; conforms to ADR-007/022/028)
- Realized by: tasks 181, 182

## Context

A dedicated integration investigation (2026-06-21) confirmed a **ship-blocker**: the entire
sequencer **and** arpeggiator subsystem is disconnected from the live Engine control path in the
shipped plugin. `SequencerEngine`/`StepSequencer` were built and unit-tested in isolation (107-era)
but never wired into `Engine::process`. Four independent breaks, all evidence-cited:

1. **Run state never set (Q1).** `StepSequencer::playing_` is set only via `setPlay`←`SequencerEngine::setSeqPlay`, whose only callers are in `tests/`. `Engine` exposes no mutating sequencer seam (only `const sequencer()`); `PluginProcessor` calls just `prepare/reset/process`; `BlockContext` has no seq field. `seqPlaying` (`Engine.cpp:329`) is therefore always false.
2. **Params + pattern never reach the engine (Q2).** Nothing in production calls `publishSnapshot`/`restoreState`, so the `SequencerEngine` runs on INIT-default `ControlSnapshot` forever — `arpHold`=false, `arpMode`=Up. `Engine::applyParamSnapshot` never reads the `seq.*`/`arp.*` APVTS params (they are attached to UI combos in `TransportModeBar` but consumed by zero production code). The SequencerGrid's edited pattern is adopted into `lastAdoptedSeq_` (`PluginProcessor.cpp:271`) but read only by `adoptedSeqPatternForTest()` — it never loads into the engine's `StepSequencer`, so `count_==0` and `advanceOnEdge` no-ops even if playing.
3. **Host-transport gate blocks Standalone, against ADR-022 (Q3).** `sequencerOwnsIngress` (`Engine.cpp:332`) requires `ctx.transport.isPlaying`. ADR-022 §Decision mandates the **Internal** clock (the default) free-runs at the RATE knob when no transport is reported (Standalone/stopped/headless). The resolved `TransportRung`/`FreeRun` capability is computed (`CapabilityShim`) then discarded (`PluginProcessor.cpp:232 ignoreUnused(caps)`). So even with run-state set, Standalone (`isPlaying==false`) could never advance.
4. **Editor run/hold dead-ends (Q4).** Run/Hold is deliberately non-APVTS (design 10 §5.3); the bar reports via `onRunStateChanged`, but `MwAudioEditor.cpp:131` (the documented task-114c stub) just stores `lastTransportRunning_` (read only by a test accessor). No processor seam exists.

There is no APVTS/automation escape hatch: `seq.mode`/`arp.mode` reach nothing. The headline 100-step
sequencer and the arpeggiator are decorative end-to-end. The missing coverage that let this ship: the
only sequencer tests drive the engine-internal path via a `const_cast` back door
(`TelemetryDatapathsTest.cpp:78`); no test exercised the editor→processor→engine wiring.

## Decision

Wire the subsystem end-to-end, RT-safe, conforming to ADR-007 (sequencer/arp behavior), ADR-022
(transport ladder/free-run) and ADR-028 (the control-dispatch seam). Two tasks:

**Task 181 — seq/arp control-dispatch + pattern-buffer load (core; extends ADR-028 to the seq/arp params).**
- `Engine::applyParamSnapshot` (or a sibling control-tick path) reads the `seq.*`/`arp.*` APVTS params
  and builds + publishes a `SequencerEngine` `ControlSnapshot` each control tick, so `seq.mode`,
  `seq.tempo_sync`, `seq.sync_div`, `arp.mode`, `arp.range`, `arp.tempo_sync`, `arp.sync_div`,
  `arp.latch` actually drive the SequencerEngine/Arp.
- The adopted seq pattern buffer (the processor's `SeqPatternHandoff`/`Extras`) is loaded into the
  engine's `StepSequencer` (a new Engine seam, e.g. `loadSeqPattern`/snapshot conversion), so edits
  from SequencerGrid (126) and preset patterns reach the engine and `count_>0`.
- Core test: after dispatching `seq.mode≠off` + a loaded pattern, the engine's sequencer reflects them
  (mode, count) and the arp mode/latch reach `arp_`; non-vacuous.

**Task 182 — run-state TRANSPORT gate + free-run + editor wiring (core + plugin; the end-to-end integration).**

RECONCILIATION (per the 181 QA): task 181's `dispatchSeqArp` is the SINGLE writer of
`StepSequencer::setSeqPlay`, and it correctly maps `setSeqPlay(seq.mode==Play)` (seq.mode IS the
persisted play/record state, design 05 §6.1/§6.3). Run/Hold is a DISTINCT transient transport (design
10 §5.3), so 182 must NOT add a competing `setSequencerPlaying`/`setSeqPlay` writer (it would clobber,
or be clobbered by, `dispatchSeqArp` every block — last-writer-wins). Instead 182 routes run/hold into
the **`transportRunning` clock/ingress gate**, leaving `setSeqPlay` solely to 181's seq.mode dispatch.
The existing gate `sequencerOwnsIngress = transportRunning && (seqPlaying || arpEnabled)`
(`Engine.cpp:332`) then composes the two cleanly: `seqPlaying` = seq.mode==Play (181); `transportRunning`
= run/hold ∧ free-run-rung (182). The sequencer advances iff BOTH (in Play mode AND transport running).

- **No `setSequencerPlaying` seam.** Instead add a `runHeld` (transient, default false) input the Engine
  reads in the gate: plumb it via `BlockContext`/`TransportInfo` (a new POD field the processor fills
  each block) — consistent with the existing POD seam.
- Replace `Engine.cpp:285` `transportRunning = ctx.transport.isPlaying` with
  `transportRunning = runHeld && (clockSource==Internal ? true : ctx.transport.isPlaying)` (Internal
  free-runs when RUN is held, per ADR-022; HostSync still requires the host to be playing). Get the
  clock source from the dispatched snapshot (181) or the resolved `TransportRung` (stop discarding `caps`).
- Processor: `std::atomic<bool> transportRunning_{false}` + `setTransportRunning(bool) noexcept`
  (message-thread RELEASE store); `processBlock` loads it (ACQUIRE) and fills `ctx`'s `runHeld` each
  block. RT-safe (one atomic read). Run-state is transient — NOT persisted in `<extras>`.
- `MwAudioEditor` `onRunStateChanged` → `processor_.setTransportRunning(running)` (replace the 114c stub).
- **The missing plugin-level proving test:** drive `MwAudioProcessor` with a loaded ≥2-distinct-pitch
  pattern, **seq.mode=Play** (the play state) AND `setTransportRunning(true)` (the transport), run
  `processBlock` for several Internal-clock periods with `isPlaying==false` (the Standalone case), and
  assert the output is the stepped pattern and `currentSeqStep()` advances. Then prove the gate composes:
  `setTransportRunning(false)` → no advance (transport off); and seq.mode≠Play with RUN held → no advance
  (not in play mode). This is the editor→processor→engine path the const_cast back door never covered.

## Consequences

- Revives the headline sequencer + arpeggiator end-to-end; closes the integration ship-blocker.
- Adds the plugin-level integration coverage whose absence let the dead path ship.
- 181 is a core change (full core-suite gate); 182 is core+plugin (full core + full plugin gates).
  Sequenced 181→182 (182 depends on the dispatch + buffer load). Both serialize on `core/Engine.cpp`.
- Preset patterns now actually play — preset re-validation (downstream) gains real audio behavior to check.
