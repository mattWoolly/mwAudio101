<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 011
title: License-header SPDX check + ctest-labels snapshot diff
status: in-review
depends-on: [006]
component: qa
estimated-size: S
stream: infra
tag: license
---

## Objective

Implement the license-header check (tests/invariants/LicenseHeaderCheck) failing on any source missing SPDX-License-Identifier: GPL-3.0-or-later, plus the checked-in ctest-labels snapshot diff that fails on a deleted/renamed suite.

## Context

- `docs/design/11 §13.2` — read first
- `docs/design/11 §8.4` — read first
- `ADR-013 C18` — read first
- `ADR-013 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `license`.

## Scope

- LicenseHeaderCheck scanning all source/CMake/docs files for SPDX-License-Identifier: GPL-3.0-or-later; missing => FAILS
- registered as a ctest under the license tag
- checked-in tests/golden/corpus/ctest-labels.snapshot (from ctest --print-labels) + a CI/ctest diff step that FAILS on divergence
- verify license check via ctest --preset default -R license --no-tests=error

## Out of scope

- the MANIFEST.toml honesty-label/provenance checks (golden-harness stream)
- the fp-flag grep guard (infra-12)

## Acceptance criteria

- [ ] any source file missing SPDX-License-Identifier: GPL-3.0-or-later FAILS ctest [docs/design/11 §13.2; ADR-013 C18]
- [ ] the committed ctest --print-labels snapshot differing from current FAILS [docs/design/11 §8.4; ADR-013 C3]
- [ ] the license test is named license* and passes under ctest --preset default -R license --no-tests=error [docs/design/11 §8.3]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R license --no-tests=error
```
