<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 035
title: FilterTables: per-sample-rate CV->g and tuning-comp tables built in prepare
status: todo
depends-on: [001, 006, 007]
component: core
estimated-size: M
stream: core-filter
tag: vcf-tables
---

## Objective

Implement FilterTables that precompute, at prepare(fsOsHz), the CV->g and Hz->g maps (folding fc=fcRefHz*2^v and g=1-exp(-2pi*fc/fs_os)) plus the residual half-sample tuning-compensation table, so no transcendental runs at audio rate.

## Context

- `02-dsp-filter.md §7` — read first
- `02-dsp-filter.md §5.2` — read first
- `ADR-003 F-08` — read first
- `ADR-003 F-11` — read first
- `ADR-003 F-14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `vcf-tables`.

## Scope

- core/dsp/FilterTables.h/.cpp: build(fsOsHz) fills preallocated std::array tables off-thread (§7.2)
- cvToG(volts) and hzToG(Hz) interpolated lookups; clamp fc to [10, min(20000,0.45*fs_os)] (§5.2, §6, F-08)
- resoTuningComp(g) from the frozen compFit constants, absorbing the <10%-at-2x half-sample error (§7.3)
- Read fcRefHz / compFit from Calibration.h; frozen kTableSize=1024; read-only at audio rate (§9, F-11, F-14)

## Out of scope

- LadderFilter integration / use of these tables in setters (core-filter-4)
- The oversample factor->fs_os derivation (consumed; quality enum is param-schema)

## Acceptance criteria

- [ ] vcf-tables test: a 1 V CV step doubles fc (1 V/oct) and fc is clamped to [10, min(20k,0.45*fs_os)], never exceeding the guard (§6 F-08, §10 F-08)
- [ ] vcf-tables test: cvToG/hzToG match the reference fc=fcRefHz*2^v and g=1-exp(...) within interpolation tolerance (§5.2)
- [ ] vcf-tables test: build is the only allocator and runs off the audio thread; table contents bit-identical across runs for fixed fs_os (§7.3 F-11/F-14)
- [ ] vcf-tables test: no (PI) literal inline; fcRefHz/compFit read from Calibration.h (§10 F-15)
- [ ] verify: ctest --preset default -R vcf-tables --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R vcf-tables --no-tests=error
```
