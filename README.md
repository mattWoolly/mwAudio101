<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# mwAudio101

A circuit-accurate, GPLv3, cross-platform software synthesizer inspired by the Roland SH-101
(monophonic analog synth, 1982), extended with the modern essentials a plugin needs to be
market-viable — built by an AI agent fleet following the [`orchestration-notes/`](orchestration-notes/)
playbook.

- **Formats:** VST3, AU (macOS), CLAP, Standalone (LV2 = goal-tier on Linux)
- **Engine:** JUCE 8 / C++20, circuit-accurate analog modeling (IR3109-class 4-pole filter,
  VCO exponential converter, sub-osc divider, per-voice drift), over a zero-JUCE `mwcore` DSP/control
  library beneath the plugin shell
- **Modern essentials:** poly/unison, oversampling, Chorus / Delay / Drive, host-synced arpeggiator +
  100-step sequencer, MPE-lite, full host automation, **65 curated IDM/acid-leaning presets**
- **Platforms:** macOS arm64 (reference) · Linux x64 (co-required) · Windows x64 (goal-tier)

## Status

**Feature-complete and CI-confirmed shippable on both primary targets (Linux + Mac).** The planned
backlog is 100% delivered; the full plugin builds and renders its panel, both signature behaviors work
end-to-end (the LFO modulates VCO pitch + PWM + VCF cutoff simultaneously; the sequencer / arpeggiator
play and recall correctly), and the 65-preset bank loads and sounds.

- **CI:** `macos-arm64` and `linux-x64` pass the full matrix — every format builds and is validated
  (pluginval / auval / clap-validator) and the complete suite (1009 core tests + the plugin suite) is
  green. `windows-x64` is goal-tier (`continue-on-error`) and not yet green — see follow-ups below.
- **Known follow-ups (optional, non-blocking; tracked in `plan/backlog/`):**
  [`188`](plan/backlog/188-dispatch-complete-render-speedup.md) (speed up a slow audit test),
  [`189`](plan/backlog/189-plugin-allocguard-hardening.md) (harden a rarely-flaky plugin RT check),
  [`190`](plan/backlog/190-windows-x64-build-fix.md) (the Windows "plus").

## Build & test

Full detail (toolchain floor, the local==CI contract, the preset/command map) is in
[`docs/BUILDING.md`](docs/BUILDING.md). The essentials:

**Toolchain:** CMake ≥ 3.25, a C++20 compiler, Unix Makefiles. Dependencies are fetched by CPM and
cached in `CPM_SOURCE_CACHE` (default `$HOME/.cache/CPM`).

```sh
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
```

### 1. Core build + test suite (fast — no JUCE)

`MW_BUILD_PLUGIN` is **OFF by default**, so the default cycle builds only the JUCE-free `mwcore`
library + the headless test binary (fetches just Catch2, not JUCE):

```sh
scripts/check.sh                 # configure → build → test (the one-command gate; identical to CI)
# or, explicitly:
cmake --preset default
cmake --build --preset default
ctest --preset default --no-tests=error --output-on-failure
```

### 2. Full plugin — build the format artifacts

Turn the plugin on (this fetches JUCE, which is large the first time). One `juce_add_plugin` target
(`mwAudio101_All`) produces every format that **resolves** for your machine:

```sh
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -G "Unix Makefiles"
cmake --build build/plugin --target mwAudio101_All           # every resolved format
# (or one format, e.g. just the standalone app — always available:)
cmake --build build/plugin --target mwAudio101_Standalone
```

**Which formats build is gated by validators (ADR-011 "no unvalidated artifact").** A format is only
built if its validator is installed, so configure prints the resolved set, e.g.
`mwAudio101: resolved formats (macos) = STANDALONE`. On a clean dev box that's **Standalone only**
(`auval` ships with macOS, but `pluginval` and `clap-validator` do not). **CI** installs the validators
and therefore builds + validates the full matrix (VST3 / AU / CLAP / Standalone). To unlock the other
formats locally, install the validators (e.g. `pluginval`, `clap-validator`) and re-configure — see
[`docs/design/09-cross-format-capability-ladder.md`](docs/design/) and `cmake/Validators.cmake`.

Artifacts land under `build/plugin/plugin/mwAudio101_artefacts/<FORMAT>/`:
`Standalone/mwAudio101.app` (macOS — double-click to run) · `VST3/mwAudio101.vst3` ·
`AU/mwAudio101.component` · `CLAP/mwAudio101.clap` (only the resolved formats appear).

### 3. Try it / install into a DAW

The **Standalone app builds out of the box** — the quickest way to play it:

```sh
cmake --build build/plugin --target mwAudio101_Standalone
open build/plugin/plugin/mwAudio101_artefacts/Standalone/mwAudio101.app   # macOS
```

For the plug-in formats (once they resolve — see above), auto-install is off
(`COPY_PLUGIN_AFTER_BUILD FALSE`); copy the built artifacts to your user plugin folders (or re-configure
with `-DCOPY_PLUGIN_AFTER_BUILD=ON` to install on every build), then rescan in your DAW:

```sh
A=build/plugin/plugin/mwAudio101_artefacts
cp -R "$A/VST3/mwAudio101.vst3"     ~/Library/Audio/Plug-Ins/VST3/        # if built
cp -R "$A/AU/mwAudio101.component"  ~/Library/Audio/Plug-Ins/Components/  # if built
cp -R "$A/CLAP/mwAudio101.clap"     ~/Library/Audio/Plug-Ins/CLAP/        # if built
```

Factory presets ship embedded in the binary (see below). To drive the sequencer: pick a `SeqArpRiff`
preset, hold a note (or run host transport), and toggle **RUN** in the transport bar — the Internal
clock free-runs in Standalone.

### 4. Run the plugin-side test suite

```sh
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin --no-tests=error --output-on-failure
```

`ctest --preset <p>` runs in parallel via the preset's `jobs`; a curated `RUN_SERIAL` subset of
RT / alloc / determinism tests is excluded from co-scheduling (see `docs/BUILDING.md`). In a
plugin-only build (`MW_BUILD_PLUGIN=ON`), a handful of *core-binary / configure-time* meta-tests show
as `Not Run` (they reference the JUCE-free `mw101_tests` binary, which that build does not produce) —
expected, not failures; every `mw101_plugin.*` case is the real signal.

## Repository layout

| Path | What |
| ---- | ---- |
| [`core/`](core/) | `mwcore` — the zero-JUCE DSP + control engine (oscillators, filter, envelopes, LFO, sequencer/arp, params, calibration, preset bake) |
| [`plugin/`](plugin/) | the JUCE plugin shell — `MwAudioProcessor`, canonical state, MIDI front-end, the `juce_add_plugin` target |
| [`ui/`](ui/) | the editor + panel modules (design-unit layout, telemetry-driven scope, OpenGL-opt-in render backend) |
| [`presets/`](presets/) | the 65 factory `.mw101preset` patches (embedded into the binary at build) |
| [`tests/`](tests/) | `unit/` + `integration/` (core, JUCE-free) and `plugin/` (JUCE-linked) Catch2 suites |
| [`docs/`](docs/) | design docs (`design/`), the build contract (`BUILDING.md`), the adversarial QA report (`QA-REPORT.md`) |
| [`plan/`](plan/) | `decisions/` (ADRs), `backlog/` (atomic task files = the source of truth for task state), `ORCHESTRATION.md` |
| [`cmake/`](cmake/), [`scripts/`](scripts/) | format/validator resolution, the `check.sh` gate, the `RUN_SERIAL` map, CI helpers |

## Factory presets (65)

`SubBass` (12) · `PWMStrings` (8) · `AcidBassLead` (14) · `Lead` (10) · `BlipsFX` (8) ·
`SeqArpRiff` (12) · plus `INIT` (the baseline every patch is authored from). The bank is baked into the
binary as a deterministic, SHA-256-checksummed flat-POD table (`core/preset/PresetBake.h`), so the audio
thread never parses JSON.

## For agents / contributors

Read, in order: [`CLAUDE.md`](CLAUDE.md) → [`AGENTS.md`](AGENTS.md) →
[`plan/ORCHESTRATION.md`](plan/ORCHESTRATION.md). Design spec:
[`docs/superpowers/specs/2026-06-17-mwaudio101-design.md`](docs/superpowers/specs/2026-06-17-mwaudio101-design.md);
QA audit: [`docs/QA-REPORT.md`](docs/QA-REPORT.md). Task state lives in the `status:` frontmatter of each
[`plan/backlog/`](plan/backlog/) file. **Every change is branch + PR + adversarial QA + a full-suite
merge gate;** `mwcore` stays JUCE-free; trademark distance is deliberate (no Roland marks / trade dress).

## Trademark & license

"Roland" and "SH-101" are trademarks of Roland Corporation. mwAudio101 is an independent, unaffiliated
work that models *documented circuit behavior*; it ships under its own name with a modern (non-replica)
UI. Licensed **GPL-3.0-or-later** (see [`LICENSE`](LICENSE)).
