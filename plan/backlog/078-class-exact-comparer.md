<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 078
title: CLASS-EXACT comparer (SHA-256 hash compare)
status: in-review
depends-on: [001, 006, 040, 076]
component: qa
estimated-size: S
stream: golden
tag: golden
---

## Objective

Implement compareExact() doing SHA-256 over the canonical byte serialization of integer/control output, asserting bit-for-bit equality required identical on arm64 and Linux x64.

## Context

- `docs/design/11 §6.2` — read first
- `ADR-013 C5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/CompareExact.h/.cpp with struct ExactResult{match,got,expected} and compareExact(got,blessed)
- Canonical byte serialization of the RenderResult integer/control payload
- Equality is whole-digest; any diff => match=false

## Out of scope

- FP/two-stage compare (golden-7)
- Engine-context refusal (lives in golden-2, called by golden-7)

## Acceptance criteria

- [ ] mw101.golden.class-exact identical blessed/got => match true; one-sample diff => match false (paired) [ADR-013 C5]
- [ ] compareExact reports the got and expected digests on mismatch [docs/design/11 §6.2]
- [ ] Serialization is stable so the same output hashes identically on arm64 and Linux [ADR-013 C5]
- [ ] verify: ctest --preset default -R class-exact --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
