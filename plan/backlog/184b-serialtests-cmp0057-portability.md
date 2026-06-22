<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 184b
title: Fix SerialTests.cmake IN_LIST/CMP0057 portability — list(FIND) (184 linux-x64 CI hotfix)
status: done
depends-on: [184]
component: build
stream: ci
tag: ci
---

## Objective

Hotfix the 184 RUN_SERIAL wiring so it works in the ctest-time CTestTestfile context on all platforms.

## Context

cmake/SerialTests.cmake is included via the directory TEST_INCLUDE_FILES property, which CMake reads at
CTEST time (CTestTestfile.cmake `include`). That context does NOT inherit the project's policy stack, so
`CMP0057` defaults to OLD and the `if("${_bare}" IN_LIST MW_SERIAL_TEST_NAMES)` operator is rejected as
"Unknown arguments specified" — which RED-failed linux-x64 CI (run 27976273094) at `ctest` time. macOS
happened to treat CMP0057 as NEW in its ctest-time CMake, so 184 passed local + macOS CI and the policy
gap only surfaced on Linux (the "passes on macOS, fails on Linux" environment class).

## Scope

- cmake/SerialTests.cmake ONLY: replace the `if(... IN_LIST ...)` operator with the policy-independent
  `list(FIND MW_SERIAL_TEST_NAMES "${_bare}" idx)` + `if(NOT idx EQUAL -1)`. Behavior identical; no
  dependency on CMP0057. No change to the RUN_SERIAL set (the 184 QA-approved 85 names) or any assertion.

## Acceptance criteria

- [ ] No `IN_LIST` operator in the TEST_INCLUDE_FILES-loaded script (policy-independent)
- [ ] `ctest --preset default --show-only=json-v1` still reports exactly 85 RUN_SERIAL core tests; `ctest -N` configures with NO CMake error
- [ ] linux-x64 CI configures + completes (the failing `ctest` step now passes the SerialTests include)

## Verification commands

```
cmake --preset default
ctest --preset default -N                       # no CMake error from SerialTests.cmake
ctest --preset default --show-only=json-v1      # 85 tests carry RUN_SERIAL
```
