<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 023b
title: Persist CC-learn bindings in plugin state (extend Extras + StateSerializer round-trip)
status: in-review
depends-on: [023, 100, 017]
component: app
estimated-size: S
stream: plugin
tag: serializer
---

## Objective

User MIDI-learn (CC-learn) bindings survive save/reload: the StateSerializer round-trips the
non-default CcLearnMap bindings in `<extras>` bit-for-bit, closing the wave-J5 QA MEDIUM (PR #106)
where state captured seq/arp/drift but NOT CC-learn (the Extras POD/017 omitted it and CcLearnMap
was never passed to captureState).

## Context

- Originating finding: 023 QA (PR #106) — design 06 §5.4/§5.5 expect CC-learn in the `<extras>`
  round-trip, but Extras (017) declared it out-of-scope and StateSerializer never receives the
  CcLearnMap (100). Real for a market plugin (MIDI-learn should persist).
- Verified facts: `plugin/state/StateSerializer.{h,cpp}` (capture/readFromBlob `<extras>` block),
  `plugin/midi/CcLearnMap.{h,cpp}` (the bindings, default seed table), `core/.../Extras.h` (the POD).
  Design: `docs/design/06 §5.4/§5.5`; ADR-008 C8; ADR-012 C16.
- TDD: write the failing test first (`tests/plugin/`, names begin `serializer`): set a non-default
  CC->param binding, capture→blob→read, assert the binding restores (and defaults stay default).

## Scope

- Carry the non-default CcLearnMap bindings through the StateSerializer `<extras>` (e.g. a `<ccLearn>`
  child of {cc, paramIndex} pairs for non-default entries). Add a captureState overload/param to
  receive the CcLearnMap (or its serializable snapshot) on the message thread. readFromBlob restores
  them; absent/garbage → defaults (ADR-021 fallback). Only NON-default bindings are written (compact).

## Out of scope

- CcLearnMap internals (100); the live MIDI-learn UI; processor wiring of getStateInformation (111).

## Acceptance criteria

- [ ] A non-default CC->param binding round-trips through capture→blob→read bit-for-bit; defaults restore to the seed table [§5.4; ADR-012 C16].
- [ ] Corrupt/absent `<ccLearn>` falls back to the default map without failing the whole load [ADR-021].
- [ ] `serializer`-tagged tests cover both; build green under MW_BUILD_PLUGIN=ON.

## Verification commands

```
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R serializer --no-tests=error
```
