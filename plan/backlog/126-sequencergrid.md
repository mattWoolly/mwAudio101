<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 126
title: SequencerGrid (100-step pattern editor view)
status: todo
depends-on: [020, 111, 087, 006, 109, 107, 125, 111c, 118c, 118d]
component: ui
estimated-size: M
stream: ui
tag: ui_seqgrid
---

## Objective

Implement the 100-step sequencer grid editor that edits the non-APVTS <extras> pattern via the processor accessor and reflects the current step from telemetry, with per-cell dirty-rect repaint.

## Context

- `docs/design/10-ui.md §5.1` — read first
- `docs/design/10-ui.md §5.3` — read first
- `docs/design/10-ui.md §9.3` — read first
- `ADR-015 C3` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui_seqgrid`.

## Scope

- ui/modules/SequencerGrid.h/.cpp
- 100-step grid editing pattern data via the processor's <extras> SPSC handoff accessor
- Current-step highlight driven by telemetry Snapshot.seqStep (display only)
- Per-cell dirty-rect repaint (no whole-editor repaint)
- layoutDesignUnits()

## Out of scope

- arp/seq engine semantics (mod-arp-seq stream)
- transport mode controls (ui-14)

## Acceptance criteria

- [ ] Pattern edits route through the processor <extras> handoff, not a tree pointer to audio (§9.3, ADR-008 C19/C20)
- [ ] Current step is read display-only from telemetry, never by polling the engine (§8.2/§8.3, ADR-015 C4)
- [ ] Per-cell repaint invalidates only that cell, never whole-editor (§5.3/§7.3, ADR-015 C7)
- [ ] Tests named ui_seqgrid* assert pattern edit handoff and dirty-rect scope; verify is 'ctest --preset default -R ui_seqgrid --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui_seqgrid --no-tests=error
```
