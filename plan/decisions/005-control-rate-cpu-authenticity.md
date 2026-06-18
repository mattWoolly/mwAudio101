<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 005: Control-rate model and 6-bit CV quantization authenticity

Status: accepted (default control mode superseded by ADR-016, 2026-06-18 — engine default is MODERN-SMOOTH, vintage 6-bit is a toggle)
*Refined post-acceptance — see ADR-016.*
Date: 2026-06-17

## Context

The SH-101 has no analog sequencing or analog key-assignment: a single Toshiba
TMP80C49 (IC6) runs one polling super-loop, with no interrupts, every 1.5-3.5 ms
and executes a fixed 11-step sequence (range read -> range/CV DAC -> keyboard
scan -> clock check -> random -> function read -> load/play/arp -> CV out ->
gate/LED), then jumps back to the top
(docs/research/07-cpu-key-assigner.md §2.2, §2.3;
docs/research/06-arpeggiator-sequencer.md §6.1). Pitch and CV are produced by a
**single 6-bit DAC time-multiplexed via a 4052 analog mux** into three
sample-and-hold destinations — VCO CV, rear-panel CV OUT, and a RANDOM voltage —
selected by DAC bits D7:D6 (`00`=CV OUT, `01`=VCO, `10`=RANDOM, `11`=parked/idle)
(07 §4.1; 06 §6.4). Pitch CV is assembled as **integer DAC counts** (key number +
range offset + octave-transpose + key-shift), with range bases spaced exactly 12
counts apart at 1 V/octave, i.e. **1 count = 1 semitone** (07 §4.2; 06 §6.4). The
variable loop time means the CV/gate update rate, portamento stepping, scan
granularity, and arp/seq edge timing are coarse and not perfectly constant
(07 §2.3, §11; 06 §6.1).

QUESTION: Do we replicate this control-rate behavior (coarse, slightly-jittery
~2 ms loop; single time-muxed 6-bit DAC; the resulting stair-stepped pitch and
portamento granularity) for authenticity, or run control at clean block/sample
rate? How authentic should the quantization be, and is "vintage" behavior
switchable?

OWNER-LOCKED DECISIONS THIS TOUCHES (re-affirmed, not reversed): The project is
**circuit-accurate analog modeling of the SH-101, modeled from documented circuit
behavior**, with no physical-unit oracle. The control core's defining facts here
are high-confidence — the 1.5-3.5 ms loop (`[service-manual, high]`), the single
6-bit DAC time-muxed via a 4052 (`[community disassembly: joebritt, high]`), and
the 12-counts/octave integer pitch assembly (corroborated by both the service
manual Range Data table and the disassembly constants). Authenticity here is a
direct expression of the lock. At the same time the lock also fixes the
**feature scope: poly/unison, per-voice drift, MPE-lite, and full
(sub-cent-capable) automation**. A hard 6-bit pitch ladder is musically
irreconcilable with MPE pitch expression, unison detune, and smooth automation:
forcing those modern-essential features onto 64 counts would make them sound
broken. The honest resolution must serve **both** locks at once, and must respect
the project's standing rule not to assert numbers we have not measured: per-route
DAC settling budget and how the variable loop time interacts with LFO jitter are
explicitly flagged **open validation gaps with no bench data** (07 §8.5, §9.5-9.6;
06 §8.5). Real-time safety (no heap alloc, no locks on the audio thread) and the
macOS arm64 bit-exact reference/bless gate also constrain the design.

## Options considered

### Persona: authenticity-purist — authentic control core always-on, one Modern escape hatch

Approach: Make the fixed-order ~1.5-3.5 ms variable-tick polling core and true
6-bit integer CV quantization a first-class, always-on part of the mono signal
path. A single shared virtual "6-bit DAC + 4052 mux + S/H" object is
time-multiplexed across VCO CV / CV OUT / RANDOM (with the `11xxxxxx` parked state
modeled), pitch is assembled as integer counts and converted to volts only at the
S/H boundary so glides genuinely stair-step, and per-tick jitter rides the
documented 1.5-3.5 ms envelope. One off-by-default "Modern Control" escape hatch
raises tick to block/sample rate and widens resolution for poly/MPE; the macOS
arm64 bless build is judged against the authentic core.

Pros: Directly honors the circuit-accurate lock using high-confidence facts, not
inferred ones; both research docs independently prescribe exactly this design
(07 §11; 06 §9), so it is consensus intent; captures the load-bearing "feel"
(stair-stepped portamento, slightly loose arp/seq timing) that a clean float path
cannot reproduce; a single shared virtual DAC/mux/S/H is genuinely simpler than
three independent float CV generators and makes RANDOM-on-clock-edge and the
CV-OUT jack fall out for free (07 §4.1, §5.3); integer-count assembly makes the
1 V/oct math exact and trivially testable against the disassembly constants
(07 §4.2); deterministic and bit-exact-friendly.

Cons (which I adopted): the persona itself concedes that mapping a hard 6-bit DAC
onto poly/unison + per-voice drift + MPE-lite is awkward and effectively forces
the Modern escape hatch anyway, so two control modes must coexist and both be
tested; that strict per-step authenticity collides with full host-automation
smoothing and continuous-knob UI; that the variable-tick jitter rests on an open
validation gap and so is a tuned guess, not a measured fact. These are the same
forces that pushed the decision toward making the vintage behavior an explicit,
labeled, switchable mode rather than a silent always-on default.

### Persona: modern-clean — clean continuous core by default, opt-in Vintage shaping

Approach: Run control at a clean fixed sub-block tick (~16-32 samples), carry
pitch/CV as full-precision float everywhere, smooth portamento with a sample-
accurate slew, and derive arp/seq edges from a clean clock. Reintroduce the
documented artifacts only as an **opt-in, 0-100% "Vintage Control"** shaping pass
(6-bit quantization on the VCO CV destination before the analog slew, coarse
update cadence, single-DAC mux cadence). Clean core is the blessed reference;
vintage is deterministic via a seeded jitter LUT.

Pros: Default sound is smooth and modern, matching what buyers reaching for
poly/unison/MPE-lite expect; the continuous core keeps per-voice detune and fine
pitch-bend off the 64-count ladder; lowest real-time risk; honest about not
fabricating the unmeasured loop-jitter/settling numbers (07 §8.5; 06 §8.5).

Cons (which I adopted in part, and where I diverged): the persona's own first con
is decisive against it — "the SH-101's character partly IS the coarse control
loop, and making it opt-in/non-default subtly distances us from the instrument,
the very thing a circuit-accurate clone is meant to nail," and "if Vintage is off
by default the out-of-box sound is arguably wrong versus a real unit." That
directly contradicts the circuit-accurate owner-lock and the design intent both
research docs state (07 §11; 06 §9). I adopted this persona's strongest
contributions — the continuous-core requirement for the modern feature paths, the
deterministic seeded-jitter approach for bit-exactness, and the honesty principle
of not asserting the unmeasured jitter/settling numbers — but **rejected its
default**: clean-by-default fails the lock.

### Persona: Product — authentic and always modeled, exposed as one labeled switchable "Vintage Control", vintage default, modern auto-engaged only where modern features force it (SELECTED)

Approach: Model the authentic control core faithfully and always, then expose its
authenticity as a single user-facing "Vintage Control" macro with named poles plus
advanced per-feature overrides, defaulting to vintage for the mono path. Three
switchable behaviors, each tied to a documented uncertainty so we only toggle what
we can defend: (1) pitch quantization VINTAGE = true 6-bit additive integer CV
(stair-stepped glides) / MODERN = continuous float; (2) control rate VINTAGE =
coarse ~1.5-3.5 ms tick with separately-toggleable loop-time jitter (since jitter
magnitude is an open gap) / MODERN = clean per-block smoothed CV; (3) clock feel
VINTAGE = H->L edge advance with clock-reset-on-keypress re-phasing and clock-tied
random S/H / MODERN = host-synced sample-accurate. Modern poles auto-engage only
where the owner-locked modern features (poly/unison, MPE-lite pitch bend, sub-cent
automation) make hard 6-bit control musically impossible. Switch is per-voice-
respecting and fully automatable; the bit-exact reference is a FIXED-tick vintage
variant (jitter off).

Pros: Default-vintage delivers the differentiating, audible 6-bit stair-stepped
portamento as a real signal behavior (lock satisfied as fact, not label); a single
labeled macro makes the tradeoff a visible feature with a clear preset/marketing
story; cleanly resolves the hard conflict between strict 6-bit and the owner-locked
modern features by auto-selecting the only viable mode per feature; ships honesty —
the research-flagged open gaps (loop-time jitter, DAC settling) become explicit
toggles rather than asserted-but-unmeasured facts; fully automatable across the ~64
presets; future-proof if bench data ever arrives.

Cons (acknowledged, mitigations carried into Contract/Consequences): more state and
test surface (two modes plus per-feature overrides); risk of option overload if
advanced overrides leak into the main UI (one macro front-and-center, advanced
hidden); "switchable authenticity" can read as hedging unless the vintage pole is
genuinely uncompromised; auto-engaging modern for MPE/poly means the most-authentic
setting is silently unavailable in those contexts and must be communicated; two
control-rate paths complicate the bit-exact reference (resolved by defining the
reference as the fixed-tick, jitter-off vintage variant).

### Panel split and resolution

The split was authenticity-purist + Product (authentic-default) versus
modern-clean (clean-default). It resolves toward **Product** because it is the
only position that honors **both** owner-locks simultaneously: it keeps the
circuit-accurate vintage control core as the always-modeled, default-shipping
behavior (satisfying the circuit-accuracy lock and both docs' stated design
intent), while structurally accommodating the owner-locked modern feature scope
(poly/unison/MPE-lite/automation) that a hard, always-on 6-bit core cannot serve.
It is, in effect, the authenticity-purist's engine (which is correct and cheap)
plus the authenticity-purist's own conceded "Modern escape hatch" — but promoted
from a hidden hatch to a first-class, labeled, automatable control with per-feature
auto-engagement, and combined with modern-clean's honesty discipline (seeded
deterministic jitter, jitter-off bit-exact reference, no fabricated numbers). The
purist's "always-on, judged-against-authentic" framing is adopted for the mono
reference; modern-clean's "clean by default" is rejected as lock-violating but its
continuous-core-for-modern-features and unmeasured-number honesty are adopted.

## Decision

We build **one** virtual control core that always models the SH-101 firmware
behavior, and expose its authenticity as a single labeled, automatable
**"Vintage Control"** parameter that selects between a VINTAGE pole and a MODERN
pole, **defaulting to VINTAGE on the mono reference engine**.

1. **Single time-muxed 6-bit DAC core, always present.** Implement one shared
   virtual "6-bit DAC + 4052 mux + sample-and-hold" object time-multiplexed across
   VCO CV / CV OUT / RANDOM, with route bits D7:D6 (`00`=CV OUT, `01`=VCO,
   `10`=RANDOM, `11`=parked) and the parked idle state modeled — not three
   independent float CV generators (07 §4.1, §11; 06 §6.4). This is simpler than
   independent generators and makes RANDOM-on-clock-edge and the CV-OUT jack fall
   out for free (07 §5.3).

2. **VINTAGE pitch quantization (default, mono path).** Pitch CV is assembled as
   **integer DAC counts** (key + range + octave + key-shift), ranges exactly 12
   counts apart, converted to volts only at the S/H boundary, so portamento and
   glides genuinely stair-step through 6-bit counts (1 count = 1 semitone) rather
   than a smooth float dithered to look quantized (07 §4.2, §11; 06 §6.4). The
   analog portamento/S&H slew (modeled elsewhere) does the smoothing **between**
   holds, exactly as the hardware RC does — the pitch itself is not smoothed away.

3. **VINTAGE control rate (default, mono path).** Control updates land on a
   fixed-order polling tick of nominally ~2 ms, sample-accurate at block
   boundaries, driven by a sample counter inside `processBlock` (never a wall-clock
   timer or background thread). Per-tick variation across the documented
   1.5-3.5 ms envelope is a **separately-toggleable** loop-time jitter, seeded from
   a cheap per-instance PRNG/LUT evaluated once per tick. Because the jitter
   magnitude/distribution is an **open validation gap** (07 §8.5, §9.5-9.6;
   06 §8.5), jitter is a labeled flavor toggle with a conservative default, and the
   **bit-exact macOS arm64 reference/bless variant runs the fixed-tick, jitter-OFF
   vintage configuration** so determinism and the Linux x64 co-gate hold in
   lockstep.

4. **MODERN pole.** Bypasses the 6-bit quantizer (continuous float pitch) and runs
   control at a clean fixed sub-block tick with smoothed CV and host-synced
   sample-accurate arp/seq edges.

5. **Per-feature auto-engage.** The owner-locked modern features —
   **poly/unison voices, MPE-lite pitch bend, and sub-cent host automation** — are
   musically incompatible with the hard 6-bit ladder and **auto-engage the MODERN
   pole for the pitch path** even when the macro is set to VINTAGE (per-voice detune
   and fine pitch bend must not collapse onto 64 counts). The mono, single-voice,
   non-MPE path honors VINTAGE fully. This auto-engagement is surfaced to the user,
   not silent.

6. **Clock feel** (already partly owned by ADR-adjacent arp/seq work): VINTAGE uses
   H->L edge advance, clock-reset-on-keypress re-phasing, and clock-tied random S/H
   (07 §5.1-5.3; 06 §4.3, §6.3); MODERN uses host-synced sample-accurate edges.

Rationale: This is the cheapest authenticity in the whole project to honor —
control runs at ~500 Hz, two orders of magnitude below audio rate, so doing it
right costs almost nothing on the audio thread (unlike oversampling or per-voice
analog modeling). It satisfies the circuit-accurate lock with high-confidence facts
(07 §2.3, §4.1, §4.2; 06 §6.1, §6.4), matches both docs' explicit design intent
("model the control core as a fixed-order polling loop", "use 6-bit pitch
quantization ... especially audible through portamento" — 07 §11; 06 §9), and is
the only option that also serves the owner-locked modern feature scope without
making MPE/poly/automation sound broken. It refuses to fabricate the unmeasured
jitter and settling numbers, exposing them as toggles instead of asserting them.

## Consequences

Commits us to:

- One shared virtual 6-bit-DAC/4052/S&H control object and an integer-count pitch
  pipeline (counts -> volts only at the S/H boundary), all allocation- and
  lock-free, driven by a sample counter inside the audio callback.
- A "Vintage Control" macro parameter (VINTAGE default on the mono reference) with
  advanced per-feature overrides hidden from the main UI, plus a separate loop-time
  jitter toggle.
- A bit-exact reference defined as the **fixed-tick, jitter-OFF vintage**
  configuration for macOS arm64 bless and the Linux x64 co-gate.
- Click-free mode switching: precompute/blend both branches and smooth (crossfade)
  the CV when the macro is automated vintage<->modern, branchless on the hot path;
  no allocation on mode switch.
- Quantizing pitch in the control domain **before** the oversampled audio render,
  so the 6-bit step is consistent regardless of oversample factor.
- Testing both control paths and the auto-engage transitions (notably MPE/poly with
  vintage quantize requested).

Forecloses / makes harder:

- More state and a larger QA/preset-validation surface than a single control path.
- The most-authentic 6-bit pitch behavior is **structurally unavailable** in
  poly/unison/MPE-lite/sub-cent-automation contexts (MODERN auto-engages there);
  this is a deliberate, communicated tradeoff, not a bug.
- We now own (and must justify) the clean control-tick rate and slew upsampling for
  the MODERN pole, since the hardware does not dictate them.
- The 0-100%/amount-style framing and the loop-time jitter are modern additions
  with no hardware analog and must be **labeled as additions, not SH-101 fidelity**
  (06 §8.7).

Owner ratification item: The out-of-box default is authentic VINTAGE control —
6-bit stair-stepped pitch lands on ~1.6-cent-spaced steps within an octave and the
coarse ~2 ms tick adds up to ~3.5 ms of modulation/portamento latency and
quantizes automation response. To modern ears this can read as slightly out of
tune or sluggish rather than as vintage character, and the most-authentic setting
is intentionally unavailable under poly/unison/MPE-lite/automation. This default,
and the auto-engage behavior, sit at the boundary of user expectation for a
"modern reimagined" product and should be confirmed by the owner.

## Contract

Normative case table; the backlog implements this verbatim. "Pitch domain" =
whether VCO CV pitch is quantized to integer 6-bit DAC counts or carried as
continuous float. Control tick is always driven by a sample counter inside
`processBlock` (sample-accurate at block boundaries; no wall-clock, no locks, no
heap on the audio thread).

| Case | Engine / context | Vintage Control macro | Loop-time jitter toggle | Pitch domain (VCO CV) | Control tick | Arp/seq edge | DAC/mux model | Bit-exact reference? |
| ---- | ---------------- | --------------------- | ----------------------- | --------------------- | ------------ | ------------ | ------------- | -------------------- |
| C1 | Mono, single voice, no MPE | VINTAGE (default) | OFF (default) | 6-bit integer counts; 12 counts/octave; counts->volts at S/H; portamento stair-steps | Fixed ~2 ms | H->L edge advance + clock-reset-on-keypress re-phasing; clock-tied random S/H | Single shared 6-bit DAC + 4052 + S/H; routes 00=CVOUT,01=VCO,10=RANDOM,11=parked | YES — this is the macOS arm64 bless / Linux co-gate reference |
| C2 | Mono, single voice, no MPE | VINTAGE | ON | 6-bit integer counts (as C1) | Variable, seeded PRNG/LUT once per tick over 1.5-3.5 ms envelope, conservative depth | As C1 | As C1 | NO — labeled flavor; not the reference (jitter is an open validation gap) |
| C3 | Mono, single voice, no MPE | MODERN | n/a | Continuous float (quantizer bypassed) | Clean fixed sub-block tick (e.g. 16-32 samples); smoothed CV | Host-synced, sample-accurate | DAC/mux cadence runs but quantizer bypassed | Deterministic; selectable; not the default reference |
| C4 | Poly / unison (per-voice detune) | VINTAGE requested | any | MODERN auto-engages: continuous float per voice (must not collapse onto 64 counts) | Clean fixed sub-block tick | Host-synced, sample-accurate | Per-voice; quantizer bypassed | Deterministic |
| C5 | MPE-lite (per-note pitch bend) | VINTAGE requested | any | MODERN auto-engages: continuous float / fine pitch bend | Clean fixed sub-block tick | Host-synced, sample-accurate | quantizer bypassed | Deterministic |
| C6 | Any, sub-cent host automation of pitch | VINTAGE requested | any | MODERN auto-engages for the automated pitch path (smooth, sub-cent) | Clean fixed sub-block tick | Host-synced | quantizer bypassed | Deterministic |
| C7 | Automating the Vintage Control macro itself | transition VINTAGE<->MODERN | any | Crossfade/blend the two CV branches; no zipper, branchless hot path | unchanged per target pole | unchanged | precompute both branches | n/a |

Invariants for all cases: no heap allocation and no locks on the audio thread; no
allocation when switching modes; pitch quantization (when active) happens in the
control domain before the oversampled audio render; auto-engagement of MODERN
(C4-C6) is surfaced to the user, never silent; the loop-time jitter and any
amount-style control are labeled as modern additions, not SH-101 fidelity.
