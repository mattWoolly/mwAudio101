<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 101
title: HostEvent -> mw::core::MidiEvent translator (plugin/midi/EventTranslator.h/.cpp)
status: in-review
depends-on: [001, 006, 007, 020, 099, 100]
component: app
estimated-size: S
stream: plugin
tag: evttranslate
---

## Objective

Implement the field-for-field allocation-free HostEvent -> mw::core::MidiEvent translation: enum remap, narrowing copies, CC numbers resolved to param index, ProgramChange consumed (not forwarded).

## Context

- `docs/design/09 §3.3` — read first
- `docs/design/00 §5.3` — read first
- `ADR-001 C11` — read first
- `ADR-011 C11` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `evttranslate`.

## Scope

- Map each HostEvent field to mw::core::MidiEvent per the §3.3 table
- ProgramChange consumed in plugin/ (preset recall hook) and NOT forwarded; all other types map 1:1
- CC numbers resolved through the §6 learn map to a param index before forwarding
- Allocation-free; produces a span ready for BlockContext.midi

## Out of scope

- mw::core::MidiEvent definition (core-types)
- CcLearnMap implementation (plugin-6)
- ParamSnapshot marshalling (plugin-10)

## Acceptance criteria

- [ ] Each field maps exactly per the §3.3 table; ProgramChange is not forwarded (§3.3)
- [ ] Identical HostEvent input yields identical mw::core::MidiEvent output (cross-format determinism precondition) [§3.3; ADR-011 C11]
- [ ] Translation performs zero heap allocation (tag 'evttranslate', AudioThreadGuard) [§3.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R evttranslate --no-tests=error
```
