<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 007: Modulation routing, arpeggiator and 100-step sequencer

Status: accepted
Date: 2026-06-17

## Context

We must define four coupled control-core behaviors for mwAudio101: (1) the
fixed modulation routing (LFO to pitch/PWM/cutoff; the shared ADSR to
cutoff/VCA/PWM; the trigger-source switch); (2) the arpeggiator modes;
(3) the 100-step sequencer model; and (4) host tempo-sync for arp/seq plus
clock-reset-on-keypress. The hard force is reconciling circuit-accurate
faithfulness with the modern-essential demand that a 2026 plugin lock to the
DAW grid.

This decision sits under, and re-affirms, the owner-locked decisions:

- **Circuit-accurate analog modeling, no physical-unit oracle.** Everything
  below is modeled from documented circuit behavior (service manual + the
  joebritt community disassembly), not from bench measurement. Items that are
  reverse-engineered or inferred are carried as such, not promoted to fact.
- **Faithful mono SH-101 path + modern essentials.** Host-synced arp/seq and
  full automation are explicitly in scope as *modern essentials*; they must be
  realized without altering the modeled internal-clock path.
- **Real-time safe: no heap alloc / no locks on the audio thread.** The arp key
  buffer and 100-slot sequencer are fixed-size, preallocated; param flow is
  through the lock-free APVTS/atomic path; preset I/O is off the audio thread.

This ADR does not reverse any lock. It does add user-facing surface
(host-rate selector, swing) flagged for owner ratification below.

## Options considered

Two personas were convened. They agreed on the faithful core and diverged only
on how host-sync is framed and whether to add swing.

### Persona: authenticity (faithful routing / trigger semantics / seq model / clock)

Advocated modeling the entire control core as one deterministic, fixed-order
state machine mirroring the single-CPU (IC6, TMP80C49P) design, with host
tempo-sync treated as *just another clock source feeding the SAME H-to-L edge
detector the original used for EXT CLK IN* — never a parallel engine with its
own per-step durations or retrigger rules. Concretely: fixed routing (not a
matrix); the GATE/TRIG switch as one control binding both note priority and
retrigger; exactly 3 arp modes (UP / U&D / DOWN) over a 32-key held bitmap with
no octave expansion; a 100-slot note/rest/tie model with no accent and no
per-step gate-time; one edge detector advancing arp, seq, and RANDOM together;
clock-reset-on-keypress default-on.

- Pros: a single edge node keeps arp/seq/RANDOM/clock-reset phase-consistent
  across internal/EXT/host sources — the original's defining behavior; coupling
  priority to GATE/TRIG reproduces the recognizable mono-legato/last-note feel;
  the slot model matches firmware and avoids importing TB-303/MC-202 features
  the 101 never had; host sync as "EXT CLK IN modernized" is historically
  grounded; the deterministic state machine is trivially testable for bit-exact
  reproducibility across the macOS-bless and Linux hard-gate platforms.
- Cons: refusing accent/per-step gate-time will disappoint users expecting a
  modern step sequencer; U&D turnaround and the exact REST/TIE bit layout are
  reverse-engineered/inferred (06 §8.2, 07 §9) so true bit-exactness is
  unverifiable without bench data; the 1.5-3.5 ms loop granularity conflicts
  with sample-accurate host sync — one must be primary; "RATE affects only LFO
  when synced" is faithful-but-surprising; one clock node makes a future
  per-voice arp awkward.

### Persona: product (host sync, swing, seq save/restore, usability)

Agreed the faithful one-pulse-per-step edge core must stay intact, then wrapped
it in a three-way mutually-exclusive **clock source** abstraction —
INTERNAL (faithful LFO/CLK, bit-identical), HOST-SYNC (DAW PPQ via
`AudioPlayHead`), EXT (CV/automation pulse) — reusing "1 pulse = 1 step"
verbatim. Adds an automatable musical-rate selector for host-sync
(1/4 .. 1/32 with dotted/triplet), a host-sync-only **SWING** (50-75%, default
off, grayed when INTERNAL), and persists the full 100-step buffer + arp + clock
state in the ValueTree to mirror battery-backed RAM.

- Pros: reuses the faithful edge core for sync so fidelity and modern sync share
  one timing-test surface; solves the dominant usability gap (free-running knob
  vs grid) without touching the locked circuit path; full state persistence
  matches both hardware battery backup and DAW recall; three clock sources mirror
  the hardware's internal-vs-EXT selector as one mental model; rate/swing as
  discrete automatable params give full automation coverage.
- Cons: adds panel surface the hardware never had (drift risk if not visually
  segregated); swing has no hardware oracle, so its taper is a pure by-ear design
  choice; defining "a step" under tempo changes/loops/scrub introduces edge cases
  the 1982 unit never had; persisting 100 steps creates a preset-format surface
  that must be versioned from day one; sub-block-accurate edge/swing placement is
  fiddlier than a free-running counter.

### Split and resolution

The panel did **not** split on the faithful core — both personas independently
converged on: one shared H-to-L edge detector advancing arp + seq + RANDOM,
fixed routing, GATE/TRIG-coupled priority+retrigger, the 100-slot
note/rest/tie/no-accent/no-per-step-gate model, and clock-reset-on-keypress
default-on. That consensus is adopted wholesale.

The only genuine divergence was **swing** and the **rate-selector framing**.
Resolution: adopt the product persona's three-way INTERNAL/HOST-SYNC/EXT clock
*wrapper* and full ValueTree persistence, but constrained by the authenticity
persona's critiques, which I adopt:

1. Host-sync must feed the **same** edge detector as EXT CLK IN — no second
   sequencer engine, no separate per-step durations or retrigger rules
   (authenticity critique adopted; resolves the product persona's own
   "transport-jump edge-case" risk by recomputing phase from absolute PPQ).
2. SWING is admitted **only** gated behind HOST-SYNC, disabled/grayed under
   INTERNAL and EXT, and explicitly labeled a non-101 modern addition — so it
   can never perturb the modeled internal-clock timing. It is flagged for owner
   ratification (it has no hardware oracle).
3. Accent and per-step gate-time are **refused** (authenticity critique adopted;
   confirmed by 06 §8.1 / 07 §5.5 — accent is a TB-303/MC-202 feature the 101
   lacks). Articulation = stored ties + global GATE/TRIG only.
4. The loop-granularity tension is resolved the authenticity persona's way: the
   control state machine runs on a tunable fixed sub-block tick (default models
   the coarse 1.5-3.5 ms update) while clock *edges* are placed sample-accurate.
   This decouples "authentic stepping feel" from "tight host sync" without a
   second engine.

## Decision

Model the modulation + arp + seq control core as **one deterministic,
fixed-order state machine** that mirrors the SH-101 single-CPU design (IC6,
TMP80C49P), per docs/research/07-cpu-key-assigner.md §2.2, §11 and
docs/research/06-arpeggiator-sequencer.md §6, §9. Its parts:

1. **Fixed modulation routing (not a matrix).** One LFO scales the *same
   instantaneous selected-LFO value* into three fixed destinations with
   per-destination depth gains: VCO pitch (single MOD depth), PWM (own
   ENV/MANUAL/LFO source switch + PWM depth), VCF cutoff (own MOD depth
   alongside ENV depth and Key Follow 0-100%). One shared ADSR routes to VCF
   cutoff (ENV depth), VCA (via the VCA ENV/GATE switch), and PWM (when PWM
   source = ENV). Keep the SH-101-specific LFO-to-VCA tremolo path. No separate
   filter/amp envelopes, no sine LFO core, no six-position selector — those are
   reissue artifacts (docs/research/04-envelope-lfo-vca.md §2.1, §3.2, §3.6,
   §6.1-6.2).

2. **Trigger-source switch = one control, two coupled behaviors (S7).** Bind
   note priority and retrigger to the single switch, never as independent params
   (docs/research/07-cpu-key-assigner.md §3.2-3.3, §11; docs/research/04 §2.3):
   GATE+TRIG = last-note priority + retrigger every key; GATE = lowest-note
   priority + no legato retrigger (mono-legato); LFO = lowest-note priority +
   envelope re-fired each LFO/clock cycle.

3. **Arpeggiator: exactly 3 modes (UP / U&D / DOWN).** HOLD latch from panel and
   pedal; up to 32 held keys in a fixed bitmap (NOT 4-note polyphony — a known
   misreading guard, 06 §8.3); cycle held keys with no automatic octave
   expansion (octave only via global TRANSPOSE). Arp engages on chord/legato; a
   single non-legato note plays normally (06 §2.1-2.4, §9). U&D turnaround-repeat
   is exposed as a documented switchable choice because the original math is only
   partially traced (06 §8.2, 07 §5.4).

4. **Sequencer: a 100-slot model, one event per slot.** Note = one slot,
   REST = one slot, each tie/long-note extension = one slot. Per-step payload =
   6-bit pitch + REST flag + TIE/legato(slide) flag. **No accent. No per-step
   gate-time.** Articulation = stored ties + global GATE/TRIG mode. LOAD toggles
   record (auto-exit at 100); PLAY toggles wrap-around loop; sequence is written
   only from the instrument keyboard. The internal RAM bit layout is an
   implementation choice (the original is community-inferred, not Roland-
   documented) (06 §3.1-3.3, §9; 07 §5.5-5.6, §11).

5. **Clock + host sync.** One H-to-L edge detector on a single clock node
   advances arp, seq, and RANDOM reload together (07 §5.1, §5.3, §11). Wrap that
   node in three mutually-exclusive sources mirroring the hardware's
   internal-vs-EXT selector:
   - INTERNAL = LFO/CLK 0.1-30 Hz, bit-identical to the modeled hardware.
   - HOST-SYNC = DAW PPQ via `AudioPlayHead`, mapped "1 host pulse = 1 step"
     exactly as the hardware maps "1 EXT pulse = 1 step" (06 §4.2; 07 §5.1).
   - EXT = CV/automation pulse.
   When HOST-SYNC or EXT is active, RATE controls ONLY the LFO as a mod source,
   NOT tempo — the original "EXT CLK cuts the internal clock" semantic (06 §4.2;
   07 §5.1). Host edges are derived per block from PPQ and placed at sub-block
   sample offsets feeding the *same* edge detector — no separate per-step engine.
   **CLOCK RESET on keypress** re-phases the clock to any new keypress while in
   LFO-trigger OR arpeggio mode; it is musically load-bearing (06 §4.3; 07 §5.2)
   and **default-on**.

6. **Modern additions, gated and labeled.** A host-rate selector
   (1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, plus dotted) and SWING (50-75%, default
   50% = off) are discrete automatable params, **active only under HOST-SYNC** and
   grayed under INTERNAL/EXT, so they can never perturb modeled internal timing.

7. **Persistence.** Persist the full 100-slot buffer + arp settings + clock
   config in plugin/preset state (JUCE ValueTree / get/setStateInformation),
   surviving session reload to mirror the hardware's battery-backed RAM (06 §3.4;
   07 §6). State I/O runs on the message thread; the audio thread reads an
   atomically-swapped immutable snapshot. Version the preset schema from v1.

This is the faithful "EXT CLK IN, modernized" realization — extending a real
hardware path rather than bolting on a parallel engine — and it keeps every
downstream behavior phase-identical across internal/EXT/host sources.

## Consequences

Commits us to:

- A single edge-driven, fixed-order state machine as the one source of truth for
  arp/seq/RANDOM/clock-reset timing — one set of timing tests covers all three
  clock sources; deterministic and bit-exact-testable across the macOS reference
  and Linux hard-gate platforms.
- Fixed-size preallocated structures: a 32-bit arp key bitmap and a 100-slot seq
  array, so LOAD/record and arp re-sort never heap-allocate on the audio thread
  (honors the no-alloc/no-lock lock).
- Host edges derived from absolute PPQ per block (not a free-running counter), so
  tempo automation, loop wrap, and scrub just re-derive the next edge; the
  keypress clock-reset path is reused for re-phase.
- A versioned preset schema carrying 100 steps + flags + arp + clock state from
  day one.

Forecloses / makes harder:

- No accent and no per-step gate-time. Users expecting a modern step sequencer
  will find these absent; the only sanctioned mitigation is labeling any such
  feature, if ever added, as an explicit non-101 addition.
- "RATE affects only LFO when synced/EXT" is faithful but surprising; must be
  surfaced honestly in the UI, not silently re-mapped to tempo.
- Tying everything to one clock node makes a future true-polyphonic/per-voice arp
  awkward, since the original was strictly mono.
- True bit-exactness of U&D turnaround and the REST/TIE bit layout is
  unverifiable without bench data we do not have (06 §8.2; 07 §9); these stay
  switchable/internal choices, not asserted facts.

Owner ratification item: SWING (50-75%, host-sync-only) and the discrete
host-rate selector are modern additions with **no hardware oracle** — swing's
taper is a pure by-ear design choice and both add panel/parameter surface the
1982 unit never had. They fit the locked "modern essentials" scope, but their
exact behavior and UI placement (kept visually segregated from the faithful
single-knob core) carry user-expectation risk and need explicit owner sign-off.

## Contract

Normative case table. The backlog implements this verbatim; the contract IS the
spec. "RT-safe" rows are governed by the no-alloc/no-lock lock.

| ID | Condition | Required behavior |
|---|---|---|
| C1 | LFO destinations | Same instantaneous selected-LFO value scaled by independent depth gains into VCO pitch, PWM, VCF cutoff. Fixed routing; no matrix. |
| C2 | PWM source switch | ENV / MANUAL / LFO selects PWM modulation source; PWM depth scales it. PWM uses ENV only when source = ENV. |
| C3 | Shared ADSR routing | One ADSR feeds VCF cutoff (ENV depth), VCA (via VCA ENV/GATE switch), and PWM (when PWM source = ENV). No second envelope. |
| C4 | S7 = GATE+TRIG | Last-note priority; envelope retriggers on every new key/step. |
| C5 | S7 = GATE | Lowest-note priority; legato keypress does NOT retrigger (mono-legato); single gate sustains. |
| C6 | S7 = LFO | Lowest-note priority; envelope re-fired on each LFO/clock cycle. |
| C7 | Arp modes | Exactly three: UP, U&D, DOWN. Mutually exclusive. |
| C8 | Arp held-key store | Up to 32 keys in a fixed 32-bit bitmap (NOT 4-note poly). Cycle in held order; no automatic octave expansion. |
| C9 | Arp octave | Octave shift only via global TRANSPOSE. While seq runs, TRANSPOSE disabled; KEY TRANSPOSE may transpose a running sequence. |
| C10 | Arp engagement | Engages on chord/legato; a single non-legato note plays normally. HOLD latch from panel and pedal keeps it running after release. |
| C11 | U&D turnaround | Switchable documented choice (repeat-endpoints vs not), default = one fixed choice; not asserted as bit-exact. |
| C12 | Seq slot model | note = 1 slot; REST = 1 slot; each tie/long-note extension = 1 slot. Max 100 slots. |
| C13 | Seq step payload | 6-bit pitch + REST flag + TIE/legato(slide) flag. No accent. No per-step gate-time. |
| C14 | Seq LOAD | Toggles record; writes from the instrument keyboard only; auto-exits at 100 slots. |
| C15 | Seq PLAY | Toggles wrap-around loop playback; advances one slot per clock H-to-L edge. |
| C16 | Seq articulation | Determined by stored ties + global GATE/TRIG mode only. TIE sustains envelope and engages portamento; REST drops the gate. |
| C17 | Clock node | One H-to-L edge detector advances arp, seq, and RANDOM reload together — phase-consistent across all sources. |
| C18 | Source = INTERNAL | LFO/CLK 0.1-30 Hz drives tempo; bit-identical to modeled hardware. |
| C19 | Source = HOST-SYNC | DAW PPQ drives edges; 1 host pulse = 1 step; edges placed at sub-block sample offsets feeding the SAME detector. Phase recomputed from absolute PPQ each block. |
| C20 | Source = EXT | External CV/automation pulse drives edges (1 pulse = 1 step). |
| C21 | RATE under HOST-SYNC or EXT | RATE controls ONLY the LFO as a mod source, NOT tempo. |
| C22 | CLOCK RESET | On any new keypress while in LFO-trigger OR arpeggio mode, re-phase the clock to that keypress. Default-on. |
| C23 | Host-rate selector | Discrete automatable: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, plus dotted. Active only under HOST-SYNC; grayed otherwise. |
| C24 | SWING | 50-75%, default 50% (off). Delays even-numbered step edges as a deterministic sample offset. Active only under HOST-SYNC; disabled/grayed under INTERNAL and EXT. Labeled non-101. |
| C25 | Persistence | Full 100-slot buffer + arp settings + clock config saved/restored via ValueTree, surviving session reload (battery-backed semantics). Schema versioned from v1. |
| C26 | RT-safe | Arp bitmap and 100-slot array preallocated; no heap alloc / no locks on the audio thread; param changes via lock-free APVTS/atomic path; state I/O off the audio thread via atomically-swapped snapshot. |
| C27 | Control tick | Control state machine runs on a tunable fixed sub-block tick (default models coarse 1.5-3.5 ms update); clock edges placed sample-accurate independent of the tick. |
