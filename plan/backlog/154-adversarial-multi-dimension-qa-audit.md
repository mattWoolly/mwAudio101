<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 154
title: Adversarial multi-dimension QA audit report (docs/QA-REPORT.md)
status: todo
depends-on: [118, 006, 077, 006, 139, 140, 141, 142, 143, 162d, 118e]
component: qa
estimated-size: M
stream: qa
tag: qa
---

## Objective

Author docs/QA-REPORT.md: an adversarial, evidence-cited audit of the feature-complete engine + harness across the determinism-class, silent-pass, provenance, RT-invariant, FP-discipline and CPU-budget dimensions. Each finding cites the design § and the ctest tag that proves (or fails to prove) it.

## Context

- `docs/design/11 §1.3` — read first
- `docs/design/11 §3.1` — read first
- `docs/design/11 §5.1` — read first
- `docs/design/11 §6` — read first
- `docs/design/11 §7.5` — read first
- `docs/design/11 §8.2` — read first
- `docs/design/11 §8.4` — read first
- `docs/design/11 §13` — read first
- `docs/design/11 Acceptance hooks` — read first
- `ADR-013 Contract C1-C22` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `qa`.

## Scope

- docs/QA-REPORT.md only: one section per audit dimension (silent-pass C1-C4, CLASS-EXACT/FP compare C5-C9/C22, bless+MANIFEST provenance C10-C14, calibration self-tests C15-C17, license/RT/FP/CPU invariants C18-C21).
- For each dimension, record the contract case, the citing design §, the proving ctest tag/name-prefix, and a pass/gap verdict with evidence (test name + observed result).
- Adversarial coverage matrix: every required name-prefix (vco vcf vca env seq prng arp cal) and every cross-cutting invariant tag (license rt fp cpu) mapped to >=1 discovered test, flagging any prefix/tag with 0 (per §8.2).
- Enumerate residual gaps the harness structurally CANNOT prove (no-oracle / measured-fidelity limits per §1.3) and label them explicitly as permanent, not TODOs.
- Cross-check the label-snapshot (§8.4) and per-prefix discovery (§8.2) outputs against the report's claimed coverage.

## Out of scope

- Modifying any harness, engine, or test source — audit is read-and-report only.
- Re-running bless or mutating MANIFEST.toml / goldens.
- Asserting measured SH-101 fidelity (forbidden per §1.3 / ADR-013 ratification).
- Defining or relocating PI constants (that is qa-2).

## Acceptance criteria

- [ ] docs/QA-REPORT.md carries one auditable verdict per ADR-013 contract case C1-C22, each citing its design § (per §3.1 / Acceptance hooks).
- [ ] Coverage matrix shows every required prefix vco/vcf/vca/env/seq/prng/arp/cal mapped to >=1 discovered ctest, flagging zeros per §8.2 (C2).
- [ ] Report enumerates the structural no-oracle limits as permanent gaps, never as measured-fidelity claims, per §1.3 / ADR-013 Consequences.
- [ ] Each RT/FP/CPU/license invariant (§13.1-§13.6) appears in the report with its proving tag (rt/fp/cpu/license) and verdict, per §3.1.
- [ ] docs/QA-REPORT.md carries the SPDX GPL-3.0-or-later header so the §13.2 license gate passes (C18).

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R qa --no-tests=error
```
