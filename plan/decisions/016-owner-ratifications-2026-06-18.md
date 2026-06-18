<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 016: Owner ratifications - out-of-box defaults

Status: accepted
Date: 2026-06-18

## Context

Several earlier ADRs deliberately surfaced "Owner ratification item:" callouts:
out-of-box behaviors that are faithful to the documented SH-101 but sit at the
boundary of what a buyer of a "modern reimagined" instrument expects, and that
the judges flagged for an explicit owner sign-off rather than silently shipping.
This ADR records the owner's 2026-06-18 ratifications as the authoritative
resolution of those callouts, so the backlog has one normative place to read the
shipped defaults from.

The question this ADR answers is therefore not "what is technically possible" -
that is already decided in ADR-005 (control rate / 6-bit quantization),
ADR-006 (voice / poly / unison), ADR-009 (vintage variance / drift), and
ADR-012 (MIDI / MPE / tuning). The mechanisms, contracts, and toggles in those
ADRs are unchanged. The only thing in flight was: with two valid poles built and
exposed, which pole does the INIT/out-of-box state select, and where does each
ADR's "default" clause land. That is a product-positioning decision, and the
owner owns it.

Owner-lock touched and re-affirmed (not reversed): the project remains
**circuit-accurate, "modeled from documented circuit behavior," with no
physical-unit oracle**, JUCE/C++20, RT-safe (no heap alloc / no locks on the
audio thread), macOS arm64 reference/bless (FP bit-exact) + Linux x64 hard gate
+ Windows goal, GPL-3.0, modern-only UI at maximum trademark distance, with the
locked feature scope (faithful mono + poly/unison, oversampling, drift,
Chorus/Delay/Drive, host-synced arp + 100-step seq, automation, MPE-lite, ~64
presets). Nothing here expands scope. The faithful pole of every behavior below
is fully built, fully reachable, and remains the bit-exact reference where a
reference is defined; the ratifications only choose which pole the default/INIT
state lands on, and make the faithful pole an explicit, labeled toggle.

This ADR reconciles and sets defaults for: ADR-005 §Decision/§Contract (Vintage
Control macro), ADR-006 §Decision/§Contract (mode = MONO/UNISON/POLY),
ADR-009 §Decision/§Contract (Vintage/Age macro + INIT state), and ADR-012
§Decision/§Contract (velocity, MPE-lite scope, tuning). It also records the
owner's accept-without-veto of the standing positions in ADR-008 (sequencer),
ADR-010 (FX / reverb), ADR-013 (honesty labels), and ADR-015 (UI / no faceplate).

## Options considered

The substantive options for each behavior were already debated to a panel
resolution inside the originating ADR; this section contrasts each ratified
**default** against the judge's original **draft default** in that ADR, and
states why the owner choice is the right one for a modern reimagined market
product. The losing pole is not deleted - it ships as a one-action toggle.

### Default control resolution (vs ADR-005 draft default)

- Judge's draft default (ADR-005): **VINTAGE control on the mono reference** -
  true 6-bit additive integer CV so portamento/glide stair-steps on
  ~1.6-cent-spaced steps, plus the coarse ~2 ms control tick. ADR-005's own
  ratification callout admitted this "can read as slightly out of tune or
  sluggish rather than as vintage character."
- Owner ratification: **default = MODERN-SMOOTH hi-res control**; the authentic
  6-bit "Vintage" stepping is a labeled toggle (the existing ADR-005 Vintage
  Control macro), one action away.
- Why owner wins for a modern reimagined product: the first ten seconds of a
  trial decide the purchase. A continuous hi-res pitch/CV path is what a buyer
  reaching for poly/unison/MPE/automation expects, and it is the only pole that
  is musically coherent with those locked modern features (ADR-005 already
  auto-engages MODERN for poly/unison/MPE/sub-cent-automation, so VINTAGE-default
  is only ever true on the narrow mono/single-voice/non-MPE path anyway). Making
  smooth the default removes the "is this broken / out of tune?" first-run risk
  the judge flagged, while losing nothing: the authentic stair-stepped
  fingerprint is fully preserved as a deliberate, discoverable, automatable
  Vintage toggle and remains the bit-exact reference variant. Default-modern,
  authentic-on-demand is the honest "reimagined" stance; default-vintage would
  optimize for a fidelity audience that the toggle already serves.

### Default velocity resolution (vs ADR-012 draft default)

- Judge's draft default (ADR-012): **velocity OFF** - faithful to the SH-101
  keyboard, which has no velocity sensing. ADR-012's ratification callout warned
  this "risks first-run 'no dynamics' confusion unless presets/UI signpost it."
- Owner ratification: **default = velocity ON** (velocity -> VCA level and VCF
  cutoff amount); faithful no-velocity is a switch, one action away.
- Why owner wins for a modern reimagined product: a modern player on a modern
  controller expects keys to respond to how hard they are played; a synth that
  is silent to dynamics out of the box reads as a defect, not as authenticity.
  Routing velocity to VCA/VCF amount lands on the documented physical nodes
  ADR-012 already blesses (the VCA level and the SDT-1000-scaled cutoff CV path),
  so this is an additive default over real circuitry, not invented structure.
  The purist who wants the literal no-velocity SH-101 keyboard gets it with one
  switch, which also remains the correct setting for faithful A/B testing.

### Default voice mode (affirms ADR-006)

- ADR-006 already names MONO as the default and bless target. The owner affirms
  it: **default = MONOPHONIC** with authentic last/low-note priority via the
  coupled S7 KeyAssigner; Poly and Unison are one toggle away.
- Why this is right for the product: mono is the faithful heart of the
  instrument and the bit-exact reference path; it is also what makes the synth
  *sound like an SH-101* on first play. Modern players still get poly/unison
  immediately via the mode control. There was no competing draft to override
  here - this is an affirmation, recorded so the backlog reads the default from
  one place.

### INIT patch drift (sets ADR-009 INIT default)

- ADR-009's `vintage.age` macro default is **0** (in tune on load), and that
  parameter default is unchanged. The open question ADR-009 left was what the
  shipped **INIT patch** selects.
- Owner ratification: the **INIT patch** ships with **subtle analog drift ON and
  the Age macro low** (a small, musical amount), rather than dead-clean.
- Why owner wins for a modern reimagined product: a hair of drift is the
  difference between "digital and sterile" and "analog and alive" on the very
  first note, and it is the single cheapest way to communicate that this is an
  analog-modeled instrument. "Low," not zero, keeps the synth unmistakably in
  tune (honoring ADR-009 §6/§10.2's in-tune-on-load intent) while still breathing.
  This sets a *patch* default; the `vintage.age` *parameter* default of 0 and the
  whole drift mechanism/contract in ADR-009 are untouched, and a true dead-clean
  sound is one macro move away.

### Accepted without veto (standing positions affirmed)

The owner reviewed and accepted, without change, these standing ADR positions,
recorded here so they are not re-litigated:

- FX (Chorus/Delay/Drive) engine-default **OFF**, but **bakeable into factory
  presets**; **no reverb in v1** (ADR-010).
- Sequencer 100 steps are **saved patch/project state, not per-step host
  automation lanes** (ADR-008).
- MPE-lite = per-note **pitch + one pressure destination, lower zone only**
  (ADR-012).
- UI is **modern-only, no faceplate skin**, maximum trademark distance (ADR-015).
- **Honesty labels everywhere**: every control tagged documented-vs-embellishment
  / vintage-vs-modern (ADR-013, ADR-009 §7).

## Decision

Record the four owner ratifications as the authoritative out-of-box defaults.
The mechanisms remain exactly as specified in the cited ADRs; this ADR only fixes
which pole the shipped default/INIT state selects and makes the faithful pole an
explicit toggle.

1. **Control = MODERN-SMOOTH hi-res by default.** The shipped default sets the
   ADR-005 Vintage Control macro to its **MODERN** pole (continuous float pitch,
   clean smoothed CV, host-synced sample-accurate arp/seq). The authentic 6-bit
   additive-integer stepping (stair-stepped portamento, coarse ~2 ms tick) is the
   **VINTAGE** pole, a labeled, automatable toggle. This **supersedes the
   default clause of ADR-005** (which set VINTAGE as the mono default). Everything
   else in ADR-005 - the always-modeled single 6-bit-DAC/4052/S&H core, the
   per-feature MODERN auto-engage, the jitter-OFF fixed-tick VINTAGE variant as
   the macOS arm64 / Linux co-gate bit-exact reference, the click-free crossfade
   on macro automation - is unchanged. (ADR-005 §Decision items 1-6, §Contract
   C1-C7.)

2. **Velocity = ON by default.** The shipped default routes MIDI velocity to
   **VCA level and VCF cutoff amount** (the documented physical nodes per
   ADR-012 §4 / `docs/research/08-power-cv-io.md` §7.2, §5.3). The faithful
   **no-velocity** behavior (SH-101 keyboard has none) is a switch. This
   **supersedes the default clause of ADR-012** (C9, which set velocity OFF as the
   default and exposed it only as an opt-in mod source). The velocity routing
   mechanism, smoothing, and RT-safety in ADR-012 are otherwise unchanged.

3. **Voice mode = MONOPHONIC by default.** The shipped default is **MONO** with
   authentic last/low-note priority via the single coupled-S7 KeyAssigner; Poly
   and Unison are one mode-control action away. This **affirms ADR-006**
   (§Decision item 3, MONO = default and bless target; §Contract C1-C8).

4. **INIT patch = subtle analog drift ON, Age low.** The factory INIT patch
   ships with a small, musical amount of analog drift engaged (Age macro low, not
   zero). This **sets the INIT-patch default for ADR-009**. The `vintage.age`
   *parameter* default remains **0** (ADR-009 §Contract VV-1; in tune on load),
   and the entire three-tier drift mechanism, per-instance seed, and contract are
   unchanged; INIT is a *patch* that moves the macro low, not a change to any
   parameter default or to the drift DSP.

5. **Accepted-without-veto positions affirmed** exactly as stated in
   ADR-008 (sequencer = saved state, not automation lanes), ADR-010 (FX
   engine-default OFF but bakeable into factory presets; no reverb v1), ADR-012
   (MPE-lite = pitch + one pressure destination, lower zone), ADR-013 (honesty
   labels everywhere), and ADR-015 (modern-only UI, no faceplate skin).

Rationale: the originating ADRs each built **both** poles and surfaced the
default as an owner question precisely because the choice is product positioning,
not circuit fidelity. For a "modern reimagined" instrument the out-of-box state
must feel modern, responsive, and alive on the first note - smooth pitch,
velocity dynamics, a touch of analog drift - while keeping a faithful SH-101 a
single toggle/mode/macro move away (and keeping the faithful pole as the
bit-exact reference). The owner's four choices select exactly that posture
without disturbing any modeled mechanism, RT-safety guarantee, or bless/co-gate
reference.

## Consequences

Commits us to:

- A factory INIT/out-of-box state that selects, on the relevant ADR-005 /
  ADR-012 / ADR-006 / ADR-009 surfaces: MODERN control, velocity ON
  (-> VCA/VCF), MONO voice mode, and subtle drift (Age low). The backlog
  implements the Contract table below verbatim for the INIT patch and the
  factory-preset baseline.
- A single labeled toggle/macro/mode path to the faithful pole of each default,
  honoring the ADR-013 honesty-labels requirement so users always know which pole
  they are in.
- The faithful poles remaining the bit-exact reference where a reference is
  defined (ADR-005's jitter-OFF fixed-tick VINTAGE variant stays the macOS arm64
  bless / Linux x64 co-gate reference; the velocity-OFF, mono path stays the
  faithful A/B baseline).

Supersession and set/affirm record (normative for the affected ADRs):

- **ADR-005 default clause is superseded by THIS ADR**: out-of-box control =
  MODERN-SMOOTH (not VINTAGE). All other ADR-005 decisions/contracts stand.
- **ADR-012 default clause is superseded by THIS ADR**: out-of-box velocity = ON
  (-> VCA/VCF), not OFF. All other ADR-012 decisions/contracts stand.
- **ADR-009 INIT-patch default is SET by THIS ADR**: subtle drift ON, Age low;
  the `vintage.age` parameter default of 0 is unchanged.
- **ADR-006 mono-default is AFFIRMED by THIS ADR**: out-of-box voice mode = MONO.

Forecloses / makes harder:

- The most-authentic SH-101 experience (6-bit stepping + no velocity + dead-clean
  tuning) is no longer the zero-action state; reaching it requires flipping the
  Vintage Control macro, the velocity switch, and the Age/drift macro. This is a
  deliberate, communicated tradeoff for the target market, mitigated by a clearly
  labeled "Vintage / Faithful" preset that sets all faithful poles at once.
- Factory presets and demos must be authored against the modern-default poles, so
  the preset library's "feel" is the modern reimagined one by default; a faithful
  preset bank is the counterbalance.

No new user-expectation/scope risk arises beyond what the originating ADRs
already flagged - this ADR is the owner's resolution of those exact callouts, so
no further "Owner ratification item:" is raised here.

## Contract

Normative table; the backlog implements this verbatim for the INIT patch and the
factory-default baseline. Each row gives the shipped default, the one-action path
to the faithful pole, and the governing ADR. "Toggle" is the existing control in
the cited ADR, not a new parameter.

| ID | Behavior | Shipped default (out-of-box / INIT) | Faithful pole (one action away) | Toggle / control | Governing ADR (this ADR's effect) |
| --- | --- | --- | --- | --- | --- |
| R-1 | Control rate / pitch quantization | MODERN-SMOOTH: continuous float pitch, clean smoothed CV, host-synced arp/seq | VINTAGE: true 6-bit additive-integer CV, stair-stepped portamento, coarse ~2 ms tick (bit-exact reference variant) | ADR-005 "Vintage Control" macro (VINTAGE pole) | ADR-005 default clause SUPERSEDED (was VINTAGE) |
| R-2 | Velocity | ON: velocity -> VCA level + VCF cutoff amount | OFF: no velocity response (faithful SH-101 keyboard) | ADR-012 velocity switch | ADR-012 default clause (C9) SUPERSEDED (was OFF) |
| R-3 | Voice mode | MONO: authentic last/low-note priority (coupled S7 KeyAssigner); bless target | (mono IS the faithful pole) Poly / Unison are the modern toggle | ADR-006 mode control (MONO/POLY/UNISON) | ADR-006 mono-default AFFIRMED |
| R-4 | Analog drift (INIT patch) | Subtle drift ON, Age macro LOW (in tune, but alive) | Dead-clean: Age = 0 / drift off (parameter default stays 0) | ADR-009 `vintage.age` macro | ADR-009 INIT-patch default SET (param default 0 unchanged) |

Invariants: no modeled DSP mechanism, RT-safety guarantee (no heap alloc / no
locks on the audio thread), or bit-exact bless/co-gate reference defined in
ADR-005/006/009/012 is altered by this ADR; only the default/INIT pole selection
changes. Every default's faithful pole stays labeled per ADR-013 so the active
pole is always discoverable.
