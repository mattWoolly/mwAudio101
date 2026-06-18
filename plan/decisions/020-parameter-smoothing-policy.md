<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 020: Parameter smoothing / de-zipper policy

Status: accepted
Date: 2026-06-18

## Context

mwAudio101 has a parameter contract (ADR-008), a per-voice drift engine with a
mandatory output smoother (ADR-009), a control-rate / CV model with click-free
mode crossfade (ADR-005, refined by ADR-016), and a golden harness that pins
"parameter-smoothing block boundaries" as a CLASS-EXACT path (ADR-013). What is
missing is the single cross-cutting rule for *which* parameters get de-zippered,
*how* (one-pole de-zipper vs. crossfade vs. nothing), and with *what* time
constant. Today that decision is scattered: ADR-008 C7 says continuous sonic
params are "automatable and smoothed (per-block LinearSmoothedValue)" but pins
no time constants and no per-class policy; ADR-009 VV-15 mandates a one-pole
~5-20 ms smoother on every *drifted* target but only for the drift path; ADR-005
C7 crossfades the two CV branches on a vintage<->modern switch but says nothing
about ordinary stepped switches (waveform, range, voice mode). The result is a
gap: nothing centrally states that, e.g., the VCF cutoff knob is de-zippered
while the VCO range selector is not, and nothing gives the implementer a number
to put in `prepareToPlay`. This ADR owns that contract.

Zipper noise (audible stair-stepping when a host sends coarse automation, or a
user drags a knob, or a block-rate value lands as a step) is the failure this
prevents. The opposite failure — smoothing a *structural* or *stepped* param —
is just as bad: low-passing the index of a 4-way waveform selector sweeps the
synth through wrong waveforms on its way to the target, and "smoothing" a
buffer-reallocating param like oversampling factor or voice count is meaningless
(it changes DSP topology, not a continuous value) and would defeat the
prepareToPlay-style reconfiguration ADR-008 C7 mandates.

Forces in tension:

- **A clean, predictable audio surface vs. faithfulness.** Continuous sonic
  params must not zipper, but de-zippering also colors fast automation; the
  time constant is a feel choice with no physical-unit oracle.
- **Per-class correctness.** A single global smoothing time would zipper slow
  params and lag fast ones; structural/stepped params must be excluded entirely,
  not given a "fast" smoother.
- **Determinism.** Whatever the smoother does, its block boundaries are a
  CLASS-EXACT golden surface (ADR-013) and its update cadence must align with the
  control-rate tick (ADR-005 / ADR-016) so the macOS arm64 bless and the Linux
  x64 co-gate agree bit-for-bit on the integer/index bookkeeping.

This ADR touches and **re-affirms** these owner locks:

- **Circuit-accurate, "modeled from documented circuit behavior", no
  physical-unit oracle.** De-zipper time constants are a playability/anti-zipper
  engineering choice, not a claimed SH-101 measurement. The one circuit-real
  slew that *is* documented — the analog portamento / S&H RC between held pitch
  steps (ADR-005 §2) — is **glide DSP, not a de-zipper**, and is explicitly
  excluded from this policy so we never double-smooth it or relabel it as a
  measured spec. Every time constant here is `(PI)`-tagged and lives in one
  table. Re-affirmed.
- **Real-time safe (no heap alloc / no locks on the audio thread).** Smoothers
  are POD state sized at `prepareToPlay`; all smoothing runs on the audio thread
  with no allocation and no locks. Re-affirmed.
- **macOS arm64 bit-exact bless + Linux x64 integer-exact / FP-tolerance gate.**
  The de-zipper is a CLASS-FP audio value, but its *block-boundary update cadence
  and any integer/index bookkeeping* are CLASS-EXACT (ADR-013 C5). Re-affirmed.
- **Modern essentials (poly/unison, oversampling, drift, FX, arp+seq, automation,
  MPE-lite, ~64 presets).** The policy must cover per-voice params, FX params,
  and host automation uniformly. Re-affirmed.

It depends on / reconciles ADR-005 (CV crossfade, control-rate tick), ADR-008
(param registry, automatable-vs-structural classification, the calibration
table), ADR-009 (the mandatory drift output smoother), ADR-013 (CLASS-EXACT
smoothing block boundaries) and ADR-016 (MODERN-SMOOTH default).

## Options considered

This is an owner-resolved cross-cutting gap rather than a fresh agent-panel
debate; the options below are the honest design alternatives weighed against the
existing ADRs.

### Option A — One global smoothing constant for all continuous params

A single `LinearSmoothedValue` ramp time (e.g. 10 ms) applied to every
automatable parameter, structural/stepped params excluded by their
`isAutomatable(false)` / choice-type flag.

- Pros: trivial to implement; one number to tune; obviously RT-safe; the
  CLASS-EXACT block boundary is uniform.
- Cons: wrong for the feel. A 10 ms ramp on glide time or on a slow filter sweep
  is fine, but the same ramp on a fast resonance stab or a hard-synced PWM edge
  either lags or still zippers depending on host automation granularity. A single
  constant cannot serve both pitch (must be near-instant to stay in tune under
  MPE) and level (wants a gentle ramp to avoid clicks). Rejected: it under-serves
  exactly the acid/bass idioms the cultural research centers.

### Option B — Per-parameter ad-hoc smoothing decided at each call site

Each DSP module owns its own smoother choice inline.

- Pros: maximum local control; each author tunes their own.
- Cons: the gap this ADR exists to close. No central table, no auditable policy,
  drift between modules, no single place to retune, and the CLASS-EXACT golden
  can't pin "the" smoothing boundary because there is no canonical one. Directly
  violates ADR-008's "no parameter behavior declared anywhere but the registry"
  spirit. Rejected.

### Option C — Table-driven per-class de-zipper, structural/stepped excluded (adopted)

Classify every parameter into a small fixed set of smoothing classes in the
ADR-008 registry; continuous/audio-affecting params get a one-pole de-zipper
with a per-class default time constant drawn from one `(PI)` calibration table;
stepped and structural params are explicitly *not* smoothed and instead use the
ADR-005 click-safe crossfade where the switch is audible.

- Pros: matches feel per class (near-instant pitch, gentle level, mid glide);
  one table to retune, consistent with ADR-008's single-source-of-truth and
  ADR-009's centralized `(PI)` calibration table; the per-class default gives the
  CLASS-EXACT golden a canonical block boundary to pin; cleanly reuses the
  existing crossfade machinery for switches instead of inventing a second
  mechanism; honors the no-oracle lock by tagging every constant as a tunable
  default. The one-pole choice (vs. linear ramp) matches ADR-009 VV-15's existing
  one-pole drift smoother, so the drifted-target path and the
  parameter-target path share the same smoother kind and there is no second
  smoother flavor to validate.
- Cons: a per-param class column is one more thing to set correctly at authoring
  time, and a mis-classified param (e.g. a stepped value tagged continuous) would
  smear through wrong values — mitigated by making the class default to "stepped /
  no-smooth" so the unsafe direction requires an explicit opt-in. The one-pole
  exponential approach never mathematically reaches target, requiring a snap
  threshold (handled in the Contract).

Option C wins because it is the only one that closes the gap centrally, serves
the per-class feel the cultural idioms demand, and reuses (rather than
contradicts) ADR-005's crossfade and ADR-009's one-pole smoother.

## Decision

Adopt a **table-driven, per-class parameter de-zipper policy**, with the
smoothing class and time constant declared in the ADR-008 `ParamDefs` registry
and the numeric time constants living in the single `(PI)` calibration table
ADR-008 §1 / ADR-009 already establish (never inlined at a call site).

1. **Two mechanisms, three behaviors.** Continuous/audio-affecting params get a
   **one-pole de-zipper** (single-pole exponential smoother, the same kind as the
   ADR-009 VV-15 drift output smoother). Stepped params (choice/enum selectors)
   and structural params get **no value smoothing**; where a switch is *audibly
   discontinuous*, the audible quantity is handled by the **ADR-005 click-safe CV
   crossfade** (ADR-005 §C7 / consequences) — precompute/blend both branches and
   crossfade, branchless on the hot path, no allocation on switch. Where a switch
   is not audibly discontinuous (e.g. arp mode), it changes on the next
   control-rate tick with no smoothing at all.

2. **One-pole, per-class time constants (all `(PI)`, centralized).** The default
   de-zipper time constants by class are: pitch/tuning **~2 ms** (must track fast
   to stay in tune under MPE-lite and per-voice detune); cutoff and other fast
   sonic continuous params (resonance, env-mod depth, LFO depth) **~10 ms**;
   level/amplitude/VCA and mix/FX-wet **~15 ms** (gentle, click-avoiding); PW /
   PWM depth **~5 ms**; glide *amount* parameter (the user-facing time knob)
   **~20 ms** on the *parameter* — distinct from the modeled portamento slew
   itself, see point 5. These are the typical 5-20 ms band with pitch as the
   sub-band exception; all are tunable defaults under the no-oracle lock, exposed
   as named constants in the calibration table for later retuning, never as a
   measured SH-101 spec.

3. **Default class is "no-smooth".** In the registry, a parameter's smoothing
   class **defaults to STEPPED/no-smooth**; continuous de-zipper is an explicit
   opt-in per param. This makes the dangerous mis-classification direction
   (smearing a discrete index through wrong values) impossible by omission, and
   makes "this param is smoothed" an auditable, reviewed decision in the single
   registry rather than an accident.

4. **Structural params are never smoothed.** `mw101.os.factor`,
   `mw101.voice.mode`, `mw101.voice.count`, `mw101.unison.count` (ADR-008 C7) and
   any other buffer-reallocating / topology-changing param MUST NOT have a
   smoother; they apply via prepareToPlay-style reconfiguration. "Smoothing" them
   is meaningless and is forbidden, not merely defaulted off.

5. **Glide / portamento is DSP slew, not a de-zipper.** The documented analog
   portamento / S&H RC slew between held pitch steps (ADR-005 §2, §3) is the
   glide *engine* and is owned by the control-rate / voice path — it is NOT a
   parameter de-zipper and MUST NOT be replaced or duplicated by one. Only the
   user-facing glide-*time* control value is de-zippered (so dragging the glide
   knob doesn't zipper), per the ~20 ms entry in point 2. This keeps the one
   circuit-real slew honest and prevents double-smoothing the pitch.

6. **Cadence aligns with the control-rate tick; boundaries are CLASS-EXACT.**
   De-zippers advance on the control-rate tick / per-block cadence defined by
   ADR-005 / ADR-016 (MODERN-SMOOTH default), not at an independent rate. The
   one-pole audio *value* is CLASS-FP (ADR-013: bit-exact on arm64, FP-tolerance
   on Linux/Windows), but the **block-boundary update bookkeeping** is the
   CLASS-EXACT "parameter-smoothing block boundaries" path (ADR-013 §Layer 2 /
   C5) and MUST be bit-identical on macOS arm64 and Linux x64. A snap-to-target
   threshold terminates the exponential tail deterministically so the integer
   "is-smoothing" bookkeeping is reproducible across platforms.

7. **RT-safe, no oracle, single source of truth.** All smoother state is POD,
   sized and reset at `prepareToPlay`, runs on the audio thread with no heap
   allocation and no locks (re-affirming the owner lock and ADR-013 C19). The
   smoothing class and the per-class time constant are declared in the ADR-008
   registry and its referenced calibration table — one edit retunes a class
   everywhere; no smoothing time is inlined at a DSP call site.

This honors the owner locks: no-oracle (all constants `(PI)`, no measured-spec
claim, the one documented slew kept as glide DSP), RT-safe (POD smoothers at
prepareToPlay, no audio-thread alloc/lock), and the bit-exact bless / Linux gate
(boundaries CLASS-EXACT, values CLASS-FP per ADR-013). It reconciles ADR-008
(registry-declared, structural excluded), ADR-009 (same one-pole smoother kind),
ADR-005/ADR-016 (crossfade for switches, control-rate cadence) and ADR-013
(CLASS-EXACT boundary pins).

## Consequences

This commits us to:

- A smoothing-class column plus a per-class time-constant block in the ADR-008
  registry / calibration table, set per parameter at authoring time and reviewed
  in PRs; the default of STEPPED/no-smooth means every smoothed param is an
  explicit, auditable opt-in.
- A single one-pole de-zipper implementation (shared kind with ADR-009 VV-15)
  with a deterministic snap-to-target threshold, sized at `prepareToPlay`,
  advancing on the ADR-005/ADR-016 control-rate cadence.
- A CLASS-EXACT golden that pins the per-class de-zipper block boundaries and the
  integer "is-smoothing"/snap bookkeeping bit-for-bit across macOS arm64 and
  Linux x64 (ADR-013 C5), plus a paired positive/negative property test (a
  continuous param de-zippers a step input; a stepped param does NOT smear, per
  ADR-013 C4).
- Reusing the ADR-005 CV crossfade as the *only* mechanism for click-safe audible
  switching, rather than adding a second value-smoothing path for selectors.

This forecloses / makes harder:

- A single global "smoothing time" knob: there is intentionally no one constant;
  retuning is per-class. A future user-facing "smoothing amount" macro, if ever
  wanted, would scale the table rather than replace it (a new ADR).
- Smoothing structural/stepped params: explicitly forbidden for structural,
  default-off for stepped; a future param that genuinely needs continuous
  morphing between discrete states would require its own crossfade design, not a
  smoother on the index.
- A free re-tune of feel: changing a per-class time constant is a localized edit
  in the calibration table but, because the de-zipper block boundary is a
  CLASS-EXACT/CLASS-FP golden surface, it forces a re-bless with a `BLESS_REASON`
  (ADR-013 Layer 3), not a silent change.

Owner ratification item: none. The time constants are `(PI)` tunable defaults
under the existing no-oracle lock, the structural/stepped exclusions and the CV
crossfade reuse are already implied by ADR-008 and ADR-005, and no new
user-facing scope or expectation is introduced — this ADR only centralizes a
contract the prior ADRs left implicit.

## Contract

Normative rules the backlog implements verbatim. "MUST" / "MUST NOT" are
binding. All time constants are `(PI)` tunable defaults (no measured-spec
claim), declared in the ADR-008 calibration table, never inlined.

| # | Param class (examples) | Smoothing kind | Default time constant | Rule |
|---|---|---|---|---|
| S1 | Pitch / tuning (VCO pitch, fine tune, per-voice detune target) | One-pole de-zipper | ~2 ms `(PI)` | MUST de-zipper fast so MPE-lite bend and per-voice detune stay in tune; MUST NOT lag perceptibly. |
| S2 | Fast sonic continuous (VCF cutoff, resonance, env-mod depth, LFO depth) | One-pole de-zipper | ~10 ms `(PI)` | MUST de-zipper; covers the acid/bass cutoff-sweep idiom without zipper under coarse host automation. |
| S3 | Pulse width / PWM depth | One-pole de-zipper | ~5 ms `(PI)` | MUST de-zipper; fast enough to keep PWM motion crisp, slow enough to kill edge-jump zipper. |
| S4 | Level / amplitude / VCA / mix / FX wet | One-pole de-zipper | ~15 ms `(PI)` | MUST de-zipper gently to avoid clicks on jumps; MUST NOT click on a hard 0->1 automation step. |
| S5 | Glide-time *parameter* (user knob) | One-pole de-zipper | ~20 ms `(PI)` | The CONTROL VALUE is de-zippered. This is NOT the portamento slew; see S6. |
| S6 | Portamento / S&H pitch slew (between held steps) | NOT a de-zipper (glide DSP, ADR-005 §2/§3) | n/a | MUST be the modeled analog RC slew owned by the control-rate/voice path; MUST NOT be smoothed by, or duplicated as, a parameter de-zipper. |
| S7 | Stepped / choice selectors (VCO range, sub mode, LFO shape, arp mode) | No value smoothing; ADR-005 crossfade where audible | n/a | The index MUST NOT be smoothed (no smearing through wrong values). Audibly-discontinuous switches MUST use the ADR-005 click-safe CV crossfade (branchless hot path, no alloc); non-audible switches change on the next control-rate tick. |
| S8 | Structural / topology (`mw101.os.factor`, `mw101.voice.mode`, `mw101.voice.count`, `mw101.unison.count`) | None (forbidden) | n/a | MUST NOT have any smoother; applied only via prepareToPlay-style reconfiguration (ADR-008 C7). |
| S9 | Default class for any new param | STEPPED / no-smooth | n/a | A new param's smoothing class MUST default to no-smooth; continuous de-zipper (S1-S5) is an explicit per-param opt-in in the ADR-008 registry. |
| S10 | Smoother kind & determinism of tail | One-pole exponential + snap threshold | — | MUST share the one-pole kind with ADR-009 VV-15; MUST snap to target at a fixed threshold so the integer "is-smoothing" state is deterministic. |
| S11 | Update cadence | — | — | De-zippers MUST advance on the ADR-005/ADR-016 control-rate tick / per-block cadence, not an independent rate. |
| S12 | Golden boundary class | — | — | The de-zipper VALUE is CLASS-FP (ADR-013 C6/C7); the block-boundary update + snap bookkeeping is CLASS-EXACT and MUST be bit-identical on macOS arm64 and Linux x64 (ADR-013 C5), with a paired positive/negative property test (ADR-013 C4). |
| S13 | Source of truth | — | — | Smoothing class and per-class time constant MUST be declared in the ADR-008 `ParamDefs` registry / calibration table; no smoothing time may be inlined at a DSP call site. |
| S14 | Real-time safety | — | — | All smoother state MUST be POD, sized/reset at `prepareToPlay`; smoothing runs on the audio thread with no heap allocation and no locks (ADR-013 C19). |
