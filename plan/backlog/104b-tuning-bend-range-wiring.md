<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 104b
title: Tuning + bend-range wiring: A4 440/442 duality, TUNE cents, per-channel + MPE bend ranges, optional MTS-ESP
status: todo
depends-on: [104, 103, 102]
component: app
estimated-size: M
stream: plugin
tag: midi
---

## Objective

Wire MidiFrontEnd::setTuning and setBendRange from the APVTS params so the A4 reference (440 default, 442 hardware-accurate preset duality), the front-panel TUNE (mw101.vco.fine, ±1.0 semitone), and the channel + MPE per-note + MPE master bend ranges all feed the front-end as continuous pre-quantizer offsets, with MTS-ESP honored only if cheap.

## Context

- `docs/design/09 §5` — read first
- `docs/design/09 §4.4` — read first
- `ADR-012 C8` — read first
- `ADR-012 C11` — read first
- `ADR-012 C21` — read first
- `ADR-012 C22` — read first
- `ADR-012 C23` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `midi`.

## Scope

- Bridge params -> MidiFrontEnd::setTuning(a4Hz, tuneCents): mw101.tune.a4 (400..460, default 440) and mw101.vco.fine (±1.0 semitone) read via ParamBridge (102); 442 is recallable via a 'hardware-accurate' preset, never the default (§5; ADR-012 C21-C23)
- Bridge params -> MidiFrontEnd::setBendRange(channelSemis, mpeNoteSemis, mpeMasterSemis): channel ±2 (0..24), MPE per-note ±48 (0..96), MPE master ±48 (0..96); pitch-bend is a continuous offset applied BEFORE quantization (§4.4; ADR-012 C8, C11)
- Per-channel bend (raw MIDI) and per-note/master bend (MPE via 103) both route through the same continuous pre-quantizer pitch-offset path; mono mode collapses MPE to channel bend (§4.4)
- Honor MTS-ESP / MIDI Tuning Standard ONLY if cheap; the master reference remains the single A4 float param — implement as an optional, clearly-gated path that defers to mw101.tune.a4 when absent (§5; ADR-012 §Decision item 7)

## Out of scope

- The 6-bit DAC quantizer itself (lives in core, 070) — this only supplies pre-quantizer offsets
- Authoring the 442 'hardware-accurate' preset content (preset stream)
- The 440-vs-442 UI signpost (129b)
- modern_unquantized toggle behavior beyond reading the flag (already in 104)

## Acceptance criteria

- [ ] setTuning drives A4 from mw101.tune.a4 (default 440, range 400..460) and TUNE from mw101.vco.fine (±1.0 semitone); 442 is reachable only via preset recall, never the default [docs/design/09 §5; ADR-012 C21-C23]
- [ ] setBendRange wires channel ±2 (0..24), MPE per-note ±48 (0..96), MPE master ±48 (0..96), all applied as continuous offsets BEFORE quantization [docs/design/09 §4.4; ADR-012 C8, C11]
- [ ] Channel bend and MPE per-note/master bend share one pre-quantizer offset path; mono mode collapses MPE to channel bend [docs/design/09 §4.4]
- [ ] MTS-ESP is consulted only when present and cheap, otherwise the single A4 float param is authoritative; wiring is RT-safe (AudioThreadGuard, no alloc/lock) [docs/design/09 §5; ADR-012 §Decision item 7]
- [ ] Tests named midi_tuning* are discovered and pass via 'ctest --preset default -R midi_tuning --no-tests=error' [ADR-013 C1]

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R midi --no-tests=error
```
