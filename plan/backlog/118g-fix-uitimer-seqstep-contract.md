<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 118g
title: Fix MwAudioEditorTimerTest stale seqStep contract (118d fallout) — coalescing proof via a live-valid discriminator
status: in-review
depends-on: [118d, 115]
component: app
estimated-size: S
stream: plugin
tag: ui_timer
---

## Objective

118d changed `Telemetry::Snapshot.seqStep` from the OLD monotonic per-block display counter to
the REAL live sequencer slot (`engine_.currentSeqStep()`, which is the `-1` sentinel — widened to
`0xFFFFFFFFFFFFFFFF` in the uint64 field — when the sequencer is not playing). 118d correctly
migrated the 111c `GuiDataPathsTest` to the new contract but MISSED a sibling test under a
different tag: `tests/plugin/MwAudioEditorTimerTest.cpp` (task 115, tag `ui_timer`) still asserts
`editor->lastSnapshotForTest().seqStep == 3` (line 87), using the now-removed monotonic counter as
the discriminator that proves the coalescing Timer pull jumped to the MOST-RECENT of three
published frames. With empty (non-playing) processBlocks, seqStep is now the `-1` sentinel for all
three frames, so the assertion fails and the FULL plugin suite is red on main (#135).

## Context

- `tests/plugin/MwAudioEditorTimerTest.cpp` (lines 75-87 + the stale comment at line 76) — the fix site.
- `tests/plugin/GuiDataPathsTest.cpp` (lines 112-129) — how 118d migrated the SIBLING test to the
  live-step contract (the precedent to mirror).
- `tests/plugin/TelemetryDatapathsTest.cpp` (A3, line ~279) — 118d's own live-step assertion.
- `tests/unit/TelemetryTest.cpp` (line 99, `out.seqStep == 3u`) — the RING-LEVEL coalescing unit
  test; it sets seqStep as an ARBITRARY tag payload to test the SPSC ring and is CORRECT — DO NOT
  touch it. (It is core/JUCE-free and passes.)
- `core/Engine.{h,cpp}` — `currentSeqStep()` (live slot) + the lfoPhase accumulator; read to choose
  the discriminator (see below).
- ADR-015 C5/C7 — the single coalescing Timer + targeted-repaint contract this test guards.

## Scope

- `tests/plugin/MwAudioEditorTimerTest.cpp` ONLY (test-only contract migration; production behavior
  is correct per 118d).
- Replace the `seqStep == 3` monotonic-counter discriminator (line 87) + fix the stale line-76
  comment, so the test still PROVES the coalescing pull lands on the MOST-RECENT published frame,
  using a discriminator VALID under the post-118d real path.

## How to fix (preserve the coalescing-to-newest INTENT — do not just delete the assertion)

Pick the cleanest deterministic discriminator that advances through the REAL processBlock path
(the test's design note forbids a test-only producer poke):
  - PREFERRED if it free-runs on empty blocks: `Snapshot.lfoPhase` (the engine's LFO accumulator
    advances each control tick from the default lfoRateHz regardless of notes). VERIFY by reading
    the Engine lfoPhase advance path AND empirically (print/observe across single-drained publishes).
    If it advances monotonically on empty blocks: after draining N total published frames the newest
    frame's lfoPhase is deterministic; assert the coalesced pull (3 publishes, 1 drain) lands on the
    value advanced by the FULL batch (newest), and that a single-frame publish+drain advances by
    exactly one step — proving the coalescing jumped to newest, not the oldest of the batch.
  - ELSE (if no field advances on silent blocks): drive the sequencer so it is genuinely PLAYING
    (transport/run state + a pattern) so `seqStep` advances through real slots 0,1,2..., then assert
    the coalesced frame carries the NEWEST real slot — the most faithful match to the original intent
    AND a real exercise of 118d's live-playhead path.
- KEEP the existing coalescing proofs: 3 publishes -> exactly +1 scope repaint (line 85), whole-editor
  repaint count == 0 (line 86, C7), and the second-tick no-op (lines 96-98). The other four ui_timer
  cases (reduce-motion etc.) are unaffected — keep them green.
- Prove NON-VACUITY: the new assertion must FAIL if the pull coalesced to the OLDEST frame instead of
  the newest (note the mutation in the PR).

## Out of scope

- Any production code change (118d's live-step seqStep is the intended contract).
- `tests/unit/TelemetryTest.cpp` (the valid ring-level coalescing test).
- The other ui_timer cases' behavior.

## Acceptance criteria

- [ ] `MwAudioEditorTimerTest.cpp` proves the coalescing Timer pull lands on the MOST-RECENT frame
      using a post-118d-valid discriminator; the stale `seqStep == 3` assertion + line-76 comment are gone
- [ ] Non-vacuous: the new assertion fails if the pull lands on the oldest of the batch (mutation noted)
- [ ] `ctest --test-dir build/plugin -R ui_timer --no-tests=error` is all-green (5/5)
- [ ] The FULL plugin suite is green (excluding the core-binary/configure-time entries that are
      Not-Run only because this is a plugin-targets build); paste real output
- [ ] No production code touched; `tests/unit/TelemetryTest.cpp` untouched

## Verification commands

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R ui_timer --no-tests=error --output-on-failure
ctest --test-dir build/plugin --no-tests=error
```
