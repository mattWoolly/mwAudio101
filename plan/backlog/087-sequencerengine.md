<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 087
title: SequencerEngine: fixed-order tick + RT-safe snapshot swap
status: todo
depends-on: [001, 006, 007, 081, 082, 083, 084, 085, 086]
component: core
estimated-size: M
stream: mod-arp-seq
tag: seqengine
---

## Objective

Implement mw::seq::SequencerEngine: the fixed-order state machine that hosts TriggerSource/Arpeggiator/StepSequencer/Clock/ModRouter, advancing all three (arp, seq, RANDOM reload) on the single clock edge, and publishing an atomically-swapped immutable ControlSnapshot.

## Context

- `docs/design/05 §2.1` — read first
- `docs/design/05 §2.2` — read first
- `docs/design/05 §2.3` — read first
- `docs/design/05 §9.1` — read first
- `docs/design/05 §9.2` — read first
- `docs/design/05 §10` — read first
- `docs/design/05 §11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `seqengine`.

## Scope

- core/control/SequencerEngine.h/.cpp per §9.2 signature; prepare/processBlock/captureState/restoreState/publishSnapshot
- processBlock runs the §2.1 fixed-order tick (Keyboard Read->Clock Check->arp/seq advance->CV/Gate output) on the control tick (kControlTickSeconds, §2.2) while consuming Clock edges sample-accurately
- One edge advances arp cursor + seq slot + RANDOM reload together, phase-consistent across all sources (§2.1 / C17)
- Double-buffered ControlSnapshot with atomic acquire/release swap; captureState/restoreState off audio thread; INIT-default fallback on bad snapshot (§9.1,§9.2,§9.3)

## Out of scope

- juce::ValueTree (de)serialization (plugin layer maps POD snapshot, owned by state-presets)
- Reading host AudioPlayHead (plugin fills TransportInfo)
- RANDOM LFO value reload generation (consumed; engine fires the edge signal)

## Acceptance criteria

- [ ] seqengine test: a single H->L edge advances arp cursor, seq slot, and RANDOM reload on the same edge, phase-consistent across Internal/HostSync/Ext (§2.1 / C17)
- [ ] seqengine test: save->reload reproduces full 100-slot buffer + arp + clock + trigger/PWM/VCA state, schemaVersion==1 written (§9.1 / C25)
- [ ] seqengine test: no heap alloc and no lock during processBlock and during a snapshot swap, verified by alloc/lock sentinel (§10 / C26)
- [ ] seqengine test: clock edges land at expected sub-block sample offsets regardless of control-tick period; tick defaults to ~2ms vintage rate (§2.2 / C27)
- [ ] ctest --preset default -R seqengine --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R seqengine --no-tests=error
```
