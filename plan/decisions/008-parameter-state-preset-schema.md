<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 008: Parameter / state / preset schema and versioning (the contract)

Status: accepted (Quality structural param registered by ADR-018; load-failure handling by ADR-021)
*Refined post-acceptance — see ADR-018.*
Date: 2026-06-17

## Context

mwAudio101 needs a single, durable definition of its APVTS parameter tree:
parameter IDs, ranges, units, skews, automatable-vs-not classification, the
host state save/restore format, a versioning/migration strategy, and the
factory preset file format for ~64 presets. This is the cross-cutting contract
that the DSP engine, the UI, the preset bank, automation, CI validation, and
documentation all bind to, so getting it stable matters more than getting any
single value perfect.

Forces in tension:

- **Forever-promises.** The moment a user draws an automation lane on the
  filter cutoff or a track recalls a preset, any later change to an ID, a
  choice-enum order, or a range remapping silently corrupts their session. The
  SH-101 lives in DAWs as a bass/acid/sequencer workhorse, so the surfaces most
  likely to be automated and sequenced (filter, resonance, riff presets) are
  exactly the ones that break if the contract is loose
  (docs/research/11-cultural-influence.md §4.3, §4.7, §4.8).
- **The DSP numbers are not settled.** The model is fit from documented circuit
  behavior, not a physical-unit oracle; many calibration values are still open
  (PI) gaps. The contract must let a constant be re-tuned in one place without a
  codebase-wide hunt, and must treat a range remap as a versioned, migrated
  event rather than silent reinterpretation.
- **Cultural identity is often the riff, not just the timbre.** Voodoo Ray is
  literally two SH-101s playing their internal sequences trigger-synced from a
  TR-808 (docs/research/11-cultural-influence.md §4.8), and the ~100-step
  sequencer / up-down-updown arpeggiator are the instrument's signature
  performance idiom and main differentiator (§4.7). A preset format that drops
  the stored pattern would discard the most culturally load-bearing part of the
  instrument.
- **Honesty discipline must be auditable.** Per-artist track claims must be
  marked inspired-by / disputed, never "as used on track X"
  (docs/research/11-cultural-influence.md §7.3); 32'/64' VCO registers and the
  Sine LFO shape are Roland-Cloud software artifacts, NOT 1982 hardware (§6.1,
  §6.2), and must be fenced as software-only so they can never masquerade as
  hardware behavior.

This ADR touches several **owner-locked decisions** and re-affirms them rather
than reversing them:

- **No physical-unit oracle.** Ranges are expressed in normalized modeled units
  with advisory display strings; the host-visible automation value is always the
  normalized 0..1, so display-unit changes never break automation. Re-affirmed.
- **JUCE / C++20.** The contract is realized as a JUCE
  `AudioProcessorValueTreeState` (APVTS) with a serialized `ValueTree` state.
  Re-affirmed.
- **Real-time safe (no heap alloc / no locks on the audio thread).** All
  parsing, migration, file I/O and preset application happen on the message
  thread; the audio thread only reads lock-free atomic parameter pointers.
  Re-affirmed.
- **Circuit-accurate, max-trademark-distance UI.** Stable string IDs decouple
  the automation/host contract from the UI, so the reimagined UI can be
  re-skinned and reordered freely without breaking a single saved automation
  lane. Re-affirmed.
- **Feature scope (poly/unison, oversampling, FX, arp + 100-step seq, MPE-lite,
  ~64 presets).** The schema must carry all of these, classifying structural
  state correctly. Re-affirmed.

## Options considered

The panel did not split on the core of the contract — all three personas
independently converged on the same backbone: a single source of truth for
parameter definitions, stable never-reused namespaced string IDs, normalized
modeled ranges with musically-chosen skews, structural/sequencer state kept off
the host automation list, a `schemaVersion` on the state root, and one linear
migration chain shared by sessions and presets. The only material disagreement
was the **on-disk factory preset format**.

### Persona: DAW-automation

Advocated treating the APVTS tree as a frozen public ABI from v1.0: immutable
`mw101.`-namespaced snake_case string IDs as the single source of truth (with
JUCE deterministically hashing them into VST3/AU/CLAP numeric IDs), all
generated from one X-macro registry (`params.def`) that also drives the APVTS
layout, the preset (de)serializer, the CI validators and the docs. Continuous
params are `NormalisableRange<float>` with musical skews; stepped switches are
`AudioParameterChoice` with fixed, append-only enum ordering; the 100-step
sequencer, voice mode and oversampling factor live in APVTS state properties /
a side-tree, NOT as automation lanes. State is the APVTS ValueTree with a
`schemaVersion` root attribute and an ordered `v1->v2->v3` migration switch that
loads unknown future versions best-effort. Factory presets are the same
versioned ValueTree (`.mw101preset`), CI-validated against the registry, then
BinaryData-embedded; software-only features (32'/64', Sine LFO) are appended at
higher choice indices behind a capability flag and tagged `sw_extension`.

- Pros: zero silent automation breakage (append-only IDs/enums + migration);
  one generated source keeps code, presets, IDs and docs in lockstep;
  string-ID-first gives stable host-portable automation; clean operationalization
  of the owner locks (normalized units, `sw_extension` fencing); keeps the
  automation panel clean by excluding sequencer/voice/OS state; CI-validatable.
- Cons: append-only discipline accrues dead/hidden no-op slots over time;
  per-step sequencer values are not host-automatable as discrete lanes
  (deliberate scope cut); musically-chosen skews with no physical oracle may need
  a feel revision, which is itself a migration event; the migration test matrix
  grows with each version; the X-macro/generated-registry tooling is upfront cost.

### Persona: Maintainability

Advocated a single declarative `ParamDefs` table (a constexpr array of
`{paramID, label, group, type, min/max/step/default, skew, unit, isAutomatable,
versionAdded}`) in `params/` that mechanically generates the APVTS
`ParameterLayout`; no parameter declared anywhere else, so UI, preset loader,
golden tests and docs all read from one table. Stable namespaced string IDs;
ranges/skews trace to research (glide 0-5s strong skew, LFO 0.1-30Hz log,
cutoff log/exp, bend sens clamped to +/-1200 cents per research 05, LFO shape
Triangle/Square/Random/Noise with Sine as a labelled software-only extension);
all invented calibration constants (PI-tagged) live in ONE calibration table the
ParamDefs references. State is the APVTS ValueTree wrapped in a root node with a
single integer `schemaVersion`, with non-APVTS state (seq steps, drift RNG seed,
UI size) hanging off the same tree as typed child nodes. Migration is a linear
chain of pure `migrateV(n)->V(n+1)` ValueTree transforms with frozen
before/after fixture tests. Presets are the IDENTICAL versioned ValueTree
serialized to a stable text format, embedded via BinaryData, run through the
SAME migration chain.

- Pros: one edit to add/retune a parameter keeps everything in sync; stable IDs
  survive UI re-skins (protects the owner-locked reimagined UI); presets and
  sessions share one format and one migration chain; pure-transform migrations
  with fixtures catch breaks in CI; gracefully absorbs the project's many open
  PI calibration gaps; the table can emit the docs parameter reference.
- Cons: a declarative/generated param layer is more upfront machinery than a
  hand-written `createParameterLayout()`; migration is ongoing discipline (a
  step + fixture per breaking change); a misspelled/badly-chosen ID is permanent
  (rename requires a migration alias); embedding full ValueTrees incl. 100-step
  sequences is verbose; routing non-automatable state through the same tree
  needs care to stay out of the host parameter count.

### Persona: preset-design

Agreed on the single flat APVTS, stable string IDs, log/stepped skews, and
structural params (`os.factor`, `poly.voices`, `unison.count`) marked
non-automatable so the host never sweeps a buffer-reallocating param on the
audio thread. Its distinctive position is a **two-layer** state design:
(1) host save/restore serializes the APVTS ValueTree to a compact binary blob
with a `schemaVersion` + `pluginVersion` root and an `<extras>` child for
non-APVTS state (the 100-step pattern with per-step note/gate/tie/rest/accent,
arp latch, preset metadata); (2) factory/user presets on disk are
human-readable, diff-able, categorizable JSON (`.mw101preset`), one file per
preset, with `meta` (name, author, category, tags, description, nullable
`inspiredBy`, `soundExt` bool) and `params`/`seq`/`arp` sections. The category
enum is drawn straight from the research taxonomy
(AcidBassLead / SubBass / Lead / PWMStrings / BlipsFX / SeqArpRiff,
docs/research/11-cultural-influence.md §7.1). A single pure `migrate()` runs off
the audio thread; unknown future params are round-tripped, not dropped.

- Pros: captures full state including the 100-step sequence, preserving the
  defining performance idiom (§4.7/§4.8) a params-only format would lose;
  on-disk presets are git-diffable and hand-authorable, so the ~64-preset bank
  and its naming/attribution discipline (`inspiredBy`, no false track claims,
  `soundExt` flags) are auditable in code review; categorizable by construction;
  explicit `schemaVersion` + ordered `migrate()` with golden fixtures;
  `soundExt` cleanly fences 32'/64' and Sine LFO from the vintage path.
- Cons: two serialization layers are more code and a second mapping that can
  drift if not centralized; human-readable JSON is larger/slower to parse (fine
  off the audio thread); the 100-step sequence sits outside the APVTS so it
  needs its own tested serialize/migrate path; embedding ~64 JSON presets grows
  the binary and couples factory content to a rebuild; JSON needs an explicit
  validator to reject malformed/out-of-range values rather than crashing.

### How the split resolved

The split was purely the on-disk preset encoding: same opaque versioned
ValueTree (DAW-automation, Maintainability) versus a separate human-readable
JSON projection (preset-design). **Resolved in favor of preset-design's
human-readable, category-tagged on-disk format**, because the cultural research
makes curation and honesty auditing first-class requirements: the ~64 presets
must be reviewable in PRs, the inspired-by / disputed / no-false-track-claim
discipline (docs/research/11-cultural-influence.md §7.3) and the `soundExt`
fencing of software artifacts (§6.1, §6.2) have to be visible in code review,
not buried in an opaque binary. A binary-only bank cannot satisfy that gate.

Critiques adopted from each persona:

- From **DAW-automation**: the append-only / never-renumber discipline for both
  IDs and choice-enum indices; deterministic string-ID hashing as the single
  source for VST3/AU/CLAP numeric IDs; the `sw_extension`/`soundExt` flag plus
  appended high choice indices for software-only features; best-effort
  forward-compatible loading (missing param => default, unknown param =>
  preserved/round-tripped); CI validation of every preset against the registry;
  the lock-free atomic-swap / double-buffer handoff and SPSC FIFO for applying a
  loaded preset to the audio thread.
- From **Maintainability**: the single declarative source-of-truth param table
  generating the APVTS layout; one centralized PI calibration table the param
  table references (so a re-tune is one localized edit); pure
  `migrateV(n)->V(n+1)` transforms with frozen before/after golden fixtures;
  `versionAdded` per param; the table emitting the docs parameter reference.
- From **preset-design**: full-state capture with the 100-step pattern as
  first-class state (per-step note/gate/tie/rest/accent + arp latch); the
  human-readable, git-diffable, category-tagged on-disk preset format; the
  category enum drawn from the research taxonomy; explicit non-automatable
  marking of buffer-reallocating structural params; a fixed-capacity 100-step
  array so loading never allocates; an explicit JSON validator/loader.

The resolution does NOT adopt preset-design's *binary* host blob as the only
host encoding constraint, nor does it duplicate the (de)serializer: there is
**one** ValueTree (de)serializer and **one** migration chain. The on-disk JSON
preset is a thin, human-readable projection of that same canonical ValueTree
(round-tripped through the one serializer), and the host's opaque blob is the
canonical ValueTree serialized to JUCE binary. This keeps Maintainability's
"one format, one migration chain, no drift" guarantee while gaining
preset-design's reviewable bank.

## Decision

Adopt a single, generated, append-only parameter contract with one versioned
ValueTree state and one migration chain shared by sessions and the ~64 factory
presets.

1. **Single source of truth.** One declarative parameter registry in `params/`
   (a constexpr `ParamDefs` table; an X-macro is an acceptable realization)
   carrying for every parameter: stable string `id`, label, group, type
   (continuous / choice / bool), `min`/`max`/`step`/`default`, `skew`, advisory
   `unit` display string, `isAutomatable`, `isDiscrete`, browser `category`
   group, and `versionAdded`. This table mechanically generates the APVTS
   `ParameterLayout`. No parameter is declared anywhere else; the APVTS layout,
   the AU/VST3/CLAP numeric IDs, the preset (de)serializer, the CI validators
   and the docs parameter reference all derive from it. All invented calibration
   constants are `(PI)`-tagged and live in ONE calibration table the registry
   references, never inlined.

2. **Stable, namespaced, append-only string IDs.** Each parameter has an
   immutable `mw101.`-namespaced snake_case string ID (e.g. `mw101.vcf.cutoff`,
   `mw101.vco.range`, `mw101.sub.level`, `mw101.lfo.shape`, `mw101.glide.time`).
   IDs are never deleted, reused, or renumbered; a deprecated param becomes a
   hidden no-op that keeps its slot, and a rename is done via a migration alias.
   The string ID is the single source of truth; JUCE deterministically hashes it
   into the VST3 paramID / AU address / CLAP id, so there is no hand-maintained
   numeric table. Display names and UI order may change freely (honors the
   owner-locked reimagined UI).

3. **Normalized modeled ranges, musical skews (no physical oracle).** Continuous
   params are `NormalisableRange<float>` in normalized modeled units; the
   host-visible automation value is always 0..1 and units are advisory display
   strings only. Skews are chosen for musical feel grounded in the cultural
   idioms — cutoff log-ish skew so the ~11-o'clock / ~2-o'clock acid sweet spots
   have resolution (docs/research/11-cultural-influence.md §4.3); glide/portamento
   time log skew over the 0-5 s range (docs/research/05-mixer-modulation-glide.md
   §1); LFO rate log over 0.1-30 Hz (docs/research/05-mixer-modulation-glide.md
   §1); resonance linear 0..1 so automation ramps to self-oscillation near the
   top are predictable; envelope A/D/R log-skewed times with linear sustain; VCO
   and VCF bend sensitivity clamped to +/-1200 cents
   (docs/research/05-mixer-modulation-glide.md §1). Re-skewing a range later is a
   compatibility event handled through migration, not a silent reinterpretation.

4. **Explicit automatable vs structural classification.** Continuous sonic
   params are automatable and smoothed. Stepped switches (VCO range 16'/8'/4'/2',
   sub mode -1sq / -2sq / -2pulse, LFO shape Tri/Sq/Random/Noise, arp mode
   up/down/up-down) are `AudioParameterChoice` with fixed, append-only enum
   ordering. Structural state that reallocates buffers or changes DSP topology
   (`mw101.os.factor`, `mw101.voice.mode`, `mw101.voice.count`,
   `mw101.unison.count`) is marked `withAutomatable(false)` and applied via
   prepareToPlay-style reconfiguration against pre-allocated max-size buffers.
   The 100-step sequencer pattern, arp latch, per-instance drift RNG seed and
   preset metadata are NOT host parameters at all — they live in a side
   `<extras>` subtree of the same ValueTree, so they never pollute any host's
   automation list or parameter count. Tempo-sync arp/LFO rates are choice
   subdivision params (1/4, 1/8, 1/8T...) with a free/sync toggle.

5. **One versioned state, one migration chain.** State is the canonical APVTS
   ValueTree wrapped in a root node carrying an integer `schemaVersion` and a
   `pluginVersion` string, with the `<extras>` subtree (sequencer, arp latch,
   drift seed, UI size) hanging off the same tree so save/restore is one atomic
   tree. `getStateInformation` serializes this canonical tree to the host's
   opaque blob as JUCE binary. `setStateInformation` runs, on the message
   thread, a linear chain of pure `migrateV(n)->V(n+1)` ValueTree transforms when
   `schemaVersion < CURRENT`, then binds to the APVTS. Forward/backward
   tolerance is the rule: a missing param defaults; an unknown future param is
   preserved/round-tripped, not dropped; an unknown future version loads
   best-effort and never crashes. Every migration step has a frozen
   before/after golden fixture test in CI.

6. **Factory presets as human-readable data.** The ~64 factory presets are
   authored as human-readable, git-diffable JSON files (`.mw101preset`, one per
   preset), each a thin projection of the same canonical ValueTree round-tripped
   through the one (de)serializer. Each carries `schemaVersion`, a `meta` block
   (name, author, `category` from the research taxonomy, tags, description,
   nullable `inspired_by`, `sound_ext` bool) and `params` / `seq` / `arp`
   sections; SeqArpRiff presets carry their stored pattern. Presets are organized
   on disk by category folder, validated in CI against the registry (every ID
   present, in range, choice indices valid; CI mirrors `presets/` 1:1), then
   embedded via CMake/BinaryData and loaded into an in-memory PresetManager at
   construction. Presets run through the SAME migration chain as session state,
   so a v1 factory preset opens correctly at v5 with zero per-preset edits.

7. **Software-extension discipline in the schema.** The hardware-faithful choice
   lists ship the canonical indices first: LFO shape Tri/Sq/Random/Noise and VCO
   range 16'/8'/4'/2' (docs/research/11-cultural-influence.md §6.1, §6.2). Any
   software-style extensions — the Sine LFO shape and the 32'/64' VCO registers,
   which are Roland-Cloud/SH-01A artifacts and NOT 1982 hardware — are *appended*
   at higher choice indices behind a capability flag, so adding them never shifts
   existing automation indices, and any preset using them sets `sound_ext: true`.
   Per-artist references render as inspired-by / disputed, never "as used on
   track X" (docs/research/11-cultural-influence.md §7.3); no "TB-303 filter"
   descriptor ships (§4.2, §6.1).

This honors the owner locks: normalized modeled ranges (no physical-unit
oracle), all parse/migrate/IO on the message thread with the audio thread only
reading lock-free atomics (no heap alloc / no locks on the audio thread), stable
IDs decoupled from the reimagined UI, and the full modern-essentials scope
carried with structural state correctly fenced off the automation list.

## Consequences

This commits us to:

- A small upfront investment in the declarative registry / generator and CI
  validators before it pays off, in exchange for one-edit parameter changes that
  keep APVTS, host IDs, preset IO, golden tests and docs in lockstep.
- Permanent append-only discipline: IDs and choice-enum indices are never
  renumbered or reused; renames go through migration aliases; deprecated params
  linger as hidden no-op slots (mild long-term cruft).
- Ongoing migration discipline: every breaking schema change (new param,
  re-skew, rename, structural change) requires a `schemaVersion` bump plus an
  ordered pure migration step and its frozen before/after fixture; the
  old->new test matrix grows with each release.
- Maintaining the on-disk JSON preset projection AND the canonical ValueTree
  through one (de)serializer; a validator/loader must reject malformed or
  out-of-range presets rather than crash.
- All preset/state work (parse, migrate, BinaryData decode, file IO) staying on
  the message thread, with application to the audio thread via APVTS atomic
  stores plus a single pre-allocated lock-free SPSC double-buffer handoff for the
  `<extras>` (100-step pattern, arp state) — never an audio-thread allocation or
  lock.

This forecloses / makes harder:

- Per-step sequencer values are NOT exposed as discrete host-automation lanes
  (they are state, not automation). DAW users wanting to automate individual
  steps are constrained; this is a deliberate scope cut to keep automation lists
  sane. Mitigable later via a few macro/CC-mappable params.
- Structural params (oversampling factor, voice/unison count, voice mode) are
  not host-automatable, so they cannot be swept within a track; they change only
  via reconfiguration.
- Choosing skews/defaults up front without a physical oracle risks a later feel
  revision, and any such re-skew is itself a migration event, not a free edit.
- Factory-content edits require a rebuild (BinaryData embedding) and grow the
  binary; full-ValueTree presets incl. 100-step sequences are more verbose than a
  minimal diff format.

Owner ratification item: the deliberate scope cut that **individual sequencer
steps are NOT host-automatable as discrete lanes** (they are saved state, not
automation) carries user-expectation risk for DAW users who may expect per-step
automation, and is not explicitly covered by the locked decisions; confirm this
v1 boundary.

## Contract

Normative rules the backlog implements verbatim. "MUST" / "MUST NOT" are binding.

| # | Case | Required behavior |
|---|---|---|
| C1 | Parameter ID format | Every param ID MUST be an immutable `mw101.`-namespaced snake_case string declared only in the `ParamDefs` registry. |
| C2 | ID lifecycle | An ID MUST NOT be deleted, reused, or renumbered. Deprecated params become hidden no-op slots; renames are done via a migration alias, never by changing the ID in place. |
| C3 | Numeric host IDs | VST3/AU/CLAP numeric IDs MUST be derived deterministically by hashing the string ID; no hand-maintained numeric table exists. |
| C4 | Range units | Continuous params MUST be `NormalisableRange<float>` in normalized modeled units; the host-visible automation value MUST always be 0..1; `unit` strings are advisory display only and MUST NOT affect the stored/automation value. |
| C5 | Choice-enum ordering | `AudioParameterChoice` enum indices MUST be fixed and append-only; new options MUST be appended at higher indices; existing indices MUST NOT shift. |
| C6 | Hardware choice canon | LFO shape canonical indices = Tri, Sq, Random, Noise; VCO range canonical indices = 16', 8', 4', 2'. Sine LFO and 32'/64' registers MUST be appended ABOVE these behind a capability flag and MUST NOT occupy a canonical index. |
| C7 | Automatable classification | Continuous sonic params MUST be automatable and smoothed (per-block LinearSmoothedValue; buffers sized at prepareToPlay). Structural params (`mw101.os.factor`, `mw101.voice.mode`, `mw101.voice.count`, `mw101.unison.count`) MUST be `withAutomatable(false)`. |
| C8 | Non-parameter state | The 100-step sequencer pattern (per-step note/gate/tie/rest/accent), arp latch, drift RNG seed and preset metadata MUST live in the `<extras>` subtree and MUST NOT appear as `AudioProcessorParameter`s (no automation lane, not in the host parameter count). |
| C9 | State root | The serialized root MUST carry an integer `schemaVersion` and a `pluginVersion` string; `getStateInformation` serializes the canonical ValueTree (params + `<extras>`) atomically. |
| C10 | Migration on load | `setStateInformation` and preset load MUST run, on the message thread only, ordered pure `migrateV(n)->V(n+1)` transforms when `schemaVersion < CURRENT`, then bind to APVTS. |
| C11 | Forward/backward tolerance | Missing param => default; unknown future param => preserved and round-tripped (not dropped); unknown future `schemaVersion` => load known IDs best-effort and MUST NOT crash. |
| C12 | Migration tests | Every migration step MUST have a committed frozen before/after golden fixture test in CI. |
| C13 | Preset format | Factory presets MUST be human-readable JSON (`.mw101preset`, one file per preset) projecting the canonical ValueTree via the single (de)serializer, carrying `schemaVersion` and `meta {name, author, category, tags, description, inspired_by(nullable), sound_ext}` plus `params`/`seq`/`arp`. |
| C14 | Preset categories | `category` MUST be one of: AcidBassLead, SubBass, Lead, PWMStrings, BlipsFX, SeqArpRiff. |
| C15 | Software-extension tagging | Any preset using a software-only feature (Sine LFO, 32'/64' register) MUST set `sound_ext: true`. |
| C16 | Attribution discipline | Preset metadata MUST render artist references as inspired-by / disputed, never "as used on track X"; no preset/text may ship a "TB-303 filter" descriptor. |
| C17 | Preset = session path | Factory and user presets MUST pass through the SAME migration chain as session state; a v1 preset MUST open correctly at the current version with no per-preset hand-editing. |
| C18 | CI preset validation | CI MUST validate every preset against the registry (every ID present, value in range, choice index valid) and MUST mirror `presets/` 1:1 before BinaryData embedding. |
| C19 | Real-time safety | Parsing, migration, BinaryData decode and file IO MUST NOT run on the audio thread. The audio thread reads only lock-free APVTS atomics; `<extras>` is handed to the audio thread via a pre-allocated lock-free SPSC double-buffer swap. No heap allocation and no locks on the audio thread. |
| C20 | Fixed-capacity sequence | The sequencer pattern MUST be a fixed-capacity (100-step) pre-reserved structure so loading a shorter or longer stored pattern never allocates on the audio thread. |
