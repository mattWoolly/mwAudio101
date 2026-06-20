<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 113
title: Single juce_add_plugin target over the shared processor (plugin/CMakeLists.txt)
status: done
depends-on: [001, 096, 111]
component: infra
estimated-size: S
stream: plugin
tag: wrappers
---

## Objective

Define the single juce_add_plugin target (VST3/AU/Standalone native + CLAP via clap-juce-extensions + LV2 via JUCE native exporter) over the one MwAudioProcessor, with formats supplied by the cmake/Formats.cmake resolution.

> **MINIMAL-bootstrap status (in-review).** A MINIMAL version was landed by the
> JUCE-phase bootstrap (branch `task/juce-bootstrap`): `plugin/CMakeLists.txt` now
> defines the real `juce_add_plugin(mwAudio101 ...)` target (`IS_SYNTH`,
> `NEEDS_MIDI_INPUT`) over the one shared `MwAudioProcessor`, links
> `mwcore` + `mw_fp_discipline` + the JUCE synth modules (`juce_audio_utils`,
> `juce_audio_processors`, `juce_dsp`) and the recommended config/warning/LTO flags,
> sets the `JUCE_*` config defines, and feeds `FORMATS` through the
> `cmake/Formats.cmake` validator gate. **Bootstrap constrains FORMATS to Standalone
> only** — the one format whose validator (`standalone-smoke`) is unconditionally
> wired; the gate hard-removed VST3/AU/CLAP here (their validators are not installed on
> the dev box). Proven GREEN: `mwAudio101_Standalone` builds and links to a real arm64
> `.app`. Also enabled C/OBJC/OBJCXX languages at the top level (guarded by
> `MW_BUILD_PLUGIN`) since JUCE needs them on Apple platforms. **Deferred to the full
> task 113:** enabling VST3/AU/CLAP (once pluginval/auval/Steinberg validator/
> clap-validator are wired in CI) and LV2 via the JUCE native exporter behind
> `MW_BUILD_LV2`; CLAP via `clap-juce-extensions` on the same processor. AAX stays
> permanently excluded (ADR-024 C6).

## Context

- `docs/design/09 §2.1` — read first
- `docs/design/09 §3.1` — read first
- `ADR-011 Decision` — read first
- `ADR-024 C1, C5` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).

## Scope

- One juce_add_plugin consuming plugin-2's resolved FORMATS list; CLAP via clap-juce-extensions on the same processor
- LV2 emitted by JUCE native exporter behind MW_BUILD_LV2; no clap-wrapper (ADR-024 C1)
- No DSP fork; every wrapper differs only in plugin/ (ADR-011 Decision)
- Link mwcore + mwplugin

## Out of scope

- Format resolution/gate (plugin-2)
- Validator location (plugin-1)
- Processor implementation (plugin-13)
- Dependency pinning (build-skeleton)

## Acceptance criteria

- [ ] All requested formats build from one juce_add_plugin over the shared MwAudioProcessor with no DSP fork (§3.1; ADR-011 Decision; ADR-024 C5)
- [ ] LV2 is emitted by the JUCE native exporter only; no clap-wrapper is vendored/invoked (ADR-024 C1)
- [ ] CLAP target is produced via clap-juce-extensions wrapping the same processor (§2.1)

## Verification commands

```
cmake --preset default
cmake --build --preset default
```
