<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 004: Oversampling strategy

Status: accepted (Drive removed from the per-voice zone; placement + PDC owned by ADR-017)
*Refined post-acceptance — see ADR-017.*
Date: 2026-06-17

## Context

The engine generates aliasing in two distinct ways that demand different treatment, and we
must decide how to spend the oversampling budget: global engine-wide oversampling vs
per-module/zone oversampling; the oversampling factor; IIR (polyphase, cheap, phase-nonlinear)
vs FIR (linear-phase, costlier) up/downsampling; and exactly which stages run oversampled.

The forces:

- The research is explicit that only the *nonlinear* signal path needs oversampling. The
  `tanh` OTA law in the filter cells and the OTA VCA, the diode-clamp resonance limiter, and
  the Drive stage are the only broadband-harmonic generators in the chain
  (docs/research/10-dsp-modeling-techniques.md §4, §5.1; docs/research/03-filter-ir3109.md
  §4.2). PolyBLEP oscillators are alias-corrected at source by fractional-delay positioning
  and need **zero** signal-path oversampling
  (docs/research/10-dsp-modeling-techniques.md §2.3, §5.1).
- Huovilainen's documented figure for the nonlinear ladder is **2x (88.2 kHz)**
  (docs/research/10-dsp-modeling-techniques.md §5.1, §8 table row "Nonlinear-filter
  oversampling"). The half-sample resonance-feedback phase compensation is validated to
  <10% tuning error at 2x for `f < Fs/4` (docs/research/10-dsp-modeling-techniques.md §3.8).
- The SH-101's resonance limiter is a near-pure-sine self-oscillation
  (docs/research/03-filter-ir3109.md §4.4), and its **output-side** Q compensation routes the
  non-inverted phase-splitter copy to the filter OUTPUT / VCA drive, so raising resonance
  drives the VCA harder (docs/research/03-filter-ir3109.md §4.3). High resonance is therefore
  the worst-case alias exposure: inharmonic foldover against an otherwise clean tone.
- The IIR-vs-FIR resampler choice is explicitly flagged as **an ADR decision, not a literature
  constant** (docs/research/10-dsp-modeling-techniques.md §5.2, §9.2). Elliptic/polyphase IIR
  halfband is ~2.5 mult/sample and >5x cheaper than FIR but phase-nonlinear; the phase
  penalty bites only when summing multiple correlated sources.

Owner-locked decisions that bound this ADR (re-affirmed, not reversed):

- **Real-time safe: no heap alloc / no locks on the audio thread.** All resampler state and
  oversampled scratch buffers must be preallocated; factor changes must not allocate.
- **Circuit-accurate modeling from documented circuit behavior; NO physical-unit oracle.**
  There is no real-SH-101 measurement to define "transparent" against; the aliasing-floor
  target is necessarily self-referential (our model vs a higher-oversampled reference of our
  own model). This is labeled as such, not hidden.
- **macOS arm64 = reference/bless + bit-exact; Linux x64 = co-required hard gate.** The
  resampler kernels must be deterministic and bit-identical across both platforms.
- **Feature scope includes poly/unison, oversampling, Drive.** The nonlinearity is per voice,
  so the oversampled zone is per voice, not a single oversample of the summed mix.

## Options considered

Two personas debated. They converged on the overall architecture and split only on the
**boundary of the oversampled zone**.

### Persona: quality-DSP (alias-free nonlinearities, transparent up/downsampling)

Advocated a single shared 2x oversampling zone wrapping the **entire contiguous nonlinear
block**: filter ladder + diode-clamp resonance feedback + the output-side-Q VCA drive /
`tanh` + the Drive module — one up/down conversion pair for the whole chain. Oscillators stay
at base rate on PolyBLEP. Polyphase IIR halfband on the realtime path; linear-phase FIR
reserved for an offline/bounce render tier and for any future stage that sums multiple
correlated copies. Factor and resampler hidden behind an interface so a 4x "HQ/render" tier
is a compile/CI parameter with no module rewrites. IIR order sized by a CI-measured
aliasing-floor target (alias products ~ -90 to -100 dBFS at full drive + self-oscillation),
not a textbook order.

- Pros: spends CPU exactly where harmonics are made; one up + one down per voice avoids the
  classic down-then-up error between adjacent nonlinear stages; 2x is the documented
  Huovilainen figure and keeps the §3.8 feedback compensation in its validated window;
  IIR-realtime / FIR-render is the correct reading of §5.2; the broad zone protects the
  worst case (VCA `tanh` driven hard by output-side Q at high resonance).
- Cons: zone-based oversampling needs a clean, stable buffer-handoff API (more discipline than
  a dumb global wrapper); two resampler implementations to keep intent-equivalent; the
  half-sample compensation table must be re-derived per factor (a 4x HQ tier is not free of
  model work); the aliasing-floor target is self-referential given no physical oracle.

### Persona: Performance (CPU budget, selective oversampling, latency)

Advocated a **narrower** selective island: oversample only the **IR3109 filter + Drive** at
2x, with everything else (oscillators, sub, noise, mod, mixing, output-side Q gain) at base
rate. Same IIR-halfband-default / FIR-opt-in / per-voice / 1x-2x-4x quality-switch posture.
Added a global CPU governor that drops quality under voice-count pressure, a hard cap on
Newton-iteration count to bound worst-case per-sample cost, and the rule that any
linear-phase-FIR latency MUST be declared to the host via `setLatencySamples` (PDC).

- Pros: smallest possible oversampled footprint; per-voice islands make the poly CPU budget
  trivial to reason about (voice count, not factor, is the cost knob); matches the literature
  constant exactly; real-time safe by construction.
- Cons: the narrow "filter + Drive only" boundary treats the output-side-Q VCA drive `tanh` as
  if it lived outside the nonlinear zone — but §4.3 shows resonance *raises VCA drive*, so the
  VCA `tanh` is a genuine broadband generator that the narrow island leaves at base rate;
  if a future routing blends the oversampled output against a dry/base-rate copy the IIR phase
  offset causes comb/flam artifacts; per-voice cost scales with polyphony.

### Split and resolution

The split is the zone boundary: **filter + Drive** (Performance) vs **filter + diode-clamp
resonance + VCA drive/`tanh` + Drive** (quality-DSP). Resolved in favor of quality-DSP's
**broader single zone**, because docs/research/03-filter-ir3109.md §4.3 is decisive: the
SH-101's Q compensation is *output-side* and drives the BA662 VCA `tanh` harder precisely as
resonance rises (§4.4 worst case). A narrow island that excludes the VCA drive would leave the
most resonance-correlated nonlinearity aliasing at base rate. Including it in the same shared
zone also avoids the down-then-up resampling error between the filter output and the VCA/Drive
that the narrow boundary risks. The contiguous nonlinear chain — filter, diode-clamp
resonance, VCA drive, Drive, in series — gets exactly one up + one down conversion per voice.

Critiques adopted from Performance (folded into the decision and Contract):

- Per-voice islands, not a single oversample of the summed mix (the nonlinearity is per voice).
- 1x "eco" / 2x "default-blessed" / 4x "HQ" quality switch, with **2x pinned as the bit-exact
  reference**.
- Global CPU governor may drop the quality setting to 1x under voice-count pressure.
- Hard-cap any Newton/fixed-point iteration count to bound worst-case per-sample cost (or
  prefer the fixed-cost Huovilainen forward-Euler-at-2x path,
  docs/research/10-dsp-modeling-techniques.md §3.7).
- Any linear-phase-FIR added latency MUST be reported via `setLatencySamples` so host PDC
  stays correct; never introduce FIR latency silently.

## Decision

Adopt **per-voice, zone-based 2x oversampling of a single shared nonlinear zone**, NOT a
global engine-wide wrapper.

1. The base graph runs at host rate. Oscillators (PolyBLEP saw/pulse, variable PWM), the 4013
   sub-osc divider+OR, noise, LFO, envelopes, and linear mixing stay at base rate. PolyBLEP
   needs no signal-path oversampling (docs/research/10-dsp-modeling-techniques.md §2.3, §5.1).
2. One contiguous oversampled zone per voice wraps the full nonlinear chain in series: the
   IR3109 `tanh` filter ladder + the diode-clamp resonance feedback limiter + the output-side-Q
   BA662 VCA drive/`tanh` + the Drive module. Exactly one upsample and one downsample per voice
   bound the zone (docs/research/10-dsp-modeling-techniques.md §4, §5.1;
   docs/research/03-filter-ir3109.md §4.2, §4.3).
3. **Factor = 2x is the shipped, blessed, bit-exact reference** (Huovilainen's documented
   figure; docs/research/10-dsp-modeling-techniques.md §5.1, §8 table). The factor sits behind
   an interface so a 4x "HQ/render" tier is a compile/CI/quality-setting parameter with no
   module rewrites. A 1x "eco" tier is available for live/low-latency use.
4. **Realtime resampler = polyphase IIR halfband** (elliptic/Butterworth). On a single
   mono-ish self-summed nonlinear bus, absolute phase linearity is not audible against itself,
   so the >5x cost saving over FIR is taken (docs/research/10-dsp-modeling-techniques.md §5.2).
   A **linear-phase FIR halfband** is reserved for the offline/bounce render tier and for any
   future stage that must sum multiple correlated sources (e.g. unison voices summed *inside*
   the zone), per the §5.2 phase-linearity caveat.
5. The IIR order/ripple/transition is **sized by an automated CI null/THD+alias test** to an
   aliasing-floor target (alias products roughly -90 to -100 dBFS across the audio band at full
   drive + self-oscillation), not by a textbook order
   (docs/research/10-dsp-modeling-techniques.md §5.2, §9.2). Because there is no physical-unit
   oracle (owner lock), the floor is measured against a higher-oversampled reference of our own
   model and is labeled as a self-referential target, not a real-SH-101 measurement.
6. If the explicit (delayed) Huovilainen loop is used, the half-sample feedback compensation
   (docs/research/10-dsp-modeling-techniques.md §3.8) is derived **per factor**; the 2x figures
   are the blessed baseline and a 4x tier requires its own re-derived compensation.

Rationale, tied to the locks: zone oversampling spends the transparency budget only on the
modules that actually make broadband harmonics, leaving linear stages and already-clean
PolyBLEP oscillators untouched — directly faithful to §5.1. The broad zone (vs the narrow
"filter + Drive" island) protects the worst case the circuit research identifies: output-side Q
driving the VCA `tanh` at high resonance against a near-pure self-oscillation sine
(docs/research/03-filter-ir3109.md §4.3, §4.4). Fixed-coefficient, allocation-free,
branch-light IIR/FIR kernels with no fast-math reassociation satisfy the real-time-safety lock
and reproduce sample-for-sample across the macOS-arm64 reference and the co-required Linux-x64
gate.

## Consequences

This commits us to:

- A clean separation between the linear/oscillator base graph and the per-voice nonlinear zone,
  with a stable, allocation-free buffer-handoff API at the zone boundary.
- Two resampler implementations (realtime polyphase IIR + offline linear-phase FIR) kept
  intent-equivalent and individually tested; the render path diverges audibly in phase from the
  realtime path (acceptable for bounce, documented as a known behavioral difference).
- A CI null/THD+alias harness that owns the aliasing-floor target and sizes the IIR order; the
  resampler design is gated by this test, not by a literature constant.
- 2x pinned as the bit-exact bless reference across macOS arm64 and Linux x64; fixed
  coefficients, no fast-math reassociation, identical kernels both platforms.
- Per-voice oversampled scratch buffers and halfband delay lines preallocated at
  `prepareToPlay()` to max block size x max supported factor; quality-setting changes vary only
  the active stride and never allocate on the audio thread. Denormals flushed (FTZ/DAZ) inside
  the oversampled loop.

This forecloses / makes harder:

- A "dumb" global 2x wrapper is rejected; engineers must respect the zone boundary and never
  down-then-up between adjacent nonlinear stages.
- Blending the oversampled zone output against a dry/base-rate copy is constrained: anything
  blended against the zone must share the same resampler or be explicitly phase-matched
  (IIR phase nonlinearity would otherwise cause comb/flam artifacts).
- A future 4x HQ tier is not free: it requires re-derived half-sample feedback compensation
  (§3.8 figures are quoted at 2x) and re-running the CI aliasing-floor gate.
- 2x may prove insufficient at extreme drive + resonance with very bright content; the CI floor
  is the arbiter and may force 4x on the realtime path for some presets, eating the CPU saving.

Owner ratification item: the user-facing **1x/2x/4x quality switch** adds preset/automation
surface and a render-vs-realtime phase divergence that users may notice on bounce. The blessed
default and bit-exact reference is **2x**; the eco/HQ tiers and the documented realtime-vs-render
phase difference are a quality-tier scope decision beyond the bare "oversampling" feature lock
and should be explicitly signed off.

## Contract

Normative cases the backlog implements verbatim. Domain rate = host sample rate.

| # | Stage / condition | Runs at | Resampler (realtime) | Resampler (offline render) | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | PolyBLEP saw/pulse/PWM oscillators | base rate | n/a | n/a | fractional-delay BLEP; no signal-path oversampling (§2.3, §5.1) |
| 2 | 4013 sub-osc divider + diode-OR | base rate | n/a | n/a | PolyBLEP every edge; no oversampling |
| 3 | Noise, LFO, envelopes, linear mixing | base rate | n/a | n/a | linear / band-appropriate; no oversampling |
| 4 | IR3109 `tanh` filter ladder | 2x (zone) | polyphase IIR halfband | linear-phase FIR halfband | inside shared per-voice zone (§4, §5.1) |
| 5 | Diode-clamp resonance feedback limiter | 2x (zone) | polyphase IIR halfband | linear-phase FIR halfband | same zone, in series after stage 4 |
| 6 | Output-side-Q BA662 VCA drive / `tanh` | 2x (zone) | polyphase IIR halfband | linear-phase FIR halfband | included because output-side Q raises VCA drive with resonance (§4.3) |
| 7 | Drive module | 2x (zone) | polyphase IIR halfband | linear-phase FIR halfband | same zone, series after VCA drive; no intermediate down/up |
| 8 | Zone up/down conversion count | — | exactly 1 up + 1 down per voice | exactly 1 up + 1 down per voice | never down-then-up between stages 4-7 |
| 9 | Oversample scope | per voice | — | — | each voice has its own zone; never oversample the summed mix |
| 10 | Default / blessed factor | 2x | — | — | bit-exact reference is 2x on macOS arm64 and Linux x64 |
| 11 | Quality tiers | 1x eco / 2x default / 4x HQ | — | — | factor behind an interface; 4x re-derives §3.8 compensation and re-runs CI floor |
| 12 | Aliasing-floor target | — | — | — | alias products ~ -90 to -100 dBFS, audio band, full drive + self-osc; CI null/THD test sizes IIR order; self-referential (no physical oracle) |
| 13 | Iteration cost | — | — | — | cap Newton/fixed-point iterations, or use fixed-cost Huovilainen Euler @2x (§3.7); benchmark worst case (max voices + max resonance + max drive) |
| 14 | Latency / PDC | — | IIR adds negligible latency | FIR latency MUST be reported | declare any FIR latency via `setLatencySamples`; never introduce silently |
| 15 | Real-time safety | — | — | — | all zone/resampler state preallocated at `prepareToPlay()` to max block x max factor; factor change varies stride only, never allocates; FTZ/DAZ inside loop; fixed coeffs, no fast-math reassociation |
| 16 | Blend rule | — | — | — | anything blended against the zone output must share the resampler or be phase-matched |
