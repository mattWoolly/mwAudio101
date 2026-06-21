<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118d
title: Publish the live sequencer step to telemetry (processor) — replace the monotonic display counter
status: todo
depends-on: [118c, 111c, 161, 162]
component: app
estimated-size: S
stream: plugin
tag: gui_datapaths
---

## Objective

Close the 111c QA MEDIUM: the processor publishes `Telemetry::Snapshot.seqStep` as a MONOTONIC
per-block display counter, not the live playhead. Once 118c exposes the real step
(`Engine::currentSeqStep()`), update the processBlock telemetry publish to read+publish the REAL
current sequencer step so the SequencerGrid (126) highlight tracks the actual playing position.

## Scope

- plugin/PluginProcessor.cpp telemetry publish: set Snapshot.seqStep from engine_.currentSeqStep()
  (the 118c accessor) instead of the monotonic counter. RT-safe (a getter read + the existing seqlock
  publish; no alloc/lock).
- Update/extend the gui_datapaths test to assert the published seqStep tracks the engine's live step.

## Out of scope

- The Engine wiring (118c); the SequencerGrid UI (126).

## Acceptance criteria

- [ ] Snapshot.seqStep == the Engine's live current sequencer step (not a monotonic counter) [§8.4]
- [ ] gui_datapaths test asserts the published step tracks the live step; no audio-thread alloc/lock added
- [ ] Built green under MW_BUILD_PLUGIN=ON; no regression

## Addendum (2026-06-20, from 118c QA MEDIUM)

118c's Engine reconstructs the live step via a `seqPlayMirror_` that can DIVERGE from the real playhead: SequencerEngine::processBlock calls seq_.resetToStart() on a keypress when clockResetOnKeypress (the ControlSnapshot DEFAULT=true) rewinds playPos_ inside the SequencerEngine, which the Engine mirror does not observe -> currentSeqStep() reports a stale slot. Therefore 118d MUST surface the REAL slot, not the mirror: add a playPos()/currentSlot() accessor on StepSequencer/SequencerEngine (an 087-surface change) and have the Engine/processor publish THAT, and fix/replace the Engine.h comment that overclaims the mirror as 'the REAL playhead slot'. Add a test that presses a key mid seq-playback with clockResetOnKeypress=true and asserts the published step matches the real rewound slot (the currently-UNTESTED divergence path).

## Addendum 2 (2026-06-20, from 127 QA MEDIUM) — telemetry publish COMPLETENESS

The processBlock telemetry publish (plugin/PluginProcessor.cpp ~L260) fills ONLY vcaLevelL/R + seqStep; the Snapshot fields scope[256] (kScopePoints, kScopeDecimation=8), vcfCutoffDisplay, and lfoPhase stay at zero, so ScopeMeterOverlay (127) renders a FLAT scope + empty cutoff indicator in the real plugin. Extend the publish to fill: (a) scope[256] = a decimated tap of the post-VCA mono/stereo wave for the block (kScopeDecimation); (b) vcfCutoffDisplay = the dispatched/modulated cutoff (available once 161 wires it); (c) lfoPhase = the LFO phase (once 162 wires the LFO). RT-safe (the publish is already on the audio thread; just fill the POD). Add a gui_datapaths/telemetry test asserting these fields are non-zero/advancing when the engine is producing sound. This is why 118d now also depends on 161 + 162.
