<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 184
title: CI ctest parallelism + docs-only skip
status: done
component: build
stream: ci
tag: ci
---

## Objective

The CI host gate runs `scripts/check.sh` -> `ctest --preset <p>` with NO parallelism
(no `jobs` on any `testPreset`). The 1006-test core suite has many audio-rendering
tests at tens of seconds each; run SERIALLY on the slow shared Linux runner that is
hours, so `linux-x64` CI never completes in practical time (the same correct code is
GREEN on the fast macOS arm64 runner). The build type is already `RelWithDebInfo` —
the bottleneck is purely serial ctest.

Make the CI test runs parallel, while keeping the run correct and complete:

1. **Parallelize ctest.** Add `"execution": { "jobs": N }` to the testPresets so
   `ctest --preset <p>` (the one line CI dispatches and a developer runs) runs the suite
   in parallel. `N` is a fixed literal `4` (the GitHub-hosted runner core count) because a
   preset cannot compute `nproc`; a larger local `-j` overrides it and stays correct.
2. **`RUN_SERIAL` the memory-/timing-sensitive subset.** The `AudioThreadGuard`
   alloc/lock-sentinel cases, the cpu-budget wall-time golden, and the cross-run
   determinism / steady-state cases would FLAKE under parallel pressure (a first-touch
   page fault trips the no-malloc guard; CPU contention inflates the budget median).
   Mark them `RUN_SERIAL` so ctest never co-schedules them — an execution property only,
   no assertion / tag / `ctest-labels.snapshot` change.
3. **Skip the full matrix on docs-only changes.** A `paths-ignore` on the CI workflow so a
   commit touching ONLY doc trees does not spin the hours-long matrix for a backlog/ADR edit.

## Context

- `docs/design/11 §9.4` / `ADR-014 C1` — local == CI; the workflow is a dumb dispatcher of
  preset names. The parallelism therefore lives in `CMakePresets.json`, not the YAML.
- `docs/design/11 §13.1` / `ADR-013 C19` / `ADR-001 C3/C4` — the AudioThreadGuard no-alloc /
  no-lock sentinel (why the alloc-guard tests are memory-pressure-sensitive).
- `docs/design/11 §13.5` / `ADR-013 C21` — the cpu-budget wall-time regression golden (why the
  budget tests are CPU-contention-sensitive).
- `docs/design/11 §8.4` / `ADR-013 C3` — the ctest-labels snapshot; `RUN_SERIAL` is a property,
  NOT a label, so the snapshot is unaffected.

## Scope

- `CMakePresets.json`: add `"jobs": 4` to the hidden `test-base` `execution` block so every
  testPreset (the CI `linux-x64` / `macos-arm64` / `windows-x64` + `default` + the sanitizer
  presets) runs parallel via the same `ctest --preset` line.
- `cmake/SerialTests.cmake` (new): applies `RUN_SERIAL ON` to the curated set via
  `set_tests_properties`, keyed off the Catch2 `<target>_TESTS` discovery variable; wired in
  through `tests/CMakeLists.txt`'s directory `TEST_INCLUDE_FILES` (catch_discover_tests
  registers at build time, so names do not exist at configure time — this is CMake's documented
  mechanism for per-test properties on discovered tests). 85 core-suite tests are marked (90
  including the 5 JUCE plugin tests only built when `MW_BUILD_PLUGIN=ON`).
- `.github/workflows/ci.yml`: `paths-ignore` on `push` + `pull_request` for the genuinely
  doc-only trees (`docs/**`, `plan/**`, `orchestration-notes/**`) + the named root prose docs
  (`AGENTS.md`, `CLAUDE.md`, `README.md`, `LICENSE`). Deliberately NOT a blanket `**/*.md`:
  Markdown is SPDX-license-gated by the `license_headers` ctest, so a `.md` landing inside a
  build tree (e.g. `presets/AUTHORING.md`) must still trigger CI.
- `docs/BUILDING.md`: document the `jobs` value and the `RUN_SERIAL` subset.

## Out of scope

- Changing any test's assertions or any production code (only EXECUTION properties + preset jobs
  + the CI paths filter + this task file + docs).
- The `ctest-labels.snapshot` (untouched — `RUN_SERIAL` is a property, not a label).
- Cross-platform speedup measurement (it shows on the Linux CI runner after merge; this task
  proves the parallel run is SAFE + correct locally).

## Acceptance criteria

- [ ] `ctest --preset <p>` runs in parallel (testPresets carry `execution.jobs`), and
      `scripts/check.sh` picks it up unchanged (local == CI) [docs/design/11 §9.4; ADR-014 C1].
- [ ] The full core suite is 100% GREEN under `ctest --preset default -j "$(sysctl -n hw.ncpu)"`
      (run TWICE), with no alloc-guard / RT / determinism / budget flakes — proving the
      `RUN_SERIAL` set is correct and complete.
- [ ] The memory-/timing-sensitive subset is `RUN_SERIAL` (an execution property only); no test
      assertion, tag, production source, or `ctest-labels.snapshot` is changed.
- [ ] A docs-only commit (only `docs/**` / `plan/**` / `orchestration-notes/**` / named root docs)
      does NOT trigger the build+test matrix; any commit also touching code/cmake/tests/presets/
      scripts/`.github` still triggers it.

## Verification commands

```sh
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake --preset default && cmake --build --preset default
# full core suite under parallelism, twice, must be 100% green (1006 tests):
ctest --preset default -j "$(sysctl -n hw.ncpu)" --no-tests=error --output-on-failure
ctest --preset default -j "$(sysctl -n hw.ncpu)" --no-tests=error --output-on-failure
# confirm the RUN_SERIAL subset (85 core tests) is applied as an execution property:
ctest --test-dir build/default --show-only=json-v1 \
  | python3 -c "import json,sys; d=json.load(sys.stdin); print(sum(1 for t in d['tests'] for p in t.get('properties',[]) if p['name']=='RUN_SERIAL' and p['value']))"
```
