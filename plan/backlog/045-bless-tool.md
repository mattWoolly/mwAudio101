<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 045
title: bless tool — arm64-only, BLESS_REASON-gated guarded writer
status: todo
depends-on: [001, 006, 047, 044]
component: qa
estimated-size: M
stream: golden
tag: manifest
---

## Objective

Implement the bless tool that writes a golden artifact and appends a MANIFEST entry, refusing on non-arm64, empty BLESS_REASON, missing CLASS-FP tolerance, or missing honesty label, and governing/recording renderVersion.

## Context

- `docs/design/11 §7.2` — read first
- `docs/design/11 §7.3` — read first
- `docs/design/11 §7.6` — read first
- `ADR-013 C10` — read first
- `ADR-013 C11` — read first
- `ADR-023 V6` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `manifest`.

## Scope

- tools/bless/bless.cpp with BlessRequest, enum BlessRefusal{NotArm64,EmptyReason,MissingTolerance,MissingHonestyLabel}, bless(request)->std::expected<ManifestEntry,BlessRefusal>
- Refuse on non-arm64 host and on empty BLESS_REASON env
- Require tolerance for CLASS-FP and a honesty label where ledger-derived
- Append the §7.3 ManifestEntry (incl. fpFlagProof, arm64HostId, renderVersion) via Manifest; bump renderVersion per Provenance

## Out of scope

- Being invoked as a test side-effect (forbidden — separate tool)
- Comparer/render logic (reused from golden-4..7)

## Acceptance criteria

- [ ] mw101.manifest bless on a simulated non-arm64 host returns NotArm64; on arm64 with valid inputs returns a ManifestEntry (paired) [ADR-013 C10]
- [ ] Empty BLESS_REASON returns EmptyReason; a CLASS-FP request without tolerance returns MissingTolerance [ADR-013 C11; docs/design/11 §7.2]
- [ ] A ledger-derived artifact without a honesty label returns MissingHonestyLabel [ADR-013 C14]
- [ ] On a tolerance-exceeding change the appended entry records a bumped renderVersion next to BLESS_REASON [ADR-023 V6]
- [ ] verify: ctest --preset default -R manifest --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R manifest --no-tests=error
```
