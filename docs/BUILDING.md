<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Building mwAudio101

This is the contract that keeps **local == CI** structurally true: every CI step
runs the *identical* `cmake --preset` line (or `scripts/check.sh`) a developer runs
locally. **No build or test logic lives only in CI YAML** — the workflow is a dumb
dispatcher of preset names [docs/design/11 §9.4; ADR-014 C1, Decision].

## Toolchain floor

- **CMake >= 3.25** — required for `CMakePresets.json` schema v6. Configuring with
  an older CMake FAILS with a clear floor message [docs/design/11 §9.1; ADR-014 C3].
- **C++20**, `CMAKE_CXX_EXTENSIONS OFF`.
- Generator: **Unix Makefiles** (the reference dev box has no ninja).
- Dependencies are fetched by CPM and cached in `CPM_SOURCE_CACHE`
  (default `$HOME/.cache/CPM`).

## Disk / dependency gating

`MW_BUILD_PLUGIN` is **OFF by default**. With it off, the build fetches only
**Catch2** (small) and builds the JUCE-free `mwcore` static lib + the headless test
binary — JUCE is **not** fetched or built. Turn `MW_BUILD_PLUGIN=ON` (via the
per-platform presets) only when you need the JUCE plugin shell; that fetches JUCE
(large). Per ADR-001, `mwcore` has zero JUCE dependency, so the test binary links
`mwcore` + Catch2 only.

## One-command gate

```sh
scripts/check.sh            # configure -> build -> test, default preset
scripts/check.sh linux-x64  # the same, for a named preset
```

## Command map (local == CI)

| Dev command                                          | CI step                          |
| ---------------------------------------------------- | -------------------------------- |
| `cmake --preset default`                             | configure (host gate)            |
| `cmake --build --preset default`                     | build (host gate)                |
| `ctest --preset default --no-tests=error --output-on-failure` | test (host gate)        |
| `scripts/check.sh`                                   | host gate (all platforms)        |
| `cmake --preset macos-arm64 && cmake --build --preset macos-arm64 && ctest --preset macos-arm64` | macOS arm64 (reference/bless) |
| `cmake --preset linux-x64 && cmake --build --preset linux-x64 && ctest --preset linux-x64`       | Linux x64 (co-required hard gate) |
| `cmake --preset windows-x64 && cmake --build --preset windows-x64 && ctest --preset windows-x64` | Windows x64 (goal, `continue-on-error`) |

Every `ctest -R`/`-L` selector carries `--no-tests=error` and test names begin with
their selector word, so a typo'd selector cannot silently match nothing
[docs/design/11 §8.3; ADR-014 C7]. Examples:

```sh
ctest --preset default -R calibration     --no-tests=error
ctest --preset default -R license_headers --no-tests=error
ctest --preset default -R fp_discipline_guard --no-tests=error
ctest --preset default -R audiothreadguard --no-tests=error
```

### Test parallelism (`jobs`) and the `RUN_SERIAL` subset

Every `testPreset` inherits `"execution": { "jobs": 4 }` from the hidden `test-base`
(`CMakePresets.json`), so `ctest --preset <p>` — the one line a developer runs and CI
dispatches — runs the suite in parallel. `4` is the GitHub-hosted runner core count;
it is a fixed literal because a preset cannot compute `nproc`. Locally you may pass a
larger `-j` (e.g. `ctest --preset default -j "$(nproc)"`), which overrides the preset
value; the run stays correct because the safety below does not depend on the job count
[task 184].

A curated subset (the `AudioThreadGuard` alloc/lock-sentinel cases, the cpu-budget
wall-time golden, and the cross-run determinism / steady-state cases) is marked
`RUN_SERIAL` so ctest never co-schedules it with other tests. Under parallel memory
pressure a first-touch page fault inside an armed no-malloc window would otherwise trip
the guard, and CPU contention would inflate the budget median — both false failures of
correct code. `RUN_SERIAL` is an *execution* property (`cmake/SerialTests.cmake`, applied
at ctest time via the directory `TEST_INCLUDE_FILES`); it changes *when* a test runs, not
*what* it asserts, and does not touch any tag or the `ctest-labels.snapshot`
[docs/design/11 §13.1/§13.5; ADR-013 C19/C21].

## JUCE-linked plugin tests (`mw101_plugin_tests`)

The headless `mw101_tests` binary links `mwcore` + Catch2 only — **no JUCE**. To
unit-test plugin-side code (the `MwAudioProcessor` seam, the JUCE-MIDI translation,
APVTS state) there is a **second** Catch2 target, `mw101_plugin_tests`, that links
the real JUCE modules. It is built **only when `MW_BUILD_PLUGIN=ON`**, so the
core/default cycle never pays for it (and never fetches JUCE).

It is wired with JUCE's `juce_add_console_app()` helper so the JUCE module compile
environment (the `JUCE_*` defines, module include dirs) is set up correctly for the
linked `juce_*` modules; the processor under test is compiled in by adding
`plugin/PluginProcessor.cpp` directly to the target. A headless run brackets JUCE's
globals with a `juce::ScopedJuceInitialiser_GUI` — no message loop, no audio device.

Drop a `tests/plugin/*Test.cpp` (e.g. `tests/plugin/FooTest.cpp`) and re-run
configure; the `file(GLOB_RECURSE … CONFIGURE_DEPENDS "tests/plugin/*.cpp")` picks it
up with **no `CMakeLists.txt` edit** (same pattern as `tests/unit`). Name each
test-case after its selector word so `ctest -R <word>` selects it.

```sh
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R plugin_harness --no-tests=error --output-on-failure
```

The smoke test `tests/plugin/PluginHarnessTest.cpp` (`[plugin_harness]`) instantiates
the processor, runs `prepareToPlay → processBlock → releaseResources` on a silent
block, and asserts the output is all finite. The first run is slow (~30 s) because
the JUCE GUI initialiser performs a one-time font/graphics scan on macOS.

## Source discovery (CONFIGURE_DEPENDS globs)

`mwcore` and the `mw101_tests` binary discover their sources via `file(GLOB_RECURSE
… CONFIGURE_DEPENDS)`, **not** an explicit list. To add code: drop a `.cpp` under
`core/**` (one of `core/{dsp,voice,control,fx,params,calibration,midi,util}/`) and
its test under `tests/unit/**` (or `tests/integration/**`), then re-run
`cmake --preset default`. **No `CMakeLists.txt` edit is needed**, and re-running
configure is what re-evaluates the glob — `CONFIGURE_DEPENDS` makes the build try to
re-glob, but the reliable step is an explicit configure after adding/removing files.
This lets the parallel development fleet add module files without serializing on
(and conflicting in) `core/CMakeLists.txt` / `tests/CMakeLists.txt`.

The standalone invariant **tools** `LicenseHeaderCheck` and `FpDisciplineCheck` stay
explicit single-file `add_executable` targets — they are tooling, not unit tests, so
they are deliberately excluded from the test glob.

**Tradeoff (glob vs explicit list):** globbing trades CMake's per-file precision for
zero-merge-conflict scaling. The cost is that a *new* file requires a configure run
to be seen (`CONFIGURE_DEPENDS` is best-effort, not guaranteed on every generator),
and the file set is implicit rather than reviewable in one list. For this fleet the
conflict-free scaling wins; if a file is silently missed, re-run `cmake --preset
default`.

## Presets

`configurePresets`, `buildPresets`, and `testPresets` share the same names 1:1.
Per-platform presets (`macos-arm64`, `linux-x64`, `windows-x64`) inherit a hidden
base and add only toolchain/generator + per-platform format scope; sanitizer
presets (`asan`, `ubsan`, `tsan`) inherit `debug` [docs/design/11 §9.2; ADR-014 C8].
