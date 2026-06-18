<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 011: Plugin formats and wrapper strategy

Status: accepted (LV2 export path + AAX exclusion owned by ADR-024)
*Refined post-acceptance — see ADR-024.*
Date: 2026-06-17

## Context

mwAudio101 must ship multiple plugin formats from one JUCE / C++20 / CMake codebase:
VST3, AU (macOS), CLAP, and Standalone, with LV2 (Linux) treated as goal-tier. The
question is HOW to produce every binary without forking the DSP, how to scope formats
per platform, and how to enforce the invariant that no format is ever built on a
platform where its validator is not wired into CI.

Forces:

- The DSP must remain a single, format- and host-agnostic `AudioProcessor` so the
  macOS arm64 bit-exact bless reference is shared verbatim by every wrapper. Any DSP
  fork would create per-format divergence the bless cannot catch on the other formats.
- CLAP is not a native JUCE exporter; it requires an external wrapper.
- AU is macOS-only and `auval` is macOS-only, so the validator matrix is intrinsically
  platform-ragged — it must be a sparse `(format, platform)` map, not a uniform list.
- A format that compiles but is never validated is a latent shipping bug. Each wrapper
  is a distinct host-contract surface (parameter automation, state save/load, MPE-lite
  note expression, host-synced arp / 100-step seq, bus layout).

This touches the owner-locked decisions and re-affirms them rather than reversing
them. The locks held above this ADR:

- Formats are VST3, AU(macOS), CLAP, Standalone; **LV2 = goal-tier**.
- Platform tiers: **macOS arm64 = reference/bless and bit-exact**; **Linux x64 =
  co-required hard gate**; **Windows x64 = goal**.
- JUCE / C++20 / CMake; GPL-3.0-or-later.
- Real-time safe: no heap allocation and no locks on the audio thread.

This ADR is bound by, and re-affirms, all of the above. It does not change which
formats or platforms are in scope; it specifies the mechanism and the per-platform
scoping that satisfies the locks.

## Options considered

Two personas were on the panel. Both converged on the same fundamental architecture
(one shared host-agnostic engine; native JUCE VST3/AU/Standalone; CLAP via
`clap-juce-extensions`; per-(format, platform) validator gate; LV2 deferred to
goal-tier behind `lv2lint`). They differed in tooling emphasis and in how explicitly
each addressed exclusions and per-format test coverage. The panel did not split on the
decision; it split only on detail, and those details were merged.

### Persona: Cross-platform (one source -> all formats; per-platform validators)

Approach: a single `juce_add_plugin(FORMATS ...)` target driven by a per-platform
`MWAUDIO_FORMATS` cache var, with a CMake-level assertion that maps each requested
format to a required validator target and hard-removes (configure-time errors on) any
format whose validator is not wired for that platform. CLAP via
`clap-juce-extensions` (Surge synth team) wrapping the same `AudioProcessor`. LV2
staged behind a feature flag with two independent routes: JUCE7+ native LV2 export OR
`clap-wrapper` re-emitting the CLAP build as LV2 — whichever passes `lv2lint` first.

- Pros: makes "no unvalidated artifact" a hard configure-time invariant, not a
  code-review hope; zero DSP fork so the bless reference is shared verbatim; treats
  CLAP as a hedge for LV2 (`clap-wrapper` gives the goal-tier path a second route);
  honors the owner-lock asymmetry (Linux stays a hard gate on VST3+CLAP+Standalone,
  all with free validators; LV2 stays non-blocking).
- Cons: external deps (`clap-juce-extensions`, `clap-wrapper`) track JUCE/CLAP SDK
  versions and must be pinned + CI-guarded against a JUCE bump; JUCE native LV2 export
  quality is uneven; getting `lv2lint` green for the full feature set (MPE-lite,
  100-step seq state, full automation) is real goal-tier work; a five-validator CI
  lengthens the pipeline; `auval` and `clap-validator` can disagree on edge cases,
  forcing a normalization layer in the shared state model.

Critiques adopted: the configure-time CMake hard-assertion as the enforcement
mechanism (the gate must be in the build, not just CI); the sparse `(format, platform)`
validator map rather than a uniform list; CLAP-as-LV2-hedge via `clap-wrapper` as the
second goal-tier route; pinning the wrapper deps and CI-guarding against JUCE bumps;
the single normalized lock-free pre-allocated event buffer drained from each wrapper's
event model with a compiled-in no-allocation assertion on `processBlock`.

### Persona: Product (market coverage + OSS/Linux niche)

Approach: core DSP and parameter model in a host-agnostic engine library; the JUCE
`AudioProcessor` is a thin shell. VST3 + AU + Standalone native from JUCE; CLAP via
`clap-juce-extensions` as a sibling target on the same processor. Parameters declared
once via `AudioProcessorValueTreeState` so automation, arp/seq, and MPE-lite map
identically across formats. Per-platform matrix tied to the owner tiers, with explicit
exclusions: no AU on Linux (building it there would violate the validator rule), and
**no AAX anywhere** (PACE/licensing is incompatible with shipping GPL-3.0 binaries and
has no open validator). Validators: `pluginval` (VST3/AU/Standalone, also exercises
CLAP), `clap-validator`, `auval` (macOS AU), `lv2lint`/`lv2_validate` (LV2). Frames the
validator pass set as a marketable trust signal.

- Pros: maximizes the documented differentiators (OSS + native Linux + CLAP/LV2
  breadth the entire $49-$199 commercial field ignores); AU captures Logic/GarageBand
  for free; single shared engine keeps behavior bit-identical across formats (critical
  for the bless requirement); LV2 promotion is a mechanical gate flip, not a rewrite.
- Cons: 4 formats x 3 platforms is real CI surface even when most cells are "free";
  AAX exclusion permanently cedes the Pro Tools base; at launch the "Linux + LV2"
  headline is partially deferred until `lv2lint` is green, so the CLAP/VST3 Linux story
  must carry the niche claim initially; MPE-lite and host-synced arp/seq must be
  validated per-format (host transport and note-expression support vary), so "full
  automation everywhere" needs explicit cross-format coverage, not a single pass.

Critiques adopted: add `pluginval` as the broad VST3/AU/Standalone validator alongside
the format-specific tools (it strengthens the gate and gives the marketable "passes
auval / pluginval / clap-validator / lv2lint" signal); the explicit **AAX exclusion**
on GPL-3.0 + PACE grounds, recorded so it is not silently revisited; the explicit "no
AU on Linux" scoping; the thin-shell engine library boundary so parameters,
automation, arp/seq, MPE-lite, and the CV/Gate semantics from research doc 08 live in
the shared engine, not per-wrapper; the requirement for explicit per-format cross-test
coverage of MPE-lite and host-synced transport, not a single validator pass.

### Resolution

The two positions are the same architecture viewed through engineering-invariant and
product-market lenses. The decision merges them: the Cross-platform persona's
configure-time CMake hard-assertion and CLAP-as-LV2-hedge, plus the Product persona's
`pluginval` addition, explicit AAX exclusion, thin-shell engine boundary, and
per-format transport/MPE test coverage. No alternative single-format or
fork-per-format approach was credible, given the bit-exact bless lock and the
one-maintainer GPL economics.

## Decision

Build **one JUCE / C++20 / CMake target** over a **host-agnostic engine library** (the
`AudioProcessor` is a thin shell), producing every binary from a single shared DSP and
a single `AudioProcessorValueTreeState` parameter model. CLAP is produced via
`clap-juce-extensions` (Surge synth team) wrapping that same `AudioProcessor` — no DSP
fork. VST3, AU, and Standalone are native JUCE wrappers.

Format scope is a **sparse per-platform map**, tied to the owner-locked platform tiers
(`docs/research/12-market-legal-landscape.md` §7.3 names Linux/format breadth — "CLAP
+ LV2 + VST3 with native Linux" — as the documented "second gap" the entire commercial
field ignores, after the open-source niche itself in §2):

| Platform | Tier | Formats shipped | Goal-tier (gated) |
| --- | --- | --- | --- |
| macOS arm64 | reference / bless / bit-exact | VST3, AU, CLAP, Standalone | — |
| Linux x64 | co-required HARD GATE | VST3, CLAP, Standalone | LV2 |
| Windows x64 | goal | VST3, CLAP, Standalone | — |

The **"never build a format where no validator is wired" rule** is enforced at CMake
configure time: each requested `(format, platform)` pair maps to a required validator
target; if that validator is not found/wired for the platform, the format is
hard-removed and force-adding it is a configure-time error. The build literally cannot
emit an unvalidated artifact. The validator map is:

| Format | Validator(s) | Platforms wired |
| --- | --- | --- |
| VST3 | `pluginval` + Steinberg `validator` | macOS, Linux, Windows |
| AU | `auval` + `pluginval` | macOS only |
| CLAP | `clap-validator` (+ `pluginval` where it exercises CLAP) | macOS, Linux, Windows |
| Standalone | headless smoke-launch (+ `pluginval` engine checks) | macOS, Linux, Windows |
| LV2 | `lv2lint` + `lv2_validate` (lilv) | Linux (goal-tier, ships only when green) |

**AAX is excluded on every platform.** It requires Avid PACE signing/licensing, which
is incompatible with shipping GPL-3.0-or-later binaries, and has no open validator — so
it cannot satisfy either the license lock or the validator rule. This is a permanent,
deliberate exclusion, not a deferral.

**LV2 stays goal-tier and non-blocking.** It ships from Linux x64 only when `lv2lint`
and `lv2_validate` are wired and green. There are two independent routes — JUCE7+
native LV2 export, or `clap-wrapper` re-emitting the CLAP build as LV2 — and we adopt
whichever passes `lv2lint` first. Because Linux x64 is a HARD GATE on VST3 + CLAP +
Standalone (all with free, cross-platform validators), LV2 being deferred never blocks
the Linux gate; the CLAP/VST3 Linux story carries the niche claim until LV2 is green.

All host-facing semantics — full automation, host-synced arp + 100-step sequencer,
MPE-lite, and the CV/Gate behavior from `docs/research/08-power-cv-io.md` §3 and §10
(1 V/oct pitch, V-trig gate, EXT CLK rising-edge step advance above ~+2.5 V, 100-step
sequencer, **no song-position awareness**) — are modeled once in the shared engine, so
behavior is bit-identical across formats and inherits the macOS bless reference.

The wrapper choice does not touch the audio thread: VST3 / AU / CLAP / LV2 / Standalone
are thin host-contract adapters around the same render callback, so the owner-locked
"no heap alloc / no locks on the audio thread" rule (re-affirmed here) is enforced once
in `processBlock` and inherited by every format. Each wrapper's parameter/event model
(CLAP's sample-accurate event queue, VST3 param queues, LV2 atom ports, AU's per-block
model, MPE-lite note expression) is drained into one normalized, fixed-capacity,
lock-free internal event buffer sized at `prepareToPlay`, with a no-allocation
assertion compiled into debug/CI builds, so no wrapper can introduce an allocation or
lock on the RT path. Per-voice (poly/unison + per-voice drift) and oversampling scratch
are pre-allocated for max-block and max-voice at `prepareToPlay`, never on note-on.

## Consequences

This commits us to:

- One shared engine library + thin `AudioProcessor` shell; the macOS arm64 bit-exact
  bless reference is the single source of truth all wrappers inherit verbatim.
- Pinning and CI-guarding `clap-juce-extensions` (and `clap-wrapper` if the LV2 route
  uses it) against JUCE/CLAP SDK bumps; a JUCE major bump may require a wrapper update
  before CLAP/LV2 rebuilds.
- A multi-validator CI matrix on macOS + Linux (and eventually Windows): `pluginval`,
  Steinberg `validator`, `auval`, `clap-validator`, headless Standalone smoke, and
  `lv2lint`/`lv2_validate` for the goal-tier LV2. The pass set becomes a publishable
  trust signal no commercial competitor advertises.
- Explicit per-format cross-test coverage of MPE-lite and host-synced transport
  (arp/100-step seq playhead read, with a free-run fallback when a host/format reports
  no transport), because host support varies per format — a single validator pass is
  not sufficient evidence.
- A normalization layer in the shared state/parameter model so `auval`,
  `clap-validator`, and `pluginval` all pass one source (param flags, state round-trip)
  without per-format divergence.

This forecloses / makes harder:

- **No AAX, permanently** — cedes the Pro Tools user base (small for a mono SH-101,
  real nonetheless); unavoidable under GPL-3.0 + PACE and the validator rule.
- **No AU outside macOS** — AU stays macOS-only by construction (auval is macOS-only).
- LV2 at launch is partially deferred; the headline "native Linux + LV2" claim is
  carried initially by CLAP + VST3 + Standalone until `lv2lint` is green.
- The configure-time gate means adding a new format is never a one-line `FORMATS`
  edit; it requires wiring the validator first, by design.

Owner ratification item: confirm that **AAX is permanently out of scope** (the
owner-locks list AAX nowhere, so this is consistent, but the exclusion is recorded here
so it is never silently reconsidered), and confirm that **LV2 may ship Linux-only at
launch behind the `lv2lint` gate** (i.e. the "Linux + LV2" headline can be deferred to
a post-launch gate flip without violating the Linux-x64 hard-gate, which is satisfied
by VST3 + CLAP + Standalone). Neither changes a lock, but both carry user-expectation
framing the owner should sign off explicitly.

## Contract

The backlog implements the following normative cases verbatim. "Configure" = CMake
configure step; "ship" = produce a distributable artifact.

| # | Case | Required behavior |
| --- | --- | --- |
| C1 | Configure VST3/CLAP/Standalone on macOS, Linux, or Windows | Format builds; its validator(s) per the map are required CI gates. |
| C2 | Configure AU on macOS | AU builds; `auval` + `pluginval` are required gates. |
| C3 | Configure AU on Linux or Windows | Configure-time error: AU is hard-removed (no `auval` off macOS). |
| C4 | Configure any AAX target on any platform | Configure-time error: AAX is unconditionally excluded (GPL-3.0 + PACE, no open validator). |
| C5 | Configure a format whose validator is not wired for that platform | Configure-time error; the format is hard-removed and force-adding it fails the configure. |
| C6 | Ship LV2 (Linux x64) | Permitted only when `lv2lint` AND `lv2_validate` are wired and green; otherwise LV2 is not built/shipped. LV2 absence never blocks the Linux x64 gate. |
| C7 | Linux x64 release gate | Must ship VST3 + CLAP + Standalone, each with its validator green. LV2 optional. |
| C8 | macOS arm64 release | Must ship VST3 + AU + CLAP + Standalone, each with its validator green; this build is the bit-exact bless reference. |
| C9 | Any wrapper draining host parameter/automation/note events | Events drain into one pre-allocated, fixed-capacity, lock-free internal buffer sized at `prepareToPlay`; no heap allocation and no lock on the audio thread (asserted in debug/CI). |
| C10 | Host reports no transport (arp / 100-step seq) | Engine falls back to free-run; reading the playhead is allocation-free; behavior matches `docs/research/08-power-cv-io.md` §10 item 2 (EXT-CLK-style rising-edge advance, no song-position). |
| C11 | Same patch/state across formats on macOS arm64 | DSP output is bit-identical across VST3/AU/CLAP/Standalone (single shared engine; the bless reference applies to all). |
