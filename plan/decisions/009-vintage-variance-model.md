<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 009: Vintage variance / analog-drift model

Status: accepted (INIT-patch default set by ADR-016, 2026-06-18 — subtle drift on (Age low))
*Refined post-acceptance — see ADR-016.*
Date: 2026-06-17

## Context

We must define the drift/variance control set and its DSP for mwAudio101: slow
thermal drift, per-note-on tuning slop, and variance spreads for
cutoff/envelope-time/PW/glide — including parameter names, ranges, defaults, and
behaviour under poly/unison. The decision must separate documented SH-101 facts
from general analog-modeling practice and stay faithful to the project's
circuit-accuracy posture.

Forces:

- **Owner lock (circuit-accurate, "modeled from documented circuit behavior",
  no physical-unit oracle).** The model must encode the actual mechanism the
  service manual and converter physics document, not an abstract "detune knob."
  Variance originates at named factory trimmer set-points within their tolerance
  bands (docs/research/09-vintage-variance-drift.md §2.1), and VCO/VCF drift
  share one kT/q tempco mechanism (§3.1, §3.3), so they must be modeled as
  correlated, not as two independent random walks.
- **Owner lock (no bench data).** Every numeric figure here is a tunable default,
  not a measured spec; the structure is faithful even where the magnitudes are
  inference/heuristic (§7, §8.3). Re-affirmed below.
- **Owner lock (modern essentials: poly/unison, per-voice drift; modern
  reimagined UI; full automation; ~64 presets).** The model must collapse cleanly
  from mono to unison/poly and present a usable surface, not a wall of knobs.
- **Owner lock (real-time safe: no heap alloc / no locks on the audio thread;
  macOS arm64 = reference/bless + bit-exact).** All drift state must be
  pre-allocated and deterministic from persisted state.

This ADR touches the circuit-accuracy and no-oracle locks. It does **not**
reverse them: it re-affirms them by anchoring the architecture to the documented
trimmer set-points and by tagging every numeric figure as a tunable default.

## Options considered

### Persona: authenticity — two-tier frozen-trimmer + shared-thermal model

Approach: a structurally honest model in three tiers. **Tier 1** = per-instance
*frozen* calibration: a deterministic seeded PRNG draws fixed offsets that
perturb the *documented* trimmer set-points within service-manual tolerance
bands (VR-7/VR-9 VCO Tune at 442 Hz, VR-2 D/A Tune 0 V +/-1 mV, VR-8 VCF Width as
a *scale* not an offset, plus the uncalibrated-on-hardware cutoff *offset* per
§2.3 — legitimately the most generous band). **Tier 2** = one shared scalar
temperature state T(t) (Ornstein-Uhlenbeck / leaky-integrated Gaussian, optional
1/f, optional warm-up transient) driving both VCO and VCF through the same
-3300 ppm/degC kT/q coefficient, so they wander *together*. Because the CEM3340
is on-die compensated (§3.2), drift depth defaults small. **Tier 3** = per-note-on
Gaussian slop, independent of T(t). Every control is UI/docs-tagged "VR-anchored
(documented)" vs "analog-modeling embellishment."

Pros: each control maps to a documented circuit reality; shared T captures the
real correlated mechanism a dual-random-walk model gets wrong; honestly small
defaults justified by on-die compensation; the per-instance seed gives a fixed
"personality" while staying bit-exact reproducible for the bless gate;
cutoff-variance generosity is itself sourced (§2.3). Cons: more conceptual
surface (frozen vs live vs note-on) than a single macro; several band widths rest
on inference/heuristic, not measurement; VR-4's function is unconfirmed so only
the 8 enumerated trimmers are perturbed; the warm-up curve is the least
authentic element.

Adopted: the entire mechanism — Tier-1 frozen trimmer-band draws,
Tier-2 single shared kT/q temperature state, Tier-3 per-note-on slop, small
honest defaults, and the documented-vs-embellishment tagging requirement. This
persona's hard line (variance *originates at named set-points*; VCO/VCF *share
one* T; CEM3340 compensation forces *small* defaults) is the load-bearing reason
we reject a generic VARIANCE macro as the *mechanism*. Adopted the warm-up
caveat: ship it off by default, tagged embellishment.

### Persona: Product — single Vintage (Age) macro over a Diva-style page

Approach: ship one "Trimmers / Vintage" page fronted by a single VINTAGE (Age)
macro (0-100%, default ~15-20%) that scales the whole group, with the individual
controls behind it for power users. The macro mapping is a preset relationship,
not its own DSP. Individual controls: Drift Depth/Rate, Tuning Slop, four
Variance spreads, Warm-Up toggle+time, a per-instance "Unit/Serial" seed with a
Re-roll button, and an orthogonal Accuracy (ZDF/oversampling) switch. Every
control carries a documented-vs-embellishment tooltip. Mono = per-instance +
per-note-on; unison/poly promotes the spreads to per-voice with one "Detune Amt"
scaler, Diva-style (§4).

Pros: one macro = instant character and a clean demo, while individual controls
satisfy sound designers and the full-automation scope; reuses the proven Diva
Trimmers template the research blesses (§4); the per-instance seed is an
emotional "your copy is unique" hook *and* makes presets reproducible. Cons: a
macro scaling 8+ params raises real automation/host questions (does the host see
the macro or the targets?); per-instance seed persistence means two instances of
one preset can differ on A/B recall and needs a clear affordance; the Age default
of 15-20% conflicts with "in tune out of the box."

Adopted: the Vintage (Age) macro as a host-thread preset mapping that writes
already-smoothed targets (so it costs the audio thread nothing); the
documented-vs-embellishment tooltip on every control; the persisted Unit seed
with a Re-roll affordance; and the unison Detune Amt scaler. **Rejected** the
15-20% default Age: the research states twice that defaults must be LOW and the
instrument in tune on load (§6, §10.2). We default Age = 0 (the
authenticity/DSP position). Adopted Product's automation-clarity critique as a
normative contract item (the macro is host-visible and writes its targets; both
are automatable; preset diffs record macro + targets).

### Persona: DSP — pre-allocated per-voice engine, block-rate, frozen-at-note-on

Approach: a fixed `DriftState[MAX_VOICES]` array allocated at prepareToPlay,
each entry holding a POD xorshift128+ PRNG, a slow-drift accumulator, a smoother
state, and frozen note-on offsets. Slow drift = one-pole-low-passed white noise
(leaky integrator; optional fixed-coefficient Voss-McCartney/Kellet pink),
updated *once per block* and interpolated to sample rate. Note-on slop = one
Gaussian/cubic draw per note-on from the per-voice PRNG (setup-time only).
Variance spreads = per-voice frozen offsets drawn at note-on, not continuous
modulation (cutoff/PW added in native domain; env-time/glide multiply the time
constant). A **mandatory per-voice one-pole output smoother (~5-20 ms)** sits on
every drifted target so any block-rate or note-on step is de-zippered. One shared
thermal scalar all voices read. Per-voice PRNGs derived deterministically from
the persisted instance seed + voice index, so renders are reproducible yet voices
decorrelate.

Pros: O(1) per-voice per-block; zero heap/locks on the audio thread; the
mandatory smoother makes zipper noise structurally impossible; identical code
path mono->unison->poly; deterministic, bounce-reproducible. Cons: block-rate +
interpolation is an approximation of true continuous 1/f (mitigated by the
smoother, worth a listening test); a single leaky integrator is not spectrally
exact pink; frozen-at-note-on variance means a held note does not wander in
cutoff/PW within the note; MAX_VOICES must be a fixed cap to keep the array
static; cubic vs Gaussian slop is a taste call to A/B.

Adopted: the entire realization discipline — pre-allocated `DriftState`,
block-rate drift generation with interpolation, frozen-at-note-on variance
spreads, the **mandatory output smoother**, deterministic per-voice derivation
(instance seed + voice index), FTZ/DAZ + denormal flush on the integrators, and
a fixed compile-time MAX_VOICES cap. Adopted the open taste/validation flags
(cubic-vs-Gaussian; block-rate-vs-continuous; single-pole-vs-true-pink) as
labelled, deferrable choices, not blockers.

### Split and resolution

No fundamental disagreement on the DSP core — all three converge on shared
thermal state, per-note-on Gaussian slop, persisted per-instance seed, block-rate
generation, per-voice fan-out under unison, and LOW defaults. The only genuine
split was on **framing and the default Age value**: Product wanted a macro-first
surface defaulting to 15-20% character; authenticity/DSP wanted the
mechanism-honest model defaulting to 0 (in tune on load). Resolved by layering,
not choosing: authenticity's two-tier *mechanism* is the model, DSP's engine is
its *realization*, and Product's Vintage macro + tagging is the *UX skin* over
it — with Age defaulting to **0** per the research's stated design intent (§6,
§10.2), overriding Product's 15-20%.

## Decision

Adopt the **two-tier frozen-trimmer + single-shared-thermal-state drift model**,
realized as a **pre-allocated per-voice engine** and skinned with a **single
LOW-default Vintage (Age) macro over a Diva-style Trimmers page**, with every
control tagged documented-vs-embellishment.

1. **Tier 1 — frozen per-instance calibration.** On instance construction, seed a
   deterministic PRNG from persisted plugin state and draw fixed offsets that
   perturb the *documented* factory trimmer set-points within their service-manual
   tolerance bands: VR-7/VR-9 VCO Tune (nominal 442 Hz), VR-2 D/A Tune
   (0 V +/-1 mV), VR-8 VCF Width (a *scale* multiplier, not an offset), and the
   filter cutoff *offset* — which §2.3 documents as uncalibrated on hardware
   (IR3109 has only the VR-8 Width/scale trim, no per-unit cutoff-offset cal), so
   it legitimately gets the most generous band. These offsets are constant for the
   instance's life (frozen trimmers) and persist with the preset. A `cal.spread`
   control widens the draw; a Re-roll button reseeds the personality.
   (docs/research/09-vintage-variance-drift.md §2.1, §2.3, §6.3.)

2. **Tier 2 — live thermal drift, physically correlated.** ONE shared scalar
   temperature state T(t) drives both VCO and VCF through the same kT/q
   ~-3300 ppm/degC coefficient (§3.1, §3.3). VCO and VCF drift are *not*
   independent — they share T and wander together, as on real hardware. T(t) is a
   bounded leaky-integrated Gaussian (Ornstein-Uhlenbeck), optionally summed with
   a 1/f component, plus an optional exponential Warm-Up transient that decays a
   shared extra T offset from "cold." Because the CEM3340 is on-die
   temperature-compensated (§3.2), drift **depth defaults small** — the SH-101 is
   the stable end of the vintage spectrum. (§3.1-§3.3, §6.2, §10.4.)

3. **Tier 3 — per-note-on slop.** A Gaussian (Box-Muller, or cubic `(2u-1)^3`)
   tuning offset latched once at each note-on, independent of T(t). (§5, §6.1.)

4. **Variance spreads** (`var.cutoff`, `var.envTime`, `var.pw`, `var.glide`) are
   per-voice offsets **frozen at note-on** (not continuous modulation), drawn from
   the per-voice PRNG. Cutoff/PW add in the parameter's native domain; env-time
   and glide multiply the time constant (`1 + spread*band`). Cutoff gets the
   widest band because the cutoff offset is uncalibrated on hardware (§2.3, §6.4).

5. **DSP realization (owner-lock-safe).** Drift integrators and the thermal model
   run **once per block** (control rate) and interpolate to sample rate; sub-Hz
   drift never runs at sample rate. Note-on slop and the four variance draws are
   computed **once at note-on**, never per sample. Every drifted target passes
   through a **mandatory per-voice one-pole output smoother (~5-20 ms)** so any
   block-rate or note-on step is de-zippered. All PRNG/T(t)/smoother state lives in
   a fixed `DriftState[MAX_VOICES]` array allocated at prepareToPlay; PRNG is
   xorshift (no `std::random` heap/locking); Re-roll sets a flag consumed
   lock-free on the audio thread. cents->ratio uses a fast `exp2` approximation or
   a small interpolated table, never `std::exp` per sample. FTZ/DAZ + a denormal
   flush guard the integrators during long silence. Per-voice PRNGs derive
   deterministically from `instance_seed + voice_index`, so the same input yields
   bit-identical output on the macOS arm64 bless gate while voices decorrelate.
   (Owner locks: no heap/no locks on audio thread; bit-exact bless gate.)

6. **Poly/unison.** Mono SH-101 path = ONE instance personality (Tier 1) + ONE
   shared T(t) (Tier 2) + per-note-on slop (Tier 3). Under unison/poly, each voice
   gets its own Tier-1 draw and its own Tier-2 drift integrator (decorrelated, same
   statistics) so stacked voices beat naturally; the warm-up *chassis*
   temperature term stays global. Tier-3 slop is already per-note. A single
   `vintage.detuneAmt` scales the per-voice spread. This is the Diva collapse of
   §4 / §7.6. The mono path is simply voice index 0; there is no separate code
   path.

7. **UX skin.** A single **Vintage (Age)** macro fronts the page; it is a
   host-thread preset mapping that scales the group and writes already-smoothed
   targets, so it adds zero audio-thread cost. Default **Age = 0** (in tune out of
   the box — §6, §10.2). Every control carries a UI/docs tag: **VR-anchored
   (documented)** for the Tier-1 trimmer set, **analog-modeling embellishment**
   for pink-noise drift, the warm-up curve, and the per-target variance — honoring
   the "modeled from documented circuit behavior" lock and keeping marketing
   claims defensible (§10.8). An orthogonal **Accuracy** quality switch governs the
   ZDF/oversampling tier (cross-ref ADR for filter/oversampling); CPU budget is
   governed by Accuracy, not by drift.

8. **No-oracle re-affirmation.** Every numeric figure below is a **tunable
   default, not a measured spec** (§9 is labelled, §7 and §8.3 are open validation
   gaps). The *structure* is faithful to documented circuit behaviour; the
   *magnitudes* (tolerance-band widths, env-time +/-5..20%, drift depth, warm-up
   shape) are inference/heuristic and explicitly labelled as such.

## Consequences

Commits us to:

- A three-tier mental model (frozen trimmers / shared live thermal drift /
  per-note-on slop) plus four variance spreads — more parameters and more
  automation IDs to maintain than a single macro, and more UI copy because every
  control is tagged documented-vs-embellishment.
- A persisted per-instance seed in plugin state: each loaded instance has a fixed
  "personality," and two instances of the same preset can sound subtly different
  on A/B recall. We must ship a clear affordance (visible seed + Re-roll, and the
  seed travels with the preset).
- A fixed compile-time `MAX_VOICES` cap; unbounded poly would break the no-alloc
  guarantee. Per-voice independent T(t) under unison multiplies live-drift work by
  the voice count (bounded, control-rate, well under 1% CPU at full unison).
- Deterministic, bounce-reproducible renders gated on the persisted seed +
  per-voice derivation, protecting the macOS arm64 bit-exact bless gate.

Forecloses / makes harder:

- Intra-note wander of cutoff/PW within a single held note: variance is
  frozen-at-note-on by design. Promoting cutoff variance to a continuous slow-mod
  path later would be added cost and a new ADR.
- A spectrally exact, lifetime-persistent 1/f drift out of the box: we ship a
  leaky-integrator (optionally a fixed-coefficient pink filter); true Voss-McCartney
  pink is available but is an explicit upgrade, not the default.
- Claiming any drift/warm-up/envelope-spread *number* as an SH-101 measurement.
  These are honest defaults and may need retuning after listening tests.

Open validation gaps carried forward (from §7, §8.3): no bench-measured SH-101
drift/warm-up curves; IR3109 tempco inferred from general OTA theory, not a
datasheet; VR-4's function unconfirmed (only the 8 enumerated trimmers are
perturbed); per-node tolerance classes unconfirmed. The warm-up curve is the
least authentic element — ships off by default, tagged embellishment.

Owner ratification item: the Vintage (Age) macro is a host-visible parameter that
scales 8+ targets — confirm the host/automation model (macro AND targets both
automatable; preset diffs record both) and confirm that **persisted per-instance
seeds may make two instances of the same preset sound subtly different on recall**
is acceptable product behaviour (with the Re-roll/visible-seed affordance as the
mitigation).

## Contract

Normative case table. All numeric values are **tunable defaults**, not measured
specs (§9). Internal bands are exposed as constants for later retuning.

| ID | Control / behaviour | Range | Default | Tag | Rule |
| --- | --- | --- | --- | --- | --- |
| VV-1 | `vintage.age` (macro) | 0-100% | 0 | embellishment | Host-thread mapping scaling all below; writes smoothed targets; in tune on load |
| VV-2 | `drift.depth` | 0-50 cents | 4 | embellishment | Slow drift amplitude; leaky-integrated/pink, block-rate |
| VV-3 | `drift.rate` | 0.01-1 Hz | 0.1 | embellishment | Effective drift bandwidth |
| VV-4 | `tune.slop` | 0-20 cents | 2.5 | embellishment | Gaussian/cubic offset latched once per note-on |
| VV-5 | `warmup.time` | off, 0-30 min | off | embellishment | Decaying extra offset on the SHARED T(t); off by default |
| VV-6 | `cal.spread` | 0-100% | 25 | VR-anchored (band width = embellishment) | Width multiplier on Tier-1 trimmer-band draws |
| VV-7 | `var.cutoff` | 0-100% | 0 | VR-anchored uncalibrated (§2.3); widest band | Frozen-at-note-on cutoff offset, native domain |
| VV-8 | `var.envTime` | 0-100% -> +/-5..20% A/D/R | 0 | embellishment | Frozen-at-note-on multiplier on A/D/R time constants |
| VV-9 | `var.pw` | 0-100% | 0 | embellishment | Frozen-at-note-on PW offset |
| VV-10 | `var.glide` | 0-100% | 0 | embellishment | Frozen-at-note-on multiplier on glide time |
| VV-11 | `vintage.detuneAmt` | 0-100% | (preset) | embellishment | Scales per-voice Tier-1/Tier-2 spread under unison/poly only |
| VV-12 | per-instance seed + Re-roll | 64-bit | seeded at construct | VR-anchored | Persisted in plugin state; perturbs VR-2/VR-7/VR-9/VR-8 set-points; Re-roll reseeds |
| VV-13 | VCO/VCF drift correlation | — | — | VR-anchored mechanism | ONE shared T(t) drives both via -3300 ppm/degC; never two independent walks |
| VV-14 | Update rate | — | block-rate + interp | — | Drift/thermal updated per block; slop/variance once per note-on; never per-sample noise gen |
| VV-15 | Output smoothing | ~5-20 ms | mandatory | — | Every drifted target passes a one-pole smoother; no zipper noise regardless of upstream step |
| VV-16 | Real-time safety | — | — | — | Fixed `DriftState[MAX_VOICES]` at prepareToPlay; xorshift PRNG; no heap/no locks on audio thread; Re-roll swapped lock-free |
| VV-17 | Determinism | — | — | — | PRNG = `instance_seed + voice_index`; same input -> bit-identical output (macOS arm64 bless gate) |
| VV-18 | Mono vs poly/unison | — | — | — | Mono = voice 0 (1 personality, 1 T(t), per-note slop); unison/poly = per-voice Tier-1 + per-voice Tier-2, shared warm-up chassis term; same code path |
