<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 040
title: SHA-256 byte hasher for golden serialization
status: in-review
depends-on: [001, 006]
component: qa
estimated-size: S
stream: golden
tag: golden
---

## Objective

Implement a self-contained SHA-256 over a byte span returning a 32-byte digest with value equality, used by CLASS-EXACT compare and artifact hashing.

## Context

- `docs/design/11 §6.2` — read first
- `ADR-013 C5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `golden`.

## Scope

- tests/golden/Sha256.h/.cpp defining struct Sha256{std::array<uint8_t,32>; operator==}
- Sha256 sha256(std::span<const std::byte>) noexcept
- Deterministic, no allocation in the hot loop; same digest on arm64 and Linux x64
- Hex-string formatter for MANIFEST/sidecar use

## Out of scope

- GoldenKey hashing (golden-2)
- Comparer wiring (golden-6)

## Acceptance criteria

- [ ] mw101.unit.golden SHA-256 of known test vectors (NIST short messages) matches published digests [docs/design/11 §6.2]
- [ ] Negative control: a single-bit flip in input changes the digest [ADR-013 C5]
- [ ] Digest is byte-identical when run twice on the same input (determinism) [docs/design/11 §6.2]
- [ ] verify: ctest --preset default -R golden --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R golden --no-tests=error
```
