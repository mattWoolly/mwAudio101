<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 023: Engine versioning, bless communication & blessed sample-rate set

Status: accepted
Date: 2026-06-18

## Context

mwAudio101 has a bless/golden discipline (ADR 013) and a frozen parameter/state
contract (ADR 008), but two gaps remain between them. First: when a DSP change
legitimately alters blessed output (a re-tuned `tanh` approximation, a new
decimator, a recomputed compensation table per ADR 003), how does a user's
already-rendered session know that re-opening it now sounds different? A
`schemaVersion` bump (ADR 008 §5) covers parameter/state shape, but it does NOT
capture "the same parameters now render different samples." Without an explicit
signal, a DSP improvement silently mutates every existing project on the next
load — the exact forever-promise breakage ADR 008 §Context warns about, but on
the audio-rendering axis rather than the parameter axis. Second: ADR 013 keys
every golden by `{render-graph hash, engine tag, oversample factor, sample rate,
block size, seed}` and ADR 003 requires the compensation/tuning table to be
**regenerated per sample rate** at `prepareToPlay` — but neither ADR names WHICH
sample rates the golden corpora are generated at, nor what happens at unusual
host rates (e.g. 192 kHz, where the mandatory 2x oversampling of ADR 003 §F-09
pushes the oversampled internal rate to 384 kHz and the cutoff/stability guard
`fc <= 0.45*fs_os` of ADR 003 §F-08 starts squeezing usable headroom).

This ADR owns those two gaps: a semantic **engine version** plus a
bless-affecting **renderVersion**, and the definition of the **blessed
sample-rate set** with defined behavior above it.

It touches and re-affirms (does not reverse) these owner-locks
(plan/ORCHESTRATION.md "Decisions locked with the project owner"):

- **Circuit-accurate modeling from documented circuit behavior; NO physical-unit
  oracle.** renderVersion communicates "we changed the DSP," never "we got
  closer to a real SH-101" — consistent with ADR 013's statement that a golden
  proves self-consistency, not measured fidelity. Re-affirmed.
- **macOS arm64 = reference/bless, FP bit-exact; Linux x64 = hard gate (integer
  bit-exact, FP tolerance-banded per design spec §5); Windows x64 = goal.** The
  blessed sample-rate set is the axis along which ADR 013's CLASS-EXACT /
  CLASS-FP corpora are generated. Re-affirmed.
- **Real-time safe (no heap alloc / no locks on the audio thread).** renderVersion
  is read/written on the message thread only (it lives in serialized state, ADR
  008 §5); the per-SR table regen happens at `prepareToPlay` (ADR 003 §F-11), not
  at audio rate. Re-affirmed.
- **JUCE / C++20 / CMake; GPL-3.0-or-later.** Re-affirmed.
- **Feature scope incl. oversampling and the full modern-essentials set.** The
  high-host-SR clamp is the boundary condition of the oversampling lock. Re-affirmed.

It depends on and reconciles: ADR 013 (bless tool, MANIFEST, two-stage comparer,
determinism-class corpora), ADR 003 (Huovilainen engine, mandatory 2x OS,
per-SR compensation table, frozen `tanh`/decimator constants as the basis of
bit-exactness, `fc <= 0.45*fs_os` guard), and ADR 008 (`schemaVersion` +
`pluginVersion` on the state root, one migration chain, append-only discipline,
all state work on the message thread).

## Options considered

The forces are settled by the locks; the genuine choices are (A) how versioning
is layered and (B) how the blessed sample-rate set is bounded.

### Axis A — one version number vs. layered semantic + render versions

**A1: Reuse `schemaVersion` (ADR 008) as the only version.** Bump it whenever
the DSP changes blessed output.

- Pros: one number, no new field, no new migration concept.
- Cons: conflates two orthogonal facts. `schemaVersion` means "the state SHAPE
  changed and must be migrated" (ADR 008 §5 / C10); a DSP re-tune changes NO
  parameter shape — every ID, range and value is identical, only the rendered
  samples differ. Bumping `schemaVersion` for a pure-DSP change forces a no-op
  migration step + fixture (ADR 008 C12) for a change that migrates nothing, and
  worse, it would make a render-only change indistinguishable from a true schema
  change in the migration switch. Rejected: it overloads a field with a second,
  contradictory meaning.

**A2: A single monotonic "build/engine" integer, no semantic structure.**

- Pros: trivially detects "different from last time."
- Cons: cannot answer "is this a bugfix I should silently take, or an
  audio-altering change I must NOT force on an old session?" A flat counter has
  no notion of "blessed output unchanged" vs "blessed output changed," so either
  every build re-flags every session (alarm fatigue, the ADR 013 "train
  reviewers to ignore red" failure mode) or none do. Rejected.

**A3 (chosen): A semantic `engineVersion` (MAJOR.MINOR.PATCH) for human/release
communication, PLUS a separate integer `renderVersion` stored in plugin state
that increments ONLY when blessed output changes.** `renderVersion` is the
audio-rendering analogue of ADR 008's `schemaVersion`: it is the contract that
"these parameters render these samples." A session carries its `renderVersion`;
re-opening does not silently upgrade it — the user opts in.

- Pros: cleanly separates state-shape versioning (ADR 008 `schemaVersion`) from
  audio-rendering versioning (this ADR); the bless tool can mechanically tie a
  `renderVersion` bump to a MANIFEST change (ADR 013 Layer 3), making "did
  blessed output change" an auditable fact, not a judgement call buried in a
  commit; existing sessions are reproducible by construction (they pin their
  `renderVersion`); release notes derive from the engineVersion + the set of
  `renderVersion` bumps in the range.
- Cons: a second version field and a "legacy render path" obligation — keeping
  old render behavior available so a pinned old `renderVersion` actually
  reproduces. This is real long-term cost (see Consequences) but is the only
  honest way to keep the forever-promise.

### Axis B — bounding the blessed sample-rate set

**B1: Bless one rate (48 kHz) only; resample everything else.** Simplest corpus.

- Cons: a host at 44.1/88.2/96 kHz would run through an internal resampler whose
  artifacts are not what ADR 003's per-SR compensation table is designed for; it
  hides real per-SR tuning differences behind a resampler and contradicts ADR 003
  §F-11's "table regenerated per sample rate." Rejected.

**B2: Bless every rate the host might present (incl. 176.4/192 kHz).** Most
coverage.

- Cons: the golden corpus (ADR 013 — binary blobs, already a clone-weight
  concern in ADR 013 §Consequences) multiplies by every rate; and at 192 kHz the
  mandatory 2x OS (ADR 003 §F-09) gives a 384 kHz internal rate where the
  `0.45*fs_os` cutoff guard (ADR 003 §F-08) leaves the Huovilainen tuning error
  region the table was built to correct largely irrelevant — blessing there spends
  corpus weight on a regime the engine is least characterized for. Rejected as
  the bless set; addressed instead as a defined clamp (B3).

**B3 (chosen): Bless the set {44100, 48000, 88200, 96000} Hz** — the two base
production rates and their 2x relatives, which are exactly the rates the ADR 003
oversampling design and the ADR 013 corpus keying already imply. Define behavior
ABOVE the set rather than blessing it: a host SR strictly above the set is
**supported but unblessed** — it runs the same engine with a per-SR table
generated for that rate, but its oversampled internal rate (and thus the `fc`
guard headroom) is **clamped** so stability holds, and it is rendered/validated
only under the FP-tolerance tier, never bit-exact.

- Pros: matches the oversampling design (ADR 003) and the existing corpus keys
  (ADR 013) with no new rate concepts; bounds corpus growth to four rates ×
  determinism classes; gives a single, documented headroom rule for high host SR
  instead of undefined behavior; keeps the bless surface where the engine is
  characterized.
- Cons: at very high host SR the user gets a defined-but-unblessed render (we do
  not guarantee bit-exactness there); that is a deliberate, disclosed boundary.

## Decision

Adopt **A3** (layered `engineVersion` + `renderVersion`) and **B3** (blessed
sample-rate set `{44100, 48000, 88200, 96000}` Hz with a defined clamp above it),
wired to ADR 013's bless/MANIFEST and ADR 003's per-SR table regen.

1. **Semantic engine version.** `engineVersion` is a human-facing
   `MAJOR.MINOR.PATCH` string, distinct from the marketing `pluginVersion` already
   on the state root (ADR 008 §5). MAJOR = an intentional sonic redesign; MINOR =
   an audio-altering change that bumps `renderVersion`; PATCH = a change proven NOT
   to alter blessed output (refactor, non-DSP fix). It appears in the about box and
   release notes; it is informational, not a migration trigger.

2. **renderVersion is the bless-affecting contract.** A monotonically increasing
   integer `renderVersion` is stored on the state root alongside `schemaVersion` /
   `pluginVersion` (ADR 008 §5, C9), serialized on the message thread. It increments
   **iff** a DSP change alters any blessed CLASS-EXACT or CLASS-FP artifact beyond
   the manifest tolerance band (ADR 013 Layer 2). It is orthogonal to
   `schemaVersion`: `schemaVersion` versions state SHAPE and drives the migration
   chain (ADR 008 C10); `renderVersion` versions rendered AUDIO and drives the
   legacy-render path. The two move independently — a DSP re-tune bumps
   `renderVersion` with `schemaVersion` unchanged; a new parameter bumps
   `schemaVersion` with `renderVersion` unchanged.

3. **Bless ties renderVersion to the MANIFEST.** Because bless is the only path
   that legitimately changes blessed output (ADR 013 Layer 3, arm64-only,
   `BLESS_REASON`-gated), the bless tool is where `renderVersion` is governed:
   a bless that changes any artifact hash beyond tolerance MUST bump
   `renderVersion`, and the new `renderVersion` is recorded in `MANIFEST.toml`
   next to the artifacts and `BLESS_REASON` it covers. CI fails if blessed
   artifacts changed without a `renderVersion` bump, or if `renderVersion` bumped
   with no corresponding MANIFEST/artifact change (the inverse of ADR 013's
   C12/C13 completeness checks, extended to renderVersion). This makes "blessed
   output changed" a mechanical, auditable fact rather than a reviewer judgement.

4. **Communication = state flag + release notes; existing sessions keep their
   renderVersion unless the user opts in.** On `setStateInformation`, if the
   loaded session's `renderVersion < CURRENT_RENDER_VERSION`, the plugin (a) keeps
   rendering at the session's stored `renderVersion` (legacy-render path), and (b)
   raises a non-modal, message-thread UI flag offering an explicit "update engine
   to vN (audio will change)" opt-in. Accepting writes the new `renderVersion` into
   state on next save; declining is sticky. A session never silently changes its
   audio across an engine update. New/blank sessions and new factory presets are
   authored at `CURRENT_RENDER_VERSION`. Release notes enumerate every
   `renderVersion` bump in the released range with its `BLESS_REASON`-derived
   summary and its honesty label (ADR 013 Layer 3) so users know what changed and
   on what evidence.

5. **Legacy-render obligation.** A pinned old `renderVersion` MUST actually
   reproduce that version's audio within its original tolerance tier. Because ADR
   003 §F-14 already makes the `tanh` approximation, decimator coefficients and
   compensation-table contents **versioned frozen constants**, the legacy-render
   path is realized by selecting the frozen constant-set tagged with that
   `renderVersion` (the engine tag of ADR 013 Layer 2 is extended to carry
   `renderVersion`). This stays real-time-safe: constant-set selection happens at
   `prepareToPlay`, never at audio rate (ADR 003 §F-11). We commit to retaining at
   most the constant-sets for shipped `renderVersion`s; we do not promise to
   reproduce never-shipped intermediate blesses.

6. **Blessed sample-rate set = {44100, 48000, 88200, 96000} Hz.** Golden corpora
   (ADR 013) — both CLASS-EXACT (hashed, bit-exact on arm64 AND Linux) and CLASS-FP
   (bit-exact on arm64, tolerance-banded on Linux/Windows) — are generated at each
   of these four rates, each keyed by sample rate per ADR 013 Layer 2. The per-SR
   compensation/tuning table (ADR 003 §F-11) is generated for each at
   `prepareToPlay`. These four are exactly the base production rates (44.1/48 kHz)
   and their 2x relatives implied by ADR 003's mandatory 2x oversampling §F-09.

7. **Behavior above the blessed set is defined and clamped.** A host sample rate
   in the blessed set runs the normal blessed path. A rate strictly above the set
   (e.g. 176.4/192 kHz) is **supported but unblessed**: it runs the same engine
   with a per-SR table generated for that exact rate, but (a) it is validated only
   under the FP-tolerance tier — never asserted bit-exact, never blessed; and (b)
   the oversampled internal rate is clamped so the ADR 003 §F-08 stability guard
   `fc <= 0.45*fs_os` holds with the documented headroom. Specifically, when 2x
   oversampling would push the internal rate past a defined ceiling
   (`OS_CEILING_HZ`, set at the 2x of the top blessed rate = 192 kHz internal), the
   oversampling factor is clamped to 1x (engine still stable, slightly more
   aliasing) rather than running an uncharacterized 384 kHz internal path; this
   clamp is reported in the engine-tag/MANIFEST provenance and surfaced in the UI
   as "running unblessed at this host rate." Rates BELOW 44.1 kHz are out of scope
   and resampled by the host as usual; we do not bless or specially clamp them.

This honors the locks: `renderVersion` carries no measured-fidelity claim
(no-oracle, ADR 013); the bless set lives entirely within the arm64-bit-exact /
Linux-hard-gate / Windows-goal compare policy (ADR 013, design spec §5); all
versioning state and table/constant-set selection are message-thread /
`prepareToPlay` only (ADR 003 §F-11, ADR 008 C19) — no audio-thread alloc or lock.

## Consequences

This commits us to:

- A second version concept (`renderVersion`) and the **legacy-render path**:
  retaining the frozen constant-sets (ADR 003 §F-14) for every shipped
  `renderVersion` and selecting them at `prepareToPlay`, so a pinned old session
  reproduces. This is ongoing weight that grows with each audio-altering release.
- Extending the bless tool and CI (ADR 013 Layer 3) to govern `renderVersion`:
  bump-on-bless-change, the inverse completeness check, and recording
  `renderVersion` in `MANIFEST.toml` and in the ADR 013 engine tag.
- A non-modal, message-thread "engine updated, audio will change — opt in?" UI
  affordance and a sticky decline, plus release notes that enumerate each
  `renderVersion` bump with its honesty label.
- Generating and storing CLASS-EXACT + CLASS-FP corpora at four sample rates
  (44.1/48/88.2/96 kHz), multiplying ADR 013's already-heavy binary golden
  footprint by the rate axis — reinforcing ADR 013's Git-LFS-or-render-on-seed
  need.
- A defined `OS_CEILING_HZ` clamp path and its provenance/UI reporting for
  high-host-SR operation.

It forecloses / makes harder:

- We do NOT bless above 96 kHz host SR; users at 176.4/192 kHz get a defined,
  supported, but explicitly unblessed (and possibly 1x-clamped) render. This is a
  deliberate boundary, not a bug.
- Silently shipping a "better" DSP into existing sessions is foreclosed: an
  audio-altering change can never auto-apply to an old session; it always requires
  an opt-in. This protects users but means an objective bugfix to the DSP still
  will not reach an old session unless the user opts in.
- We do not promise to reproduce never-shipped intermediate blesses — only
  shipped `renderVersion`s — bounding the legacy-constant-set retention.

Owner ratification item: this ADR introduces a NEW user-expectation boundary that
is not covered by the existing locks: **(a) opening an old session after an engine
update does NOT change its sound unless the user explicitly opts in** (some users
may expect bugfixes to apply automatically), and **(b) host sample rates above
96 kHz are supported but unblessed and may run at clamped (1x) oversampling**, so
behavior there is not bit-exact-guaranteed. Both have direct user-expectation
impact and should be explicitly acknowledged by the owner.

## Contract

Normative rules the backlog implements verbatim. "Reference" = the macOS arm64
bless platform (ADR 013). "Blessed set" = {44100, 48000, 88200, 96000} Hz.

| # | Case | Required behavior |
| --- | --- | --- |
| V1 | engineVersion presence | The state root MUST carry an `engineVersion` `MAJOR.MINOR.PATCH` string distinct from `pluginVersion` and `schemaVersion`; it is informational and MUST NOT trigger state migration. |
| V2 | engineVersion semantics | MAJOR = intentional sonic redesign; MINOR = an audio-altering change that bumps `renderVersion`; PATCH = a change PROVEN not to alter any blessed artifact. |
| V3 | renderVersion presence | The state root MUST carry a monotonically increasing integer `renderVersion`, serialized on the message thread alongside `schemaVersion`/`pluginVersion`. |
| V4 | renderVersion orthogonality | `renderVersion` MUST be independent of `schemaVersion`: a pure DSP re-tune bumps `renderVersion` with `schemaVersion` unchanged; a parameter-shape change bumps `schemaVersion` with `renderVersion` unchanged. A DSP-only change MUST NOT add a no-op migration step. |
| V5 | renderVersion bump trigger | `renderVersion` MUST increment iff a bless changes any CLASS-EXACT artifact hash or moves a CLASS-FP artifact outside its manifest tolerance band (ADR 013 Layer 2). |
| V6 | Bless governs renderVersion | The bless tool (arm64-only, `BLESS_REASON`-gated, ADR 013 Layer 3) MUST bump `renderVersion` on any such change and record the new `renderVersion` in `MANIFEST.toml` next to the affected artifacts and `BLESS_REASON`. |
| V7 | CI bump completeness | CI MUST FAIL if blessed artifacts changed without a `renderVersion` bump, OR if `renderVersion` bumped with no corresponding MANIFEST/artifact change. |
| V8 | No silent audio change | On load, if a session's `renderVersion < CURRENT`, the plugin MUST keep rendering at the session's stored `renderVersion` (legacy-render path) and MUST NOT change its audio without explicit user opt-in. |
| V9 | Opt-in flag | On `renderVersion < CURRENT`, the plugin MUST raise a non-modal, message-thread opt-in offering "update engine (audio will change)"; accepting writes `CURRENT` on next save; declining is sticky. New/blank sessions and new factory presets author at `CURRENT`. |
| V10 | Legacy reproducibility | A pinned old `renderVersion` MUST reproduce that version's audio within its original tolerance tier by selecting the frozen constant-set (ADR 003 §F-14) tagged with that `renderVersion`, chosen at `prepareToPlay`, never at audio rate. |
| V11 | Engine-tag carries renderVersion | The ADR 013 Layer 2 engine tag MUST include `renderVersion`; a golden compared across a different `renderVersion` than it was blessed under is refused (extends ADR 013 C22). |
| V12 | Blessed sample-rate set | Golden corpora (CLASS-EXACT and CLASS-FP) MUST be generated at each of {44100, 48000, 88200, 96000} Hz, each keyed by sample rate; the per-SR compensation table (ADR 003 §F-11) is generated for each at `prepareToPlay`. |
| V13 | Blessed-rate render | At a host SR in the blessed set, the engine MUST run the normal blessed path (bit-exact on Reference; tolerance-banded on Linux/Windows per ADR 013). |
| V14 | High-host-SR support | At a host SR strictly above the blessed set, the engine MUST still produce stable, defined audio using a per-SR table for that rate, validated under the FP-tolerance tier ONLY — never asserted bit-exact, never blessed. |
| V15 | Oversampling clamp | When 2x oversampling would push the internal rate above `OS_CEILING_HZ` (192 kHz internal = 2x the top blessed rate), the oversampling factor MUST be clamped to 1x; the ADR 003 §F-08 guard `fc <= 0.45*fs_os` MUST continue to hold. |
| V16 | Clamp provenance + UI | A clamped or unblessed-rate render MUST record the clamp/rate in the engine-tag/MANIFEST provenance and surface "running unblessed at this host rate" in the UI. |
| V17 | Below-set rates | Sample rates below 44.1 kHz are out of scope: resampled by the host, neither blessed nor specially clamped. |
| V18 | RT safety | renderVersion read/write, constant-set selection, table regen and the opt-in flag MUST run on the message thread / at `prepareToPlay` only; no heap allocation and no locks on the audio thread (ADR 003 §F-11, ADR 008 C19). |
