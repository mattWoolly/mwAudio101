<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 006
title: tests/CMakeLists.txt — Catch2 binary, catch_discover_tests, silent-pass gates
status: in-review
depends-on: [003, 005]
component: qa
estimated-size: M
stream: infra
tag: harness
---

## Objective

Create tests/CMakeLists.txt building the Catch2 console binary linking mwcore only, registering tests via catch_discover_tests with TEST_PREFIX 'mw101.' under --no-tests=error, plus the per-prefix discovery assertion and the FAIL_REGULAR_EXPRESSION backstop.

## Context

- `docs/design/11 §8.1` — read first
- `docs/design/11 §8.2` — read first
- `docs/design/11 §4.1` — read first
- `ADR-013 C1` — read first
- `ADR-013 C2` — read first
- `ADR-014 C7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `harness`.

## Scope

- Catch2 console binary mw101_tests linking mwcore ONLY (+ mw_fp_discipline), gated on MW101_TESTS
- catch_discover_tests(... TEST_PREFIX "mw101." PROPERTIES FAIL_REGULAR_EXPRESSION "No tests ran")
- per-prefix discovery assertion step: required prefixes vco vcf vca env seq prng arp cal each >=1 discovered test, else ctest FAILS
- a tiny self-test TU so the binary is non-empty and the discovery wiring is exercised
- ensure -R/-L selectors carry --no-tests=error (via testPresets) and names begin with the selector word

## Out of scope

- the actual subsystem unit tests (other streams populate vco/vcf/vca/env/seq/arp; prng/smooth here)
- the label-snapshot diff (infra-11)
- AudioThreadGuard / license / fp / cpu invariant gates (infra-10/11/12, golden/full-engine streams)

## Acceptance criteria

- [ ] an empty/mis-linked/mis-filtered binary FAILS via --no-tests=error and the 'No tests ran' FAIL_REGULAR_EXPRESSION, never green [docs/design/11 §8.1; ADR-013 C1]
- [ ] the per-prefix discovery assertion FAILS if any required prefix (vco vcf vca env seq prng arp cal) has 0 discovered tests [docs/design/11 §8.2; ADR-013 C2]
- [ ] the test binary links mwcore only (no JUCE/plugin/audio device) [docs/design/00 §9.3; ADR-001 C13]
- [ ] tests register under the mw101. prefix and -R/-L selectors carry --no-tests=error [docs/design/11 §8.3; ADR-014 C7]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R harness --no-tests=error
```
