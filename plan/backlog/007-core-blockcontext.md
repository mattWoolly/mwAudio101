<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 007
title: core/BlockContext.h — POD seam aggregate + views
status: in-review
depends-on: [005]
component: core
estimated-size: S
stream: infra
tag: core
---

## Objective

Author core/BlockContext.h defining the POD seam aggregates: AudioBlockView, TransportInfo, MidiEvent, MidiEventView, and BlockContext, with no JUCE types and no owning allocation.

## Context

- `docs/design/00 §5.3` — read first
- `docs/design/00 §5.2` — read first
- `ADR-001 Decision (processing contract)` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `core`.

## Scope

- AudioBlockView {float* const* channels; int numChannels; int numFrames}
- TransportInfo {double bpm; double ppqPosition; bool isPlaying; double sampleRate}
- MidiEvent POD (type/channel/noteId/data0/value/sampleOffset) == mw::core::MidiEvent placeholder per docs/design/09; MidiEventView non-owning span
- BlockContext aggregate {audio, const ParamSnapshot*, transport, midi}
- all PODs, header-only, namespace mw, no JUCE include

## Out of scope

- the Engine prepare/process/reset surface (engine stream)
- ParamSnapshot field catalogue (param-schema stream owns IDs/fields; this references the type)
- HostEvent->MidiEvent translation in plugin/ (midi-frontend stream, docs/design/09)

## Acceptance criteria

- [ ] BlockContext and members are PODs with no owning allocation and no juce::AudioBuffer/juce::MidiBuffer/APVTS type [docs/design/00 §5.2, §5.3; ADR-001 C14]
- [ ] field shapes match §5.3 verbatim (AudioBlockView/TransportInfo/MidiEvent/MidiEventView) [docs/design/00 §5.3]
- [ ] the header compiles in mwcore with zero JUCE dependency [docs/design/00 §5.2; ADR-001 C1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R core --no-tests=error
```
