<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 186
title: Give the dispatch_complete audit cases a generous ctest TIMEOUT (linux-x64 CI green)
status: done
depends-on: [184, 165]
component: build
stream: ci
tag: ci
---

## Objective

Stop the 165 completeness-audit cases from tripping ctest's default 1500s timeout on the slow shared
Linux runner, so linux-x64 CI goes green (the last gap to both-primary-target CI confirmation).

## Context

After 184 (ctest parallelism) + 184b (CMP0057 fix) let linux-x64 CI actually run, it failed on ONE
test: `#188 dispatch_complete: exempt params ... behave as classified` **Timeout 1500.05s**. The
dispatch_complete suite renders minutes of audio per param battery — measured macOS-arm64 durations:
#188 = 570s, #185 (FX audit) = 461s, #179 = 264s. The Linux runner is ~2.6x slower, so #188 (~1480s)
tips over ctest's 1500s DEFAULT. The core already flushes denormals (Engine.cpp FTZ/DAZ at process
entry), so this is NOT a denormal stall — the audit is simply exhaustive and slow. macOS passes (570 <
1500); Linux just needs a higher per-test ceiling.

## Scope

- cmake/SerialTests.cmake: in the existing discovered-test loop, set the ctest `TIMEOUT` property to
  3000s on every `dispatch_complete:*` case (covers #188 ~1480s + #185 ~1200s on Linux + margin). Every
  OTHER test keeps the 1500s default so a genuine hang still fails fast. TIMEOUT is an EXECUTION
  property only — no assertion / label / source change; RUN_SERIAL behavior (85 tests) unchanged.

## Out of scope

- SPEEDING UP the dispatch_complete renders (trimming render length without weakening the all-91-params
  audit) — real perf debt, tracked as a follow-up (the 570s/461s single-test times are unhealthy for
  every full-suite run, local + CI).
- Any production / test-assertion change.

## Acceptance criteria

- [ ] Every dispatch_complete:* test carries TIMEOUT 3000 (verify via `ctest --show-only=json-v1`); the RUN_SERIAL count stays 85; non-dispatch tests keep the 1500s default
- [ ] linux-x64 CI completes green (no #188 timeout); macOS + the full local suite stay green
- [ ] No production code / test assertion / ctest-labels.snapshot change

## Verification commands

```
cmake --preset default
ctest --preset default --show-only=json-v1   # dispatch_complete:* carry TIMEOUT=3000; 85 RUN_SERIAL
ctest --preset default --no-tests=error      # full suite still green locally
```
