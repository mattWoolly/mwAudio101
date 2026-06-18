<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 017
title: State-tree identifiers + Extras POD payload (StateTree.h, Extras.h)
status: done
depends-on: [001, 006]
component: core
estimated-size: S
stream: params
tag: statetree
---

## Objective

Define the canonical root-tree identifiers/attribute keys and the trivially-copyable Extras POD (fixed-capacity 100-step sequence, arp latch, drift seed/lock) handed to the audio thread.

## Context

- `docs/design/06-parameters-state-presets.md §5.1` — read first
- `§5.4` — read first
- `§5.5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `statetree`.

## Scope

- core/state/StateTree.h: root id MW101_STATE, attribute keys schemaVersion/pluginVersion/engineVersion/renderVersion, child ids PARAMS, extras, seq, ccLearn, step, and extras attribute keys per §5.1/§5.4
- core/state/Extras.h: SeqStep POD (noteSemitone, gate, tie, rest — NO accent), kMaxSeqSteps=100, Extras struct (steps array, stepCount, arpLatch, driftSeed, seedLocked) trivially copyable, no heap (§5.4)
- Static-assert Extras and SeqStep are trivially copyable and contain no accent field (§5.4; ADR-025)
- Test asserting kMaxSeqSteps==100, trivial-copyability, and absence of an accent member

## Out of scope

- (de)serialization of the tree/extras (params-7)
- the SPSC double-buffer mechanism (consumed from plugin-processor)
- CC-learn binding semantics (midi-frontend)

## Acceptance criteria

- [ ] Root tree shape ids/attribute keys match §5.1 (schemaVersion, pluginVersion, engineVersion, renderVersion, PARAMS, extras) [§5.1]
- [ ] Extras/SeqStep are trivially copyable POD with kMaxSeqSteps==100 and NO accent field [§5.4; ADR-025]
- [ ] Test names begin with statetree and assert trivial-copyability, capacity, and accent-absence [§5.4; Acceptance hooks accent]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R statetree --no-tests=error
```
