<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 086
title: Clock: single H->L edge node, 3 sources, swing, keypress reset
status: todo
depends-on: [001, 006, 007, 081]
component: core
estimated-size: M
stream: mod-arp-seq
tag: clock
---

## Objective

Implement Clock: one H->L edge detector behind three mutually-exclusive sources (Internal LFO 0.1-30Hz, HostSync from absolute PPQ, Ext pulses), placing sample-accurate edges into a pre-sized span, with host-only swing and keypress re-phase.

## Context

- `docs/design/05 §7.1` — read first
- `docs/design/05 §7.2` — read first
- `docs/design/05 §7.3` — read first
- `docs/design/05 §7.4` — read first
- `docs/design/05 §7.5` — read first
- `docs/design/05 §7.6` — read first
- `docs/design/05 §7.7` — read first
- `docs/design/05 §7.8` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `clock`.

## Scope

- core/control/Clock.h/.cpp per §7.7 signature; setSource/setInternalRateHz/setHostRate/setSwing/setClockResetOnKeypress/resetToKeypress
- renderEdges fills caller's pre-sized span (no alloc) for all three sources; HostSync derives next-boundary from absolute PPQ each block, 1 pulse=1 step (§7.4); Ext maps supplied pulse offsets (§7.2)
- HostRate->quarter-note mapping table from §7.8; RATE drives tempo only under Internal (§7.2,§7.3)
- Swing offset = kSwingTaper(s) on even step boundaries, host-sync only, inert under Internal/Ext (§7.6); resetToKeypress re-phases when reset-on-keypress enabled (§7.5)

## Out of scope

- LFO core waveform generation (Internal edges consume LFO core)
- Reading juce::AudioPlayHead (plugin layer fills TransportInfo POD)
- Knob-to-frequency RATE taper (owned by param-schema/LFO)

## Acceptance criteria

- [ ] clock test: under HostSync, host edges derived from absolute PPQ; tempo change/loop wrap/scrub re-derive next edge with no cumulative drift; 1 host pulse = 1 step (§7.4 / C19)
- [ ] clock test: each HostRate maps to its quarter-note period per §7.8; under HostSync/Ext RATE does not change step tempo, under Internal RATE sets 0.1-30Hz tempo (§7.8,§7.2 / C18,C21,C23)
- [ ] clock test: swing 50%=no offset, 75%=half-step offset on even steps, inert under Internal/Ext, default 50% (§7.6 / C24)
- [ ] clock test: resetToKeypress re-phases edges to the keypress sample, default-on, togglable off (§7.5 / C22); renderEdges does no heap alloc under sentinel (§7.7)
- [ ] ctest --preset default -R clock --no-tests=error passes

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R clock --no-tests=error
```
