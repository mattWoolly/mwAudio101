<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 014b
title: Complete the ParamIDs.h §3.0 catalogue (all 92 ids + exact-count static_assert)
status: in-review
depends-on: [014, 019]
component: core
estimated-size: S
stream: params
tag: paramids
---

## Objective

Finish task 014's explicitly-deferred scope: expose a named `inline constexpr const char*`
constant in `mw::params::ids` for EVERY canonical parameter id (the header currently exposes
only 31 of the 92), so no UI/plugin call site hand-types an id string, and add the exact-count
compile-time assert. This unblocks the panel-module UI fleet (120-127) from editing the shared
ParamIDs.h in parallel.

## Context

- `core/params/ParamIDs.h` — currently a "representative set" (31 constants) with an explicit
  `TODO(task-014): complete the full §3.0 catalogue (91 live IDs + the deprecated mw101.os.factor
  alias) and add the exact-count static_assert`. This task closes that TODO.
- `core/params/ParamDefs.h` — the authoritative registry (the 91 live ids + the os.factor alias);
  the constant strings MUST match these VERBATIM. `docs/design/06 §3.0` is the catalogue.
- The 61 currently-missing ids include all of: fx.* (chorus/delay/drive), arp.*, seq.*, lfo.delay/
  dest/sync_div/tempo_sync, mod.*, mpe.*, vintage.*, vel.*, var.*, voice.*, unison.count, quality,
  control.vintage, pitch.modern_unquantized, out.mono, key.trigger_priority, amp.expression,
  drift.rate/depth, warmup.time.

## Scope

- Add the missing `inline constexpr const char*` constants to `mw::params::ids` (core/params/ParamIDs.h),
  one per canonical id, snake_case id -> kPascalCase constant, grouped by subsystem; strings VERBATIM
  from ParamDefs.h. Keep the existing 31 unchanged (purely additive). Include the deprecated
  `mw101.os.factor` alias as a clearly-commented `kOsFactorAlias` (NOT a live param).
- Add a compile-time / unit assertion (tag `paramids`) that EVERY id string in kParamDefs has a
  matching ids:: constant and the live-id count is exactly 91 (+1 alias), so a future registry
  change without a constant fails the build/test.

## Out of scope

- Changing kParamDefs / the registry itself (019); APVTS layout (020); any UI binding (117/120+).
- Renaming any existing constant (purely additive).

## Acceptance criteria

- [ ] Every one of the 91 live canonical ids (and the os.factor alias) has a named ids:: constant matching ParamDefs.h verbatim [docs/design/06 §3.0]
- [ ] A `paramids`-tagged test/static_assert proves 1:1 coverage (every kParamDefs id ↔ a constant) and the exact live count (91) — and would fail if a registry id lacked a constant
- [ ] Purely additive: the existing 31 constants are unchanged; mwcore stays JUCE-free; builds green under --preset default

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R paramids --no-tests=error --output-on-failure
```
