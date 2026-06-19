<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 019
title: Declarative parameter registry (ParamDefs.h)
status: done
depends-on: [014, 015, 007]
component: core
estimated-size: M
stream: params
tag: paramdefs
---

## Objective

Declare the single constexpr kParamDefs table: every live parameter's type, range/skew/step, default, automatable/discrete flags, group, smoothing class, versionAdded and software-ext flag, plus static_assert invariants.

## Context

- `docs/design/06-parameters-state-presets.md §3.0` — read first
- `§3.1` — read first
- `§3.3` — read first
- `§3.4` — read first
- `§3.5` — read first
- `§3.7` — read first
- `§3.8` — read first
- `§3.9` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `paramdefs`.

## Scope

- core/params/ParamDef struct + ParamType/ParamGroup enums exactly as §3.1; one constexpr kParamDefs entry per §3.0 live ID (91) plus the os.factor alias slot
- Continuous ranges/skews/defaults from §3.3, choice indices+canonicalChoiceCount+defaults from §3.4/§3.7/§3.8, bool defaults from §3.5; skew/time-constant/default numbers referenced from Calibration.h via params-2 accessor where applicable (§3.10)
- Structural params (quality, voice.mode, voice.count, unison.count, control.vintage) carry isAutomatable=false and smoothing=NoSmooth (§3.7, §3.8)
- isSoftwareExt flags on vco.range indices >=4 and lfo.shape index 4 sit at/above canonicalChoiceCount (§3.4)
- static_assert + CI test: IDs unique/mw101.-prefixed; choice params have choices!=nullptr and choiceCount>=canonicalChoiceCount; structural => non-automatable + NoSmooth; software-ext index >= canonicalChoiceCount (§3.1 invariants)

## Out of scope

- building the juce ParameterLayout (params-4)
- the smoothing DSP
- calibration numeric values themselves (core-types Calibration.h)

## Acceptance criteria

- [ ] kParamDefs has 91 live entries matching §3.0 IDs/types/ranges/defaults/automatable/smoothing exactly, plus the os.factor alias [§3.0]
- [ ] Structural params are isAutomatable==false and SmoothingClass::NoSmooth, enforced by static_assert [§3.7; §3.8; §3.1]
- [ ] Choice indices/canonical counts match §3.4/§3.7/§3.8 and software-ext indices sit >= canonicalChoiceCount [§3.4]
- [ ] Skews/defaults/time-constants are referenced from Calibration.h, never inlined [§3.10]
- [ ] Test names begin with paramdefs and verify the §3.1 invariants over kParamDefs [§3.1; Acceptance hooks §1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R paramdefs --no-tests=error
```
