<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 139
title: Cross-format bit-exactness test (VST3/AU/CLAP/Standalone identical DSP output)
status: done
depends-on: [113, 104, 119, 077, 136]
component: qa
estimated-size: M
stream: integration
tag: crossformat_exact
---

## Objective

Assert that an identical patch/state + identical normalized event sequence drained through each wrapper's HostEvent->MidiEvent path yields bit-identical engine output across VST3/AU/CLAP/Standalone on the macOS arm64 reference.

## Context

- `docs/design/09 §3.1` — read first
- `docs/design/09 §3.3` — read first
- `plan/decisions/011 C11` — read first
- `plan/decisions/022 C4` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `crossformat_exact`.

## Scope

- Feed identical input through each wrapper's normalization to mw::core::MidiEvent streams
- Assert the resulting MidiEvent streams are identical across formats (single boundary erases format shape per §3.3)
- Run the shared engine on each normalized stream and compare DSP output bit-for-bit
- Cover the three note-expression rungs feeding the same engine path (ADR-022 C4)

## Out of scope

- Per-format wrapper construction (format-wrappers)
- Validator execution (integration-7/8/10)
- PDC invariance (integration-11)

## Acceptance criteria

- [ ] All wrappers emit identical mw::core::MidiEvent streams for identical input per §3.3
- [ ] DSP output is bit-identical across VST3/AU/CLAP/Standalone on macOS arm64 per §3.1 / ADR-011 C11
- [ ] Same rung across two formats yields bit-identical output; only the rung source differs per ADR-022 C4
- [ ] ctest --preset default -R crossformat_exact --no-tests=error is green; test names begin with crossformat_exact

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R crossformat_exact --no-tests=error
```
