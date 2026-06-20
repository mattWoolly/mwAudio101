<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 111
title: MwAudioProcessor shell: prepare/process/reset + block-split + setLatencySamples (plugin/PluginProcessor.h/.cpp)
status: done
depends-on: [001, 006, 007, 118, 020, 119, 099, 104, 101, 102, 112, 105, 102b]
component: app
estimated-size: M
stream: plugin
tag: processor
---

## Objective

Implement MwAudioProcessor: own Engine/MidiFrontEnd/CapabilityShim/LatencyReporter/ParamBridge, drain each wrapper's native event surface into the NormalizedEventBuffer, assemble BlockContext (split at event offsets), drive Engine::prepare/process/reset, and call setLatencySamples from prepare.

> **MINIMAL-bootstrap status (in-review).** A MINIMAL version was landed by the
> JUCE-phase bootstrap (branch `task/juce-bootstrap`): `plugin/PluginProcessor.{h,cpp}`
> with an `MwAudioProcessor` that owns a `mw::Engine`, drives the three-call seam
> (`prepareToPlay`->`prepare`, `processBlock`->build `BlockContext`+`process`,
> `releaseResources`->`reset`), translates the JUCE `MidiBuffer`+playhead into the core
> POD `mw::MidiEvent`/`BlockContext` seam (allocation-free, pre-sized scratch), exposes a
> single-param APVTS (master gain) + a `GenericAudioProcessorEditor`, and provides
> `createPluginFilter()`. Proven GREEN by building the Standalone. **Deferred to the full
> task 111 / leaf tasks:** the full §3.2 lock-free `NormalizedEventBuffer` + compiled
> no-alloc assertion, the `MidiFrontEnd`/CC-learn map/MPE/note-expression rungs (104), the
> `CapabilityShim` (102), the `LatencyReporter`+`setLatencySamples` PDC (105), the
> `ParamSnapshot` bridge for the full APVTS (020), and ProgramChange preset recall.

## Context

- `docs/design/09 §3.1-3.2` — read first
- `docs/design/00 §4.4` — read first
- `docs/design/00 §5.1` — read first
- `ADR-001 C2-C6` — read first
- `ADR-011 C9` — read first
- `ADR-024 C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `processor`.

## Scope

- prepareToPlay -> Engine::prepare(sr,maxBlock,maxVoices) + size NormalizedEventBuffer + LatencyReporter + setLatencySamples once
- processBlock: drain native param/event/note surface (incl. CLAP typed note-expr Native rung) into NormalizedEventBuffer, translate to mw::core::MidiEvent, build BlockContext, call Engine::process; releaseResources/reset
- ProgramChange consumed for preset recall; no DSP fork across formats
- Compiled-in no-allocation assertion around processBlock (debug/CI)

## Out of scope

- Engine DSP (full-engine)
- MidiFrontEnd/translator/bridge internals (plugin-7/8/10)
- Format target wiring (plugin-14)
- State save/load serialization (state-presets)

## Acceptance criteria

- [ ] Engine seam driven exactly as prepare(double,int,int)/process(const BlockContext&)/reset (§5.1; ADR-001 C2-C5)
- [ ] Native event surface drains into the pre-sized lock-free buffer; processBlock performs zero heap alloc and no lock (tag 'processor', AudioThreadGuard) [§3.2; ADR-011 C9; ADR-024 C7]
- [ ] setLatencySamples is called only from prepare with the LatencyReporter constant and never mutated from processBlock (§3.1; ADR-017 L10)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R processor --no-tests=error
```
