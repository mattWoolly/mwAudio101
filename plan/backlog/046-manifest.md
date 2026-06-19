<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 046
title: Manifest — parse/validate MANIFEST.toml with completeness, orphan, honesty-label and renderVersion checks
status: done
depends-on: [001, 006, 040]
component: qa
estimated-size: M
stream: golden
tag: manifest
---

## Objective

Implement Manifest read/validate over tests/golden/corpus/MANIFEST.toml, exposing ManifestEntry (all §7.3 fields) and validation that fails on missing-hash, orphan-entry, missing honesty label, and renderVersion bump mismatches.

## Context

- `docs/design/11 §7.1` — read first
- `docs/design/11 §7.3` — read first
- `docs/design/11 §7.5` — read first
- `ADR-013 C12` — read first
- `ADR-013 C13` — read first
- `ADR-013 C14` — read first
- `ADR-023 V7` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `manifest`.

## Scope

- tests/golden/Manifest.h/.cpp: parse MANIFEST.toml into ManifestEntry records (artifactSha256, blesser, isoDate, commitSha, blessReason, engine, oversampleFactor, sampleRate, seed, blockSize, corpusClass, tolerance, compiler, fpFlagProof, arm64HostId, renderVersion, honestyLabels)
- Completeness check: every golden blob hash present in MANIFEST else fail
- Orphan check: every MANIFEST entry has a corresponding test/artifact else fail
- Honesty-label check: artifact deriving from a ledger §2-§8 fact carries its label; renderVersion bump-vs-artifact-change consistency

## Out of scope

- Writing/appending entries (bless tool, golden-10)
- The TOML library choice (use the build harness dependency)

## Acceptance criteria

- [ ] mw101.manifest a blob hash absent from MANIFEST.toml => validation FAILS; a complete corpus PASSES (paired) [ADR-013 C12]
- [ ] A MANIFEST entry with no corresponding artifact/test => validation FAILS [ADR-013 C13]
- [ ] An entry whose claim derives from a ledger §2-§8 fact without its honesty label => FAILS [ADR-013 C14]
- [ ] Blessed artifact changed without renderVersion bump => FAILS, and renderVersion bumped with no artifact change => FAILS (paired) [ADR-023 V7]
- [ ] verify: ctest --preset default -R manifest --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R manifest --no-tests=error
```
