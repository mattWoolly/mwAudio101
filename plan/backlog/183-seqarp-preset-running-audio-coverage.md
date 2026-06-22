<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 183
title: SeqArpRiff/Acid preset RUNNING-sequencer audio coverage (validate the now-live seq/arp on the headline presets)
status: done
depends-on: [181, 182, 131]
component: app
estimated-size: M
stream: plugin
tag: preset_seqaudio
---

## Objective

Prove that the headline sequencer/arp presets actually PRODUCE their intended sequenced/arpeggiated
audio now that 181+182 wired the sequencer end-to-end. The existing PresetSeqArpRiffTest validates only
schema/loader round-trip — NOTHING renders these presets with the sequencer RUNNING. This closes that
verification gap (the same "loads but does it actually play?" class the integration epic addressed).

## Context

- `plan/decisions/030-sequencer-arp-engine-integration.md` — the now-live seq/arp path (181 dispatch +
  pattern load; 182 run/hold transport gate + Internal free-run).
- `tests/plugin/SeqRunStateTest.cpp` (182) — the mechanism proof (setSeqPattern + seq.mode=Play +
  setTransportRunning + Internal clock → stepped output). Mirror its drive pattern.
- `tests/plugin/PresetSeqArpRiffTest.cpp` — the existing schema/loader-only coverage (the gap).
- `presets/SeqArpRiff/*.mw101preset` (12) + the seq-using `presets/AcidBassLead/*.mw101preset` (14).
- `plugin/PluginProcessor.{h,cpp}` — load a preset (setCurrentProgram / PresetManager), setTransportRunning,
  processBlock, currentSeqStep accessor.

## Scope

- New `tests/plugin/PresetSeqAudioTest.cpp` (tag preset_seqaudio). For each SeqArpRiff preset (and the
  seq-using AcidBassLead presets that ship seq.mode=Play with a non-empty pattern):
  - Load the preset through the real processor program/preset path so its params + seq pattern apply.
  - Engage transport: setTransportRunning(true) (Internal clock; isPlaying=false Standalone case).
  - Render several clock periods; ASSERT objective health + that the sequencer is ALIVE:
    * no NaN/Inf; output is non-silent (RMS > floor); no hard clip (peak bounded).
    * `currentSeqStep()` ADVANCES across the render (the pattern actually steps) for presets whose
      seq.mode=Play with count>0; for arp-only presets, the arp engages and the held-note arpeggiates.
    * the rendered audio VARIES over the pattern (a stepped/arpeggiated contour, not a static drone) —
      e.g. windowed-RMS or pitch variance across steps exceeds a static-render baseline.
  - For presets NOT using the seq/arp (or seq.mode=Off), skip the stepping assertion but keep the health check.
- Keep it data-driven over the bundled corpus (enumerate the BinaryData/preset dir), so new presets are
  auto-covered.

## Out of scope

- Subjective "is it amazing" judgement (not automatable). Re-authoring/tuning presets.
- The LFO multi-dest preset re-audit (180 already landed; existing per-category render tests pass).
- Changing any preset file or engine code (this is added COVERAGE, read-only on production).

## Acceptance criteria

- [ ] preset_seqaudio renders each SeqArpRiff (+ seq-using Acid) preset with the sequencer RUNNING and asserts it steps + produces healthy, varying audio (not silent, not static, no NaN/clip)
- [ ] Data-driven over the bundled corpus (new presets auto-covered); non-vacuous (a static/silent render fails the stepping/variance assertion)
- [ ] No production code or preset files changed; full plugin suite green; verify 'ctest --test-dir build/plugin -R preset_seqaudio --no-tests=error'

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R preset_seqaudio --no-tests=error --output-on-failure
ctest --test-dir build/plugin --no-tests=error
```
