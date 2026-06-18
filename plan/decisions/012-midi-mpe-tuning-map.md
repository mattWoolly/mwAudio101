<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 012: MIDI / MPE-lite mapping & tuning reference

Status: accepted (default velocity superseded by ADR-016, 2026-06-18 — velocity is ON by default, switchable to faithful)
*Refined post-acceptance — see ADR-016.*
Date: 2026-06-17

## Context

mwAudio101 must define its complete MIDI implementation: how note/gate drive the
mono/poly engine, the MPE-lite per-note expression scope, the MIDI-CC/automation
map, pitch-bend range, and the tuning reference (hardware-documented A4 = 442 Hz
vs the 440 standard).

The defining force is that the stock SH-101 has **zero MIDI**: the service notes
connector list, spec table, and block diagram contain no MIDI, DCB, or DIN-sync,
and the firmware has no MIDI handling — MIDI, DCB and the sine-LFO/extended
registers are explicitly later software-emulation artifacts
(`docs/research/08-power-cv-io.md` §2.3, §2.1; `docs/research/07-cpu-key-assigner.md`
§4.3). Therefore there is **no physical-unit oracle** for any MIDI decision; every
choice here is a clone-layer policy choice. Under the owner-locked
"circuit-accurate, modeled-from-documented-circuit-behavior" rule that is not a
licence for invention — it is the reason the MIDI layer must be *disciplined*: it
must translate INTO the documented CV/gate/key-assigner behavior rather than
around it, so a DAW-driven instrument still behaves like an SH-101.

Owner-locked decisions this ADR touches and re-affirms (it does not reverse any):

- MPE-lite, full automation, poly/unison, host-synced arp + 100-step seq are all
  declared in-scope; this ADR specifies them, it does not expand scope.
- Real-time safe: no heap allocation and no locks on the audio thread. The MIDI
  parser, voice allocator, CC smoothing and pitch math all run on the audio
  thread and are bound by this lock.
- Tuning: the owner mandate is to make A4 a parameter with **default 440**. The
  documented hardware calibration is A4 = 442 Hz via the VR-7 procedure
  (`docs/research/08-power-cv-io.md` §4, §5.2, "high" confidence). This ADR honors
  the 440 default and does NOT silently restore 442 as the default.

## Options considered

Two personas were convened. They agreed on the entire load-bearing skeleton —
MIDI as a faithful front-end into the documented key-assigner/CV core, a single
coupled GATE/GATE+TRIG/LFO priority+trigger switch, 6-bit-quantized 1 V/oct pitch
with bend pre/post the quantizer as a continuous offset, MPE-lite bounded to
pitch + one pressure destination, APVTS-first automation with a remappable CC
layer, default bend ±2, and A4 as a float parameter defaulting to 440 with 442
available as the documented hardware setting. The only material split was the
scope of the per-note pressure destination and how loudly to advertise a
"modern" un-quantized pitch escape hatch.

### Persona: performance-MIDI (expressive MPE-lite, CC map, pitch-bend)

Approach: MIDI is a lock-free clone-layer translator into the existing key
assigner. Note/gate feeds a JUCE key-assigner mirroring the coupled S7
priority/trigger switch (`07` §3.2); velocity off by default but exposed as an
optional mod source. Pitch computed in float then pushed through the 6-bit DAC
quantizer (`07` §4.2); channel and per-note bend land as continuous offsets
*before* quantization. Channel bend range param default ±2 (0..24). MPE-lite =
lower zone only, member channels default off / opt-in 1..15, per-note bend ->
per-voice pitch offset, per-note pressure -> ONE assignable destination
defaulting to VCF cutoff CV (the path already SDT-1000-scaled, `08` §7.2),
deliberately NO per-note timbre matrix. APVTS-first automation; secondary default
CC map (CC1/7/11/74/71/5/64-sustain->HOLD) plus MIDI-learn. A4 float param default
440 with a recallable 442 factory preset; front-panel TUNE stays a separate ±50 ct.

Honest pros: single source of truth (mono path stays bit-exact, poly replicates
it); expression lands on physically real nodes; tuning-as-param satisfies the
mandate while a 442 preset preserves the hardware fact; MPE-lite cap matches a
one-VCF/one-VCA synth; APVTS-first is JUCE-idiomatic and gives sample-accurate
automation free. Honest cons: MPE purists want more than one timbre destination;
per-note bend through the 6-bit quantizer can stair-step audibly on big slides;
velocity-off is unusual for a modern instrument; the 440-vs-442 duality is a
UX/doc burden; a full MIDI-learn table adds serialization and test surface.

### Persona: authenticity-CV

Approach: Map MIDI as a "virtual patch cable" onto the original CV/Gate
semantics. Note-on/off feed a single key-assigner reproducing the coupled
priority+trigger behavior (`07` §3.2-3.3, §11): GATE = lowest-note + no legato
retrigger; GATE+TRIG = last-note + retrigger; LFO = lowest-note + clock-triggered
envelopes — priority and trigger NEVER split. Pitch -> 6-bit-quantized 1 V/oct
then exponentiated (`07` §4.2; `08` §4), out-of-range notes clamp like the 32-key
matrix. Pitch-bend continuous (bypasses quantization), default ±2, 0..24. MPE-lite
= per-note pitch + per-note pressure ONLY and only in poly; pressure mapped to a
mod-grip-equivalent destination (MGS-1 semantics, `08` §2.4). Tuning Reference
(A4) param ~400-460 Hz, default 440 per the lock, with 442 as the documented
hardware-accurate value surfaced in a preset/notes.

Honest pros: preserves the single most distinctive SH-101 trait (coupled
priority/retrigger switch); the 6-bit-quantized glide is a real sonic fingerprint
a float path would erase; tuning-as-param loses no fact and reverses no decision;
MPE bounded to pitch+pressure stays honest; clean separation of faithful mono
reference vs flagged poly/MPE enhancement; EXT-CLK-style host-sync semantics
carried in. Honest cons: coupling priority+trigger is less flexible than DAW users
expect; 6-bit quantization on MIDI input can read as a "bug" and fights MTS-ESP
microtuning; pitch+pressure-only MPE under-scopes vs the MPE spec (no CC74 slide);
velocity-off is surprising; 440-vs-442 is subtle; faithful EXT-CLK/RATE-decouple
behavior is inferred, not measured (`07` §9 item 8).

### Split and resolution

The split: performance-MIDI wants per-note pressure to be **user-assignable** to
any single destination (defaulting to VCF cutoff CV); authenticity-CV wants it
pinned to a mod-grip-equivalent destination and is wary of anything that smells
generic. Resolution: **adopt performance-MIDI's single *assignable* pressure
destination defaulting to VCF cutoff CV**, because the cutoff CV path is the one
documented, already-scaled physical node (`08` §7.2, §5.3) and the MGS-1 grip
pinout is explicitly undocumented (`08` §2.4) — we can only model grip *behavior*,
not a hardware bus, so "assignable, default cutoff" is both honest and the more
useful default. We adopt authenticity-CV's hard refusal of a per-note timbre
*matrix* and its insistence that priority+trigger stay ONE coupled switch and
that pitch pass through the 6-bit quantizer.

Critiques adopted into the decision:

- From authenticity-CV: coupled (never-split) GATE/GATE+TRIG/LFO priority+trigger;
  mandatory 6-bit quantization on the note-pitch path; out-of-range note clamp to
  the modeled span; MPE-lite strictly bounded to pitch + one pressure target.
- From performance-MIDI: per-note bend (and channel bend) applied as a continuous
  offset *before* quantization so bend/TUNE remain the only continuous pitch
  controls (`08` §4); a clearly-labeled "modern" un-quantized pitch option to
  defuse the stair-step-on-slides surprise; lock-free double-buffered CC/learn map
  with atomic pointer publish; fixed pre-allocated voice pool for MPE.
- Adopted from both as residual-risk signposting: velocity-off-by-default and the
  440-vs-442 duality must be surfaced in UI/preset notes or users will mistrust
  tuning and think the synth is broken.

## Decision

Implement MIDI/MPE as a **lock-free, allocation-free clone-layer translator that
feeds the documented key-assigner / DAC-CV model** (`docs/research/07-cpu-key-assigner.md`
§11; §3.2-3.3, §4.2), never a parallel control path. Specifically:

1. **Note/gate -> engine.** MIDI note-on/off drive one key-assigner that mirrors
   the coupled S7 switch as a single "Trigger/Priority" parameter with three
   values: GATE = lowest-note priority + no legato retrigger; GATE+TRIG =
   last-note priority + retrigger every key; LFO = lowest-note priority +
   envelopes (re)triggered by the LFO/clock (`07` §3.2, §3.3, §11). Priority and
   trigger are never independent parameters. Poly/unison instantiate N copies of
   this same assigner; the mono path stays bit-exact. **Velocity is OFF by
   default** (the SH-101 keyboard has none, `07` §3.2) and exposed only as an
   optional mod-routing source.

2. **Pitch path.** Note number assembles additively (key + range + octave +
   key-shift) and passes through the **6-bit DAC quantizer at exactly 1 V/oct**
   before exponentiation (`07` §4.2; `08` §4) — MIDI must NOT bypass quantization,
   preserving the stair-stepped portamento fingerprint. Notes outside the modeled
   span clamp like the 32-key matrix (`07` §3.1; `08` §3.1 CV-IN 0-7 V semantics).
   A clearly-labeled, off-by-default **"modern un-quantized pitch"** option may
   bypass quantization for smooth MPE glides; this is a flagged clone deviation,
   not the default.

3. **Pitch-bend.** Channel pitch-bend is a **continuous** offset applied
   *before* quantization (the analog bender + front-panel TUNE are the only
   continuous pitch controls, `08` §4, §7.4). Channel bend range = parameter,
   **default ±2 semitones, range 0..24**. This is the bender analog and an
   explicit clone addition (the MGS-1 grip pinout is undocumented, `08` §2.4 — we
   model behavior, not a hardware bus).

4. **MPE-lite.** "Lite" = **lower zone only**, configurable member-channel count
   (**default off; opt-in 1..15**). Per-note pitch-bend -> per-voice continuous
   pre-quantizer pitch offset. Per-note pressure (channel pressure / CC74) ->
   **ONE assignable destination, default VCF cutoff CV** (the documented,
   SDT-1000-scaled cutoff path, `08` §7.2, §5.3). **No per-note timbre matrix** —
   the hardware has one VCF and one VCA, so faithful modeling forbids inventing
   per-note analog structure (`08` §7.2-7.3). MPE master/per-note bend ranges are
   parameters defaulting to ±48 (MPE spec defaults). In mono mode MPE collapses to
   channel bend + channel pressure.

5. **CC / automation map.** Every modeled panel control (cutoff, resonance, env
   mod, LFO rate/depth/waveform, PWM, sub level, noise, mixer, ADSR, portamento,
   drive/chorus/delay, arp/seq controls) is an APVTS host automation parameter —
   the **primary, sample-accurate automation surface**. A **secondary default
   MIDI-CC map** (CC1 mod, CC7 vol, CC11 expr, CC74 cutoff/brightness, CC71
   resonance, CC5 portamento time, CC64 sustain -> HOLD / external-HOLD which is a
   real stock jack, `08` §2.1, `07` §7.1 T0) plus a **fully user-remappable
   MIDI-learn table**. No CC invents a control the hardware lacks.

6. **Clock / transport.** EXT CLK / host transport advances the arp + 100-step
   seq via a **rising-edge model** (`07` §5.1; `08` §3.3); CLOCK RESET re-phases on
   note-on in LFO-trigger or ARPEGGIO mode (`07` §5.2). Under external/host clock
   the RATE control no longer sets arp/seq tempo (`07` §5.1) — flagged inferred
   per `07` §9 item 8.

7. **Tuning reference.** A4 is a single float **parameter, default 440 Hz** (owner
   mandate) over a ~400-460 Hz range, with the documented **442 Hz** VR-7 value
   provided as a recallable "hardware-accurate" preset/note (`08` §4, §5.2, "high"
   confidence). MTS-ESP / MIDI Tuning Standard honored if cheap, but the master
   reference is this single float param. The front-panel **TUNE stays a separate
   ±50-cent** continuous control layered on top of the A4 reference (`08` §7.4).

Rationale: because there is no MIDI oracle (`07` §4.3; `08` §2.3), discipline =
landing expression on documented physical nodes (bend pre-quantizer because bend
and TUNE are the only continuous pitch controls; pressure on the real, already
scaled cutoff CV) and refusing structure the single-VCF/single-VCA hardware never
had. A4-as-param default 440 satisfies the mandate while the 442 preset loses no
documented fact.

## Consequences

This commits us to:

- A single control model: MIDI feeds the existing key-assigner/CV core, so the
  mono path stays bit-exact and poly is N faithful replicas — no second control
  path to keep in sync.
- An RT-safe MIDI front-end: lock-free double-buffered CC/learn map (single-writer
  atomic pointer publish from the message thread, no mutex on audio), a fixed
  voice pool sized at `prepareToPlay` (no per-note heap; steal/reject beyond
  pool), fixed-cost one-pole de-zipper smoothers for bend/pressure/CC, and the
  6-bit quantizer as a trivial round + table lookup. CC events are timestamped and
  applied at block sub-sample offsets without allocation. Net CPU is dominated by
  the oversampled analog model, not this layer.
- Serialization/test surface for the remappable CC/learn table and the dual
  tuning convention (440 default param + 442 preset), plus the un-quantized-pitch
  flag, all of which must round-trip in presets and project state.

This forecloses / makes harder:

- A full MPE per-note timbre matrix (multiple per-note destinations) — capped at
  one assignable pressure destination to protect the faithful model.
- Independent last/low/high priority and independent retrigger modes — priority
  and trigger stay a single coupled switch, less flexible than some DAW users
  expect.
- Clean fine-grained microtuning/MTS-ESP on the default path — the mandatory 6-bit
  quantizer fights continuous retuning (the un-quantized option is the escape
  hatch, with the deviation flagged).

Owner ratification item: three deliberate, user-visible policy choices carry
expectation/scope risk beyond the locks and should be signed off. (a)
**Velocity OFF by default** (faithful but unusual for a modern instrument) — risks
first-run "no dynamics" confusion unless presets/UI signpost it. (b) **MPE-lite
capped at pitch + one assignable pressure destination, lower zone only** — under
the MPE spec, will disappoint controller users expecting per-note slide/timbre.
(c) The **"modern un-quantized pitch" escape hatch** — it intentionally departs
from the documented quantized-CV behavior for smooth MPE glides; confirm we want
to ship a switch that can defeat a load-bearing authenticity trait.

## Contract

Normative case table; the backlog implements verbatim. "Pre-Q" = applied as a
continuous offset before the 6-bit quantizer; "Post-Q" = the quantized path.

| ID | Condition | Required behavior |
| --- | --- | --- |
| C1 | Trigger/Priority = GATE, monophonic | Lowest-note priority; legato keypress does NOT retrigger envelopes (gate held) |
| C2 | Trigger/Priority = GATE+TRIG, monophonic | Last-note priority; every new note retriggers both envelopes |
| C3 | Trigger/Priority = LFO | Lowest-note priority; envelopes (re)triggered by LFO/clock, not the keyboard |
| C4 | Priority and trigger exposed | They are ONE coupled parameter (3 values); never two independent params |
| C5 | Any MIDI note-on, default pitch path | Pitch assembled key+range+octave+key-shift, passed through 6-bit 1 V/oct quantizer (Post-Q) |
| C6 | Note outside modeled 32-key span | Clamp to span edge (no pitch beyond the modeled CV equivalent) |
| C7 | "Modern un-quantized pitch" = ON | Note pitch bypasses the 6-bit quantizer (smooth); OFF is the shipped default |
| C8 | Channel pitch-bend | Continuous offset applied Pre-Q; range = param, default ±2 semitones, range 0..24 |
| C9 | Velocity (default) | OFF — no effect on output; available only when explicitly routed as a mod source |
| C10 | MPE member channels (default) | OFF (0 members); opt-in lower-zone only, 1..15 members |
| C11 | MPE per-note pitch-bend | Per-voice continuous offset Pre-Q; per-note bend range param default ±48, master default ±48 |
| C12 | MPE per-note pressure (chan pressure / CC74) | Routed to ONE assignable destination; default = VCF cutoff CV; no other per-note timbre routing |
| C13 | Mono mode + MPE messages | Collapse to channel pitch-bend + channel pressure |
| C14 | Panel-control automation | Every modeled control is an APVTS host parameter (primary sample-accurate surface) |
| C15 | Default CC map | CC1 mod, CC7 vol, CC11 expr, CC74 cutoff, CC71 resonance, CC5 portamento time, CC64 sustain -> HOLD; fully user-remappable via MIDI-learn |
| C16 | MIDI-learn / map edit at runtime | Lock-free single-writer atomic swap (double-buffered map); no mutex/alloc on the audio thread |
| C17 | MPE voice allocation | Fixed pre-allocated voice pool sized at prepareToPlay; beyond pool -> steal/reject, never allocate |
| C18 | EXT CLK / host transport present | Arp + 100-step seq advance on rising clock edge; RATE stops setting arp/seq tempo |
| C19 | Note-on in LFO-trigger or ARPEGGIO mode | Assert CLOCK RESET — arp/LFO-clocked pattern re-phases to the keypress |
| C20 | CC64 sustain (default map) | Drives HOLD / external-HOLD input semantics (real stock jack) |
| C21 | Tuning reference A4 | Single float parameter, default 440 Hz, range ~400-460 Hz |
| C22 | "Hardware-accurate" tuning preset | Recalls A4 = 442 Hz (VR-7 documented value); never the default |
| C23 | Front-panel TUNE | Separate ±50-cent continuous control layered on top of the A4 reference |
| C24 | Bend/pressure/CC value changes | De-zippered via fixed-cost one-pole/linear smoothers, O(1) per sample, no branch on message arrival |
