<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 099
title: HostEvent POD + NormalizedEventBuffer (plugin/host/HostEvent.h)
status: todo
depends-on: [001, 006, 007]
component: app
estimated-size: S
stream: plugin
tag: hostevent
---

## Objective

Define the plugin-side HostEvent POD and the fixed-capacity lock-free NormalizedEventBuffer (prepare sizes, push drops-never-grows, clear, begin/end/size), sized maxEvents = 4*maxBlockSize+256.

## Context

- `docs/design/09 §3.2` — read first
- `ADR-011 C9` — read first
- `ADR-024 C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `hostevent`.

## Scope

- HostEvent POD and HostEventType enum exactly per §3.2
- NormalizedEventBuffer with prepare (sole alloc), push returning false on overflow (lowest-priority drop, ParamValue before NoteOn/Off), clear, iterators
- Overflow drop + debug assert; never allocates from push
- Capacity constant referenced from Calibration.h (core-types)

## Out of scope

- HostEvent->MidiEvent translation (plugin-8)
- Draining wrapper surfaces (plugin-13)
- Calibration.h definition (core-types)

## Acceptance criteria

- [ ] HostEvent is trivially copyable POD with the §3.2 field set
- [ ] push beyond capacity returns false, drops, and asserts without allocating (tag 'hostevent', AudioThreadGuard) [§3.2; ADR-011 C9]
- [ ] prepare(N) then N pushes succeed and size()==N; iteration order matches insertion (§3.2)

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R hostevent --no-tests=error
```
