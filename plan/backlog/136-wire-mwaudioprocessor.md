<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 136
title: Finalize MwAudioProcessor — MIDI ProgramChange→preset recall handoff + full-assembly integration test
status: done
depends-on: [111, 104, 113, 020, 118, 119]
component: app
estimated-size: S
stream: integration
tag: processor_wire
---

## Objective

> **RE-SCOPED (2026-06-19).** Task 111 already implemented the full MwAudioProcessor assembly
> (owns Engine/MidiFrontEnd/CapabilityShim/NormalizedEventBuffer/LatencyReporter/ParamBridge,
> drains into the lock-free buffer, builds BlockContext, drives Engine::prepare/process/reset,
> and declares constant latency via setLatencySamples from prepareToPlay) and QA verified all of
> it GREEN. This task is therefore re-scoped to the RESIDUAL not closed by 111:
>
> 1. **Close the 111 QA MEDIUM** — the MIDI ProgramChange→preset-recall handoff is only half-wired:
>    processBlock captures the PC into the atomic `pendingProgramChange_`, but nothing reads that
>    atomic on the message thread, so a PC *in the MIDI stream* never recalls a preset (only a
>    host-driven `setCurrentProgram` does). Wire the message-thread side (e.g. an AsyncUpdater
>    triggered from processBlock that reads the atomic and calls `setCurrentProgram` →
>    `PresetManager::loadPreset`), keeping the audio thread POD-only (no preset parse on the
>    audio thread).
> 2. **Add the full-assembly `processor_wire` integration test** the original 136 wanted, now
>    against the real assembled processor: a host-style drive (prepare → busy processBlock →
>    release) asserting the seam invariants end-to-end.

Originally: implement the MwAudioProcessor assembly (engine + frontend + capability shim +
latency reporter). That wiring now lives in 111; do NOT re-implement it — only add the residual
above. Editing plugin/PluginProcessor.{h,cpp} for the ProgramChange handoff is expected.

## Context

- `docs/design/09 §3.1` — read first
- `docs/design/00 §5.2` — read first
- `docs/design/00 §7.1` — read first
- `docs/design/09 §8.3` — read first
- `docs/design/09 §3.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `processor_wire`.

## Scope

- plugin/PluginProcessor.{h,cpp}: wire the MIDI ProgramChange → preset-recall MESSAGE-THREAD handoff.
  processBlock already captures the PC into the atomic `pendingProgramChange_` (POD, audio-thread
  safe) — add the consumer: trigger a juce::AsyncUpdater from processBlock when a new PC arrives;
  in handleAsyncUpdate() (message thread) read+clear the atomic and call `setCurrentProgram(index)`
  (which already routes to PresetManager::loadPreset). Clamp the index to the bank size; ignore an
  out-of-range PC. NO preset parse / NO heap / NO lock added to the processBlock path.
- tests/plugin/ (tag `processor_wire`): the full-assembly integration test — construct the real
  MwAudioProcessor, prepare → drive several busy processBlocks (notes + a ProgramChange) → release;
  assert (a) the seam invariants (no JUCE type crosses into core; setLatencySamples only from
  prepare), and (b) a MIDI ProgramChange in the buffer results in the corresponding preset being
  recalled after the message-thread tick (drive the AsyncUpdater synchronously in the test).

## Out of scope

- The 111 wiring already done (Engine/MidiFrontEnd/CapabilityShim/NormalizedEventBuffer/Latency/
  ParamBridge) — do NOT re-implement it.
- MidiFrontEnd/CapabilityShim/LatencyReporter/CcLearnMap internals; APVTS tree (020); state/preset
  (de)serialization internals (024/025); format wrapper targets (113/137).

## Acceptance criteria

- [ ] A MIDI ProgramChange in the processBlock buffer recalls the matching preset via the message-thread AsyncUpdater→setCurrentProgram→PresetManager path; an out-of-range PC index is ignored (closes the 111 QA MEDIUM) [§5.2; docs/design/06 §10]
- [ ] No heap alloc / no lock / no preset parse is added to the processBlock path; the audio thread stays POD-only (re-assert the no-alloc probe over a PC-bearing block) [§3.2; ADR-001 C3/C4]
- [ ] setLatencySamples remains prepare-only; no juce type crosses the core seam [§7.1/§8.3; §5.2]
- [ ] processor_wire tests pass; names begin processor_wire; built under MW_BUILD_PLUGIN=ON (JUCE — the default-preset block below is stale, mirrors 111)

## Verification commands

```
# JUCE-forced (PluginProcessor + JUCE-linked test); the default-preset block is stale (see 111).
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R processor_wire --no-tests=error
```
