<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 102b
title: Define core/params/ParamSnapshot.h — the concrete normalized param POD the engine reads
status: done
depends-on: [019, 007]
component: core
estimated-size: S
stream: params
tag: paramsnapshot
---

## Objective

`core/params/ParamSnapshot.h` exists as the concrete JUCE-free POD that `BlockContext.params`
points at (currently only forward-declared) — the per-block immutable normalized parameter view
the engine reads to drive its modules. Closes the DAG gap surfaced by wave-J2 QA (PR #87): the
ParamBridge (102) had to emit an interim plugin-side type because this core type did not exist.

## Context

- Originating gap: `core/BlockContext.h` forward-declares `struct ParamSnapshot;` and holds
  `const ParamSnapshot* params;`, but no task ever defined it. ParamBridge (102), the Engine
  (118), and the full processor (111) all need it.
- Verified facts: `core/BlockContext.h` (the forward-decl + pointer), `core/params/ParamDefs.h`
  (the 91-entry registry — the field universe), `plugin/params/ParamBridge.{h,cpp}` (emits an
  interim NormalizedParamSnapshot keyed by registry index — the shape to canonicalize).
  Design: `docs/design/06-parameters-state-presets.md §5.2/§5.4`; ADR-001 C7/C14.
- TDD: write the failing test first under `tests/unit/`; test names begin with `paramsnapshot`.

## Scope

- `core/params/ParamSnapshot.h`: a JUCE-free, trivially-copyable, standard-layout POD holding the
  per-block normalized value for every live param (indexed by `kParamDefs` order, or named fields
  per design 06 §5.2 — match the design), readable by the engine. No allocation; constructed/filled
  off the audio thread (the bridge populates it). Add the include path so `BlockContext.params`
  resolves to the real type.

## Out of scope

- Wiring ParamBridge to emit this type + the Engine reading/applying it to modules — that is the
  full-processor task 111 (and the param-application step). This task only DEFINES the type + its
  test. JUCE/APVTS stays plugin-side.

## Acceptance criteria

- [ ] `core/params/ParamSnapshot.h` is a JUCE-free trivially-copyable standard-layout POD with a
      slot per live `kParamDefs` entry; `BlockContext` includes it and `params` is a real pointer [§5.2; ADR-001 C7].
- [ ] A test constructs/fills it and reads back per-param normalized values; compile-time POD asserts pass.
- [ ] `cmake --preset default` builds; `ctest --preset default -R paramsnapshot --no-tests=error` green.

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R paramsnapshot --no-tests=error
```
