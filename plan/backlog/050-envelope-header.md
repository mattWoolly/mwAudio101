<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 050
title: Envelope.h header: EnvStage/EnvTrigMode/EnvParams + Envelope class layout
status: todo
depends-on: [007, 006, 001]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: env_header
---

## Objective

Declare the ADSR generator's public API and POD state in core/dsp/Envelope.h exactly as the §2.2 class layout, with noexcept hot-path signatures.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §2.1` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §2.2` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S14` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `env_header`.

## Scope

- Declare enum class EnvStage {Idle,Attack,Decay,Sustain,Release} and EnvTrigMode {GateTrig,Gate,Lfo} (§2.2)
- Declare POD EnvParams struct with documented defaults (§2.2)
- Declare Envelope class: prepare/reset/setParams/noteOn/noteOff/clockTrigger/tick + stage/active/level accessors (§2.2)
- POD private state fields per §2.2; all hot paths noexcept
- namespace mw101::dsp

## Out of scope

- Coefficient/curve math (.cpp task)
- Calibration constant values
- ModRouting depth scaling

## Acceptance criteria

- [ ] Header compiles standalone; Envelope is POD-state, no heap members (§2.1, ADR-020 S14)
- [ ] tick()/noteOn/noteOff/clockTrigger/process are noexcept (§2.1)
- [ ] ctest --preset default -R env_header --no-tests=error passes; test env_header_api compiles against the declared signatures

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R env_header --no-tests=error
```
