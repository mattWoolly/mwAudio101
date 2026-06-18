<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 025: Sequencer per-step accent — removed for v1

Status: accepted
Date: 2026-06-18

## Context

The Phase-2b coherence pass surfaced a direct contradiction between two accepted
ADRs over whether the built-in 100-step sequencer carries a per-step **accent**:

- **ADR-007 (Modulation routing, arpeggiator & 100-step sequencer)** forbids
  accent. Its resolution explicitly *refuses* accent and per-step gate-time
  (resolution point 3: "Accent and per-step gate-time are **refused** …
  confirmed by 06 §8.1 / 07 §5.5 — accent is a TB-303/MC-202 feature the 101
  lacks"), and its contract row **C13** ("Seq step payload") states the payload
  is `6-bit pitch + REST flag + TIE/legato(slide) flag. No accent. No per-step
  gate-time." Articulation = stored ties + global GATE/TRIG only.
- **ADR-008 (Parameter / state / preset schema — the contract)** included
  accent. Its resolution adopts preset-design's "full-state capture with the
  100-step pattern as first-class state (per-step **note/gate/tie/rest/accent** +
  arp latch)", and its contract row **C8** ("Non-parameter state") enumerates the
  pattern as "per-step note/gate/tie/rest/**accent**" living in the `<extras>`
  subtree.

So ADR-007 C13 says the per-step model has no accent field, while ADR-008 C8
reserves one in the persisted state schema. Both cannot stand.

The hardware record resolves which side is faithful. The SH-101's built-in
sequencer is firmware on a single CPU (Toshiba TMP80C49P, IC6) and has **no
per-step accent**: per-step data is a ~6-bit note packed into the low bits plus
two high flag bits inferred as TIE/legato(slide) and REST, and articulation is
determined entirely by stored LEGATO ties plus the global ENV GATE/TRIG selector
— there is no independent per-step gate-time and no accent bit
(docs/research/06 §3.2-3.3, §6.2, §9 point 3-4). The research record is explicit
that accent is a **TB-303/MC-202 trait, not an SH-101 one**, and that any
expanded sequencer functionality appearing only in later software is a
software-emulation artifact, not 1982 hardware behavior
(docs/research/06 §8.7). Per-step accent is therefore not faithful to the
instrument this project models.

This ADR touches the owner-locked **"faithful mono SH-101 path + modern
essentials"** decision. It re-affirms that lock: it resolves the contradiction in
favor of faithfulness for v1 and does not reverse any lock. The deferred future
option (accent as a labeled modern addition) sits within the "modern essentials"
scope and would itself be a future, owner-flagged ADR.

## Options considered

Two honest options were on the table once the contradiction was named.

### Option A — Keep accent as a modern step-sequencer extra

Retain ADR-008 C8's accent field, expose a per-step accent in the editor, and
label it (like SWING) as an explicit non-101 modern addition.

- Pros: meets the expectation of users coming from modern step sequencers and
  the TB-303 acid idiom that the SH-101 is culturally paired with
  (docs/research/06 §4.2 lists the TB-303 among recommended clock partners);
  accent + the sub-oscillator is a recognizable bass-line gesture; one more
  expressive lever for the SeqArpRiff preset category.
- Cons: directly contradicts ADR-007 C13 and its resolution point 3, which
  refused accent on faithfulness grounds already adopted by the panel; imports a
  TB-303/MC-202 feature the 101 never had (docs/research/06 §8.7), exactly the
  "do not masquerade a software/other-instrument feature as SH-101 fidelity"
  failure mode the project guards against; needs its own honesty label, UI
  surface, and per-step value model, plus an articulation-precedence rule against
  the global GATE/TRIG mode; widens the v1 sequencer scope for a feature with no
  hardware oracle.

### Option B — Drop per-step accent for v1 (faithful)

Remove accent from the sequencer model and the persisted state schema for v1;
align everything to ADR-007's no-accent payload; defer accent to a future ADR if
it is ever wanted as a labeled modern extra.

- Pros: faithful — matches the documented SH-101 firmware (no accent bit;
  articulation = stored ties + global GATE/TRIG only; docs/research/06 §3.2-3.3,
  §9); resolves the C13-vs-C8 contradiction cleanly by deleting the disputed
  field rather than reconciling two semantics; keeps the v1 sequencer payload
  minimal and bit-faithful (note + REST + TIE); avoids shipping a TB-303 trait
  under an SH-101 banner; pre-release, so no migration cost.
- Cons: users expecting a modern accented step sequencer will find accent absent
  in v1 (the same expectation gap ADR-007 already accepted for accent / per-step
  gate-time); the only sanctioned later path is to add accent explicitly labeled
  as a non-101 modern addition via a future ADR, not silently.

## Decision

**Drop per-step accent for v1.** Adopt Option B.

1. The sequencer per-step payload is **note / rest / tie / gate only**, exactly
   ADR-007 C13: `6-bit pitch + REST flag + TIE/legato(slide) flag`, with **no
   accent** and no per-step gate-time. Articulation is stored ties + the global
   ENV GATE/TRIG mode (docs/research/06 §3.2-3.3, §9).
2. This **supersedes the accent field in ADR-008 C8.** ADR-008 C8's per-step
   tuple is reduced from `note/gate/tie/rest/accent` to `note/gate/tie/rest`
   (no accent). All other ADR-008 C8 behavior (the pattern lives in the
   `<extras>` subtree, is not a host parameter, etc.) is unchanged.
3. Align the design docs to ADR-007's no-accent sequencer: the parameters /
   state / preset schema doc (**docs/design/06** — parameters/state/presets) and
   the modulation / arp / seq doc (**docs/design/05** — modulation/arp/seq) both
   describe the per-step payload as note/rest/tie/gate only, with no accent field
   in the `<extras>` pattern, the `seq` preset section, or the migration schema.
4. Accent is recorded as a **candidate FUTURE modern-essential** (a labeled
   non-101 addition, in the spirit of SWING and the host-rate selector under
   ADR-007), **deferred to a future ADR.** It is not in v1 scope and must not be
   added silently; if ever added it must be fenced and labeled as a non-101
   modern addition, never asserted as SH-101 fidelity (docs/research/06 §8.7).

This resolves the cross-ADR contradiction by deletion (the cleaner of the two
fixes) on the side the panel had already chosen on faithfulness grounds in
ADR-007, and keeps the v1 sequencer payload bit-faithful to the documented
hardware.

## Consequences

This commits us to:

- A single, contradiction-free sequencer per-step model across the contract:
  ADR-007 C13 and ADR-008 C8 now agree — per-step payload is note/rest/tie/gate
  only, with no accent and no per-step gate-time.
- **ADR-008 C8's accent field is removed.** The persisted state / preset format
  drops the accent field: the `<extras>` 100-step pattern and the `.mw101preset`
  `seq` section store note/rest/tie/gate per step only. There is **no migration
  needed** — the project is pre-release and no accent-carrying state or presets
  have shipped, so the field is simply never introduced (no `schemaVersion` bump
  on this account).
- Articulation in the sequencer remains a function of stored LEGATO ties plus the
  global GATE/TRIG mode only, as ADR-007 already specified.

This forecloses / makes harder:

- No per-step accent in v1. Users expecting a modern accented step sequencer (the
  TB-303 acid idiom) will find it absent; this is the same expectation gap
  ADR-007 already accepted for accent and per-step gate-time. The only sanctioned
  mitigation is a future ADR that adds accent as an explicitly labeled non-101
  modern addition.
- Should accent later be admitted, it will then be a true schema change (a new
  per-step field + `schemaVersion` bump + migration + golden fixture under
  ADR-008 C10/C12) rather than a free edit, because by then the format will have
  shipped.
