<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118
title: Wire all engine modules into Engine::prepare/process/reset assembly
status: todo
depends-on: [006, 073, 075, 071, 091, 092, 093, 006]
component: engine
estimated-size: M
stream: integration
tag: engine_assembly
---

## Objective

Implement core/Engine.h/.cpp that wires the voice loop, voice manager, control core, and post-voice FX chain behind the prepare/process/reset seam, walking active voices in fixed index order and running the shared FX once on the mono sum.

## Context

- `docs/design/00 §4.1` — read first
- `docs/design/00 §5.1` — read first
- `docs/design/00 §5.5` — read first
- `docs/design/00 §6.1` — read first
- `docs/design/00 §4.4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `engine_assembly`.

## Scope

- core/Engine.h/.cpp: prepare(double,int,int) noexcept sizes pool/scratch via consumed modules; process(const BlockContext&) noexcept; reset() noexcept
- Sub-block chunking at MIDI/event offsets, fixed kRenderBlock-capped chunks (§4.4)
- Single-threaded voice loop, fixed voice-index accumulation into mono mix (§6.1)
- FTZ/DAZ set at process entry; touches only pre-sized member storage
- Run post-voice FX chain (fx-drive/chorus/delay) once on the mono sum after voice accumulation

## Out of scope

- DSP internals of any module (owned by their streams)
- JUCE/plugin marshalling and setLatencySamples (integration-6/plugin-processor)
- Golden bless capture (golden-harness)

## Acceptance criteria

- [ ] Engine exposes exactly prepare(double,int,int) noexcept / process(const BlockContext&) noexcept / reset() noexcept per §5.1
- [ ] Voices sum in fixed voice-index order; voice loop uses no synchronization primitive per §6.1
- [ ] Events at sample offsets apply sample-accurately; segments render in fixed kRenderBlock-capped chunks per §4.4
- [ ] ctest --preset default -R engine_assembly --no-tests=error is green; test names begin with engine_assembly

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R engine_assembly --no-tests=error
```
