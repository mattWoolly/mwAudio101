<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118d
title: Publish the live sequencer step to telemetry (processor) — replace the monotonic display counter
status: todo
depends-on: [118c, 111c]
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
