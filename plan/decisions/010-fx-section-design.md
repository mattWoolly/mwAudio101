<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 010: Built-in FX Section (Chorus / Delay / Drive)

Status: accepted (Drive placement (post-voice) + PDC owned by ADR-017)
*Refined post-acceptance — see ADR-017.*
Date: 2026-06-17

## Context

mwAudio101 ships a post-voice FX section as part of the locked "modern
essentials" scope (Chorus / Delay / Drive). This ADR fixes the topology,
signal-chain order, mono-to-stereo widening strategy, bypass semantics, and the
per-block parameter set. The brief is to keep it "tasteful and not
identity-diluting."

Forces:

- The SH-101 is a **monophonic, single-VCO** instrument. "The SH-101 is mono"
  is an owner-locked identity statement; any stereo behavior is an addition, not
  a reproduction.
- The owner-locked modeling contract is circuit-accurate analog modeling of the
  **voice** ("modeled from documented circuit behavior"); there is **no
  physical-unit oracle**, and recordings are secondary local-only cross-check.
  The FX have no documented SH-101 circuit at all and therefore cannot live
  under the same modeling-accuracy contract as the voice.
- macOS arm64 is the **reference/bless + bit-exact** platform; Linux x64 is a
  co-required hard gate. Whatever we add must not make the bit-exact reference
  fragile.
- Real-time safety is locked: **no heap allocation, no locks on the audio
  thread.**
- The cultural research is unusually prescriptive about where identity lives and
  where it does not, which directly constrains FX legitimacy (see Decision).

This ADR touches three owner-locked decisions and **re-affirms** each rather than
reversing any: (1) "the SH-101 is mono" — preserved by keeping the dry path
strictly mono; (2) the bit-exact reference gate — preserved by placing FX
*outside* the blessed signal contract; (3) RT-safety — preserved by
fixed-topology, preallocated, lock-free DSP.

## Options considered

### Persona: Product (market-expected / musical / stereo width / tempo-sync)

Approach: three discrete, individually true-bypassable blocks in a fixed
opinionated "console" order Drive -> Chorus -> Delay; small musical param sets
(3-4 knobs + a mode switch per block); tempo-syncable delay as the headline
modern feature; a global Width/mono-collapse at the chain output; FX state baked
into the ~64 factory presets so the instrument sounds finished out of the box
(acid presets: drive on, chorus off; "Strings": chorus I+II; pads: delay +
chorus).

Pros: closes the competitive gap against other SH-101 clones; culturally honest
per the research (drive is in the documented acid recipe; the Juno/IR3109
filter-family link makes a Juno-style chorus in-family); width is opt-in and
reversible; small knob counts keep it tasteful; baked-in preset FX drives demos.

Cons: a fixed chain order will frustrate power users; baking FX into factory
presets risks letting the instrument be judged on a "finished" sound rather than
its bare voice; tasteful BBD-flavored chorus/delay are harder to voice than clean
digital versions; tempo-sync correctness across hosts is a known bug source.

Adopted: the fixed Drive -> Chorus -> Delay order; per-block + master true
bypass; small musical param sets; tempo-syncable delay with note divisions;
opt-in/reversible width with a global mono-collapse; full automation on every
param. Adopted partially: FX baked into presets (see split resolution below).

### Persona: DSP (algorithm quality; anti-aliased drive)

Approach: same three stages in the same fixed order, with the critical quality
constraint that **Drive runs inside the engine's existing oversampling** (the
nonlinearity generates harmonics above Nyquist and must be band-limited *before*
any interpolation-based stage can fold them back in). Drive = asymmetric
waveshaper + pre-/de-emphasis tilt + DC blocker, at the oversampled rate, reusing
the voice oversampler (not a second path). Chorus = Juno-style BBD-emulation, two
anti-phase modulated delay lines panned hard L/R = the primary, identity-legit
widener. Delay = single mono core to stereo output, fractional-delay read,
one-pole damping LPF (+ gentle saturation) in the feedback path, tempo-synced.
Mono-to-stereo confined to chorus/delay wet; dry summed equally L/R so FX-off is
bit-exact mono.

Pros: drive-first-at-oversampled-rate is the correct anti-aliasing placement —
"this is where SH-101 saw grit lives or dies"; the Juno chorus is
identity-reinforcing via the documented IR3109 filter-family lineage; confining
width to wet keeps FX-off honestly bit-exact mono; fixed order keeps gain-staging
predictable and shrinks QA combinatorics; all stages are fixed-topology and
RT-safe.

Cons: oversampling the drive adds CPU and a small decimation-FIR latency that
must be reported (PDC) and held constant for the bit-exact reference; BBD
coloration is taste-heavy with no oracle; anti-phase chorus taps can partially
cancel in a mono fold-down; a conservative last-position delay will feel
underpowered to dub users.

Adopted: the full topology, the drive-inside-oversampling placement, the
dry-summed-to-stereo rule, the feedback-path damping + clamp, and the
fixed/reported/constant FX latency requirement for bit-exact stability. This
persona's topology is the spine of the Decision.

### Persona: authenticity-minimal (FX as an optional layer)

Approach: identical fixed Drive -> Chorus -> Delay order and identical mono-dry /
stereo-born-in-chorus-delay rule, plus two stronger stances: (1) treat the entire
FX block as living **outside the modeled-signal-path contract** — the blessed,
bit-exact mono voice path is oscillators -> sub -> mixer -> IR3109 VCF -> VCA ->
drift -> oversampled output, and FX are a sweetener tapped from the mono sum
*after* that; (2) ship **default-OFF** with dry-first preset voicing, a Width=0
true-mono collapse on each stereo block, and a global hard **Mono Output** switch
for club/broadcast mono rigs. No reverb (argued to be the most identity-diluting
and best left to the host).

Pros: keeps the bit-exact arm64 gate robust because FX are explicitly excluded
from the golden-reference contract; honors "the SH-101 is mono" literally with a
guaranteed phase-coherent mono fold-down; chorus is the one FX with defensible
Roland-family lineage; drive maps to the documented acid "overdrive for grit";
minimal fixed param set is cheap, fully automatable, trivially RT-safe; FX runs
once on the mono sum (post-poly/unison), so cost is independent of voice count.

Cons: less flexible than a reorderable/parallel rack; no reverb will read as an
omission to some; Width-collapse discipline forgoes some lush demo presets; a
single static waveshaper is less characterful than a bespoke modeled distortion;
binding stereo strictly to chorus/delay means "width without chorus/delay
character" has no path (by design).

Adopted: the **FX-outside-the-modeled-signal-contract** framing (this is what
protects the bless gate); the explicit Width=0 true-mono collapse per stereo
block; the global **Mono Output** switch; the no-reverb scope cut; the
single-tasteful-waveshaper drive (not a 303-distortion clone, since the research
flags "blip"/extra-character idioms as general-practice, not sourced SH-101
fact). Not adopted wholesale: a hard global default-OFF for *all* presets (see
split resolution).

### Split and resolution

The panel was unanimous on the hard architecture: fixed Drive -> Chorus -> Delay,
mono dry path, stereo born only in chorus/delay wet, true per-block + master
bypass, drive inside the existing oversampler, FX outside the bit-exact contract.

The only real split was on **defaults / preset voicing**: Product wanted FX baked
into factory presets for a finished out-of-box sound; authenticity-minimal wanted
default-OFF and dry-first so the instrument is judged on the bare voice.

Resolved as a middle path, anchored to the research's preset taxonomy
(docs/research/11-cultural-influence.md §7.1): the **engine/global default is FX
OFF**, and the **plugin's default/INIT patch is dry**, so a reviewer hears the
real instrument first (authenticity-minimal's concern). But individual factory
presets MAY ship with genre-appropriate FX engaged where the research itself
prescribes it — specifically the "PWM / Strings" category, which §4.5 and §7.1
explicitly define as a "mono PWM + chorus stylization" (Product's concern). FX is
never *forced*, is always one switch from bit-exact mono, and "Strings"-type
presets must remain labelled as a mono stylization per §4.5.

## Decision

Ship a **fixed-order, post-voice, mono-in FX section: Drive -> Chorus -> Delay**,
processed **once on the mono voice sum** (post poly/unison/drift, post the
engine's oversampled voice output), with each block individually **true-bypassed**
(early-out skip, not dry/wet=0) plus a single master FX bypass. The FX block lives
**outside the modeled-signal-path / bit-exact contract**; with all FX off the
output is bit-identical to the blessed mono voice. This re-affirms — does not
reverse — the locks on "the SH-101 is mono," the bit-exact reference, and
RT-safety.

**Why this order and these three, per the research:**

- **Drive first.** Overdrive is part of the *documented* acid idiom, not
  gold-plating: the MusicRadar acid-house recipe explicitly adds "overdrive for
  grit" and Roland's own "Acid Lead" guidance corroborates the squelchy
  resonant voicing (docs/research/11-cultural-influence.md §4.3). Drive must hit
  the raw mono squelch before any time/modulation smearing. Crucially, the drive
  nonlinearity runs **inside the engine's existing oversampling**
  (docs/research/10-dsp-modeling-techniques.md §5, ~2x for the nonlinear path)
  so its generated harmonics are band-limited before any interpolation-based
  stage can alias them back in — reusing the voice oversampler, not adding a
  second path. Drive is a single tasteful asymmetric waveshaper, **not** a
  bespoke 303-distortion clone, because §4.6 flags "blip"/extra-character idioms
  as general subtractive-synthesis practice, not sourced SH-101 fact.

- **Chorus second, as the primary widener.** A Juno-style BBD chorus is
  circuit-family-*legitimate*, not a costume: the SH-101's self-oscillating VCF
  is built on the Roland **IR3109**, shared with the **Juno-6/60 and Jupiter
  family** (docs/research/11-cultural-influence.md §4.2, §5 filter-lineage row),
  and §4.5 / §7.1 explicitly prescribe "PWM + chorus" as the honest mono
  stylization for the "Strings" category. Chorus is the natural, in-family
  mono-to-stereo widener via two anti-phase LFO-modulated delay lines panned
  hard L/R. (Per §6.1 the "same filter as the TB-303" claim is FALSE and FROZEN;
  no 303-filter descriptor is implied here.)

- **Delay last, conservative.** Tempo-synced delay is the single most
  market-expected modern feature and serves the SH-101's *defining performance
  idiom* — the onboard sequencer/arp riff, trigger-synced from an 808 with a 303
  counter-riff, as on "Voodoo Ray" (docs/research/11-cultural-influence.md §4.7,
  §4.8). Delay is **not** a documented SH-101 circuit idiom, so it sits last,
  stays conservative, and is trivially bypassable to keep it from diluting
  identity.

**Mono-to-stereo:** the dry/voice path is strictly mono through Drive and is
summed **equally to L/R**, so "all dry" is honestly mono. Stereo is *born only*
inside the chorus and delay wet content. Each stereo block exposes a Width whose
0 position is a true centered-mono collapse, and there is a global hard **Mono
Output** switch that forces a guaranteed phase-coherent mono sum (club/broadcast
rigs, and the always-recoverable "pure mono SH-101" guarantee per the lock). No
reverb in v1 (left to the host).

**Defaults:** engine/global default and the INIT patch are dry; factory presets
default to dry-first voicing, with genre-appropriate FX engaged only where the
research prescribes it (notably "PWM / Strings" = mono PWM + chorus,
docs/research/11-cultural-influence.md §4.5, §7.1). Every FX parameter is fully
host-automatable; FX state is stored per preset.

## Consequences

Commits us to:

- A fixed serial Drive -> Chorus -> Delay graph with no reorder/parallel UI in
  v1; gain-staging and QA combinatorics stay bounded.
- Reusing the voice oversampler for the drive stage and reporting a **fixed,
  constant** FX latency via the host PDC path; this latency must be held
  invariant so the macOS arm64 bit-exact reference stays stable.
- A hard contract that **FX off == bit-exact blessed mono voice**: FX are
  excluded from the golden-reference comparison and must early-out (skip the DSP)
  when bypassed so bypassed blocks cost ~0.
- Preallocating all delay/chorus ring buffers at max time/depth in
  prepareToPlay; tempo-sync only moves an interpolated read pointer; feedback
  paths use ScopedNoDenormals/flush-to-zero and clamp feedback < 1.0; all param
  changes are smoothed/crossfaded (delay-time glide) to avoid zipper/pops; no
  heap, no locks on the audio thread (param updates via atomics).
- FX running once on the post-voice mono sum, so FX cost is independent of voice
  count.

Forecloses / makes harder:

- Power-user routings (delay-into-drive, parallel chains, self-oscillating dub
  delay) are intentionally not served in v1.
- No reverb; some users will expect one (we treat it as host territory).
- "Width without chorus/delay character" has no path (stereo is bound to those
  two blocks by design).
- BBD chorus/delay coloration and the drive curve are taste-heavy targets with
  **no physical-unit oracle**; calibration leans on Juno/BBD literature plus
  local recording cross-check — softer ground than the voice, and explicitly
  outside the modeling-accuracy contract.
- Anti-phase chorus widening can partially cancel under downstream mono
  fold-down; the global Mono Output switch and Width=0 collapse are the required
  mitigations and must be verified with a mono-sum check.

Owner ratification item: confirm the resolved default policy — engine/INIT
default FX OFF and dry-first preset voicing, but allowing FX (notably chorus on
the "PWM / Strings" category per §4.5/§7.1) to be baked into individual factory
presets. This is the one point where the panel split and it carries
user-expectation/identity risk beyond the locks.

Owner ratification item: confirm **no reverb in v1** as an accepted scope cut.

## Contract

Normative behavior the backlog implements verbatim. "Bit-exact mono" means
sample-identical to the blessed mono voice output on the macOS arm64 reference.

| ID | Condition | Required behavior |
| --- | --- | --- |
| FX-1 | Master FX bypass ON (or all three blocks bypassed) | Output is bit-exact mono voice (dry summed equally to L/R); FX DSP fully skipped (early-out), ~0 cost. |
| FX-2 | Signal-chain order | Always Drive -> Chorus -> Delay. No runtime reorder. |
| FX-3 | Per-block bypass | A true early-out that skips that block's DSP entirely; not dry/wet=0. Bypassed block adds ~0 CPU. |
| FX-4 | Dry path | Strictly mono through Drive; summed equally L/R. Stereo content is produced only inside Chorus and Delay wet signals. |
| FX-5 | Drive stage | Runs inside the engine's existing oversampler (>= 2x). Params: Drive (input gain), Tone (pre/de-emphasis tilt), Output (makeup), On/Off. DC blocker after the shaper. Single asymmetric waveshaper; no multi-mode amp sim. |
| FX-6 | Chorus stage | Juno-style BBD: two anti-phase LFO-modulated fractional-delay lines panned hard L/R. Params: Mode (I / II / I+II / Off), Rate, Depth, Width, Mix. Width=0 => centered true mono. |
| FX-7 | Delay tempo sync | When Sync=ON, delay time = host-BPM note division from the set {1/4, 1/8, 1/8., 1/8T, 1/16, 1/16T, dotted/triplet variants}; when Sync=OFF, free time in ms. Conversion cached; recomputed only on tempo/division change. |
| FX-8 | Delay stage | Single mono core to stereo out, fractional-delay read with interpolation; one-pole damping LPF (+ gentle saturation) in feedback path. Params: Time (division or ms), Feedback (clamped < 1.0), Damp/Tone, Width, Mix, Sync On/Off, Ping-pong On/Off, On/Off. Width=0 => centered mono. |
| FX-9 | Global Mono Output switch | When ON, force a phase-coherent mono sum at the chain output regardless of FX width settings. |
| FX-10 | RT-safety | All buffers preallocated at max in prepareToPlay; no heap alloc, no locks on the audio thread; param updates via atomics; feedback paths guarded with flush-to-zero / ScopedNoDenormals. |
| FX-11 | Parameter changes | All FX params smoothed; delay-time changes crossfade or pointer-glide to avoid zipper/clicks. Every FX param is host-automatable. |
| FX-12 | Latency | FX-introduced latency (drive decimation FIR) is fixed, reported via setLatencySamples for host PDC, and held constant across builds for bit-exact reference stability. |
| FX-13 | Defaults | Engine/global default and INIT patch: FX OFF / dry. Factory presets default dry-first; FX may be engaged per-preset only where the research prescribes it (e.g. "PWM / Strings" = mono PWM + chorus). FX state stored per preset. |
| FX-14 | Scope | No reverb in v1. |
