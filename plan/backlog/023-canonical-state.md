<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 023
title: Canonical state (de)serializer (StateSerializer.h/.cpp)
status: todo
depends-on: [016, 017, 007]
component: core
estimated-size: M
stream: params
tag: serializer
---

## Objective

Implement the single canonical serializer: captureState builds the MW101_STATE tree from APVTS+Extras, writeToBlob emits the host blob, readFromBlob parses back, with full <extras>/<seq> round-trip.

## Context

- `docs/design/06-parameters-state-presets.md §5.1` — read first
- `§5.2` — read first
- `§5.4` — read first
- `§5.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `serializer`.

## Scope

- core/state/StateSerializer.h/.cpp implementing captureState/writeToBlob/readFromBlob per the §5.2 signatures
- captureState writes root attributes (schemaVersion/pluginVersion/engineVersion/renderVersion), <PARAMS> from APVTS state, and <extras> incl. <seq> per-step note/gate/tie/rest (no accent), arpLatch, driftSeed, seedLocked (§5.1, §5.4, §5.5)
- <seq> serialization writes stepCount + one <step> per active step (§5.5); readFromBlob returns nullopt on structural parse failure (§5.2)
- All on the message thread; no audio-thread work (§5.2)
- Round-trip test: capture->write->read reproduces every param value and the full Extras (100-step note/rest/tie/gate seq, drift seed+lock, CC-learn) bit-for-bit (Acceptance hooks round-trip)

## Out of scope

- migration (params-9)
- load-failure recovery / clamping (params-10)
- JSON preset projection (params-11)

## Acceptance criteria

- [ ] captureState->writeToBlob->readFromBlob round-trips every param and full <extras> (incl. 100-step note/rest/tie/gate sequence, drift seed+lock, CC-learn) bit-for-bit [§5; Acceptance hooks round-trip]
- [ ] readFromBlob returns nullopt on structural-parse failure [§5.2]
- [ ] <seq> carries note/gate/tie/rest only — no accent attribute [§5.5; ADR-025]
- [ ] Test names begin with serializer and assert round-trip + structural-failure nullopt [§5; Acceptance hooks]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R serializer --no-tests=error
```
