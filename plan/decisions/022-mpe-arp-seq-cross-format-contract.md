<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 022: MPE-lite & arp/seq cross-format behavior

Status: accepted
Date: 2026-06-18

## Context

ADR-011 builds every binary (VST3, AU, CLAP, Standalone, LV2 goal-tier) from one
shared host-agnostic engine and explicitly defers — as a flagged gap — the
"explicit per-format cross-test coverage of MPE-lite and host-synced transport
... because host support varies per format" (ADR-011 Consequences; Contract C10).
ADR-012 specifies *what* MPE-lite and the MIDI front-end do, and ADR-007
specifies *what* the arp + 100-step sequencer do — but both assume the host hands
us note-expression and a sample-accurate playhead. They do not. Each wrapper
exposes a different, sometimes absent, surface:

- **Note-expression** (per-note pitch / per-note pressure on member channels):
  CLAP has first-class typed note-expressions; VST3 has `INoteExpression` but
  MPE arrives as raw per-channel MIDI in most hosts; AU (AUv2 via JUCE) has no
  note-expression API and only multi-channel MIDI; LV2 carries MPE only as raw
  MIDI atoms; Standalone receives raw MIDI from a hardware/virtual port. So
  "MPE-lite" cannot assume a uniform note-expression channel.
- **Sample-accurate transport** (sub-block PPQ for placing clock edges, ADR-007
  C19): CLAP delivers a sample-accurate transport event; VST3/AU/LV2/Standalone
  give at best one `AudioPlayHead::PositionInfo` per block (block-quantized), and
  Standalone or a stopped/unsupported host gives *no* transport at all.
- **Tempo / PPQ availability**: present whenever a host transport exists; absent
  in Standalone and in hosts that report no playhead.

This ADR owns that gap end-to-end. The question: *when a host/format lacks
note-expression or sample-accurate transport, what is the normative fallback, and
what does each format actually get?*

Owner-locks this touches and re-affirms (none reversed):

- **Circuit-accurate, modeled-from-documented-circuit-behavior, no physical
  oracle.** There is no SH-101 MIDI/host-sync oracle (ADR-012 Context; ADR-007
  Context); every fallback here is a *disciplined clone-layer policy*, not
  invention. Fallbacks must degrade toward the documented EXT-CLK / CV-gate /
  key-assigner behavior, never around it.
- **RT-safe: no heap alloc / no locks on the audio thread.** Every fallback path
  (collapse, block-quantize, free-run) is pre-decided at `prepareToPlay`/host
  query time and is allocation-free on the audio thread (ADR-011 C9, C10;
  ADR-012 C16-C17; ADR-007 C26).
- **macOS arm64 bit-exact bless + Linux integer bit-exact / FP tolerance-banded.**
  Identical *input* (same note-expression resolution, same transport availability)
  must yield bit-identical output across formats (ADR-011 C11). This ADR makes
  the *capability normalization* the thing that is bit-exact, so the bless still
  applies once the wrapper has been collapsed to a common representation.
- **One shared engine, formats are thin adapters** (ADR-011). All per-format
  divergence is confined to a capability-detection + normalization shim *in the
  wrapper*, never in the DSP.

This ADR reconciles and depends on: **ADR-011** (the format/validator matrix and
the single normalized lock-free event buffer it mandates), **ADR-012** (MPE-lite
scope: lower-zone, per-note bend pre-quantizer, one assignable pressure
destination default VCF cutoff CV; mono collapse), and **ADR-007** (the single
H-to-L edge detector, three clock sources INTERNAL/HOST-SYNC/EXT, PPQ-derived
edges, RATE-decouple-when-synced, CLOCK RESET default-on).

## Options considered

The split is not between personas on architecture — ADR-011 already fixed the
single-engine + normalization-shim shape — but between two honest fallback
philosophies for what happens at the format boundary.

### Option A: Per-format feature gating (advertise only what each format supports)

The plugin queries the host at init and *disables* MPE-lite where there is no
note-expression API and *disables* host-sync where there is no sample-accurate
transport, surfacing each as greyed-out/unavailable.

- Pros: never lies to the user about capability; no "MPE looks on but behaves
  like global bend" surprise; trivially testable (a feature is on or off).
- Cons: fragmentary and hostile to the ADR-011 promise that "all host-facing
  semantics are modeled once and behave bit-identically across formats." MPE-lite
  would silently vanish on AU/Logic (a huge user base), and host-sync would vanish
  in Standalone — exactly the formats users most expect those features in. It also
  conflates "the *API* is absent" with "the *capability* is absent": MPE arriving
  as raw multi-channel MIDI is still MPE, and a block-quantized playhead is still a
  usable clock. Gating throws away recoverable capability.

### Option B: Graceful degradation with a normalized capability ladder (chosen)

Treat note-expression and transport as *capabilities with documented fallback
rungs*, normalized in the wrapper before the engine ever sees them. The engine
always runs the same MPE-lite and the same edge detector; the wrapper feeds it the
best available representation and records which rung it used.

- Note-expression ladder: native typed note-expression (CLAP) -> MPE-as-raw-MIDI
  reconstructed into the same per-voice bend/pressure the engine expects
  (VST3/AU/LV2/Standalone, when MPE mode is enabled) -> **collapse to global
  channel behavior** (channel bend + channel pressure, exactly ADR-012 C13's mono
  collapse) when no per-note channel data is present.
- Transport ladder: sample-accurate transport events (CLAP) -> **block-quantize**
  one `PositionInfo` per block into the same PPQ-derived edge placement
  (VST3/AU/LV2) -> **free-run** the INTERNAL clock at the RATE knob when no
  transport is reported (Standalone, stopped/headless hosts), exactly ADR-007's
  INTERNAL source and ADR-011 C10's free-run fallback.

- Pros: honors the ADR-011 single-engine promise — same DSP, same bless reference,
  every format; recovers all recoverable capability (MPE-over-MIDI is not thrown
  away); each fallback rung lands on an *already-documented* behavior (mono-collapse
  from ADR-012, INTERNAL free-run from ADR-007) so nothing is invented; the
  capability rung is a single enum the wrapper resolves once, RT-safe.
- Cons: the "MPE enabled but collapsed to global" state can surprise a user on a
  non-MPE host (mitigated by surfacing the active rung in the UI — owner
  ratification below); block-quantized edges on VST3/AU jitter by up to one block
  vs CLAP's sample-accurate edges (acceptable and bounded; documented, not hidden);
  reconstructing MPE from raw MIDI needs a per-format channel-rotation parser that
  must itself be cross-tested.

### Why B wins over A

Option A reverses the spirit of ADR-011 (uniform host-facing semantics) and the
spirit of ADR-012/007 (one control model, the mono/INTERNAL path is the faithful
core and everything else is a faithful *replica or extension* of it). Crucially,
ADR-012 already *defines the collapse target*: mono MPE collapses to channel bend +
channel pressure (C13). ADR-007 already *defines the free-run target*: the INTERNAL
0.1-30 Hz clock (C18) and ADR-011 C10 already names free-run as the no-transport
fallback. Option B therefore introduces **no new behavior** — it only routes the
existing documented behaviors as the lower rungs of a ladder. That is the
disciplined, no-oracle-needed resolution.

## Decision

Adopt **graceful degradation via a normalized capability ladder**, resolved once in
each wrapper's capability shim and fed into the single shared engine, per ADR-011's
"one normalized, fixed-capacity, lock-free internal event buffer" (ADR-011
Decision, C9). The engine is capability-agnostic; the wrapper is capability-aware.

1. **Note-expression normalization (MPE-lite).** At a level above the engine, each
   wrapper resolves a `NoteExpressionRung`:
   - **Native** — CLAP typed note-expressions (`CLAP_NOTE_EXPRESSION_TUNING` for
     per-note pitch, `..._PRESSURE` for per-note pressure) map directly to the
     per-voice continuous **pre-quantizer** pitch offset and the one assignable
     pressure destination (default VCF cutoff CV) of ADR-012 C11-C12.
   - **MPE-over-MIDI** — VST3, AU, LV2, Standalone, when MPE-lite member channels
     are enabled (ADR-012 C10, default OFF). Raw per-channel MIDI (per-channel
     pitch-bend, channel pressure / CC74) is reconstructed into the *same*
     per-voice bend/pressure the Native rung produces. Lower-zone only, member
     channels 1..15, per ADR-012 §4.
   - **Collapsed (global)** — whenever no per-note channel data is available or
     MPE-lite is OFF: per-note expression collapses to **channel pitch-bend +
     channel pressure**, i.e. ADR-012 C13's documented mono-MPE collapse, applied
     globally. This is the universal floor and is bit-identical to running the
     synth without MPE.

   All three rungs feed the identical engine path; only the *source* of the
   per-voice offset differs. Because the engine input is normalized, the macOS
   bless reference (ADR-011 C11) holds across formats for a given rung.

2. **Transport normalization (arp + 100-step seq clock).** Each wrapper resolves a
   `TransportRung` for ADR-007's HOST-SYNC source:
   - **Sample-accurate** — CLAP's transport event places clock H-to-L edges at
     exact sub-block sample offsets (ADR-007 C19 in full fidelity).
   - **Block-quantized** — VST3, AU, LV2: one `AudioPlayHead::PositionInfo` per
     block. PPQ + tempo are read once per block; edges that fall inside the block
     are derived from absolute PPQ (ADR-007 Consequences: "host edges derived from
     absolute PPQ per block") and placed at the **block boundary** (sample 0 of the
     block in which the edge's PPQ falls). Phase is still recomputed from absolute
     PPQ each block, so tempo changes / loop / scrub stay correct (ADR-007 C19);
     only the within-block sample placement is quantized to the block start. Edge
     ordering, count, and PPQ-derived phase are identical to the Sample-accurate
     rung; only sub-block timing differs.
   - **Free-run** — no transport reported (Standalone; stopped or playhead-less
     hosts): the clock falls back to ADR-007's **INTERNAL** source (LFO/CLK
     0.1-30 Hz at the RATE knob), exactly ADR-011 C10's free-run fallback and
     ADR-007 C18. Reading the (absent) playhead is allocation-free. If the user has
     selected HOST-SYNC but no transport exists, HOST-SYNC silently behaves as
     INTERNAL until a transport appears, then re-locks from absolute PPQ.

   In every transport rung the **single H-to-L edge detector** of ADR-007 C17
   advances arp + seq + RANDOM together, and **RATE decouples from tempo** when
   HOST-SYNC/EXT is the active source (ADR-007 C21). SWING and the host-rate
   selector (ADR-007 C23-C24) are active only under the Sample-accurate and
   Block-quantized rungs (i.e. only when a transport exists), greyed under
   Free-run.

3. **Tempo / PPQ.** Used only to derive HOST-SYNC edges and the musical-rate /
   swing math. When absent (Free-run), RATE drives the INTERNAL clock; no tempo is
   fabricated.

4. **Capability resolution is RT-safe and pre-decided.** The wrapper resolves both
   rungs at init / `prepareToPlay` and re-checks transport presence per block via a
   branch-free read of the cached playhead pointer (ADR-007 Consequences; ADR-011
   C10). No rung transition allocates or locks on the audio thread. The resolved
   rung is published to the UI via the same lock-free atomic-pointer path used for
   the CC/learn map (ADR-012 C16) so the user can *see* which rung is active.

5. **Cross-test obligation discharged.** This ADR converts ADR-011's flagged "must
   have explicit per-format cross-test coverage of MPE-lite and host-synced
   transport" gap into the normative per-format Contract table below, which the CI
   matrix (ADR-011 Consequences) tests verbatim against each format's validator and
   a host-capability harness.

Rationale: there is no MIDI/host-sync oracle (ADR-012 Context §; ADR-007 Context),
so discipline = every fallback rung is one of the *already-documented* behaviors
(ADR-012 C13 collapse; ADR-007 C18 INTERNAL free-run) rather than a new invented
mode. The engine sees one normalized representation, so the bit-exact bless
(ADR-011 C11) is preserved per rung.

## Consequences

This commits us to:

- A per-format **capability shim** (note-expression rung + transport rung)
  resolved once and fed into ADR-011's single normalized lock-free event buffer;
  the DSP stays capability-agnostic and the bless reference is unchanged.
- An **MPE-over-MIDI reconstruction parser** (per-channel bend / channel pressure
  / CC74 -> per-voice offset) for VST3/AU/LV2/Standalone, itself cross-tested, so
  MPE works on hosts with no note-expression API (notably AU/Logic).
- A **block-quantized edge placement** path: VST3/AU/LV2 arp/seq edges land on
  block boundaries (up to one block of jitter vs CLAP), with PPQ-derived phase /
  count / ordering identical to the sample-accurate rung. The jitter is bounded and
  documented, not hidden.
- Surfacing the **active rung in the UI** (note-expression: Native / MPE-over-MIDI
  / Collapsed; transport: Sample-accurate / Block-quantized / Free-run) so a
  collapsed-or-free-run state is visible, not a silent surprise.
- Extending the CI capability-harness to assert the Contract table per format
  against ADR-011's validator matrix.

This forecloses / makes harder:

- **No sample-accurate MPE or sample-accurate arp/seq edges outside CLAP** — VST3,
  AU, LV2, and Standalone are bounded by their host API (note-expression
  reconstruction; block-quantized edges). This is a host-API limit, not a design
  shortfall, but it means CLAP is the only format with full sub-block fidelity.
- **No per-format feature divergence in the DSP** — adding a capability rung is
  always a wrapper-shim change feeding the same engine, never a DSP fork (re-affirms
  ADR-011).
- A user on a non-MPE host who enables MPE-lite gets the Collapsed (global) rung;
  this is correct and documented but can read as "MPE not working" without the UI
  rung indicator.

Owner ratification item: two user-expectation risks arise beyond the locks. (a)
**MPE-lite silently collapses to global channel behavior on non-note-expression
hosts** (VST3 without MPE-mode, AU/Logic, LV2, Standalone) — faithful to ADR-012
C13 but will read as "MPE doesn't work here" unless the active-rung UI indicator
is shipped; confirm we surface the rung. (b) **Arp/seq edges are block-quantized
(up to one block of jitter) on every format except CLAP, and free-run in
Standalone** — confirm this bounded, format-dependent timing fidelity is the
accepted behavior rather than gating host-sync off where it is not sample-accurate.

## Contract

Normative. The backlog implements this verbatim; the CI capability-harness asserts
it per format alongside the ADR-011 validator matrix. "Collapse" = ADR-012 C13
global channel bend + channel pressure. "Block-quantize" = edge placed at the
block boundary containing its absolute-PPQ position, phase/count/order identical to
sample-accurate. "Free-run" = ADR-007 INTERNAL clock at RATE (ADR-011 C10).

Per-format capability matrix:

| Format | Note-expression rung | Sample-accurate transport rung | Tempo / PPQ availability |
| --- | --- | --- | --- |
| VST3 | MPE-over-MIDI when MPE-lite ON; else Collapsed | Block-quantized (one PositionInfo/block); Free-run if no transport | Available when host transport present; else absent -> Free-run |
| AU | MPE-over-MIDI when MPE-lite ON (no note-expression API); else Collapsed | Block-quantized; Free-run if no transport | Available when host transport present; else absent -> Free-run |
| CLAP | Native typed note-expression; Collapsed if none sent | Sample-accurate (transport event, sub-block edges); Free-run if no transport | Available when host transport present; else absent -> Free-run |
| LV2 | MPE-over-MIDI (raw MIDI atoms) when MPE-lite ON; else Collapsed | Block-quantized; Free-run if no transport | Available when host transport present; else absent -> Free-run |
| Standalone | MPE-over-MIDI (raw MIDI port) when MPE-lite ON; else Collapsed | Free-run (no host transport) | Absent -> Free-run (RATE drives INTERNAL clock) |

Behavioral cases:

| # | Case | Required behavior |
| --- | --- | --- |
| C1 | Format exposes native note-expression (CLAP), MPE-lite ON | Native rung: typed per-note pitch -> per-voice pre-Q offset; per-note pressure -> assignable destination (default VCF cutoff CV), per ADR-012 C11-C12 |
| C2 | No note-expression API but MPE-lite ON (VST3/AU/LV2/Standalone, members enabled) | MPE-over-MIDI rung: reconstruct per-channel bend + channel pressure / CC74 into the SAME per-voice offset as C1; lower-zone only, members 1..15 (ADR-012 §4, C10) |
| C3 | MPE-lite OFF, or no per-note channel data present (any format) | Collapsed rung: per-note expression -> channel pitch-bend + channel pressure globally (ADR-012 C13). Bit-identical to running without MPE |
| C4 | Same expression input, two formats, same rung, macOS arm64 | DSP output bit-identical across formats (ADR-011 C11); only the rung *source* differs, never the engine path |
| C5 | Host provides sample-accurate transport (CLAP) | Sample-accurate rung: H-to-L edges placed at exact sub-block sample offsets (ADR-007 C19) |
| C6 | Host provides only one PositionInfo per block (VST3/AU/LV2) | Block-quantized rung: edges derived from absolute PPQ, placed at containing block boundary; phase recomputed from absolute PPQ each block; count/order identical to C5 (ADR-007 C19 + Consequences) |
| C7 | No transport reported (Standalone; stopped/playhead-less host) | Free-run rung: clock falls back to ADR-007 INTERNAL source at RATE (0.1-30 Hz); reading the absent playhead is allocation-free (ADR-011 C10; ADR-007 C18) |
| C8 | User selects HOST-SYNC but no transport exists | Behaves as INTERNAL (Free-run) until a transport appears; then re-locks from absolute PPQ with no allocation |
| C9 | Any transport rung active | Single H-to-L edge detector advances arp + seq + RANDOM together (ADR-007 C17); RATE decouples from tempo under HOST-SYNC/EXT (ADR-007 C21) |
| C10 | SWING / host-rate selector | Active only under Sample-accurate or Block-quantized rungs; greyed under Free-run (ADR-007 C23-C24) |
| C11 | Capability rung resolution | Resolved at init/prepareToPlay; per-block transport-presence recheck is a branch-free cached-pointer read; no rung transition allocates or locks on the audio thread (ADR-011 C9-C10; ADR-007 C26) |
| C12 | Active rung visibility | Both rungs published to the UI via the lock-free atomic-pointer path (ADR-012 C16); collapsed / free-run states are user-visible, not silent |
| C13 | Adding/altering a capability rung | Always a wrapper-shim change feeding the same engine; never a DSP fork (re-affirms ADR-011) |
