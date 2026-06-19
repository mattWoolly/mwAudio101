<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 103
title: MPE-over-MIDI reconstruction parser (plugin/midi/MpeReconstructor.h/.cpp)
status: in-review
depends-on: [001, 006, 020, 073]
component: app
estimated-size: M
stream: plugin
tag: mpereconstruct
---

## Objective

Implement the per-channel rotation parser that reconstructs raw per-channel MIDI (per-note bend, channel pressure/CC74) into the same per-voice pre-Q pitch offset + pressure the Native rung produces; lower-zone only, members 1..15.

## Context

- `docs/design/09 §7.3` — read first
- `docs/design/09 §7.1` — read first
- `ADR-022 C2` — read first
- `ADR-012 C10-C13` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `mpereconstruct`.

## Scope

- prepare(maxVoices) sizes channelToVoice_ state; reset
- noteOn/noteOff manage channel<->voice rotation (master channel 1, members 2..16, configurable count)
- pitchBend(channel,semis) -> per-voice pre-Q offset; pressure(channel,norm) -> assignable destination
- Mono / MPE-OFF collapse to channel bend + channel pressure (§7.1; ADR-012 C13)

## Out of scope

- Rung resolution / selecting this parser (plugin-11)
- Native CLAP note-expression mapping (plugin-13)
- Assignable-destination parameter definition (param-schema)

## Acceptance criteria

- [ ] Member channels default OFF (0), opt-in 1..15, lower zone only (§7.1; ADR-012 C10)
- [ ] Per-note pitch -> per-voice pre-Q offset and pressure -> one assignable destination (default VCF cutoff CV); no other per-note routing (§7.1; ADR-012 C11-C12)
- [ ] prepare-then-parse over random channel sequences performs zero alloc/lock (tag 'mpereconstruct', AudioThreadGuard) [§7.3; ADR-022 C11]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R mpereconstruct --no-tests=error
```
