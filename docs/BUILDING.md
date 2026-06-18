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

## Presets

`configurePresets`, `buildPresets`, and `testPresets` share the same names 1:1.
Per-platform presets (`macos-arm64`, `linux-x64`, `windows-x64`) inherit a hidden
base and add only toolchain/generator + per-platform format scope; sanitizer
presets (`asan`, `ubsan`, `tsan`) inherit `debug` [docs/design/11 §9.2; ADR-014 C8].
