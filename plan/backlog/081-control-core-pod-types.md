<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 081
title: Control-core POD types: events, enums, ModInputs/Outputs, ControlSnapshot
status: todo
depends-on: [001, 006, 007]
component: core
estimated-size: S
stream: mod-arp-seq
tag: modseqtypes
---

## Objective

Define the shared POD header for the control core: the stream's enums (PwmSource, VcaSource, TrigMode, NotePriority, ArpMode, ClockSource, HostRate), the SeqStep/SeqBuffer storage layout, the KeyEvent/ControlEvent block-boundary events, and the immutable ControlSnapshot. No logic, only trivially-copyable types.

## Context

- `docs/design/05 §3.2` — read first
- `docs/design/05 §4.2` — read first
- `docs/design/05 §5.4` — read first
- `docs/design/05 §6.2` — read first
- `docs/design/05 §7.7` — read first
- `docs/design/05 §9.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `modseqtypes`.

## Scope

- core/control/ControlTypes.h with all stream enums (uint8_t-backed) per §3.2/§4.2/§5.4/§7.7
- SeqStep POD with kPitchMask/kRestFlag/kTieFlag constants + isRest/isTie/pitch accessors; SeqBuffer = std::array<SeqStep,kMaxSteps>, kMaxSteps=100 (§6.2)
- KeyEvent and ControlEvent PODs (pitch/gate/trig/porta/mod fields + sampleOffset) used by SequencerEngine::processBlock (§2.3, §9.2)
- ModInputs/ModOutputs/ModDepths and TriggerDecision/KeyState/SeqPlayResult/ClockEdge structs (§3.2,§4.2,§6.5,§7.7)
- ControlSnapshot immutable struct with schemaVersion=1 (§9.2)
- static_assert trivially-copyable on all PODs

## Out of scope

- Any algorithm/class implementation (later tasks)
- Parameter IDs/ranges/skews (owned by param-schema)
- TransportInfo / MidiEvent (consumed from core-types)

## Acceptance criteria

- [ ] modseqtypes test asserts SeqStep is 1 byte and pitch/REST/TIE bit decode matches §6.2 masks
- [ ] modseqtypes test asserts kMaxSteps==100 and SeqBuffer size (§6.2)
- [ ] modseqtypes test asserts all enums are uint8_t and have the §3.2/§4.2/§5.4/§7.7 enumerators in the stated order
- [ ] modseqtypes test asserts ControlSnapshot.schemaVersion default==1 and struct is trivially copyable (§9.2)
- [ ] ctest --preset default -R modseqtypes --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R modseqtypes --no-tests=error
```
