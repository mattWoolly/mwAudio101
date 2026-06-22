<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 185
title: setCurrentProgram must publish the recovered <extras> seq pattern (preset recall plays its riff)
status: done
depends-on: [181, 182, 183]
component: app
estimated-size: S
stream: plugin
tag: preset_recall_seq
---

## Objective

Fix a confirmed production defect on the preset-recall path: `MwAudioProcessor::setCurrentProgram`
recovers a preset's 91 params into APVTS AND fills the local `<extras>` POD with the preset's 100-step
`<seq>` pattern (via `PresetManager::loadPreset`), then DISCARDS that pattern. It never assigns
`seqPattern_` nor calls `seqPatternHandoff_.publish()`. So a host MIDI Program-Change recall
(`handleAsyncUpdate -> setCurrentProgram`) OR a PresetBrowser selection of a SeqArpRiff preset applies
`seq.mode=Play` but leaves the engine's StepSequencer pattern EMPTY (`count_==0`) — the headline riff
DOES NOT PLAY. `setStateInformation` already publishes (it parses the tree via `readSeqPattern` and
calls `seqPatternHandoff_.publish`), so session reload works; Program-Change / browser recall does not.

Found by task 183's preset-audio coverage (PresetSeqAudioTest's file header records the gap and works
around it with an explicit `setSeqPattern(extras)` in the test).

## Fix (mirror the state-restore path; minimal + RT-safe)

In `setCurrentProgram`, after `loadPreset` fills `extras`, adopt + publish it exactly like
`setStateInformation`:

```
seqPattern_ = extras;                     // the recovered <extras> pattern becomes the canonical copy
seqPatternHandoff_.publish(seqPattern_);  // RT-safe message-thread publish; audio thread adopts next block
```

`loadPreset` already populated `extras` with the pattern POD, so no re-parse is needed (unlike
`setStateInformation`, which parses the tree via `readSeqPattern`). The publish is the SAME lock-free
`SeqPatternHandoff` path the editor's `setSeqPattern` and `setStateInformation` use. No change to
`loadPreset`, the handoff, or any other recall behavior. The out-of-range / empty-bank no-op guards stay
intact. This also fixes the MIDI Program-Change path (`handleAsyncUpdate -> setCurrentProgram`) for free.

## TDD — failing test first

`tests/plugin/PresetRecallSeqTest.cpp` (case names begin `preset_recall_seq`, tag `preset_recall_seq`).
The PRODUCTION-PATH test constructs a real `MwAudioProcessor`, calls `setCurrentProgram(index)` for a
SeqArpRiff `seq.mode=Play` preset with a non-empty pattern WITHOUT any `setSeqPattern` workaround, drives
a PLAYING host playhead with advancing PPQ (the shipped corpus is HostSync), renders the real
`processBlock` over several clock periods, and asserts the engine sequencer pattern loaded (`count>0` via
`currentSeqStep>=1`) AND the audio is healthy + time-varying. Non-vacuity: this fails against the pre-fix
`setCurrentProgram` (pattern discarded -> never steps, silent/static). The MIDI Program-Change route
shares `setCurrentProgram`, so the fix covers it; a case also drives the AsyncUpdater handoff explicitly.

## Out of scope

Core, the handoff, `loadPreset`, and every other recall behavior. The only production change is the
two-line publish in `setCurrentProgram`.
