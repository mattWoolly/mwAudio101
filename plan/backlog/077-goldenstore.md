<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 077
title: GoldenStore — blob/sidecar keying, lookup and load
status: in-review
depends-on: [001, 006, 041, 076]
component: qa
estimated-size: M
stream: golden
tag: golden
---

## Objective

Implement GoldenStore mapping a GoldenKey to on-disk golden blob (raw f32/WAV) and sidecar JSON, with has/load/blobPath/sidecarPath, loading a blessed RenderResult.

## Context

- `docs/design/11 §5.4` — read first
- `docs/design/11 §7.1` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/GoldenStore.h/.cpp implementing has(), load() (throws if absent), blobPath(), sidecarPath()
- Deterministic path derivation from hash(GoldenKey), partitioned by determinism class and sample rate
- Sidecar JSON read/write recording the GoldenKey fields + human-readable render-graph description
- Blob (de)serialization of f32 samples + sampleRate + EngineTag

## Out of scope

- Comparison (golden-6/golden-7)
- MANIFEST validation (golden-8)
- Writing blessed artifacts (golden-10 bless tool)

## Acceptance criteria

- [ ] mw101.unit.golden a stored blob+sidecar round-trips to an identical RenderResult [docs/design/11 §5.4]
- [ ] load() on an absent key throws; has() returns false for it (paired present/absent) [docs/design/11 §5.4]
- [ ] blobPath/sidecarPath are stable functions of the GoldenKey and distinct across determinism class and sample rate [docs/design/11 §7.1]
- [ ] verify: ctest --preset default -R golden --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
