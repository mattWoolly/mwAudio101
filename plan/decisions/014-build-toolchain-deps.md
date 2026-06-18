<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 014: Build system, dependency management & toolchain

Status: accepted
Date: 2026-06-17

## Context

This ADR fixes the build system for mwAudio101: the CMake structure and presets
(`default`/`debug`/sanitizers/per-platform), how JUCE / Catch2 / clap-juce-extensions
are vendored and version-pinned, the local==CI command mapping documented in
`docs/BUILDING.md`, and the floating-point-discipline compiler flags per platform.

This decision touches several owner-locked decisions, which sit ABOVE this ADR and are
re-affirmed, not reopened:

- **One command set locally; CI is a thin 1:1 mirror added last** (spec §6, §1.15;
  `AGENTS.md` "CI / cross-platform"). The build system is the *mechanism* that makes
  local==CI structurally true rather than a documentation promise.
- **FP discipline pinned in the build** — `-ffast-math` OFF, `-ffp-contract=off`
  (`/fp:precise` on Windows) wherever bit-exactness / golden compare is claimed
  (spec §5; `AGENTS.md` "Tests"). This ADR centralizes and enforces those FROZEN flags
  but does not change them.
- **Pin external tools by version + SHA-256; mismatch = hard fail** (`AGENTS.md`
  "CI / cross-platform"). This ADR extends that rule from CLI tools to the entire C++
  dependency graph.
- **Platforms:** macOS arm64 = reference/bless + bit-exact; Linux x64 = co-required hard
  gate; Windows x64 = goal (`continue-on-error`) (spec §6, §1.8; `AGENTS.md`). Bit-exact
  bless and the tolerance-compare gate (`max abs ≤ 1e-6`, spec §5) are meaningless if the
  toolchain or a dependency can drift underneath them — so reproducibility is a
  first-class requirement here, not a nicety.
- **Never build a format where no validator is wired** (spec §6; `AGENTS.md`). Format
  scoping must therefore live in the preset, per platform.
- **Tree split:** `core/` = pure C++20, no JUCE; `plugin/` = JUCE shell; `ui/`; `tests/`;
  `presets/` = data + loader (spec §3.1–§3.4; ratified by ADR-001). The CMake structure
  must realize this split, and the no-JUCE-in-core build guard from ADR-001 lives in it.
- **GPL-3.0-or-later** (spec §1; `LICENSE`). The dependency manifest doubles as the
  license-provenance audit trail.
- **~64 presets** as version-controlled data (spec §1.10, §3.4); presets must never be
  parsed or allocated on the audio thread (the RT lock, ADR-001 C3/C4).

Forces: JUCE is large (cold-build cost), the three OSes must not diverge in toolchain
install, mutable git refs are the dominant silent source of golden breakage, and the
audio-thread no-alloc/no-lock invariant (ADR-001) needs a mechanical gate, not prose.

## Options considered

Both panelists converged on the same shape: **CMakePresets.json as the sole entrypoint,
CPM.cmake vendoring with full-commit-SHA pins in one manifest file, a centralized
FP-discipline INTERFACE target, and asan/ubsan/tsan presets** — with `docs/BUILDING.md`
as the local↔CI command map. There was no split vote on mechanism. They differed in
emphasis, and each contributed a load-bearing detail. The decision is the union of their
proposals with the sharper enforcement from each.

### Persona: cross-platform-build

Advocated reproducibility-first: CMake 3.25+ (C++20, `CMAKE_CXX_EXTENSIONS OFF`) driven
entirely by a committed `CMakePresets.json` so `cmake --preset X` is the ONLY entrypoint
and CI is a thin matrix calling the same presets verbatim. Single vendoring mechanism =
CPM.cmake (a thin FetchContent wrapper), pinning every dep to a full 40-char commit SHA
(not a branch/tag, which is mutable): JUCE to a release commit, Catch2 to a v3.x SHA,
clap-juce-extensions to a SHA, CLAP transitively pinned. Adds `CPM_SOURCE_CACHE` +
`FETCHCONTENT_FULLY_DISCONNECTED` offline path and a one-file pin manifest
(`cmake/Dependencies.cmake`) as the single auditable pin set. Presets: hidden base +
inheriting `default`/`debug`/`asan`/`ubsan`/`tsan`/`release` and per-platform
`macos-arm64`/`linux-x64`/`windows-x64`; build/test presets pair 1:1; ctest via
testPresets. FP discipline as one INTERFACE target (`mw_fp_discipline`) linked by every
DSP/golden target; per-platform format scoping in the preset; clap behind `MW_BUILD_CLAP`.
Presets (the 64 patches) ship as versioned data with a deterministic loader baking a flat
POD table off-thread, plus a ctest that round-trips every patch (schema + checksum).

- Pros: full-SHA pins make builds byte-reproducible across OSes and over time (the
  precondition for the bit-exact bless and the tolerance gate); CMakePresets-as-sole-entry
  makes local==CI literally a shared file; CPM = plain CMake + caching + offline with no
  extra package manager / no vcpkg-Conan toolchain split; the single manifest doubles as
  the GPLv3 provenance trail; sanitizer presets make the RT/UB invariants CI-gated; format
  scoping enforces "never build a format with no validator"; presets-as-data with a
  checksum ctest keeps patches reproducible and off the audio thread.
- Cons: FetchContent/CPM rebuilds JUCE from source per fresh checkout — cold CI is slow
  without aggressive caching; full-SHA pins go stale and need a deliberate reviewed bump
  PR; submodules are arguably more transparent / network-free at configure; pinning
  clap-juce-extensions by SHA means lagging upstream CLAP fixes until a bump; cross-compiler
  reproducibility is not free (Linux/Windows stay tolerance-only because of libm/codegen);
  baked-POD presets need a regen + re-checksum on schema change.

### Persona: Maintainability

Took CMakePresets as the single contract and added the concrete realization: top-level
`CMakeLists.txt` (project, C++20, GPL headers, options) → `add_subdirectory(core)`
(pure C++20 static lib, NO JUCE — the testable heart) → `plugin` (`juce_add_plugin`,
links core) → `ui` → `tests` (Catch2, gated on `-DMW101_TESTS=ON`). Deps isolated in
`cmake/dependencies.cmake`; FP flags isolated in `cmake/CompilerFlags.cmake` applied via
an INTERFACE target `mw101_fp_strict` so discipline is structural, not copy-pasted. CPM
committed at `cmake/CPM.cmake`, pinned by version AND git SHA AND a checked
`CPM_DOWNLOAD_HASH`, each pin carrying an inline `version / SHA / date / why` comment; a
renovate-style bump is one reviewed PR. Presets schema v6 with `ci-*` presets that inherit
a base and ONLY add toolchain/generator. testPresets carry `--output-on-failure` and the
locked `--no-tests=error`. FP grep ctest scans `compile_commands.json` for forbidden flags
(fast-math poisoning caught mechanically, not by trust). A `scripts/check.sh` thin wrapper
(configure→build→test for the host) is the one-command entry, and CI calls
`scripts/check.sh` too; `docs/BUILDING.md` is a 2-column dev-command↔CI-step table.

- Pros: local==CI enforced by a shared file (drift structurally impossible); single
  dependency manifest reviewable in one place; FP discipline on one INTERFACE target +
  the compile-DB grep ctest catches poisoning mechanically across all three platforms;
  pure-C++20 `core/` with no JUCE builds and unit-tests fast (tight TDD loop, headless
  Linux gate); sanitizer presets are first-class; vendored (not system) Catch2 keeps
  results bit-reproducible; new contributor runs one `scripts/check.sh` and is green.
- Cons: CPM adds an abstraction layer over FetchContent (purists prefer submodules for
  obviousness/offline — mitigated by `CPM_SOURCE_CACHE` + committed `CPM.cmake`); SHA pins
  require deliberate bump PRs for security fixes (intentional friction); schema v6 needs
  CMake ≥ 3.25, excluding very old distro CMakes (must be documented as the floor); more
  upfront wiring (helper files, INTERFACE targets, the grep ctest) than a flat
  `CMakeLists.txt`; cold-cache first configure downloads a large JUCE repo.

### Critiques and mechanisms adopted

- From **cross-platform-build**: full-commit-SHA pinning of *every* dep (branch/tag refs
  are mutable and forbidden); the single one-file pin manifest doubling as the GPLv3
  provenance trail; `CPM_SOURCE_CACHE` + `FETCHCONTENT_FULLY_DISCONNECTED` offline path;
  hidden-base preset inheritance to avoid duplication; per-platform format scoping in the
  preset; clap behind an `MW_BUILD_CLAP` option; presets-as-versioned-data with a
  schema+checksum round-trip ctest, baked to a flat POD table off the audio thread.
- From **Maintainability**: the concrete tree split realizing spec §3 / ADR-001 (`core`
  pure-C++20 first, no JUCE); FP flags isolated in `cmake/CompilerFlags.cmake` on ONE
  INTERFACE target; the **`compile_commands.json` grep ctest** that fails the build if a
  golden/DSP target carries a forbidden fast-math/contract flag (mechanical, not trust);
  the `scripts/check.sh` host-wrapper as the one-command entry that CI also calls;
  `docs/BUILDING.md` as a 2-column dev↔CI mapping; `CPM_DOWNLOAD_HASH` integrity check on
  the CPM bootstrap itself; vendored (not system) Catch2.
- Reconciled naming/granularity: the two proposals used different identifiers
  (`mw_fp_discipline` vs `mw101_fp_strict`; `cmake/Dependencies.cmake` vs
  `cmake/dependencies.cmake`). Adopted the project-consistent `mwcore`/`mw…` family from
  ADR-001: the INTERFACE target is **`mw_fp_discipline`** and the manifest is
  **`cmake/Dependencies.cmake`**. This is a naming choice, not a behavioral split.

## Decision

Adopt **CMakePresets.json (schema v6, CMake ≥ 3.25, C++20, `CMAKE_CXX_EXTENSIONS OFF`) as
the sole build entrypoint; CPM.cmake vendoring with full-commit-SHA pins in one
`cmake/Dependencies.cmake` manifest; a centralized `mw_fp_discipline` INTERFACE target
guarded by a `compile_commands.json` grep ctest; first-class `asan`/`ubsan`/`tsan`
sanitizer presets; and `docs/BUILDING.md` as the dev↔CI command map.**

**Tree / CMake structure** (realizes spec §3.1–§3.4 and ADR-001):

- Top-level `CMakeLists.txt`: `project(mwAudio101 CXX)`, C++20, `CMAKE_CXX_EXTENSIONS OFF`,
  GPL SPDX headers, options (`MW101_TESTS`, `MW_BUILD_CLAP`, `MW_BUILD_LV2`, sanitizer
  toggles wired through presets, not bare cache edits).
- `add_subdirectory(core)` → `mwcore`, a pure C++20 **static lib with zero JUCE include/link
  dependency** (ADR-001 C1 guard lives here); links `mw_fp_discipline`.
- `add_subdirectory(plugin)` → `mwplugin` via `juce_add_plugin`, links `mwcore`; links
  `mw_fp_discipline` on its DSP-bearing translation units.
- `add_subdirectory(ui)` → editor, links `mwplugin`.
- `add_subdirectory(tests)` → Catch2 console binary linking `mwcore` only (gated on
  `-DMW101_TESTS=ON`); links `mw_fp_discipline` so golden-compare math matches the shipped
  core exactly.
- `add_subdirectory(presets)` → deterministic loader that bakes the ~64 patches into a flat
  POD table at build/load time, never parsing on the audio thread (RT lock, ADR-001 C3/C4).
- Helper files: `cmake/CPM.cmake` (committed bootstrap, integrity-checked via
  `CPM_DOWNLOAD_HASH`), `cmake/Dependencies.cmake` (the single pin manifest),
  `cmake/CompilerFlags.cmake` (the `mw_fp_discipline` INTERFACE target).

**Dependency vendoring** — CPM.cmake (a thin, auditable FetchContent wrapper) is the
**single** mechanism. No submodules, no vcpkg/Conan (chosen to avoid an extra package
manager install and a toolchain-file divergence between local and CI on three OSes). Every
dependency is pinned in `cmake/Dependencies.cmake` to a **full 40-char commit SHA** — never
a branch, floating tag, or `main`, because mutable refs are the dominant silent source of
golden-test breakage and supply-chain risk, and the bless/tolerance gate (spec §5) is
meaningless if a dep can drift. This extends `AGENTS.md`'s "pin by version + SHA-256; mismatch
= hard fail" rule to the C++ dependency graph. Each pin carries an inline comment recording
*version, SHA, date pinned, why, and SPDX license* (JUCE GPL-3.0 path, CLAP MIT, Catch2 BSL),
so the one file is simultaneously the reproducibility manifest and the GPLv3 license-provenance
audit trail (spec §1; the open-source/libre positioning depends on clean provenance,
`docs/research/12-market-legal-landscape.md` §2, §7.3). Specifically:

- **JUCE** — `CPMAddPackage(GIT_REPOSITORY juce-framework/JUCE GIT_TAG <commit-SHA>)`,
  pinned to a known-good 8.0.x release commit (not "8.x"). The GPL path is recorded.
- **Catch2** — vendored (not system) at a v3.x release `GIT_TAG <SHA>`, so test results are
  bit-reproducible across machines (BSL-1.0 recorded).
- **clap-juce-extensions** — `GIT_TAG <SHA>` (no semver releases exist; SHA pin is
  mandatory), gated behind `MW_BUILD_CLAP`; CLAP itself transitively SHA-pinned (MIT).

`CPM_SOURCE_CACHE` (a shared checkout dir for local + CI) plus an offline
`FETCHCONTENT_FULLY_DISCONNECTED` path make builds reproducible and network-light. A
dependency bump is one reviewed PR that re-pins SHA + hash + date together.

**Presets (CMakePresets.json)** — hidden base presets + inheritance to avoid duplication.
`configurePresets`: `default` (RelWithDebInfo, FP-disciplined), `debug`, `release`, `asan`,
`ubsan`, `tsan`, and per-platform `macos-arm64` / `linux-x64` / `windows-x64` that inherit a
base and ONLY add the toolchain/generator + the per-platform **format scoping** (VST3 + AU +
CLAP + Standalone on macOS; VST3 + CLAP + Standalone + LV2-goal on Linux; VST3 + CLAP +
Standalone on Windows) so no format is built where no validator is wired (spec §6;
`AGENTS.md`). `buildPresets` and `testPresets` share the SAME names 1:1. `testPresets` carry
`--output-on-failure` and the locked `--no-tests=error` (silent-pass prevention, spec §6;
`AGENTS.md` "Tests"). The CMake-version floor (≥ 3.25 for schema v6) is documented in
`docs/BUILDING.md`.

**FP discipline** — `cmake/CompilerFlags.cmake` defines ONE INTERFACE target
`mw_fp_discipline`, linked by `mwcore`, the DSP-bearing `plugin` units, and the golden
tests. It carries (per spec §5, FROZEN — this ADR enforces, never relaxes):

- GCC / Clang / AppleClang: `-fno-fast-math -ffp-contract=off -fno-finite-math-only
  -fno-associative-math -fno-reciprocal-math -fexcess-precision=standard`; explicitly
  **never** `-ffast-math` / `-Ofast`; `-fdenormal-fp-math=ieee` (runtime FTZ/DAZ flush is
  set in `process` per ADR-001 C11, so denormal flushing is never left to a build flag that
  could silently change golden output).
- MSVC: `/fp:precise /fp:contract-`; never `/fp:fast`.

A ctest (`fp_discipline_guard`) greps `compile_commands.json` for forbidden flags
(`-ffast-math`, `-Ofast`, `/fp:fast`, `-ffp-contract=fast`) on every golden/DSP target and
**fails the build** if any appears — fast-math poisoning is caught mechanically, not by
review. This protects the macOS-arm64 bit-exact bless and the Linux-x64 ≤ 1e-6 tolerance
gate from FMA contraction / reassociation divergence (spec §5; `docs/research/10-dsp-modeling-techniques.md`
§5.1, §3.7 — the nonlinear ladder + oversampler is exactly where reordered FP math would
break the cross-platform compare).

**local==CI mapping** — `docs/BUILDING.md` is the contract: a 2-column table mapping each dev
command to the identical CI step. The developer runs
`cmake --preset linux-x64 && cmake --build --preset linux-x64 && ctest --preset linux-x64`
(or `scripts/check.sh` for the host); CI runs the SAME lines / the SAME `scripts/check.sh`.
**No build or test logic lives only in CI YAML** — the workflow is a dumb dispatcher of preset
names (spec §6, §1.15; `AGENTS.md` "CI / cross-platform"). Windows runs
`continue-on-error: true` (goal tier, spec §6).

Rationale recap: the owner locks demand a bit-exact macOS bless, a Linux co-required hard
gate, GPLv3, an RT-safe audio path, and a 1:1 local↔CI mirror. CMakePresets is the only
mechanism that makes local==CI a *structural* guarantee (one shared file both invoke).
CPM-over-FetchContent with full-SHA pins is the only way to keep the bless reproducible years
later without a second package manager fracturing local↔CI. Centralizing the FROZEN FP flags
on one INTERFACE target + a grep ctest is what keeps a future DSP file from silently dropping
the flags and breaking the tolerance compare. Submodules and a flat CMakeLists were rejected:
submodules lose the single-file pin manifest, the offline cache ergonomics, and the
license-provenance trail; a flat CMakeLists cannot enforce the no-JUCE-in-core boundary or the
structural FP-target discipline.

## Consequences

This commits us to:

- A reviewed, deliberate dependency-bump PR cadence (full-SHA pins go stale on purpose);
  security/bugfix updates to JUCE/CLAP/clap-juce-extensions require re-pinning SHA + hash +
  date, not floating — intentional friction that is the price of a reproducible bless.
- A non-trivial cold CI build (JUCE is large); mitigated by `CPM_SOURCE_CACHE` shared
  local↔CI plus a compiler cache (ccache/sccache) layer — added build-config surface that
  is purely build-time and never touches runtime.
- A pinned, documented toolchain floor: CMake ≥ 3.25 (schema v6); very old distro CMakes are
  excluded and must be stated in `docs/BUILDING.md`.
- Upfront wiring (`cmake/` helpers, the `mw_fp_discipline` INTERFACE target, the
  `fp_discipline_guard` and preset-checksum ctests) rather than a single flat CMakeLists —
  day-one cost that pays off at scale and is required to enforce the locks.
- Presets baked to a flat POD table need a regen + re-checksum step on any schema change
  (a small, gated pipeline step), keeping patch loading off the audio thread.

This forecloses / makes harder:

- Using git submodules or a system/`find_package` Catch2/JUCE (would break the single pin
  manifest, the offline cache, and bit-reproducible test results).
- Adding a second package manager (vcpkg/Conan) — rejected to avoid a toolchain-file split
  between local and CI on three OSes.
- Putting any build/test logic in CI YAML beyond dispatching preset names (drift would break
  the local==CI lock).
- Shipping a plugin format on a platform with no wired validator (the per-platform preset
  format scoping forbids it).
- Cross-compiler *bit*-identical output: the build system pins flags but cannot make Apple
  Clang and GCC produce identical floats — Linux/Windows remain tolerance-only (≤ 1e-6,
  spec §5); only macOS arm64 is the bit-exact bless reference. This is a re-affirmation of an
  existing owner lock, not a new promise.

This ADR stays strictly inside the owner locks (spec §1, §3, §5, §6; `AGENTS.md`) and the
research it cites; it introduces no new user-facing feature scope or behavior. No owner
ratification item.

## Contract

Normative cases the backlog implements verbatim. "MUST" items are acceptance checkboxes.

| # | Condition / trigger | Required behavior | Enforcement |
|---|---|---|---|
| C1 | A developer or CI wants to configure/build/test | `cmake --preset X` is the ONLY entrypoint; CI runs the identical preset names / `scripts/check.sh`; no build-test logic in YAML | `docs/BUILDING.md` 2-column map + CI review |
| C2 | Any dependency (JUCE, Catch2, clap-juce-extensions, CLAP) is added/updated | Pinned in `cmake/Dependencies.cmake` to a full 40-char commit SHA (never a branch/tag/`main`) with version + SHA + date + why + SPDX license inline | manifest review; CPM `CPM_DOWNLOAD_HASH` integrity check |
| C3 | A CMake older than 3.25 (or a non-v6 preset schema) is used | Configure FAILS with a clear floor message | `cmake_minimum_required` + documented floor |
| C4 | A golden/DSP target compiles | It links `mw_fp_discipline`; flags are `-fno-fast-math -ffp-contract=off` (+ no-finite/assoc/recip, `-fexcess-precision=standard`) on GCC/Clang, `/fp:precise /fp:contract-` on MSVC | `cmake/CompilerFlags.cmake` INTERFACE target |
| C5 | A forbidden FP flag (`-ffast-math`, `-Ofast`, `/fp:fast`, `-ffp-contract=fast`) reaches any golden/DSP target | Build FAILS | `fp_discipline_guard` ctest greps `compile_commands.json` |
| C6 | A plugin format is requested on a platform | Built only where a validator is wired: macOS = VST3+AU+CLAP+Standalone; Linux = VST3+CLAP+Standalone (+LV2 goal); Windows = VST3+CLAP+Standalone | per-platform preset format scoping |
| C7 | Any `ctest` selector (`-R`/`-L`) runs | Carries `--no-tests=error`; test names begin with the selector word | testPresets + `AGENTS.md` "Tests" |
| C8 | `configurePresets`, `buildPresets`, `testPresets` are defined | Names pair 1:1; per-platform presets inherit a hidden base and add only toolchain/generator + format scope | CMakePresets.json schema v6 |
| C9 | The ~64 presets are loaded | Baked into a flat POD table at build/load time; never parsed/allocated on the audio thread; a ctest round-trips every patch (schema + checksum) | preset loader + `presets_roundtrip` ctest |
| C10 | A build runs with no network | Succeeds via `CPM_SOURCE_CACHE` + `FETCHCONTENT_FULLY_DISCONNECTED` | offline-build ctest / CI cache |
| C11 | The `core` target's link/include closure is inspected | Contains zero JUCE target/header (re-affirms ADR-001 C1) | no-JUCE-in-core build guard |
| C12 | Windows x64 CI job runs | `continue-on-error: true` (goal tier); macOS arm64 + Linux x64 are hard gates | CI matrix |
