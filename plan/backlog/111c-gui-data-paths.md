<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 111c
title: Processor GUI data-paths — publish telemetry from processBlock + expose Consumer + <extras> seq-pattern handoff accessor
status: done
depends-on: [111, 107, 087, 017]
component: app
estimated-size: M
stream: plugin
tag: gui_datapaths
---

## Objective

Close the orphaned processor-side GUI-support gap that 115/126/127 all depend on but no task owns:
the MwAudioProcessor must (a) PUBLISH a Telemetry::Snapshot from the audio thread each block via the
107 Producer (RT-safe), (b) EXPOSE a Telemetry::Consumer the editor's Timer drains, and (c) EXPOSE a
message-thread `<extras>` seq-pattern editing accessor that hands the edited 100-step pattern to the
audio thread via an RT-safe SPSC/atomic-swap handoff. 107 built the SPSC TYPES but declared the
production/consumption WIRING out of scope; 111 wired the audio seam but not telemetry. This is that
wiring — the single owner of these PluginProcessor edits so 115/126/127 stay pure consumers.

## Context

- `core/ui/Telemetry.h` (107, done) — Snapshot POD + Buffer + Producer + Consumer (seqlock SPSC).
- `core/state/Extras.h` (017, done) — the SeqStep[kMaxSeqSteps=100] pattern POD (note/gate/tie/rest,
  NO accent per ADR-025).
- `core/seq/SequencerEngine.h` (087, done) — publishes an atomically-swapped ControlSnapshot incl.
  the current seq step; surface the step into the Telemetry Snapshot.
- `plugin/PluginProcessor.{h,cpp}` (111/136, done) — the processBlock you publish from + the
  message-thread surface you expose accessors on.
- `docs/design/10-ui.md §8.3/§8.4` (telemetry frame + Timer consumer), `§9.3` (seq pattern editing),
  `docs/design/00 §5.4` (snapshot() is pure arithmetic, audio-thread-safe), ADR-015 C5.

## Scope

- Construct a Telemetry::Buffer member (off the audio thread, in prepare); in processBlock, after the
  engine processes, PUBLISH one Snapshot via the Producer (seqStep from the SequencerEngine control
  snapshot + any meter/scope level the design names) — RT-safe: NO heap alloc, NO lock, just the
  seqlock publish. Expose a `telemetryConsumer()` (or getSnapshotConsumer()) accessor returning a
  Consumer view for the editor's Timer.
- Expose message-thread `<extras>` seq-pattern accessors: read the current SeqStep[100] pattern and
  write an edited pattern, handing the new pattern to the audio thread via an RT-safe handoff
  (atomic-pointer swap / SPSC — the audio thread never parses; it only adopts a published POD). The
  pattern also round-trips through capture/recoverState `<extras>` (already wired) — keep that intact.
- Keep the existing 111/136 wiring + 114 state persistence intact (additive edits only).

## Out of scope

- The editor Timer itself (115), the SequencerGrid view (126), the ScopeMeterOverlay (127) — they
  CONSUME these accessors; do NOT implement them here.
- The SequencerEngine internals (087); the Telemetry types (107).

## Acceptance criteria

- [ ] processBlock publishes a Telemetry::Snapshot every block via the 107 Producer with NO heap alloc / NO lock (re-assert with an mstats/no-alloc probe over many publishing blocks) [§8.3; §5.4; ADR-001 C3/C4; ADR-015 C5]
- [ ] A Consumer accessor returns coalesced most-recent frames (seqStep present); a test drives processBlock then pulls a Snapshot and sees the advancing step [§8.4]
- [ ] A message-thread seq-pattern accessor reads + writes the `<extras>` SeqStep[100] pattern; an edit is adopted by the audio thread via the RT-safe handoff without an audio-thread parse/alloc, and still round-trips through capture/recoverState [§9.3; §5.4]
- [ ] gui_datapaths-tagged tests cover all three; built green under MW_BUILD_PLUGIN=ON; no regression to processor/wrappers/serializer suites

## Verification commands

```
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R "gui_datapaths|processor|serializer" --no-tests=error
```
