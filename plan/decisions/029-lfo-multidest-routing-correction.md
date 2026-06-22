<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR-029 — LFO multi-destination routing correction (restore ADR-007 topology)

- Status: accepted
- Date: 2026-06-21
- Deciders: orchestrator (deviation-correction; conforms to the already-ratified ADR-007)
- Supersedes: the undocumented single-destination gating introduced in task 162 dispatch

## Context

The Phase-5 audit (task 154, docs/QA-REPORT.md §4.5, finding **QA-154-3 / HIGH**) confirmed a
circuit-fidelity **regression** in the as-built LFO routing, evidence-cited and unambiguous:

- **ADR-007 §Decision item 1 (ratified topology):** one LFO scales the *same instantaneous
  selected-LFO value* into **THREE FIXED DESTINATIONS with per-destination depth gains** — VCO pitch
  (MOD depth), PWM (own source switch + PWM depth), VCF cutoff (own MOD depth). There is **no
  `lfo.dest` mux** in the ratified topology.
- **docs/design/05 §3.3** routing table marks LFO→VCO pitch = "always" and LFO→VCF cutoff = "always".
- **docs/design/03 §3.6:** "Routing is fixed with per-destination depths, NOT a matrix … the LFO
  value feeds VCO pitch … pulse width … VCF cutoff … VCA/gate tremolo."
- **docs/design/06 ~L381:** `lfo.dest` "selects which destination the single hardware LFO routing
  **EMPHASIZES**" — an emphasis selector, NOT an exclusive mux.

The as-built dispatch contradicts all four sources: `core/voice/Voice.cpp ~L252-256` is a
`switch (c.lfoDest) { case 1: cutoff; case 2: pwm; default: pitch; }` that applies **exactly one**
leg per control tick and zeroes the other two. Its justifying comment
(`ControlDispatchLfoConstants.h L26-31`, `Voice.cpp L247`) **mis-cites** docs/design/03 §3.2 — which
governs the LFO *waveform* being single-selection ("one waveform active at a time, not simultaneous
outputs"), and says nothing about *destinations*. No ADR ratifies single-destination gating. The
sibling `vcf.lfo_mod` always-on leg (162e, `Voice.cpp L264`, applied OUTSIDE the switch) is correct
and is the template.

The real SH-101 LFO modulates VCO pitch, PWM, and VCF cutoff **simultaneously**, each by its own
front-panel depth. Single-dest gating makes two of the three depth knobs inert at any moment — an
audible fidelity defect in the product's core value proposition.

## Decision

1. **Restore always-active multi-destination routing** per ADR-007: every control tick, the single
   selected LFO value is applied to **all three** legs simultaneously — VCO pitch (× `lfo.depth_pitch`),
   PWM (× `lfo.depth_pwm`), VCF cutoff (× `lfo.depth_cutoff`) — each scaled by its own depth, mirroring
   how `vcf.lfo_mod` is already applied unconditionally. Delete the single-leg `switch (lfoDest)`.
2. **Reinterpret `lfo.dest` as a (PI) emphasis gain, not a mux.** Per docs/design/06 L381 it
   *emphasizes* a destination. The selected destination receives a modest emphasis multiplier on its
   depth (a documented (PI) constant in Calibration.h, e.g. selected = ×1.0, unselected = ×1.0 by
   default so the control is non-destructive until a deliberate emphasis curve is ratified) — the
   exact emphasis curve is a (PI) and MUST centralize in core/calibration/. `lfo.dest` remains a live
   param (the 91-param count and the UI selector are unchanged); only its *semantics* change from
   exclusive-mux to emphasis. The default behavior must be all-three-active.
3. **Add the missing simultaneity coverage:** a `[dispatch_lfo]` test that drives a non-zero
   `lfo.depth_pitch` AND `lfo.depth_pwm` AND `lfo.depth_cutoff` at once and measures that **all three**
   destinations move in the output (Goertzel/measured), failing against the old single-leg switch.

## Consequences

- Restores documented circuit behavior; closes QA-154-3.
- **Preset impact:** any factory preset that set `lfo.dest` expecting exclusive-mux behavior will now
  hear all three legs. Presets must be re-validated after this lands (tracked separately) — they
  should have been authored against the correct always-active behavior regardless.
- Realized by **task 180**. Full core-suite gate (core DSP change). The exact emphasis (PI) curve, if
  any beyond unity, is recorded in Calibration.h with provenance.
