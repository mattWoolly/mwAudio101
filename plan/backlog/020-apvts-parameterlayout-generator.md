<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 020
title: APVTS ParameterLayout generator (ParameterLayout.cpp)
status: done
depends-on: [019, 007]
component: core
estimated-size: S
stream: params
tag: paramlayout
---

## Objective

Implement buildParameterLayout() that mechanically emits one juce parameter per kParamDefs entry, with the string ID driving the deterministic host numeric ID and structural params constructed non-automatable.

## Context

- `docs/design/06-parameters-state-presets.md §4` — read first
- `§3.2` — read first
- `§3.1` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `paramlayout`.

## Scope

- core/params/ParameterLayout.h/.cpp: buildParameterLayout() iterating kParamDefs (§4)
- Continuous -> AudioParameterFloat with NormalisableRange{min,max,step,skew} (symmetric where symmetricSkew); Choice -> AudioParameterChoice with fixed labels; Bool -> AudioParameterBool (§4)
- Each constructed with juce::ParameterID{def.id, def.versionAdded}; structural params withAutomatable(false) (§4; §3.2)
- Test: layout is a pure function of kParamDefs (count == live param count); structural params non-automatable; numeric IDs deterministic across two builds via stable string-ID hash (§3.2; §4)

## Out of scope

- ParamDefs table contents (params-3)
- binding atomic pointers in the processor (plugin-processor stream)
- state serialization (params-7)

## Acceptance criteria

- [ ] Layout emits exactly one parameter per kParamDefs live entry; no parameter constructed elsewhere [§4; Acceptance hooks §1]
- [ ] Each parameter uses juce::ParameterID{id, versionAdded} so VST3/AU/CLAP numeric IDs are the deterministic hash of the string ID [§3.2; §4]
- [ ] Structural params are withAutomatable(false) [§4; §3.8]
- [ ] Test names begin with paramlayout and assert purity-of-kParamDefs and deterministic numeric IDs [Acceptance hooks §1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R paramlayout --no-tests=error
```
