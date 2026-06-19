<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 068
title: Glide.h/.cpp — per-voice portamento slew
status: in-review
depends-on: [001, 006, 007, 067]
component: core
estimated-size: S
stream: voice-control
tag: glide
---

## Objective

Implement the per-voice Glide portamento object: an RC-style exponential slew on the pitch target with OFF/ON/AUTO modes, a 0-5 s time mapping, and a snap (no-glide) path.

## Context

- `docs/design/04-voice-and-control.md §5.5` — read first
- `ADR-005 §Decision item 2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `glide`.

## Scope

- core/voice/Glide.h/.cpp matching the §5.5 class signature exactly
- GlideMode {Off,On,Auto}; setMode/setTimeSeconds/setTarget(legato,arpActive)/nextValue/snapTo
- RC-style exponential coeff derived from the 0-5 s TIME via a (PI) mapping read from Calibration.h
- AUTO glides only on legato; glide suppressed when arpActive is true (§5.5 table); ON always glides
- snapTo jumps current_=target_ for first-note/arp

## Out of scope

- the glide TIME parameter ID/skew (doc 06)
- 6-bit pitch quantization (ControlCore owns the stair-step; Glide only smooths between holds)
- deciding legato/arpActive — caller supplies them

## Acceptance criteria

- [ ] TDD: TIME spans 0-5 s and longer TIME yields slower convergence to target (§5.5 table)
- [ ] AUTO glides only when legato=true; with arpActive=true nextValue snaps (no glide) regardless of mode (§5.5)
- [ ] ON always glides toward target; snapTo makes nextValue return target immediately (§5.5)
- [ ] exponential approach: per-step error shrinks monotonically toward target, no overshoot (§5.5 curve)
- [ ] test names begin with the tag; verify: ctest --preset default -R glide --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R glide --no-tests=error
```
