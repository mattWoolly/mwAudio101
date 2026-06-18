<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 018: Quality-tier parameter registration

Status: accepted
Date: 2026-06-18

## Context

Three accepted ADRs each describe a "quality" knob, and read together they
contradict each other on how many such knobs exist and what each controls:

- ADR-002 (oscillator anti-aliasing) defines PolyBLEP as the per-voice default
  and minBLEP as an opt-in "HQ" tier, with a switchable per-voice escalation
  from PolyBLEP to minBLEP above a ~2 kHz fundamental
  (002 Contract C7-C9; docs/research/10-dsp-modeling-techniques.md §2.3, §5.1,
  §8). On its face this reads like an oscillator-local quality control.
- ADR-004 (oversampling) defines a "1x eco / 2x default / 4x HQ" quality switch
  for the nonlinear zone, with 2x pinned as the bit-exact bless reference, and
  flags the user-facing switch as an owner-ratification item
  (004 Contract C11; docs/research/10-dsp-modeling-techniques.md §5.1, §8).
- ADR-008 (parameter schema) registers `mw101.os.factor` as a structural,
  non-automatable param and lists oversampling factor among the structural
  state that reconfigures buffers (008 Contract C7; §4 item 4).

The contradiction: ADR-004's "4x HQ" and ADR-002's minBLEP "HQ" are two
different "HQ" notions, and the engine risks shipping (a) an oversample-factor
control, (b) a separate oscillator-AA control, and (c) the per-voice escalation
all as independently exposed surface — a confusing, under-specified user model
and a combinatorial bless/CI matrix. ADR-008 named only `mw101.os.factor`,
leaving the oscillator-AA tier's status (param? compile flag? internal?)
unresolved, and ADR-004 explicitly left the user-facing switch for owner
sign-off.

Owner-locked decisions this touches, re-affirmed (not reversed):

- **macOS arm64 = reference/bless + bit-exact; Linux x64 = co-required hard
  gate.** A single, well-defined blessed quality state is required so the
  bit-exact reference is unambiguous. Re-affirmed: the blessed state is
  Standard.
- **Modern essentials include oversampling.** A user-facing quality choice is
  in scope; this ADR fixes its shape, not its existence.
- **Real-time safe (no heap alloc / no locks on the audio thread).** The quality
  control reallocates buffers / changes DSP topology, so it MUST be structural
  and applied off the audio thread, consistent with ADR-008 C7/C19.
- **Circuit-accurate from documented circuit behavior; no physical oracle.**
  The internal >~2 kHz minBLEP escalation is documented internal model behavior
  (the Valimaki limit), not a user setting.

This ADR reconciles ADR-002, ADR-004 and ADR-008 and depends on the parameter
registry mechanics of ADR-008.

## Options considered

### Option A — One structural Quality parameter; oscillator HQ bound to it

A single, structural, non-automatable `mw101.quality` enum with three values —
Eco(1x) / Standard(2x) / HQ(4x) — is the ONLY quality control. The oversample
factor and the oscillator AA mode are both derived from it: HQ binds the
minBLEP HQ oscillator tier (ADR-002), Eco/Standard run PolyBLEP. The documented
>~2 kHz auto-escalation (ADR-002 C9) stays internal and is NOT a parameter.
Standard(2x) is the blessed bit-exact reference (ADR-004 C10).

- Pros: one mental model, one preset/automation surface, one bless point; the
  bless/CI matrix is three named states, not a product of two axes; satisfies
  ADR-008 C7 (structural, non-automatable) with a single ID; resolves all three
  ADRs without reopening any DSP decision; oscillator HQ is exactly where a
  purist expects it (the same place they pay for 4x oversampling).
- Cons: oscillator AA mode is no longer independently selectable (cannot run 1x
  oversampling with minBLEP oscillators, or 4x oversampling with PolyBLEP) — a
  deliberate loss of an orthogonal axis the raw ADR-002/004 text implied.

### Option B — Two independent controls (oscillator AA + oversample factor)

Expose `mw101.os.factor` (1x/2x/4x) and a separate `mw101.osc.aa`
(PolyBLEP/minBLEP) as two structural params.

- Pros: maximally orthogonal; lets a user pick cheap oscillators with deep
  filter oversampling or vice versa.
- Cons: four+ effective combinations to bless/CI (worst case crosses with the
  per-voice escalation), an opaque user model ("which HQ do I want?"), two
  preset fields that can disagree, and two owner-ratification surfaces instead
  of one. ADR-004 already pinned a single 1x/2x/4x ladder; a second axis
  multiplies the bit-exact bless surface with no clear user benefit. Rejected.

### Option C — Oscillator escalation as a user param

Promote ADR-002's >~2 kHz escalation to a user-facing toggle.

- Pros: exposes the documented behavior.
- Cons: it is documented internal model behavior keyed off pitch (the Valimaki
  limit, docs/research/10-dsp-modeling-techniques.md §8), not a preference; a
  toggle would let users defeat alias suppression that the model owns, and adds
  yet another quality surface. Rejected — kept internal.

Conflicting positions reconciled: ADR-002's "HQ tier" and ADR-004's "4x HQ"
were two separate "HQ" notions, and ADR-008 had registered only the oversample
factor. Option A wins because it collapses both into ONE structural control,
preserves every underlying DSP decision verbatim (PolyBLEP default, minBLEP HQ,
2x bless, per-voice escalation), and gives the bit-exact bless a single
unambiguous reference state — directly serving the bless/gate owner lock that
Options B/C dilute.

## Decision

Register **exactly one** structural, **non-automatable** quality control, with
per-instance saved state, as the sole quality knob in the engine:

`mw101.quality` : enum { **Eco** = 1x, **Standard** = 2x, **HQ** = 4x }

1. **Standard(2x) is the DEFAULT and the blessed bit-exact reference** on macOS
   arm64 and the co-required Linux x64 integer-bit-exact / FP-tolerance gate,
   exactly as ADR-004 pinned 2x (ADR-004 Contract C10;
   docs/research/10-dsp-modeling-techniques.md §5.1, §8).

2. **Oversample factor is derived from `mw101.quality`**: Eco -> 1x, Standard ->
   2x, HQ -> 4x, driving the single per-voice nonlinear zone of ADR-004 (ADR-004
   Contract C8-C11). There is NO independent `mw101.os.factor` axis;
   `mw101.os.factor` as named in ADR-008 §4/C7 is realized AS `mw101.quality`
   (same structural, non-automatable slot — see Consequences for the ID note).

3. **Oscillator AA mode is BOUND to the tier, not a separate parameter**: HQ
   selects the minBLEP HQ oscillator tier of ADR-002; Eco and Standard run the
   PolyBLEP default (ADR-002 Contract C7, C8;
   docs/research/10-dsp-modeling-techniques.md §2.3). There is NO second,
   independent oscillator-quality control.

4. **The documented internal >~2 kHz minBLEP auto-escalation is internal model
   behavior, NOT a parameter** (ADR-002 Contract C9;
   docs/research/10-dsp-modeling-techniques.md §8, Valimaki Table VIII). It is
   active per the engine's own pitch logic regardless of tier and is never
   exposed.

5. **Structural / non-automatable application** follows ADR-008 verbatim:
   `mw101.quality` is `withAutomatable(false)`, lives in per-instance saved
   state (it serializes with the ValueTree and rides the one migration chain),
   and is applied via prepareToPlay-style reconfiguration against pre-allocated
   max-size (max-factor) buffers — never reallocating or locking on the audio
   thread (ADR-008 Contract C7, C19; ADR-004 Contract C15).

6. **Governor interaction**: ADR-004's CPU governor may transiently drop the
   active oversample stride toward 1x under voice-count pressure (ADR-004 §4
   resolution); this is a runtime stride change, NOT a write to the saved
   `mw101.quality` value, which is preserved.

This is consistent with the locks: a single blessed Standard(2x) state keeps the
bit-exact bless unambiguous across macOS arm64 / Linux x64; the control is
structural and applied off the audio thread (RT-safe lock); the internal
escalation stays documented internal model behavior (circuit-accurate, no
physical oracle). It reconciles ADR-002 (oscillator HQ bound to HQ), ADR-004
(1x/2x/4x ladder, 2x blessed) and ADR-008 (structural, non-automatable schema
registration).

## Consequences

This commits us to:

- A single `mw101.quality` enum as the ONE quality surface for users, presets,
  and automation lists — registered structural / non-automatable in the ADR-008
  registry, with `versionAdded` and a centralized derivation table mapping the
  enum to {oversample factor, oscillator AA mode}.
- Standard(2x) as the default and the single blessed bit-exact reference; Eco
  and HQ are tested but NOT the bless reference. The bless/CI matrix is three
  named tiers along one axis (plus the always-on internal escalation), not a
  product of two axes.
- HQ implies both 4x oversampling AND minBLEP oscillators together; a 4x tier
  re-derives ADR-004's half-sample feedback compensation and re-runs the CI
  aliasing-floor gate (ADR-004 Consequences / Contract C11).
- The internal >~2 kHz escalation remaining an untoggleable model behavior,
  documented but never surfaced.

This forecloses / makes harder:

- Independent selection of oscillator AA and oversample factor (Option B) is
  foreclosed: you cannot pair PolyBLEP with 4x or minBLEP with 1x. This is a
  deliberate simplification of user-facing surface and bless scope.
- Per-step or automatable quality changes are foreclosed (structural,
  non-automatable per ADR-008 C7); the tier changes only by reconfiguration.

ID-registration note for the backlog: ADR-008 named the structural slot
`mw101.os.factor`. This ADR fixes its registered identity as the single quality
enum. Per ADR-008 Contract C1-C2 (append-only, never reuse/renumber/rename in
place), the canonical ID is registered as `mw101.quality`; if `mw101.os.factor`
was already minted at v1, it is retained as a migration alias to `mw101.quality`
rather than renamed in place. Only ONE structural quality ID is live.

Owner ratification item: this supersedes ADR-004's open owner-ratification item
on the user-facing 1x/2x/4x switch by fixing it as exactly one non-automatable
Quality enum (Eco/Standard/HQ, default Standard) with no second quality control
and no exposed oscillator-escalation toggle; confirm this single-knob,
non-automatable shape and the Standard-as-bless default.

## Contract

Normative rules the backlog implements verbatim. "MUST" / "MUST NOT" are
binding. There is exactly ONE quality control; the table below is its complete
mapping.

| # | Case | Required behavior |
| --- | --- | --- |
| Q1 | Single control | The engine MUST expose exactly ONE quality control, `mw101.quality`, enum { Eco, Standard, HQ }. No second, independent quality/oscillator-AA/oversample parameter MUST exist. |
| Q2 | Registration | `mw101.quality` MUST be registered in the ADR-008 `ParamDefs` registry as structural, `withAutomatable(false)`, append-only choice indices Eco=0, Standard=1, HQ=2, with `versionAdded`. |
| Q3 | Saved state | `mw101.quality` MUST be per-instance saved state, serialized with the canonical ValueTree and carried through the single migration chain (ADR-008 C9/C10/C17). |
| Q4 | Default / bless | The DEFAULT value MUST be Standard. Standard MUST be the blessed bit-exact reference on macOS arm64 (FP bit-exact) and the Linux x64 gate (integer bit-exact, FP tolerance-banded per design spec §5). |
| Q5 | Application | A change to `mw101.quality` MUST be applied only via prepareToPlay-style reconfiguration against pre-allocated max-factor buffers, off the audio thread; it MUST NOT allocate or lock on the audio thread (ADR-008 C7/C19). |
| Q6 | Escalation is internal | The documented >~2 kHz-fundamental minBLEP auto-escalation (ADR-002 C9) is internal model behavior, MUST be active per the engine's pitch logic in every tier, and MUST NOT be exposed as a parameter or toggle. |
| Q7 | Governor | The CPU governor MAY transiently reduce the active oversample stride toward 1x under voice-count pressure; it MUST NOT modify the saved `mw101.quality` value. |
| Q8 | ID uniqueness | Only ONE live structural quality ID MUST exist; any prior `mw101.os.factor` ID MUST be retained as a migration alias to `mw101.quality`, never renamed in place (ADR-008 C2). |

Quality enum mapping (the derivation table the backlog implements verbatim):

| Quality | Oversample factor (ADR-004) | Oscillator AA mode (ADR-002) | Automatable | Blessed reference |
| --- | --- | --- | --- | --- |
| Eco | 1x | PolyBLEP (base rate) | false | no |
| Standard | 2x | PolyBLEP (base rate) | false | yes (default) |
| HQ | 4x | minBLEP HQ tier | false | no |
