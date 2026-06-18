<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 070
title: ControlCore pitch assembly — 6-bit integer DAC-count pitch (VINTAGE quantization)
status: in-review
depends-on: [001, 006, 007, 067]
component: core
estimated-size: S
stream: voice-control
tag: pitchcounts
---

## Objective

Implement the pure static VINTAGE pitch pipeline: assemblePitchCounts (key + range base + octave + key-shift as integer DAC counts) and countsToVolts (1 count = 1 semitone, 12 counts/octave), plus the 6-bit DAC/4052 route-bit model constants.

## Context

- `docs/design/04-voice-and-control.md §7.2` — read first
- `docs/design/04-voice-and-control.md §7.3` — read first
- `ADR-005 §Decision item 1` — read first
- `ADR-005 §Decision item 2` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `pitchcounts`.

## Scope

- static int assemblePitchCounts(midiNote,rangeBase,octaveOffset,keyShift) and static float countsToVolts(counts) per §7.8 signatures, placed in ControlCore.h/.cpp
- range bases 16'/8'/4'/2' = 12/24/36/48 counts (1/2/3/4 V); octave offsets -12/mid/+12 (§7.3 tables)
- D7:D6 route enum/constants 00=CVOUT,01=VCO,10=RANDOM,11=parked (§7.2)
- counts assembled as integers; conversion to volts only at the S/H boundary (§7.3)

## Out of scope

- the control-tick advance loop, jitter, MODERN tick, auto-engage, crossfade (voice-control-8)
- the RANDOM regeneration / clock edge timing (control-tick task / arp ADR)
- MIDI-note origin (provided by caller)

## Acceptance criteria

- [ ] TDD: ranges land exactly 12 counts apart; 16'/8'/4'/2' bases = 12/24/36/48 counts == 1/2/3/4 V via countsToVolts (§7.3 tables; ADR-005 §Decision item 2)
- [ ] 1 count == 1 semitone and 12 counts == 1 octave; octave offsets add -12/0/+12 and keyShift adds verbatim (§7.3)
- [ ] assemblePitchCounts/countsToVolts are pure static noexcept functions, integer-count domain before volts (§7.3; ADR-005 §Decision item 1)
- [ ] route-bit constants match the §7.2 D7:D6 table (00/01/10/11)
- [ ] test names begin with the tag; verify: ctest --preset default -R pitchcounts --no-tests=error

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R pitchcounts --no-tests=error
```
