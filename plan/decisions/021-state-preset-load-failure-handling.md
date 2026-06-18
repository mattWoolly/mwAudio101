<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 021: State / preset load-failure handling

Status: accepted
Date: 2026-06-18

## Context

ADR-008 fixed the parameter/state/preset schema, the one versioned ValueTree,
the one migration chain, and the forward/backward tolerance *rule* (missing
param => default; unknown future param => preserved; unknown future
`schemaVersion` => best-effort, never crash). What ADR-008 did not pin down is
the concrete *runtime failure behavior* when a load goes wrong: what the plugin
actually does when a host hands `setStateInformation` a truncated or garbage
blob, when an old build opens a session written by a newer build, when a
`.mw101preset` file is malformed or out of range, or when an expected
BinaryData/factory preset is missing at construction. ADR-008's Contract states
the tolerance as a principle (C11) but does not enumerate the failure cases, the
fallback target, or how the user is told. This ADR owns that gap and turns the
principle into a normative, testable contract.

Forces in tension:

- **Never destroy the user's other work.** A plugin instance lives inside a host
  session that may contain dozens of other tracks. A crash or an exception
  thrown out of `setStateInformation` can take down the whole DAW or corrupt the
  surrounding project. The SH-101 clone is a bass/acid/sequencer workhorse that
  lives in long-lived DAW sessions (docs/research/11-cultural-influence.md §4.3,
  §4.7, §4.8), so a load failure must degrade locally and silently-enough, never
  catastrophically.
- **Fail safe, but fail visibly.** Silently swallowing a corrupt load and coming
  up as INIT would let a user keep working on top of a patch that is not the one
  they saved, and never know their state was lost. The user needs a
  non-intrusive signal, but a modal dialog from a plugin during host project
  load is itself a hazard (it can block the host's load thread / nested message
  loop). So the warning must be non-modal and message-thread only.
- **Round-trip preservation of the unknown.** ADR-008 C11 already promises an
  unknown future param is preserved and round-tripped. The same instinct must
  extend to a whole unknown-future-version blob loaded by an older build: drop as
  little as possible, default what we cannot interpret, and where feasible keep
  the raw original so re-saving from the old build does not silently strip the
  newer data a collaborator added.
- **Real-time safety is non-negotiable.** All of this recovery — parsing,
  validation, migration, fallback construction, warning surfacing — is on the
  message thread (owner lock; ADR-008 §5, C19). The audio thread must never see a
  half-applied state; the handoff to the audio thread is the existing atomic
  APVTS stores plus the pre-allocated lock-free SPSC `<extras>` double-buffer
  (ADR-008 C19/C20). A failed load must result in a complete, valid state being
  handed over, or the previously-running state being left untouched — never a
  torn one.

Owner locks this touches and re-affirms (does not reverse):

- **RT-safe (no alloc / no locks on the audio thread).** All failure handling is
  message-thread only; the audio thread keeps reading lock-free atomics.
  Re-affirmed.
- **Circuit-accurate "modeled from documented circuit behavior" + ~64 presets +
  100-step seq.** The fallback target is the INIT patch defined by the ADR-008
  registry defaults, and the missing-preset path must not silently drop the
  culturally load-bearing stored sequence (docs/research/11-cultural-influence.md
  §4.7, §4.8) for *other* presets that did load. Re-affirmed.
- **JUCE / C++20.** Realized through `setStateInformation` / preset-load on the
  message thread, JUCE `ValueTree` parsing, and a non-modal UI affordance.
  Re-affirmed.
- **Modern UI, max trademark distance.** The warning is a plugin-owned non-modal
  affordance, decoupled from the reimagined UI's look. Re-affirmed.

This ADR depends on and extends ADR-008 (the schema, the single migration chain,
C9-C12, C19-C20). It reconciles the C11 tolerance principle with a concrete
operational contract; it does not change the schema.

## Options considered

### Option A — Throw / propagate on bad load (rejected)

Let parse/validation failures propagate as exceptions or hard error returns out
of `setStateInformation` / preset load and let the host deal with it.

- Pros: simplest to write; surfaces the problem loudly; no recovery code to
  maintain.
- Cons: directly violates the "never crash, never take down the host" force; an
  exception escaping `setStateInformation` during host project load can corrupt
  or abort the surrounding session; behavior becomes host-dependent and
  untestable; no defined fallback state, so the instance can be left half-built.
  Rejected outright.

### Option B — Silent fallback to INIT, no signal (rejected)

On any failure, quietly reset to the INIT patch and continue. Never tell the
user.

- Pros: never crashes; trivially RT-safe; simplest UX (none).
- Cons: violates "fail visibly" — the user keeps working believing their saved
  patch loaded, then saves over the session with INIT, compounding the data
  loss; makes corruption bugs invisible in the field and impossible to triage.
  Rejected: data-loss without notice is worse than the failure itself.

### Option C — Best-effort recovery + non-modal warning + raw-preservation (chosen)

Treat load failure as a graded recovery, not a binary. Classify the failure,
recover to the most-faithful valid state reachable (preferring partial recovery
over full reset, and INIT only as the last resort), preserve the raw original
blob for round-trip where feasible, and surface a single non-modal, dismissible
warning on the message thread describing what happened. All recovery is
message-thread only; the audio thread is handed a complete valid state via the
existing atomic/SPSC handoff or is left untouched.

- Pros: satisfies every force — never crashes (Option A's failure), never loses
  state silently (Option B's failure), and minimizes dropped data via partial
  recovery and raw-preservation; reuses ADR-008's single migration chain and
  handoff; fully testable as enumerated cases with fixtures.
- Cons: most recovery code and the largest test matrix of the three; the
  "preserve raw for round-trip" path adds a stored-blob side channel and only
  works "where feasible" (a structurally unparseable blob cannot be preserved
  meaningfully); a non-modal warning affordance is UI surface the panel must
  design. Accepted: the cost buys the only behavior consistent with the locks.

This ADR also reconciles a latent contradiction inside ADR-008 C11 itself:
"unknown future param => preserved/round-tripped" pulls toward keeping unknown
data, while "unknown future version => load known IDs best-effort" could be read
as license to drop everything else. Option C resolves it by ranking outcomes:
partial-load-with-raw-preserved beats reset-to-INIT, and INIT is reserved for
the cases where nothing interpretable survives.

## Decision

Adopt **Option C**: graded best-effort recovery with a non-modal warning, layered
on ADR-008's single ValueTree, single migration chain (ADR-008 §5, C9-C12) and
RT-safe handoff (ADR-008 §5, C19-C20). The runtime rules below are normative; the
Contract table is the verbatim backlog spec.

1. **Never crash, never propagate.** `setStateInformation` and preset load MUST
   NOT throw out of the call or abort the process on any malformed, truncated,
   out-of-range, or unrecognized input. Every parse/validate/migrate step is
   wrapped so failure becomes a classified recovery, on the message thread only.

2. **Graded fallback, INIT as last resort.** Recovery prefers the most faithful
   valid state reachable:
   (a) if the root parses and `schemaVersion <= CURRENT`, run the ADR-008
   migration chain and bind, defaulting any missing/invalid param (ADR-008 C11);
   (b) if `schemaVersion > CURRENT` (newer build wrote it), bind every known ID
   that is present and valid, default the rest, and preserve the raw original for
   round-trip (rule 4);
   (c) only if the root is structurally unparseable or carries no interpretable
   params does the instance fall back to the **INIT patch** — the canonical
   defaults from the ADR-008 `ParamDefs` registry, including a default (empty)
   `<extras>` sequence. INIT is never reached while any interpretable state
   survives.

3. **Atomic, never-torn application.** A recovered state is fully assembled on
   the message thread *before* any handoff. Parameters are applied via the APVTS
   atomic stores and the `<extras>` via the pre-allocated lock-free SPSC
   double-buffer (ADR-008 C19/C20). If recovery cannot produce a complete valid
   state, the previously-running state is left untouched rather than partially
   overwritten; the audio thread MUST NOT observe a half-applied load. The
   fixed-capacity 100-step structure (ADR-008 C20) means even a stored sequence
   of the wrong length is clamped/padded without allocation.

4. **Preserve raw for round-trip where feasible.** When an unknown future
   `schemaVersion` (or an otherwise-newer blob) is loaded by an older build, the
   original serialized blob is retained alongside the down-interpreted state.
   On the next `getStateInformation`, the build re-emits the preserved newer data
   merged with any user edits made in this session to *known* params, so opening
   a collaborator's newer session in an older build and re-saving does not
   silently strip the newer fields. "Where feasible" = the root parsed as a
   ValueTree; a blob that does not parse at all cannot be preserved and falls to
   rule 2(c) INIT. This generalizes ADR-008 C11's per-param round-trip to the
   whole-blob case.

5. **Missing BinaryData / factory preset => INIT + warn, don't abort the bank.**
   If an embedded factory preset is absent or fails to decode at PresetManager
   construction, that preset slot resolves to INIT and a warning names the
   missing preset; the rest of the ~64-preset bank still loads. A missing user
   preset file selected from disk loads INIT and warns. A missing embedded
   resource MUST NOT abort plugin construction or empty the whole bank.

6. **Non-modal, message-thread, deduplicated warning.** Every recovery that
   deviates from a clean full load surfaces exactly one non-modal, dismissible
   warning describing the case (corrupt/unparseable, newer-version
   down-interpreted, out-of-range values clamped, missing preset). The warning is
   raised on the message thread only — never a modal/blocking dialog (a modal
   dialog during host project load can deadlock the host's load path). If the
   editor is not open, the condition is recorded and shown when the editor next
   opens (and logged). Multiple sub-failures in one load coalesce into a single
   warning, not a storm.

This honors the owner locks: all classification, migration, recovery and warning
are message-thread only with the audio thread reading lock-free atomics (no alloc
/ no locks on the audio thread; ADR-008 §5, C19); the fallback target is the
circuit-modeled INIT defaults from the ADR-008 registry; the warning is a
plugin-owned affordance independent of the reimagined UI.

## Consequences

This commits us to:

- Wrapping every load path (`setStateInformation`, preset load, BinaryData
  decode) in classified, non-throwing recovery on the message thread, with a
  defined fallback ladder ending at INIT.
- Carrying a retained-raw-blob side channel on instances that loaded a
  newer-than-current state, and a merge-on-save path that re-emits preserved
  newer fields alongside this-session edits to known params.
- A non-modal warning affordance in the editor plus a deferred-warning record for
  loads that happen while the editor is closed, and message-thread logging of
  every recovery for field triage.
- An expanded test matrix beyond ADR-008 C12's migration fixtures: a fixture per
  failure case in the Contract table (truncated blob, garbage bytes, newer
  version, out-of-range value, invalid choice index, missing embedded preset,
  wrong-length sequence) asserting no crash, the correct fallback target, raw
  preservation where required, and exactly one coalesced warning.
- PresetManager construction tolerating missing/undecodable embedded presets
  per-slot without aborting the bank.

This forecloses / makes harder:

- A blob that does not parse as a ValueTree at all cannot have its raw content
  preserved for round-trip; that data is unrecoverable by design (rule 4) and the
  instance comes up as INIT. Whole-blob round-trip is only guaranteed when the
  outer structure survives.
- The merge-on-save of preserved newer fields is best-effort: if a newer build
  changed the *meaning* of a known param (a re-skew that ADR-008 routes through
  migration), an older build re-saving can only preserve the newer raw subtree it
  did not understand, not reconcile semantic shifts in params it does interpret.
- A non-modal warning can be missed by a user who is not looking at the plugin;
  this is the deliberate trade against the host-deadlock risk of a modal dialog.
  The logged record is the backstop for triage.

Owner ratification item: the choice to surface load failures **non-modally**
(and to defer the notice until the editor opens, when a load happens with the
editor closed) means a user can briefly work on a recovered INIT patch without
having seen the warning. This is a deliberate trade against the
host-deadlock/abort risk of a modal dialog during project load, but it carries a
user-expectation risk (silent-feeling recovery) not explicitly covered by the
locks; confirm non-modal-only is the intended v1 behavior.

## Contract

Normative rules the backlog implements verbatim. "MUST" / "MUST NOT" are binding.
These extend ADR-008's Contract (C1-C20) and do not supersede it.

| # | Failure case | Required behavior |
|---|---|---|
| L1 | `setStateInformation` given a truncated/garbage/structurally-unparseable blob | MUST NOT throw out of the call or crash; the instance falls back to the INIT patch (ADR-008 `ParamDefs` defaults + empty `<extras>`) and raises exactly one non-modal warning. |
| L2 | Root parses, `schemaVersion <= CURRENT` | MUST run the ADR-008 migration chain (C10), bind APVTS, default any missing/invalid param (C11). Warn only if a value was missing or had to be clamped. |
| L3 | Root parses, `schemaVersion > CURRENT` (newer build wrote it) | MUST bind every known ID present and valid, default the rest, retain the raw original blob for round-trip (L6), and raise one non-modal warning. MUST NOT reset to INIT while interpretable params survive. |
| L4 | Individual param value out of range / invalid choice index | MUST clamp continuous values into `NormalisableRange` and reset an invalid choice index to its default; MUST NOT crash; coalesce into the single load warning. |
| L5 | Fallback ranking | Recovery MUST prefer partial-load-with-defaults over full reset; INIT is the last resort, reachable only when no interpretable params survive (L1). |
| L6 | Round-trip preservation | When a newer-than-current blob parsed as a ValueTree, the original serialized blob MUST be retained; the next `getStateInformation` MUST re-emit the preserved newer data merged with this-session edits to known params. A blob that did not parse cannot be preserved and falls to L1. |
| L7 | Never-torn application | A recovered state MUST be fully assembled on the message thread before any handoff; parameters via APVTS atomics and `<extras>` via the pre-allocated lock-free SPSC double-buffer (ADR-008 C19/C20). If a complete valid state cannot be produced, the previously-running state MUST be left untouched; the audio thread MUST NOT observe a half-applied load. |
| L8 | Wrong-length stored sequence | A stored sequence shorter or longer than 100 steps MUST be padded/clamped into the fixed-capacity structure (ADR-008 C20) with no audio-thread allocation; MUST NOT crash. |
| L9 | Missing/undecodable embedded factory preset | The affected preset slot MUST resolve to INIT and warn naming the preset; the rest of the bank MUST still load; construction MUST NOT abort or empty the bank. |
| L10 | Missing user preset file on disk | MUST load INIT and raise one non-modal warning; MUST NOT crash. |
| L11 | Malformed `.mw101preset` (bad JSON / failed schema validation) | MUST NOT crash; MUST load INIT (or partial-load per L2/L3 if the canonical ValueTree projection is recoverable) and warn. |
| L12 | Warning surfacing | Every recovery deviating from a clean full load MUST raise exactly one non-modal, dismissible, message-thread-only warning (NEVER a modal/blocking dialog). Multiple sub-failures in one load MUST coalesce into a single warning. If the editor is closed, the condition MUST be recorded, logged, and shown when the editor next opens. |
| L13 | Real-time safety of recovery | All classification, parsing, migration, validation, fallback construction, raw retention and warning MUST run on the message thread only. The audio thread MUST NOT parse, allocate, lock, or observe an in-progress recovery. |
| L14 | Recovery test fixtures | Each case L1-L11 MUST have a committed fixture asserting no crash, the correct fallback target, raw preservation where required (L6), and exactly one coalesced warning (L12). These extend the ADR-008 C12 migration fixtures. |
