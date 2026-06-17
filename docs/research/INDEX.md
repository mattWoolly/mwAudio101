<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Research index (Phase 1)

Cited, adversarially-verified research — the "trace-or-deviate" source of truth every downstream
technical claim points at. Produced by an 18-dimension fan-out (round 1 + gap-fill round 1b),
each claim verified by an adversarial checker and a completeness critic.
**Overall confidence: medium** — the circuit spine is solid and well-sourced, but several facts
remain disputed or rest on clone-derived / reverse-engineered / unmeasured data. Those are
catalogued, not hidden — read **[13](13-validation-gaps-and-disputes.md) first**.

> **Honesty stance (locked decision 1.7):** mwAudio101 is "modeled from *documented* circuit
> behavior." We have no physical-unit measurements; recordings/sample packs are a secondary,
> local-only cross-check, never the calibration oracle. Anything needing bench data is a labelled
> validation gap (see doc 13 §5), not a delivered fact.

## Documents

| # | Doc | Covers |
|---|-----|--------|
| 01 | [architecture-signal-flow](01-architecture-signal-flow.md) | Block diagram, panel sections, **frozen IC table** (CEM3340=IC13, IR3109=IC14, CPU=IC6) |
| 02 | [vco-suboscillator-noise](02-vco-suboscillator-noise.md) | VCO (CEM3340), divider sub-osc (4013), noise (2SC945) |
| 03 | [filter-ir3109](03-filter-ir3109.md) | **Tone-defining** 4-pole OTA LPF: cutoff/CV, diode-clipped resonance, self-osc, clone-vs-original figures |
| 04 | [envelope-lfo-vca](04-envelope-lfo-vca.md) | ADSR, LFO (tri/sq/random/noise — **no sine**), BA662 VCA |
| 05 | [mixer-modulation-glide](05-mixer-modulation-glide.md) | Source mixer, MGS-1 bender, portamento |
| 06 | [arpeggiator-sequencer](06-arpeggiator-sequencer.md) | Arp modes, 100-step sequencer, clock/sync |
| 07 | [cpu-key-assigner](07-cpu-key-assigner.md) | TMP80C49 firmware: note priority, retrigger, DAC mux (behavioral model) |
| 08 | [power-cv-io](08-power-cv-io.md) | Full I/O inventory, CV/Gate, calibration trimmers, rails (**no ext-audio, no MIDI**) |
| 09 | [vintage-variance-drift](09-vintage-variance-drift.md) | Trimmers/tolerances/tempco → proposed drift/variance control set |
| 10 | [dsp-modeling-techniques](10-dsp-modeling-techniques.md) | **Adopted DSP theory**: PolyBLEP/DPW, ZDF-TPT/Huovilainen ladder, tanh OTA, oversampling |
| 11 | [cultural-influence](11-cultural-influence.md) | IDM/acid/techno use + sound-design idioms (feeds presets) — marketing-soft |
| 12 | [market-legal-landscape](12-market-legal-landscape.md) | Competitors + trademark/trade-dress (validates trademark-distance) |
| 13 | [validation-gaps-and-disputes](13-validation-gaps-and-disputes.md) | **Honesty ledger** — frozen / disputed / hardware-gap / clone-derived / cultural cautions |

## What hardened, what stays open

**Frozen (cite, don't re-debate):** signal path VCO+sub+noise → mixer → IR3109 VCF → BA662 VCA →
out; IC designators; Gate ON = 12 V (service manual); TH1 in VCF cutoff-CV path; VCF caps 240 pF /
Juno-6/60 topology / diode-clipped resonance; LFO has no sine; footages 16'/8'/4'/2'; lowest-note
(GATE) vs last-note (GATE+TRIG) priority.

**Open / labelled (doc 13):** ADSR & filter response curves (no bench data); IR3109 electrical
figures (AS3109 clone-derived); BA662 internals (reverse-engineered); rail-regulator topology;
sequencer byte format (community disassembly); cultural attributions (hedge/drop track-level
Aphex & Vince Clarke; TB-303 does **not** use the IR3109).
