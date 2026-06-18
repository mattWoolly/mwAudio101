<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 136
title: Wire MwAudioProcessor: engine + frontend + capability shim + latency reporter
status: todo
depends-on: [111, 104, 113, 020, 118]
component: app
estimated-size: M
stream: integration
tag: processor_wire
---

## Objective

Implement the MwAudioProcessor assembly that owns the Engine, MidiFrontEnd, CapabilityShim, NormalizedEventBuffer, and LatencyReporter, marshals host state into BlockContext in processBlock, and declares constant latency via setLatencySamples from prepareToPlay.

## Context

- `docs/design/09 §3.1` — read first
- `docs/design/00 §5.2` — read first
- `docs/design/00 §7.1` — read first
- `docs/design/09 §8.3` — read first
- `docs/design/09 §3.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `processor_wire`.

## Scope

- plugin/PluginProcessor.h/.cpp: hold consumed components; prepareToPlay sizes buffers + calls Engine::prepare + LatencyReporter + setLatencySamples
- processBlock: drain into NormalizedEventBuffer, build BlockContext, call Engine::process; no JUCE type crosses the seam
- Resolve capability rungs at prepareToPlay and per-block recheck via CapabilityShim
- Report the single constant worst-case latency from plugin/ (never from processBlock)

## Out of scope

- MidiFrontEnd/CapabilityShim/LatencyReporter/CcLearnMap internals (midi-frontend/format-wrappers)
- APVTS parameter tree definition (param-schema)
- State/preset (de)serialization (state-presets)
- Format wrapper targets (integration-7/format-wrappers)

## Acceptance criteria

- [ ] No juce::AudioBuffer/MidiBuffer/APVTS crosses the core seam per §5.2; BlockContext built per §5.3
- [ ] setLatencySamples is declared from prepareToPlay and never mutated from processBlock per §7.1/§8.3
- [ ] Wrapper events drain into the pre-sized lock-free NormalizedEventBuffer with no audio-thread alloc per §3.2
- [ ] ctest --preset default -R processor_wire --no-tests=error is green; test names begin with processor_wire

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R processor_wire --no-tests=error
```
