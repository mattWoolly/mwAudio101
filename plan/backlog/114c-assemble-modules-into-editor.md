<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 114c
title: Assemble all modules + components into MwAudioEditor (the UI integration that makes the panel real)
status: done
depends-on: [114, 116, 117, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129]
component: ui
estimated-size: M
stream: ui
tag: ui_assembly
---

## Objective

MwAudioEditor (114) is intentionally a SHELL: it owns the design->pixels AffineTransform + the
geometry seam but instantiates NO modules (its header: "the modules will later lay out into" the
seam). This task is that integration — instantiate every panel module + component as MwAudioEditor
members, addAndMakeVisible them, and position each in DESIGN UNITS per docs/design/10 §4/§5 so the
plugin actually shows its full SH-101-inspired panel. Without this, the editor is blank.

## Context

- `docs/design/10-ui.md §4` (1000x640 layout) and `§5` (module placement map) — read first.
- The modules/components to place (all merged before this runs): ModulatorModule (117), VcoModule
  (120), SourceMixerModule (121), VcfModule (122), VcaModule (123), ControllerStrip (124),
  TransportModeBar (125), SequencerGrid (126), BackgroundLayer (116), ScopeMeterOverlay (127),
  PresetBrowser (128), StatusBanner (129).
- `ui/MwAudioEditor.h` / `plugin/ui/MwAudioEditor.cpp` (the shell + the seam you assemble into).
- The processor accessors: apvts() (for the modules' attachments), the PresetManager (for 128),
  the load-failure/disclaimer surface (for 129).

## Scope

- Add the modules/components as MwAudioEditor members (ctor: construct with apvts()/PresetManager
  as needed; addAndMakeVisible). BackgroundLayer is the bottom cached layer; ScopeMeterOverlay sits
  on top; PresetBrowser + StatusBanner per §9.
- resized(): position every member in DESIGN UNITS via each module's layoutDesignUnits(), driven by
  the §5 placement map; all geometry from a NEW core/calibration/EditorLayoutConstants.h (no inlined
  magic numbers).
- Wire the TransportModeBar (125) non-APVTS seams: onScalePresetSelected -> the editor's scale snap
  (114); onReduceMotionChanged -> the telemetry Timer (115) suppression; onRunStateChanged -> the
  transport/arp run state.
- Keep paint() background-only (BackgroundLayer does the chrome); no DSP in the editor.

## Out of scope

- Module internals (each is its own task); the telemetry Timer behavior (115) and OpenGL hatch
  (130) — those EDIT MwAudioEditor too and are sequenced separately (serialize editor edits).
- Authoring the disclaimer string (legal); the scope-overlay data feed internals (115/telemetry).

## Acceptance criteria

- [ ] MwAudioEditor instantiates + addAndMakeVisible's all 12 modules/components and positions them per the §4/§5 design map in design units (geometry from a calibration header) [§4; §5]
- [ ] The TransportModeBar seams are wired (scale-preset -> editor snap; reduce-motion -> Timer; run/hold -> transport) [§5.3; ADR-015 C2/C8]
- [ ] A ui_assembly test constructs the editor against a real processor (ScopedJuceInitialiser_GUI), asserts every expected child is present + within bounds, and that a resize re-lays-out without overlap/out-of-bounds
- [ ] Built green under MW_BUILD_PLUGIN=ON; paint() stays background-only; no shared file edited beyond MwAudioEditor.{h,cpp} + the new calibration header + test

## Verification commands

```
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R ui_assembly --no-tests=error
```
