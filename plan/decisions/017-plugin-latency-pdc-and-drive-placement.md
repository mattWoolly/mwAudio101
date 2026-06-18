<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 017: Plugin latency (PDC) policy & Drive placement

Status: accepted
Date: 2026-06-18

## Context

Two earlier ADRs describe the Drive nonlinearity in mutually incompatible ways, and no ADR
yet owns the plugin's global plugin-delay-compensation (PDC) story. This ADR resolves both
in one place because they are the same physical question: *where do the broadband
nonlinearities live, and how much constant latency does that cost the host?*

The contradiction:

- **ADR-004 (oversampling strategy)** places "the Drive module" *inside the per-voice shared
  nonlinear zone*, in series after the BA662 VCA drive, with exactly one up/down conversion
  pair wrapping filter ladder + diode-clamp resonance + VCA drive + Drive (ADR-004 Decision
  point 2; Contract rows 4-9, esp. row 7 "Drive module ... same zone, series after VCA
  drive; no intermediate down/up").
- **ADR-010 (FX section)** places Drive as the **first stage of a post-voice, mono-in FX
  section** (Drive -> Chorus -> Delay), processed **once on the mono voice sum** after
  poly/unison/drift, explicitly *outside the modeled-signal-path / bit-exact contract*
  (ADR-010 Decision; Contract FX-2, FX-4, FX-5). ADR-010 §"DSP persona" assumed it would
  "reuse the voice oversampler," which only makes sense if Drive is per-voice — the very
  thing that conflicts with "once on the mono sum."

Both cannot be true: a stage cannot be both per-voice (inside ADR-004's zone) and once on the
mono sum (ADR-010's FX chain). The contradiction also leaks into latency: if Drive is
per-voice it costs nothing extra (it is already inside the per-voice zone's single up/down
pair), but if it is a separate post-voice oversampled stage it adds its *own* up/down pair
and group delay that must be reconciled with the PDC contract.

Owner locks this ADR touches and re-affirms (does not reopen):

- **Faithful mono SH-101 + modern essentials (the FX Drive/Chorus/Delay are essentials).**
  Re-affirmed: the *faithful voice* is the modeled circuit; the FX Drive is a modern
  essential layered after it, not part of the documented SH-101 circuit.
- **Circuit-accurate "modeled from documented circuit behavior"; NO physical oracle.**
  Re-affirmed: only the *voice* carries the modeling-accuracy contract; the FX Drive curve is
  taste-calibrated and outside it (consistent with ADR-010).
- **macOS arm64 reference/bless (FP bit-exact) + Linux x64 hard gate; FX-off audio is the
  blessed reference.** Re-affirmed and sharpened below: "FX-off" remains bit-exact, and the
  constant oversampled-zone group delay is declared part of that contract.
- **RT-safe: no alloc/lock on the audio thread.** Re-affirmed: a constant, worst-case-padded
  latency is sized in `prepare` and never changes on the audio thread.

This ADR reconciles ADR-004 and ADR-010, depends on ADR-001 (the `setLatencySamples` /
PDC path lives in `plugin/`; the core only owns deterministic group delay) and on
`docs/research/10-dsp-modeling-techniques.md` §5 (oversampling), §5.2 (IIR vs FIR phase),
and `docs/research/11-cultural-influence.md` §4.3 (Drive is a post-voice acid sweetener).

## Options considered

This is primarily a *reconciliation* of a contradiction (Drive placement) plus a *new* global
policy (constant PDC). Below are the honest options for each.

### A. Drive placement

**A1 — Drive per-voice, inside the ADR-004 zone (ADR-004's literal position).**

- Pros: zero extra latency (no second up/down pair); a single contiguous nonlinear chain per
  voice; matches ADR-004's "no intermediate down/up" elegance.
- Cons: directly contradicts ADR-010's "Drive runs once on the mono sum, post poly/unison,
  outside the bit-exact contract" — which is the *load-bearing* framing that keeps the FX
  block out of the golden-reference comparison. Per-voice Drive also makes FX cost scale with
  voice count (ADR-010 explicitly wanted FX cost independent of polyphony), and it pulls a
  taste-calibrated, no-oracle FX curve *inside* the modeling-accuracy / bit-exact voice
  contract, which is exactly what ADR-010 fought to prevent. It also conflates the
  *modeled* BA662 VCA drive (a real SH-101 circuit, genuinely per-voice) with the *FX* Drive
  (a modern-essential sweetener) under one name.

**A2 — Drive is a single post-voice FX stage on the mono sum, with its OWN 2x oversampling
(this ADR's resolution).**

- Pros: honors ADR-010's spine (FX outside the bit-exact contract, once on the mono sum, cost
  independent of voice count) and resolves ADR-004's overreach cleanly; cleanly separates the
  two distinct nonlinearities — the per-voice *modeled BA662 VCA drive* (stays in the ADR-004
  zone, it is real circuit) versus the *FX Drive sweetener* (post-voice, its own zone);
  anti-aliasing is still correct because the FX Drive gets its own 2x up/down pair before
  Chorus/Delay can fold its harmonics back in (ADR-010's DSP-persona requirement preserved,
  just on a dedicated FX oversampler rather than "reusing the voice oversampler").
- Cons: a *second* oversampler pair (one per-voice in the zone, one post-voice for FX Drive)
  and therefore a second source of constant group delay to declare; "reuse the voice
  oversampler" language in ADR-010 §DSP must be corrected to "its own FX-rate oversampler."

A2 wins. The decisive reason is the bit-exact/owner-lock argument: ADR-010's whole reason for
existing is to keep the FX block *outside* the blessed contract so the macOS-arm64 reference
stays robust. Putting the FX Drive per-voice (A1) drags a no-oracle taste curve back inside
that contract and makes FX cost scale with polyphony — reversing two ADR-010 commitments. The
correct reading is that ADR-004 over-scoped its zone: the *per-voice* nonlinear zone is the
modeled circuit only (IR3109 ladder + diode-clamp resonance + BA662 VCA drive); the FX Drive
was never a circuit element and does not belong in it.

### B. PDC / reported-latency policy

**B1 — Report dynamic latency (change `setLatencySamples` when FX toggle on/off or Quality
tier changes).** Pros: reports the literal current latency, saving a few samples when FX/HQ
are off. Cons: most hosts re-launch PDC and may glitch, re-flush, or re-render on any latency
change; automation lanes and bounced audio shift sample-position when a user toggles Drive or
changes Quality, which is user-hostile and makes A/B and recall unreliable; it also makes the
"FX-off == bit-exact reference" comparison fragile because the *time base* moves.

**B2 — Report a CONSTANT total latency, padded to worst case, independent of FX on/off and
Quality tier (this ADR's policy).** Pros: bounce, automation alignment, and recall never
change when the user toggles FX or switches Quality; the host's PDC graph is stable for the
plugin's whole lifetime; "FX-off" audio is still bit-exact and now sits at a *fixed, declared*
sample offset, so the golden harness compares like-for-like; trivially RT-safe (latency sized
once in `prepare`, never touched on the audio thread). Cons: the plugin reports a few samples
more latency than strictly necessary when FX/HQ are off (we *pad* short configurations up to
the worst case by delaying the dry/short path); a tiny, deliberate, fully-disclosed cost.

B2 wins. Constant latency is the industry-correct posture for an instrument whose latency
sources are all small and bounded, and it is the only policy compatible with the owner lock
that "FX-off audio remains the blessed bit-exact reference": a stable time base is a
precondition for a stable golden.

## Decision

**Drive placement.** The FX **Drive is a SINGLE POST-VOICE FX-section stage, NOT per-voice.**
The per-voice 2x-oversampled nonlinear zone defined in ADR-004 is corrected to contain the
**modeled circuit only**: the IR3109 `tanh` filter ladder + the diode-clamp resonance limiter
+ the BA662 VCA drive/`tanh`. **The FX Drive module is removed from ADR-004's per-voice
zone.** The FX Drive runs **once on the mono voice-sum** (post poly/unison/drift, post the
per-voice oversampled output), as the first stage of the ADR-010 FX chain Drive -> Chorus ->
Delay, and it has **its own dedicated 2x oversampling** (one up + one down pair around the FX
Drive shaper, so its generated harmonics are band-limited before Chorus/Delay can fold them
back in). This corrects ADR-010 §DSP's "reuse the voice oversampler" phrasing: the FX Drive
uses its *own* FX-rate 2x oversampler, not the per-voice voice oversampler, because it runs
once on the mono sum after the voices are summed. This preserves both ADR-004's anti-aliasing
intent and ADR-010's "FX outside the bit-exact contract / once on the mono sum / cost
independent of voice count" spine (ADR-004 Decision pts 1-2; ADR-010 Decision, Contract FX-4,
FX-5; `docs/research/10-dsp-modeling-techniques.md` §5, §5.1;
`docs/research/11-cultural-influence.md` §4.3).

This distinction is real, not bookkeeping: the **BA662 VCA drive** is a documented SH-101
circuit and is genuinely per-voice (it is driven harder by output-side resonance per ADR-004
/ `docs/research/03-filter-ir3109.md` §4.3) and stays in the per-voice zone under the
modeling-accuracy contract; the **FX Drive** is a modern-essential sweetener with no SH-101
circuit and stays outside that contract (ADR-010).

**PDC / latency policy.** The plugin reports a **CONSTANT total latency to the host via
`setLatencySamples`** (declared from `plugin/` per ADR-001), **independent of FX on/off and
independent of the Quality tier (1x/2x/4x).** We **pad to the worst case**: the reported
latency equals the maximum group delay the plugin can introduce across any FX/Quality
combination, and shorter configurations are delay-aligned up to that worst case so the
reported number never changes for the lifetime of the plugin instance. Consequently
**bounce/automation alignment and preset recall never change reported latency**, and the host
never re-launches PDC on an FX toggle or Quality switch. The latency value is computed and
sized in `prepare` (ADR-001's `prepare`), is never recomputed on the audio thread, and is
held **constant across builds** for bit-exact reference stability (extending ADR-010 FX-12).

**"FX-off" stays the blessed bit-exact reference.** With all FX bypassed the audio is
bit-identical to the blessed mono voice (ADR-010 FX-1), now sitting at the *fixed, declared*
worst-case sample offset. Any **constant group delay from the per-voice oversampled zone**
(the realtime polyphase IIR halfband of ADR-004 — small but nonzero) is **part of that blessed
contract**: it is a deterministic, fixed offset present in the golden itself, identical on
macOS arm64 and Linux x64 (ADR-004 Contract rows 10, 14; ADR-001 C9). The realtime IIR path's
group delay is therefore *declared, not "negligible-and-ignored"*: this ADR supersedes the
loose "IIR adds negligible latency" note in ADR-004 Contract row 14 — the IIR group delay is
measured, included in the worst-case pad, and reported.

**What contributes to reported PDC, precisely:**

1. **Per-voice oversampled-zone group delay** (IR3109 ladder + resonance + BA662 VCA drive,
   ADR-004): the realtime IIR halfband up/down has a fixed group delay. CONTRIBUTES.
2. **FX Drive 2x oversampling group delay** (its own up/down pair, this ADR): a fixed group
   delay when the FX Drive oversampler is engaged. CONTRIBUTES (and is *always* counted toward
   the reported worst case, even when Drive is bypassed — that is what "constant, padded"
   means).
3. **FX Delay user time** (ADR-010 Chorus/Delay): the Delay's musical time (ms or tempo
   division) and the Chorus modulation delay are *intended musical delay*, not algorithmic
   latency; they are user-set, time-varying, and **do NOT contribute to reported PDC** (a host
   must not PDC-compensate an effect's musical delay).

The Quality tier (1x/2x/4x, ADR-004 Contract row 11) may change the *actual* zone group delay,
but **never the reported latency**: we always report the worst-case (the tier with the largest
group delay), and lower tiers are padded up to it. This keeps the owner-locked Quality switch
free of any PDC-relaunch side effect.

## Consequences

This commits us to:

- One **per-voice** oversampled zone (modeled circuit: ladder + resonance + VCA drive) and one
  **post-voice, once-on-the-mono-sum** FX Drive oversampler — two distinct 2x oversampling
  instances with two distinct, measured, constant group delays.
- Computing the **worst-case total group delay** across all FX/Quality combinations once in
  `prepare`, **delay-aligning** shorter configurations up to it (a small fixed delay line on
  the short paths), and reporting that single constant via `setLatencySamples` from `plugin/`.
- A regression test that asserts the reported latency is **invariant** to (a) master FX bypass
  and per-block bypass, (b) the Quality tier, and (c) build-to-build changes; any change to the
  number is a deliberate, reviewed event (extends ADR-010 FX-12 and ADR-004 Contract row 14).
- A golden-harness update so the blessed "FX-off" reference is captured **at the declared
  worst-case offset**, with the per-voice IIR zone group delay treated as part of the contract
  (not subtracted, not ignored).
- Correcting ADR-004 Contract row 7 (Drive in the per-voice zone) and ADR-010 §DSP's "reuse
  the voice oversampler" wording: the FX Drive is post-voice with its own oversampler. The
  per-voice zone's stages 4-6 of ADR-004 are unchanged; stage 7 (Drive) is moved out.

This forecloses / makes harder:

- Saving the last few samples of latency when FX/HQ are off — deliberately given up for a
  stable time base (B2 over B1).
- Any per-voice FX Drive variant or a Drive whose cost scales with polyphony — foreclosed by
  the once-on-the-mono-sum placement.
- A future *zero-latency* mode would require either dropping oversampling entirely (1x eco,
  which still reports the constant worst case unless we widen this policy) or an explicit new
  ADR to introduce dynamic latency.

Owner ratification item: confirm the **constant / worst-case-padded latency** policy — the
plugin will report a few samples more latency than strictly necessary when FX and HQ are off,
in exchange for never changing reported latency on FX-toggle, Quality-switch, or rebuild. This
is a user-observable trade (stable bounce/automation vs minimal latency) that sits beyond the
bare feature locks and should be signed off.

## Contract

Normative behavior the backlog implements verbatim. "Reported latency" = the value passed to
`setLatencySamples` from `plugin/` (ADR-001). "FX-off bit-exact" = sample-identical to the
blessed mono voice on the macOS arm64 reference at the declared offset.

| ID | Latency source / condition | Contributes to reported PDC? | Required behavior |
| --- | --- | --- | --- |
| L1 | Per-voice oversampled-zone group delay (IR3109 ladder + diode-clamp resonance + BA662 VCA drive; realtime polyphase IIR halfband, ADR-004) | YES | Fixed, measured group delay; included in the worst-case pad; part of the blessed FX-off bit-exact contract; identical on macOS arm64 and Linux x64. |
| L2 | FX Drive 2x oversampling group delay (its own post-voice up/down pair, this ADR) | YES | Always counted toward the reported worst case, even when Drive is bypassed (constant-padded). FX Drive runs once on the mono voice-sum, not per-voice. |
| L3 | FX Delay musical time + Chorus modulation delay (ADR-010) | NO | User-set / intended musical delay; never reported as PDC; never compensated by the host. |
| L4 | Reported latency value | — | A single CONSTANT = worst-case total group delay across ALL FX on/off and ALL Quality tiers (1x/2x/4x). Computed and sized in `prepare`; never recomputed on the audio thread. |
| L5 | Constant-latency rule | — | Reported latency is INVARIANT to: master/per-block FX bypass; Quality tier; and build-to-build. Shorter configurations are delay-aligned (padded) up to the worst case so the number never changes. |
| L6 | FX-off reference | — | With all FX bypassed, output is FX-off bit-exact (ADR-010 FX-1) at the declared worst-case offset; the L1 zone group delay is inside the golden, not subtracted. |
| L7 | Quality tier change (1x/2x/4x, ADR-004) | NO change to reported PDC | Tier may change actual zone group delay; reported latency stays at the worst-case constant; lower tiers padded up. No host PDC relaunch on tier change. |
| L8 | FX bypass (master or per-block) | NO change to reported PDC | Bypass still early-outs the DSP (ADR-010 FX-1/FX-3, ~0 cost); but the bypassed stage's worst-case latency remains in the constant pad so reported latency is unchanged. |
| L9 | Drive zone membership | — | FX Drive is OUTSIDE the per-voice ADR-004 zone (removed from ADR-004 Contract row 7). Per-voice zone = ladder + diode-clamp resonance + BA662 VCA drive ONLY. FX Drive = single post-voice stage with its own 2x oversampler. |
| L10 | RT-safety | — | Latency value sized in `prepare` (ADR-001); padding delay lines preallocated to worst case; no alloc/lock on the audio thread; reported number never mutated from `process`. |
| L11 | Build stability | — | Reported latency held constant across builds; any change is a deliberate, reviewed event with a golden re-bless (extends ADR-010 FX-12). |
