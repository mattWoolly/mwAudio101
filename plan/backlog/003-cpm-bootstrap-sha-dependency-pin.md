<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 003
title: CPM bootstrap + full-SHA dependency pin manifest
status: in-review
depends-on: [001]
component: infra
estimated-size: M
stream: infra
tag: build
---

## Objective

Add the committed, hash-checked cmake/CPM.cmake bootstrap and cmake/Dependencies.cmake pinning JUCE, Catch2, clap-juce-extensions and CLAP to full 40-char commit SHAs with provenance comments, plus the offline-cache path.

## Context

- `docs/design/11 §10` — read first
- `ADR-014 C2` — read first
- `ADR-014 C10` — read first
- `ADR-014 Decision (Dependency vendoring)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- cmake/CPM.cmake committed bootstrap, integrity-checked via CPM_DOWNLOAD_HASH
- cmake/Dependencies.cmake: single pin manifest, every dep to a full 40-char SHA (never branch/tag/main)
- each pin carries inline version + SHA + date + why + SPDX comment (JUCE GPL-3.0, Catch2 BSL-1.0, clap-juce-extensions, CLAP MIT)
- clap-juce-extensions/CLAP gated behind MW_BUILD_CLAP
- CPM_SOURCE_CACHE + FETCHCONTENT_FULLY_DISCONNECTED offline path so a no-network build succeeds

## Out of scope

- juce_add_plugin wiring (plugin stream)
- Catch2 test-binary registration (infra-9)
- the mw_fp_discipline flags (infra-4)

## Acceptance criteria

- [ ] Every dependency is pinned to a full 40-char commit SHA with version+SHA+date+why+SPDX inline; no branch/tag/main [docs/design/11 §10; ADR-014 C2]
- [ ] the CPM bootstrap is CPM_DOWNLOAD_HASH integrity-checked [docs/design/11 §10; ADR-014 C2]
- [ ] clap-juce-extensions/CLAP are gated behind MW_BUILD_CLAP [docs/design/11 §10]
- [ ] a no-network build succeeds via CPM_SOURCE_CACHE + FETCHCONTENT_FULLY_DISCONNECTED [docs/design/11 §10; ADR-014 C10]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R build --no-tests=error
```
