<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Research (`docs/research/`)

Phase-1 deliverable. Cited, adversarially-verified research that every downstream technical claim
traces to (the "trace-or-deviate" source of truth). Two tracks:

- **Technical circuit** — VCO core & exponential converter, IR3109 4-pole OTA lowpass, sub-osc
  divider, noise, source mixer, ADSR envelope, LFO, VCA, arpeggiator, 100-step sequencer, glide
  /portamento, modulation/bender, power/CV interfacing, panel control ranges.
- **Cultural influence** — the SH-101 in IDM (Aphex Twin et al.), acid, and electronic music;
  notable records, artists, and sound-design idioms that inform preset design.

## Conventions

- Every doc starts with the SPDX/license header and is markdown-linted.
- Number every section so it can be cited as a stable address (`§3.2`).
- Each substantive claim carries an inline source citation. Claims that could not be verified are
  fenced in a clearly-labelled "Unverified / disputed" subsection — never stated as fact.
- Runnable/binary references live in the gitignored `research-cache/` (never committed,
  never the calibration oracle — see `plan/ORCHESTRATION.md` decision on the calibration oracle).

> **Status:** populated in Phase 1 (deep research).
