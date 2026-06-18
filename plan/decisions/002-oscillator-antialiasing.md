<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 002: Anti-aliased oscillator generation (saw / pulse / PWM / sub)

Status: accepted (minBLEP HQ tier registered under the single Quality param by ADR-018)
*Refined post-acceptance — see ADR-018.*
Date: 2026-06-17

## Context

The VCO section (modeled CEM3340) emits a sawtooth and a variable-width pulse
(PWM, ~50% down to a practical ~5% minimum), and the 4013-derived sub-oscillator
emits a -1 octave square, a -2 octave square, and a -2 octave 25%/75% pulse
formed by a diode-OR of the two squares (docs/research/02-vco-suboscillator-noise.md
§2.3, §2.5, §3.4, §3.5, §7.2). All four shapes are hard-edged: their sonic
identity lives in the discontinuities and the resulting harmonic combs. We must
pick an anti-aliasing strategy that suppresses aliases across the full keyboard
(including 2'-footage and transpose-high playing) while staying within the
real-time and CPU budget at poly/unison scale.

Candidate methods (docs/research/10-dsp-modeling-techniques.md §2): PolyBLEP
(closed-form, table-free, no oversampling, ~2135 Hz aliasing-free limit at
44.1 kHz, doc 10 §8 / Table VIII), BLEP/minBLEP residual table (64x Blackman,
deeper stopband, frequency-dependent cost, doc 10 §2.1-2.2 / §5.1), DPW
(cheapest but aliasing worsens at high fundamentals and needs its own
oversampling, doc 10 §2.5), and wavetable.

Owner-locked decisions this touches, re-affirmed (not reversed):

- Circuit-accurate analog modeling from documented circuit behavior, with NO
  physical-unit oracle. The 25% sub-pulse is the logical OR of two phase-locked
  squares (a fixed diode-OR), not an independent PWM oscillator
  (doc 02 §3.5 / §7.2; doc 10 §7) — the chosen method must preserve that exact
  mechanism and the strong 2nd-harmonic spectrum of the 25% duty.
- Real-time safe: no heap allocation and no locks on the audio thread. Any
  residual table must be built once in `prepareToPlay` and be read-only on the
  audio thread.
- macOS arm64 = reference/bless and bit-exact; Linux x64 = co-required hard
  gate. Alias suppression must be verifiable against a reference, not asserted.
- Feature scope includes poly/unison and oversampling; oversampling is owned by
  the nonlinear filter/VCA path (doc 10 §5.1), so the oscillator AA choice must
  not silently depend on it.

## Options considered

The panel agreed on almost all mechanics and split only on which method is the
per-voice default. The shared, undisputed mechanics are recorded in the Contract
below.

### Persona: authenticity-DSP (spectral faithfulness, correct PWM sidebands)

Advocated a tiered residual-correction stack: PolyBLEP as the default for saw,
two-BLEP variable PWM, and OR-then-PolyBLEP sub; minBLEP table as the blessed
reference oracle and opt-in HQ tier; DPW explicitly rejected for the audio band.
Stressed that PolyBLEP/minBLEP place the band-limited step at the exact fractional
sub-sample position (doc 10 §2.1-2.3), which is what makes PWM faithful — the
falling-edge correction lands at the true duty-cycle phase so the width-dependent
nulls match the modeled 3340 (doc 02 §2.5; doc 10 §2.3). Argued the sub-osc is
the strongest case for residual-correction over wavetable: a wavetable 25% pulse
is a separate table that cannot guarantee bit-exact phase-lock to the saw wrap,
and DPW's differentiator would blur the OR edges (doc 02 §3.5 / §7.2; doc 10 §7).

- Pros: closed-form and table-free (no oscillator oversampling tax); width-tracking
  PWM down to ~5%; sub done as OR + per-edge BLEP guarantees drift-free phase-lock
  and the correct 25%-duty spectrum; minBLEP gives a true band-limited oracle for
  the bit-exact bless; hard C1 edges are exactly PolyBLEP/minBLEP's proven case, no
  BLAMP needed.
- Cons (adopted): PolyBLEP is 2nd-order, perceptually aliasing-free only to ~2135 Hz
  fundamental (doc 10 §8) — top octave leaks without help; two BLEPs/period risk
  overlap at narrow widths and high pitch; the 4-edge sub pattern needs careful edge
  ordering or the 75/25 spectrum corrupts silently (doc 10 §7 / §9.2).

### Persona: Performance (lowest CPU at poly/unison scale, simplicity)

Advocated PolyBLEP as the single universal method for all three sources, run at
base rate with NO oscillator oversampling, reserving oversampling strictly for the
nonlinear filter/VCA path (doc 10 §5.1). Emphasized that PolyBLEP is the only method
in the adopted stack needing zero oscillator oversampling (doc 10 §2.3, §5.1), is
branch-light, fully inlineable, allocation-free and lock-free by construction —
directly satisfying the owner-locked RT rule — and is already the research-adopted
default (doc 10 §10). Noted the SH-101 has no hard sync, so the minBLEP path is
essentially unused in normal operation.

- Pros: lowest steady-state CPU per voice (the biggest win at unison stacking);
  table-free / state-light; PWM and the OR'd sub are native closed-form cases; one
  algorithm across all shapes = minimal, easily bit-exact across macOS/Linux.
- Cons (adopted): 2nd-order suppression leaks at the top of the 2' range; a fidelity
  purist hears the difference on a solo'd high saw; narrow PWM at high pitch can have
  near-overlapping residuals; pushes top-octave headroom onto the filter path or an
  optional global 2x rather than solving it in the oscillator.

### Persona: alias-quality (maximal suppression across the full range)

Advocated minBLEP (64x Blackman residual, fractional edge-event model) as the
PRIMARY method for all three sources, with PolyBLEP only as a base-rate fallback in
the low/mid register and DPW excluded entirely. Anchored on the hard number: 2nd-order
PolyBLEP is perceptually aliasing-free only to ~2135 Hz fundamental at 44.1 kHz
(doc 10 §8 / Table VIII), so the top ~2 octaves and 2'+transpose-high playing
accumulate audible aliases precisely where cleanliness must be guaranteed. Argued
minBLEP doubles as the project's documented reference tier and the natural home for
hard-sync if ever added (doc 10 §2.1-2.2).

- Pros: deepest alias suppression of the discontinuity-correction family; hard-edge
  and variable-PWM exact; sub handled correctly with no BLAMP; serves as the objective
  reference bar; RT-safe (residual table is a preallocated read-only const).
- Cons (adopted): higher and frequency-dependent CPU that peaks at exactly the high
  notes and worsens under unison stacking (doc 10 §2.2); more implementation complexity
  (residual ring-buffer / overlap-add, table generation + verification); marginal
  benefit in the low/mid register where PolyBLEP is already transparent; goes against
  the docs' stated default and so must justify overturning the easy path.

### Split and resolution

All three personas converge on: (1) the three implementation mechanics (saw, two-BLEP
PWM, OR-then-correct sub with no BLAMP); (2) rejecting DPW from the audio path; (3)
keeping minBLEP as the reference oracle; (4) running oscillators at base rate and NOT
leaning on the filter's oversampling to fix oscillator aliasing. The only real split is
the per-voice DEFAULT method: PolyBLEP (authenticity-DSP, Performance — 2 of 3) vs
minBLEP-primary (alias-quality — 1 of 3).

Resolved in favor of the tiered design that two personas already proposed and that the
research adopts as its default (doc 10 §2.3, §5.1, §10): PolyBLEP is the per-voice
default; minBLEP is the blessed reference oracle AND an opt-in "HQ" tier. The
alias-quality critique is not discarded — it is adopted as a concrete mitigation: a
documented, switchable per-voice escalation to the minBLEP HQ tier above a pitch
threshold (~2 kHz fundamental, the Valimaki limit), so peak CPU lands only on the high
notes that need it while idle/low/mid voices stay cheap. This keeps the unison-scale CPU
win, satisfies the bit-exact bless via the minBLEP oracle, and closes the top-octave gap
alias-quality is chartered to defend. DPW is retained only as a documented compile-time
low-CPU fallback flag, never the default.

## Decision

Adopt **PolyBLEP** (closed-form, table-free, two-segment polynomial residual,
doc 10 §2.3) as the per-voice **default** anti-aliasing method for the VCO sawtooth,
the variable-width pulse (PWM), and the divider-derived 25% sub-pulse, run at **base
sample rate with no oscillator-specific oversampling** (doc 10 §5.1). Build a
**minBLEP** residual table (64x oversampling, Blackman window, doc 10 §2.2 / §5.1) once
at `prepareToPlay`; it serves both as the **blessed reference oracle** for A/B alias-
suppression certification under the macOS arm64 bit-exact bless and as an **opt-in HQ
tier**. Provide a documented, switchable escalation from PolyBLEP to the minBLEP HQ tier
above a ~2 kHz-fundamental pitch threshold (doc 10 §8, Valimaki Table VIII) to cover the
top octave. **DPW is excluded from the audio path** and retained only as a compile-time
low-CPU fallback flag (doc 10 §2.5). No BLAMP is used: every edge in the saw, pulse, and
the OR'd sub is a true level step (doc 10 §2.4 / §7).

Application mechanics (all from doc 10 §2.3 and doc 02 §7.2, agreed by the full panel):

- Sawtooth: trivial ramp `(2*t - 1)` minus one PolyBLEP at the wrap.
- Pulse / PWM: two INDEPENDENT BLEPs per period — added at the rising edge (phase 0),
  subtracted at the falling edge placed at the duty-cycle phase — so swept width tracks
  correctly down to the ~5% modeled minimum (doc 02 §2.5).
- Sub-osc: derive the -1 and -2 octave squares as exact phase-locked subharmonics of the
  SAME master phase accumulator, form `out = Q1 OR Q2` for the 75%/25% rectangle, and
  PolyBLEP every resulting edge (doc 02 §3.5 / §7.2; doc 10 §7). Modeling it as OR-then-
  correct (not a separate oscillator or wavetable) is required by the owner-locked
  circuit-accurate mandate and guarantees drift-free phase-lock at every footage.

This is consistent with the locked decisions: it is the documented circuit mechanism for
the diode-OR sub (no independent oscillator), it is allocation-free and lock-free on the
audio thread (PolyBLEP is stateless; the minBLEP table is preallocated read-only), and it
does not silently depend on the filter path's oversampling.

## Consequences

Commits us to:

- A single closed-form PolyBLEP core shared across saw, PWM, and the sub edges — minimal
  code surface, easy to keep bit-exact across macOS arm64 and Linux x64.
- Building and maintaining a minBLEP residual table at init, used as the certification
  oracle for the bit-exact bless and as the HQ tier — a one-time `prepareToPlay`
  allocation that must never touch the audio thread.
- Implementing and testing the 4-edge OR pattern of the sub within one -2 oct period;
  edge-ordering correctness is load-bearing for the 75/25 spectrum (doc 10 §9.2 flags this
  as not scope-timing-verified — confirm against the schematic before locking the sub
  logic, per doc 02 §3.5).
- A switchable per-voice PolyBLEP -> minBLEP escalation above ~2 kHz fundamental, plus dt
  clamping so the two PWM BLEPs never overlap at the ~5% width extreme.

Forecloses / makes harder:

- Pure-wavetable oscillators are foreclosed for these sources: a wavetable cannot guarantee
  bit-exact phase-lock of the sub to the saw wrap and interpolation-aliases on fast PWM
  sweeps (doc 02 §7.2).
- Hard mathematical alias-freeness at the very top octave from PolyBLEP alone is not
  achieved; we accept a touch of top-octave alias energy in PolyBLEP mode and rely on the
  documented minBLEP HQ escalation for purist use — an explicit, owned tradeoff.
- DPW-only ultra-low-CPU operation is demoted to a non-default fallback flag.

There is no user-facing scope or expectation change beyond the locked decisions: PolyBLEP
is already the research-adopted default, the minBLEP HQ tier is additive, and no behavior
is promised that the locks did not already cover. No owner ratification item.

## Contract

Normative behavior the backlog implements verbatim. `t` = phase in `[0,1)`,
`dt = freq/fs`. PolyBLEP residual: for `t < dt` (`t' = t/dt`) `blep = 2*t' - t'^2 - 1`;
for `t > 1 - dt` (`t'' = (t-1)/dt`) `blep = t''^2 + 2*t'' + 1`; else `0`
(doc 10 §2.3).

| # | Source / condition | Required behavior | Source |
| --- | --- | --- | --- |
| C1 | VCO sawtooth | `value = (2*t - 1) - polyBLEP(t)`; one residual per wrap | doc 10 §2.3 |
| C2 | VCO pulse / PWM | two independent BLEPs per period: `+polyBLEP` at rising edge (phase 0), `-polyBLEP` at falling edge placed at the duty-cycle phase | doc 10 §2.3; doc 02 §2.5 |
| C3 | PWM width range | duty sweeps ~50% down to a ~5% floor; clamp effective duty / dt so the two BLEPs never overlap at the extreme | doc 02 §2.5; doc 10 §2.3 |
| C4 | Sub squares (-1, -2 oct) | derived as exact phase-locked subharmonics of the master phase accumulator; never an independent oscillator | doc 02 §7.2; doc 10 §7 |
| C5 | Sub 25% pulse | `out = Q1 OR Q2` (logical OR of the two squares) forming a 75%/25% rectangle at -2 oct; PolyBLEP every resulting edge; no BLAMP | doc 02 §3.5 / §7.2; doc 10 §2.4 / §7 |
| C6 | Edge type | all saw/pulse/sub edges are treated as level steps (C1-continuous); BLAMP is NOT applied | doc 10 §2.4 |
| C7 | Default tier | PolyBLEP per voice at base sample rate; NO oscillator-specific oversampling | doc 10 §2.3 / §5.1 |
| C8 | HQ / reference tier | minBLEP 64x Blackman residual table built once in `prepareToPlay`, read-only on the audio thread; used as the A/B alias-suppression oracle for the bit-exact bless and as an opt-in HQ tier | doc 10 §2.2 / §5.1 |
| C9 | HQ escalation | switchable per-voice escalation from PolyBLEP to minBLEP above ~2 kHz fundamental (the 2nd-order PolyBLEP aliasing-free limit) | doc 10 §8 (Table VIII) |
| C10 | DPW | excluded from the audio path; available only behind a compile-time low-CPU fallback flag, never default | doc 10 §2.5 |
| C11 | Real-time safety | no heap allocation and no locks on the audio thread for any tier; PolyBLEP stateless, minBLEP table preallocated read-only | owner-lock; doc 10 §5.1 |
