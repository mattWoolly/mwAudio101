<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 110b
title: JUCE plugin skeleton bootstrap — juce_add_plugin Standalone + minimal processor wrapping Engine
status: in-review
depends-on: [001, 096, 118]
component: app
estimated-size: M
stream: plugin
tag: juce_bootstrap
---

## Objective

A buildable JUCE plugin scaffold exists: a `juce_add_plugin` target (Standalone, formats gated
by `cmake/Formats.cmake`) over a minimal `MwAudioProcessor` that wraps the complete `mw::Engine`
behind `prepareToPlay/processBlock/releaseResources` — proving the full JUCE build+link works in
this environment and giving the leaf JUCE tasks a target to fill in.

## Context

- Proves JUCE-phase feasibility (Standalone built+linked green: 29.5 MB arm64 Mach-O).
- The RICH processor work is explicitly deferred and remains `todo`: lock-free NormalizedEventBuffer
  + processor no-alloc test (111), 91-param APVTS layout + ParamSnapshot bridge (020), MIDI
  front-end/CC-learn/MPE (104), CapabilityShim (102), LatencyReporter/PDC (105), real UI (108+),
  VST3/AU/CLAP/LV2 format targets (113). This task is the scaffold only.

## Scope

- `plugin/CMakeLists.txt` real `juce_add_plugin(mwAudio101)` (IS_SYNTH, NEEDS_MIDI_INPUT), links
  mwcore + mw_fp_discipline + JUCE modules; FORMATS via `mw_resolve_formats` (Standalone now; AAX excluded).
- `plugin/PluginProcessor.h/.cpp` minimal processor owning `mw::Engine`; the three-call seam;
  minimal APVTS + GenericAudioProcessorEditor; `createPluginFilter()`. `(PI)` event-buffer sizing
  from `mw::cal::host` (centralized).
- Top-level CMake enables C/OBJC/OBJCXX under MW_BUILD_PLUGIN (JUCE needs ObjC on Apple).

## Out of scope

- Full processor 111, APVTS layout 020, MIDI front-end 104, PDC 105, UI 108+, formats 113 — all remain todo.

## Acceptance criteria

- [ ] `cmake -B build/plugin -DMW_BUILD_PLUGIN=ON` configures; the Standalone target builds + links.
- [ ] `mwcore` stays JUCE-free (ADR-001); core/ untouched.
- [ ] `(PI)` event-buffer capacity reads `mw::cal::host::kEventBufferBlockFactor/kEventBufferSlack` (not inlined).

## Verification commands

```
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mwAudio101_Standalone
```
