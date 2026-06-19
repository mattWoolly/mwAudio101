<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 102
title: APVTS <-> ParamSnapshot marshalling (plugin/ParamBridge.h/.cpp)
status: in-review
depends-on: [001, 006, 007, 020]
component: app
estimated-size: M
stream: plugin
tag: parambridge
---

## Objective

Implement ParamBridge: read APVTS atomics once per block into an immutable normalized mw::ParamSnapshot, with no JUCE type crossing the seam and no atomic reads in tight loops.

## Context

- `docs/design/00 §5.2` — read first
- `docs/design/00 §5.4` — read first
- `ADR-001 C7` — read first
- `ADR-001 C14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `parambridge`.

## Scope

- Build/own the parameter-id->APVTS-pointer table at prepare from the param-schema layout
- snapshot() reads each atomic once per block into the normalized [0,1]/typed-enum ParamSnapshot
- No juce::AudioBuffer/MidiBuffer/APVTS type appears in any core header (consumes ParamSnapshot from core-types)
- Normalized->engineering mapping stays in core; bridge produces normalized only

## Out of scope

- ParamSnapshot field definition (core-types)
- Parameter IDs/ranges (param-schema)
- BlockContext assembly (plugin-12)

## Acceptance criteria

- [ ] Core reads no std::atomic in tight loops; APVTS atomics sampled once per block (§5.4; ADR-001 C7)
- [ ] No JUCE type crosses into the snapshot; ParamSnapshot is the normalized POD (§5.2; ADR-001 C14)
- [ ] A test (tag 'parambridge') asserts a step change in a normalized param appears once-per-block in the snapshot (§5.4)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R parambridge --no-tests=error
```
