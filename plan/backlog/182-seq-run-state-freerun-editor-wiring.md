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

- core/Engine.{h,cpp}: `void setSequencerPlaying(bool) noexcept` → `sequencer_.setSeqPlay`. Lock-free flag.
- Free-run gate: the Internal clock advances regardless of host `isPlaying` (honor the resolved
  TransportRung/FreeRun; stop discarding `caps`). Plumb the rung into BlockContext/Engine OR gate on
  `clockSource==Internal ? always : transportRunning`. HostSync must still hold when the host is stopped.
- plugin/PluginProcessor.{h,cpp}: `std::atomic<bool> transportRunning_{false}` + `setTransportRunning(bool)
  noexcept` (message-thread RELEASE); `processBlock` loads it (ACQUIRE) and calls
  `engine_.setSequencerPlaying(running)` before process(). RT-safe: one atomic read + one flag write.
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
  pattern path (setSeqPattern); set seq.mode to a playing mode; `setTransportRunning(true)` (or
  simulate the editor's onRunStateChanged(true) via a constructed MwAudioEditor); run `processBlock`
  for several Internal-clock periods with `isPlaying == false` (the STANDALONE case); assert the
  rendered output is the STEPPED pattern (pitch changes per step / measured) AND `engine.currentSeqStep()`
  ADVANCES across steps. Then `setTransportRunning(false)` and assert NO further advance.
- A HostSync-vs-Internal case: under HostSync with the host stopped (isPlaying=false), assert the seq
  does NOT advance (the rung distinction holds); under Internal it DOES.
- Non-vacuity: each assertion must fail against the pre-fix code (run-state never set / host gate blocks Standalone).

## Acceptance criteria

- [ ] Editor run/hold → processor.setTransportRunning → engine.setSequencerPlaying drives the real sequencer; the 114c stub is replaced [ADR-030 Q1/Q4]
- [ ] The Internal clock free-runs in Standalone/stopped hosts (isPlaying=false) per ADR-022; HostSync still holds when stopped; `caps` is no longer discarded [ADR-030 Q3; ADR-022]
- [ ] The plugin-level seq_runstate integration test proves press-RUN→stepped-output→step-advances end-to-end (Standalone), and run-off → no advance; non-vacuous; RT-safe (atomic, no lock/alloc)
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
