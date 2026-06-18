<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 072
title: DriftModel orchestration engine (Tier1/2/3 + smoothing + reroll)
status: todo
depends-on: [001, 006, 007, 020, 063, 064, 065, 073]
component: engine
estimated-size: M
stream: vintage
tag: vintage_model
---

## Objective

Implement DriftModel.h/.cpp owning DriftState[kMaxVoices], orchestrating Tier-1 frozen cal, shared/per-voice Tier-2 thermal, Tier-3+variance note-on draws, mandatory output smoothing, and lock-free Re-roll, with all allocation confined to prepare().

## Context

- `08-vintage-variance.md §3.2` — read first
- `08-vintage-variance.md §5.2` — read first
- `08-vintage-variance.md §8.2` — read first
- `08-vintage-variance.md §8.3` — read first
- `08-vintage-variance.md §9` — read first
- `08-vintage-variance.md §11` — read first
- `08-vintage-variance.md §12` — read first
- `ADR-009 §Decision 5` — read first
- `ADR-009 §Decision 6` — read first
- `ADR-009 VV-14` — read first
- `ADR-009 VV-16` — read first
- `ADR-009 VV-18` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vintage_model`.

## Scope

- prepare(sampleRate, blockSize, numVoices) allocates state + sets kDriftSmoothMs via setTimeConstant; processBlock/noteOn noexcept, no alloc/lock
- Map shared T(t): driftCents = T*drift.depth (pitch), cutoffDriftCents = T*drift.depth*kVcfDriftRatio (cutoff) per §5.2
- noteOn draws Tier-3+variance; per-voice smoothed accessors pitchOffsetCents/cutoffOffset/pwOffset/envTimeScale/glideTimeScale feed through OnePoleSmoother (§9)
- setInstanceSeed + pendingReroll_ atomic consumed at block boundary; mono == voice 0, per-voice Tier-2 under unison with shared warm-up chassis term (§11)

## Out of scope

- Host-thread Age macro mapping (vintage-5)
- Param schema/atomic plumbing (consumed from param-schema)
- Persisting instance_seed/seedLocked in <extras> (consumed from state-presets)

## Acceptance criteria

- [ ] Tests named vintage_model* verify bit-identical processBlock output for fixed seed across runs and across re-prepare (§12.7, VV-17)
- [ ] Tests verify VCO/VCF drift perfectly correlated (both = T scaled); kVcfDriftRatio=0 removes cutoff drift while pitch drift persists (§5.2, VV-13)
- [ ] Tests verify within a held note slop/varCutoff/varPw stay constant (smoother settles), only Tier-2 moves; a target step de-zippers over ~kDriftSmoothMs (§7.1, §9, VV-15)
- [ ] Tests verify no-alloc/no-lock in processBlock/noteOn via AudioThreadGuard, block-rate update once per block, Re-roll applied lock-free at block boundary, mono==voice 0 (§12, VV-14, VV-16, VV-18)
- [ ] Verify: ctest --preset default -R vintage_model --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vintage_model --no-tests=error
```
