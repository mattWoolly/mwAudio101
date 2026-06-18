<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 006: Voice architecture, polyphony and unison

Status: accepted (mono default affirmed by ADR-016, 2026-06-18)
*Refined post-acceptance — see ADR-016.*
Date: 2026-06-17

## Context

The SH-101 is monophonic: a single CPU runs one keyboard-scan / note-priority
state machine that drives one analog voice (CEM3340 VCO + sub, IR3109 VCF,
BA662A VCA, one shared ADSR, one LFO). mwAudio101 must keep that mono path
bit-faithful while adding the in-scope modern essentials: optional polyphony,
unison with detune/spread, and per-voice analog drift. This ADR defines the
Voice / VoiceManager split, voice allocation and stealing, mono note-priority
faithfulness, unison behavior, and where per-voice drift state lives.

Forces:

- The single most load-bearing, lowest-ambiguity behavioral fact in the
  research is that on the SH-101 **note priority and envelope trigger are one
  physical control** (the GATE / GATE+TRIG / LFO selector S7), not two
  independent parameters. Per docs/research/07-cpu-key-assigner.md §3.2-§3.3
  and §11: GATE = lowest-note priority + no legato retrigger; GATE+TRIG =
  last-note priority + retrigger every key; LFO = lowest-note priority +
  envelope (re)triggered by the clock H->L edge, with CLOCK RESET re-phasing on
  keypress in LFO/ARP mode. §11 states explicitly: "Do not treat priority and
  trigger as independent parameters." Most clones get this wrong by exposing a
  separate priority dropdown and a separate retrigger toggle.
- The firmware resolves notes in a fixed-order polling super-loop running every
  1.5-3.5 ms (docs/research/07-cpu-key-assigner.md §2.3, §11), feeding 6-bit
  quantized pitch CV (§4.2). The coarse, slightly variable control rate shapes
  the stair-stepped portamento and scan-granularity feel; §11 asks the clone to
  emulate this rather than slick continuous float pitch.
- Last-note selection is the firmware's XOR of changed-down keys against the
  prior scan, taking the lowest of the just-pressed (§3.2). This mapping rests
  on the joebritt disassembly plus community consensus and is NOT confirmed
  against a Roland-authored statement (§3.2, §8.4 residual risk).
- The original has exactly one ADSR and one LFO per signal chain
  (docs/research/04-envelope-lfo-vca.md §2.1, §3.1); polyphony therefore means N
  independent copies of the one-EG / one-LFO topology, not a globalized
  modulation section.
- Poly mode has no historical SH-101 behavior to be faithful to; its stealing
  policy is a designed choice, not a ported one.

Owner-locked decisions this touches (re-affirmed, not reversed): circuit-
accurate analog modeling of the SH-101 modeled from documented circuit
behavior; faithful mono path plus modern essentials (poly/unison, per-voice
drift); real-time safe with no heap allocation and no locks on the audio
thread; macOS arm64 = reference/bless and bit-exact, Linux x64 = co-required
hard gate (so renders must be deterministic). This ADR keeps mono bit-faithful
by construction; the modern features are strictly additive and may not perturb
the mono path.

## Options considered

A three-persona panel debated the design. All three converged on the same
spine: a fixed, preallocated Voice pool (no audio-thread allocation or locks);
one bit-faithful firmware key-assigner that couples priority and trigger to the
single S7 selector; and per-voice drift state seeded per voice for reproducible
renders. The genuine split was **how unison is driven**.

### Persona: authenticity-mono (firmware note-priority + retrigger faithfulness)

Approach: port the IC6 keyboard_read / play state machine first as a standalone,
allocation-free `KeyAssigner` (held-note bitset/stack, one `GateTrigMode { Gate,
GateTrig, Lfo }` enum mirroring S7), then layer poly/unison as a strictly-
additive wrapper that never touches the mono code path. `Voice` is the analog
signal chain only plus per-voice drift; it does not decide priority. The
KeyAssigner emits `{activeNote, gate, retrigger}` as the only thing the mono
Voice consumes. Faithfulness is locked behind a golden-trace test against a
disassembly-semantics reference. Priority resolution runs on a coarse fixed
control tick feeding 6-bit-quantized CV with portamento stepping, not
per-sample.

Pros: mono is bit-faithful by construction; the subtle audible details
(legato-no-retrigger single sustained gate, lowest-of-just-pressed XOR,
CLOCK RESET re-phasing) are reproduced; the golden trace pins the one
medium-confidence item (priority mapping) to a documented reference; drift
lives cleanly on Voice.

Cons: two note-handling code paths is more surface than a unified allocator;
the last-note/XOR mapping is only as authoritative as the disassembly
(§8.4); the coarse 1.5-3.5 ms tick is deliberately "worse" resolution and may
be filed as a bug; poly's policy can't lean on the firmware; one coupled enum
is less flexible for power users wanting last-note WITHOUT retrigger.

Adopted: the golden-trace test against disassembly semantics; the coupled S7
enum as the only priority/trigger control; the coarse fixed control-tick +
6-bit quantized CV for portamento feel; the documentation of the control-tick
behavior as intentional, not a bug.

### Persona: product-poly (musically-useful poly + unison, stealing, stacking)

Approach: build ONE canonical Voice (full circuit-accurate chain + seeded
per-voice drift, plus one EG and one LFO PER voice per doc 04 §2.1) and make
mono the degenerate N=1 case of that same Voice rather than a parallel path. A
VoiceManager owns a fixed preallocated pool. Three modes: MONO (one Voice +
faithful firmware key-assigner), POLY (held-note list, deterministic steal =
oldest-released, then quietest, then oldest-held, via a short forced fade to
avoid clicks), UNISON (N voices on the same note, symmetric cents detune +
stereo spread, each with a distinct deterministic drift seed; unison stacks on
poly so effective poly = floor(maxVoices/unisonCount), and stealing removes
whole unison groups).

Pros: delivers the headline features cleanly; per-voice seeded drift makes
unison detune sound like real analog beating (the biggest "does it sound good"
lever); per-voice one-EG/one-LFO matches doc 04 §2.1; deterministic stealing +
seeded drift keep the bless/co-gate renders reproducible.

Cons: N full voices with oversampling is the heaviest part of the synth (unison
x8 multiplies cost up to 8x); two notions of "priority" (mono firmware vs poly
steal policy) can confuse users/UI; musical stealing is hard to tune;
per-voice independent LFO/EG won't phase-lock by default; MPE-lite interacts
with allocation and unison grouping; more per-voice state to serialize.

Adopted: per-voice one-EG / one-LFO topology (doc 04 §2.1) instead of a
globalized modulation section; deterministic, index-and-seed-derived drift and
stealing for reproducible renders; symmetric cents detune + stereo spread with
a distinct drift seed per unison voice; fade-then-steal (not hard cut) to
avoid clicks; unison stealing operates on whole voice groups.

### Persona: RT-engineer (fixed pool, no-alloc, per-voice layout, CPU scaling)

Approach: a compile-time-fixed, flat, contiguous, value-type `Voice[kMaxVoices]`
pool allocated entirely in prepareToPlay, each Voice an SoA-friendly aggregate
carrying its own DSP state plus an inline per-voice drift block (xorshift seed +
one-pole LPF). One allocation-free `MonoAssigner` is the single source of truth
for note priority and is ALWAYS in the path for MONO and UNISON; POLY is the
only mode that bypasses it and runs a real allocator. Unison feeds all its
voices from the one MonoAssigner so unison note-feel stays mono-faithful.
Allocation/stealing is an O(maxVoices) branch-light linear scan over integer
note-serial stamps (no priority queue, no sort, no timestamp math). Note-off and
steals only flag state transitions; release tails finish in place; a steal that
needs the slot immediately uses a fast fade ramp. An active-voice list lets the
block loop skip idle voices so CPU scales with sounding voices.

Pros: zero heap alloc / zero locks on the audio thread (satisfies the lock
directly); bit-faithful mono is structurally protected as the only note-
priority path, reused verbatim in unison and diffable bit-for-bit against the
arm64 bless target; deterministic worst-case CPU; per-voice independent drift
with no shared-RNG correlation; cache-friendly value-type layout, no virtual
dispatch in the inner loop; unison fed from the single assigner keeps unison
note-feel mono-faithful, which a poly-derived unison would silently break.

Cons: a fixed compile-time pool wastes a little memory at small voice counts and
caps polyphony at recompile (mitigated by sizing generously); two code paths
(MonoAssigner vs poly allocator) is more surface; any fixed steal policy
occasionally steals an audible note; per-voice drift makes voice count drive
drift-LPF cost; MPE-lite and arp/seq must plug into the same assigner/allocator
boundary with a clean "who owns the note" contract or faithfulness regresses.

Adopted: the fixed-size value-type pool sized and allocated in prepareToPlay;
the integer note-serial-stamped O(maxVoices) stealing scan with no allocating
containers; the active-voice list so idle slots cost nothing; the lock-free
mode/voice-count reconfiguration only at prepare time; and the decisive framing
that **unison is driven by the same MonoAssigner, so only POLY bypasses it**.

### Split and resolution

The split was the unison data flow. authenticity-mono and product-poly both
described unison as something layered on (over the mono path, or stacked on
poly). RT-engineer drew the sharper line: unison must be driven by the same
single MonoAssigner as mono so that unison note priority and retrigger feel
identical to mono; only POLY introduces a separate per-key allocator and bypasses
the assigner. This best honors docs/research/07-cpu-key-assigner.md §3.2-§3.3
and §11 (priority and trigger are one coupled control) because in unison the
note-selection logic stays the literal firmware model rather than being smeared
by an N-voice allocator. We resolve in favor of RT-engineer's framing as the
spine, integrating authenticity-mono's golden-trace test and coarse control-tick
faithfulness and product-poly's per-voice one-EG/one-LFO topology and detune/
drift design.

## Decision

We adopt a fixed, compile-time-sized, preallocated Voice pool owned by a single
VoiceManager, with one bit-faithful KeyAssigner that is the sole note-priority
authority for MONO and UNISON, bypassed only in POLY.

1. **Voice** = the circuit-accurate signal chain only (CEM3340 VCO + sub,
   IR3109 VCF, BA662A VCA, one ADSR, one LFO -- per docs/research/04-envelope-
   lfo-vca.md §2.1 and §3.1, instantiated per Voice, never globalized) plus an
   inline per-voice drift state block (a deterministic seed + slow random-walk /
   one-pole-LPF for tuning, PW, and cutoff drift). A Voice has no knowledge of
   polyphony; it consumes `{activeNote, gate, retrigger}` and a glide target. It
   does not decide priority. Per-voice drift is seeded from voice index plus a
   global instance seed so presets and renders are byte-reproducible for the
   macOS arm64 bless gate and the Linux x64 co-gate.

2. **KeyAssigner** = a small allocation-free struct that is the literal model of
   the firmware keyboard_read / play state machine: a 32-entry held-note
   stack/bitset (the 4x8 matrix analog) and one `GateTrigMode { Gate, GateTrig,
   Lfo }` enum bound to S7 (docs/research/07-cpu-key-assigner.md §3.1-§3.3). It
   couples priority and trigger exactly as the hardware does -- there is
   deliberately no separate priority parameter and no separate retrigger toggle
   (§11: "Do not treat priority and trigger as independent parameters"). It
   resolves at a coarse fixed control tick modeling the 1.5-3.5 ms super-loop
   (§2.3) and emits 6-bit-quantized pitch CV (§4.2) with portamento stepping,
   preserving the stair-stepped glide and scan-granularity feel; audio
   (VCO/VCF/VCA) runs per-sample / oversampled as normal. Mono behavior is
   locked behind a golden-trace test: a disassembly-semantics reference
   implementation (low-to-high scan for lowest-note; XOR-of-changed-down for
   last-note) and the C++ KeyAssigner must emit identical sequences over a
   battery of legato/overlap/release-order inputs.

3. **VoiceManager** = owns `Voice[kMaxVoices]` allocated in prepareToPlay
   (kMaxVoices a compile-time constant sized for the worst case;
   kMaxVoices >= maxPoly x maxUnison, with maxUnison capped at 8). Three modes
   are skins over the one pool:
   - **MONO** (default, bless target): exactly ONE active Voice driven verbatim
     by the KeyAssigner. Zero behavioral logic on top -- a pass-through, so mono
     is bit-faithful by construction.
   - **UNISON**: U voices (U in 1..8) all fed the SAME note resolved by the SAME
     single KeyAssigner, with symmetric cents detune (centered) and stereo
     spread, each voice carrying its own drift seed so detune is real analog
     beating, not a static pitch fan. Priority/retrigger remain owned by the
     KeyAssigner, so unison note-feel stays mono-faithful.
   - **POLY**: the only mode that bypasses the KeyAssigner. A per-key allocator
     runs over the pool: prefer an idle voice; if a held key is re-struck, reuse
     its own voice; otherwise steal by a fixed deterministic policy (oldest in
     release first, then quietest by current VCA/env level, then oldest-held),
     using a monotonically increasing integer note-serial stamp -- no timestamps,
     no sort, no allocating containers. Every poly note is its own fresh trigger
     (GATE+TRIG-style); there is no cross-voice lowest-note concept. Stealing is
     a fast forced fade-then-reuse (not a hard cut); release tails finish in
     place. Unison stacks on poly: effective poly = floor(maxPoly / U), and
     stealing operates on whole unison groups.

4. **Real-time safety**: the pool, all per-voice DSP scratch, and the drift
   state are sized and allocated in prepareToPlay; the audio thread only
   activates/idles existing voices. Allocation/stealing is an O(kMaxVoices)
   integer-comparison scan run once per note event, not per sample. Mode and
   voice-count changes happen only at prepare time or via a lock-free flag read
   by the audio thread -- never mid-block. Drift is a few flops per voice per
   control tick. Worst-case CPU = active voices (<= maxPoly x U) x per-voice
   oversampled cost + shared FX, budgeted with headroom measured on both the
   macOS arm64 reference and the Linux x64 hard-gate target. No allocation or
   locking anywhere on the audio thread.

This satisfies the owner-locked split (circuit-accurate mono + additive modern
essentials) by structure: the KeyAssigner is the single faithful note-priority
path, mono is its degenerate one-voice case, unison reuses it verbatim, and only
poly -- which has no historical behavior to betray -- introduces a designed
allocator that cannot reach back into the mono/unison note logic.

## Consequences

Commits us to:

- A single KeyAssigner as the only note-priority code path for mono and unison,
  validated by a golden-trace test against joebritt disassembly semantics; mono
  is diffable bit-for-bit against the arm64 bless render.
- A coupled S7 enum (Gate / GateTrig / Lfo) as the only priority+trigger control
  -- no independent priority dropdown, no independent retrigger toggle.
- A coarse fixed control tick (modeling 1.5-3.5 ms) with 6-bit quantized pitch
  CV for the mono/unison note + portamento path; audio remains per-sample /
  oversampled.
- Per-voice EG and LFO (one each, per Voice), per-voice inline drift seeded
  deterministically from voice index + instance seed for reproducible renders.
- A compile-time kMaxVoices pool, no audio-thread allocation/locks, O(kMaxVoices)
  integer-stamped stealing, maxUnison capped at 8.

Makes harder / forecloses:

- Two note-handling code paths (KeyAssigner vs poly allocator) instead of one
  unified allocator -- more surface area; the mono/unison-vs-poly mode switch
  must be handled at prepare/boundary to avoid stuck notes.
- Raising polyphony past kMaxVoices is a recompile, not a runtime knob (mitigated
  by sizing generously).
- Last-note / lowest-note semantics rest on the disassembly + community
  consensus, not a Roland-authored statement (docs/research/07 §3.2, §8.4); the
  golden trace is only as authoritative as that reference.
- Power users cannot get last-note priority WITHOUT retrigger -- a deliberate
  omission, because decoupling them is exactly what breaks SH-101 authenticity;
  it may draw user complaints.
- The coarse control tick is intentionally lower resolution than sample-accurate;
  must be documented as faithful behavior, not a latency bug.
- Poly voice-stealing is a designed musical compromise that will occasionally
  steal an audible note and needs tuning.
- MPE-lite and the host-synced arp / 100-step sequencer must plug into the same
  KeyAssigner / allocator boundary with a clear "who owns the note" contract;
  reconciling per-note expression with the bit-faithful mono assigner and with
  unison grouping is deferred to those features' own ADRs/design.

Owner ratification item: the modern poly/unison feature set is in the locked
scope, but several behaviors here are designed choices not dictated by the
SH-101 and carry user-expectation risk: (a) the deliberate refusal to expose
priority and retrigger as independent parameters (one S7 enum only); (b) the
poly voice-stealing policy (oldest-released -> quietest -> oldest-held, with
fade-then-steal) and unison-group stealing; (c) unison detune driven by the
single mono KeyAssigner (so unison plays with mono note-priority, not as
independent poly-per-note); and (d) the intentionally coarse, slightly variable
control-tick resolution for the mono note/portamento path. These should be
owner-confirmed before they harden into the contract below.

## Contract

Normative case table. "control tick" = the decimated update modeling the
1.5-3.5 ms firmware loop. "retrigger" = re-fire the shared ADSR from its trigger
state. All note selection in MONO/UNISON is via the single KeyAssigner; POLY
bypasses it.

| ID | Mode | Selector (S7) | Event | Required behavior |
|---|---|---|---|---|
| C1 | MONO | Gate | New key while a note is held | Lowest-held-note wins; gate stays asserted (no retrigger); portamento glides to the new lowest note. |
| C2 | MONO | Gate | Release of the current lowest while others held | Active note becomes the next-lowest still-held; gate stays asserted; glide to it. |
| C3 | MONO | GateTrig | New key while a note is held | Last-note priority; ADSR retriggers on the new key. |
| C4 | MONO | GateTrig | Multiple keys go down within one control tick | Pick the LOWEST of the just-pressed (XOR of changed-down vs prior scan); retrigger once. |
| C5 | MONO | Lfo | Key held | Pitch uses lowest-note priority; ADSR is (re)triggered by the clock H->L edge, not the key. |
| C6 | MONO | Lfo | New keypress | Assert CLOCK RESET (re-phase the clock/sequence). (Also applies in ARP mode.) |
| C7 | MONO/UNISON | any | All keys released | Gate de-asserts; ADSR enters release. |
| C8 | MONO/UNISON | any | Pitch CV update | 6-bit quantized at 1 V/oct; portamento steps at the control tick (stair-stepped), not continuous per-sample. |
| C9 | UNISON | any | Note resolved by KeyAssigner | Stack U voices (U in 1..8) on that one note; selection/retrigger identical to the MONO rules above (C1-C6). |
| C10 | UNISON | any | Detune / spread | Symmetric cents detune centered on the note; stereo spread distributed; each voice has a distinct deterministic drift seed (real beating, not a static fan). |
| C11 | UNISON | any | Steal | Steal whole unison groups, never individual stacked voices. |
| C12 | POLY | n/a (bypassed) | Each note-on | Allocate its own voice; every note is a fresh trigger (GATE+TRIG-style); no cross-voice lowest-note concept. |
| C13 | POLY | n/a | Re-strike of a held key | Reuse that key's own voice (no doubling). |
| C14 | POLY | n/a | No idle voice available | Steal in order: oldest in release -> quietest (lowest current VCA/env level) -> oldest-held. Tie-break by ascending integer note-serial. Deterministic. |
| C15 | POLY | n/a | A steal | Fast forced fade-out into reuse (no hard cut); other voices' release tails finish in place. |
| C16 | POLY | n/a | Effective polyphony under unison | floor(maxPoly / U) active groups; active-voice count hard-capped at kMaxVoices. |
| C17 | all | n/a | Audio thread | No heap allocation, no locks. Pool/drift/scratch sized in prepareToPlay. Mode and voice-count changes only at prepare or via a lock-free flag, never mid-block. |
| C18 | all | n/a | Determinism | Drift seeds and steal ordering derive from voice index + instance seed and integer note-serials, never wall-clock, so renders are byte-stable on the arm64 bless gate and Linux co-gate. |
| C19 | MONO | any | Golden-trace conformance | The C++ KeyAssigner must emit identical {activeNote, gate, retrigger} sequences to the disassembly-semantics reference over the legato/overlap/release-order test battery. Poly/unison are exempt and tested separately. |
