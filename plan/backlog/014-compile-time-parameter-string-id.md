<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 014
title: Compile-time parameter string-ID constants (ParamIDs.h)
status: in-review
depends-on: [001, 006]
component: core
estimated-size: S
stream: params
tag: paramids
---

## Objective

Declare one constexpr const char* per parameter ID so no call site ever hand-types an ID string. Pure header, no logic.

## Context

- `docs/design/06-parameters-state-presets.md §2` — read first
- `§3.0` — read first
- `§3.2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `paramids`.

## Scope

- core/params/ParamIDs.h with one constexpr const char* per live ID in §3.0 plus the deprecated mw101.os.factor alias slot
- All IDs are immutable mw101.<group>.<name> snake_case strings (§3.2)
- Group constants logically (vco, sub, mixer, vcf, env, lfo, vca, glide, mod, arp, seq, key, tune, vel, amp, mpe, vintage, drift, var, warmup, fx, out, voice, control, quality)
- Static-assert / test that the count matches 91 live + 1 alias and that every string is mw101.-prefixed and unique

## Out of scope

- ParamDef registry table (params-3)
- APVTS layout (params-4)
- any range/default/smoothing metadata

## Acceptance criteria

- [ ] Every ID present is mw101.-prefixed snake_case and unique [§3.0; §3.2]
- [ ] Exactly 91 live IDs plus the mw101.os.factor deprecated alias are declared; no amp.volume / tune.fine / sub.shape variant exists [§3.0; §3.2; §7.4]
- [ ] Test names begin with paramids and verify uniqueness and prefix via static data over the constant set [Acceptance hooks §1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R paramids --no-tests=error
```
