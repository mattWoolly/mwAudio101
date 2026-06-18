<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 024
title: Load-failure recovery ladder (LoadFailure.h/.cpp)
status: todo
depends-on: [019, 023, 021, 022, 007]
component: core
estimated-size: M
stream: params
tag: loadfail
---

## Objective

Implement recoverState: a never-throwing graded fallback that parses, migrates, clamps/defaults, retains newer raw blobs, pads/clamps the sequence, and returns a complete valid canonical tree plus a coalesced RecoveryReport.

## Context

- `docs/design/06-parameters-state-presets.md §8.1` — read first
- `§8.2` — read first
- `§8.3` — read first
- `§5.3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `loadfail`.

## Scope

- core/state/LoadFailure.h/.cpp: RecoveryOutcome enum, RecoveryReport struct, recoverState() per §8.2 signature
- Ladder per §8.3: L1 unparseable->INIT+empty extras; L2 schema<=CURRENT->migrate+default missing/clamp; L3 schema>CURRENT->bind known, default rest, retain rawNewerBlob; L4 clamp continuous / reset invalid choice; L5 prefer partial over INIT; L8 pad/clamp seq to 100; L6 raw round-trip preservation
- Never throws; assembles a complete valid tree fully on the message thread; coalesces deviations into one RecoveryReport note list (§8.1; §8.3 L12)
- Uses migrateToCurrent (params-9), the serializer parse (params-7), kParamDefs ranges/choice counts (params-3), and INIT (params-8) as last resort
- TDD fixtures for L1-L8/L4/L6: no throw/crash, correct fallback target, raw preserved on L6, seq padded/clamped without allocation on L8

## Out of scope

- preset-file (JSON) recovery L9-L11 (params-11/params-13)
- the actual UI warning affordance (ui-skeleton)
- SPSC handoff to the audio thread (plugin-processor)

## Acceptance criteria

- [ ] recoverState never throws and always returns a complete valid canonical tree + report for any input [§8.1; §8.2]
- [ ] L1-L8 behave per the §8.3 table: INIT on unparseable, clamp/default in range, schema>CURRENT retains rawNewerBlob and binds known IDs, seq padded/clamped to 100 without allocation [§8.3]
- [ ] Deviations coalesce into one RecoveryReport note list [§8.3 L12]
- [ ] Test names begin with loadfail and provide a fixture per L1-L8 asserting no-crash + correct fallback + raw preservation (L6) [§8.3 L14]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R loadfail --no-tests=error
```
