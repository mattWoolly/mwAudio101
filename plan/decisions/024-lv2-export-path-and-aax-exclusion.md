<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 024: LV2 export path & AAX exclusion

Status: accepted
Date: 2026-06-18

## Context

ADR-011 and ADR-014 disagree on how the goal-tier LV2 format is produced, and the
contradiction must be resolved before the build backlog can wire the LV2 path.

- **ADR-011** (Decision, lines 166-168; Resolution, lines 60-61) specifies **two
  independent LV2 routes** — "JUCE7+ native LV2 export, or `clap-wrapper` re-emitting
  the CLAP build as LV2 — and we adopt whichever passes `lv2lint` first." Its
  Consequences (lines 194-195) explicitly anticipate pinning `clap-wrapper` "if the
  LV2 route uses it."
- **ADR-014** (Decision, "Dependency vendoring", lines 181-186; Contract C2, line 285)
  pins exactly **three** vendored dependencies in `cmake/Dependencies.cmake` — JUCE,
  Catch2, and clap-juce-extensions (with CLAP transitively pinned) — and forbids adding
  any dependency that is not in that single manifest. `clap-wrapper` is a **distinct**
  project from `clap-juce-extensions` and appears nowhere in ADR-014's manifest.

So the `clap-wrapper` LV2 route ADR-011 keeps open would require a new vendored
dependency that ADR-014's pin manifest never admits. The two ADRs cannot both be
correct as written: either ADR-014's manifest must grow a fourth dependency, or
ADR-011's second LV2 route must be dropped. This ADR picks the latter.

Forces:

- The fewer-dependencies, fewer-validators, one-maintainer GPL economics
  (ADR-011 Resolution, lines 121-123) argue against carrying a second LV2 toolchain
  whose only job is a hedge for a goal-tier format.
- `clap-wrapper` tracks both the CLAP SDK and the wrapped-host ABIs and would add a
  second JUCE/CLAP-bump exposure surface (ADR-011 Cons, line 68; ADR-014 Consequences,
  lines 245-247) on top of `clap-juce-extensions`, for a Linux-only, non-blocking
  target.
- The LV2 ship gate is `lv2lint` + `lv2_validate` regardless of which exporter
  produced the bundle (ADR-011 Contract C6, line 238), so the choice of route does not
  change the validator surface — only the dependency and maintenance cost.

Owner-locks this touches and re-affirms (it changes none of them): formats are
**VST3/AU/CLAP/Standalone with LV2 as a goal-tier, Linux-only-at-launch target gated by
`lv2lint`**; **AAX is not in scope**; **GPL-3.0-or-later**; **JUCE / C++20 / CMake**;
**macOS arm64 bit-exact bless + Linux x64 hard gate + Windows goal**; **no heap alloc /
no locks on the audio thread**. This ADR depends on and reconciles **ADR-011**
(plugin formats and wrapper strategy) and **ADR-014** (build system, dependency
management and toolchain), and is bound by both.

## Options considered

### Option A — Keep both LV2 routes (native export OR `clap-wrapper`), add `clap-wrapper` to the manifest

Honor ADR-011 literally: keep JUCE native LV2 export and the `clap-wrapper` route both
live, "adopt whichever passes `lv2lint` first," and amend ADR-014's manifest to pin
`clap-wrapper` (full-commit SHA + hash + SPDX) as a fourth dependency.

- Pros: preserves ADR-011's hedge — if JUCE's native LV2 exporter cannot get `lv2lint`
  green for the full feature set (MPE-lite, 100-step seq state, full automation), the
  CLAP-derived bundle is a fallback; no edit to ADR-011's stated routes.
- Cons: directly violates ADR-014's pin-manifest invariant (Decision lines 181-186;
  Contract C2) by introducing a new vendored dependency the toolchain ADR never
  admitted; adds a second JUCE/CLAP-SDK bump-exposure surface (a JUCE major bump can now
  break two wrappers, not one) for a Linux-only, non-blocking, goal-tier format;
  two LV2 build paths is more CI surface and more maintenance for one maintainer than the
  goal-tier payoff justifies; the hedge buys nothing the validator gate doesn't already
  guarantee — `lv2lint` + `lv2_validate` decide ship/no-ship identically regardless of
  the producing exporter.

### Option B — JUCE native LV2 exporter ONLY; drop `clap-wrapper`; manifest stays JUCE + Catch2 + clap-juce-extensions (chosen)

Resolve the contradiction in ADR-014's favor on the dependency question and in
ADR-011's favor on everything else: LV2 is produced **solely** by JUCE's native LV2
exporter built from the same shared `AudioProcessor`. The `clap-wrapper` second route
in ADR-011 is **withdrawn**. ADR-014's pin manifest is **unchanged** — JUCE + Catch2 +
clap-juce-extensions (CLAP transitive). LV2 stays goal-tier, Linux-only at launch, and
ships only when `lv2lint` AND `lv2_validate` are wired and green.

- Pros: zero new dependency — preserves ADR-014's single-manifest invariant and its
  GPLv3 provenance trail verbatim; one wrapper-bump exposure surface, not two; LV2 is
  produced from the same shared engine as every other format, so it inherits the macOS
  arm64 bless reference with no DSP fork (ADR-011 Decision, lines 173-185); the validator
  gate is unchanged, so the only thing dropped is redundant cost; matches the
  one-maintainer GPL economics ADR-011 itself invoked to reject fork-per-format
  (lines 121-123).
- Cons: removes ADR-011's fallback — if JUCE's native LV2 exporter cannot reach
  `lv2lint`/`lv2_validate` green for the full feature set, there is no second route, and
  LV2 simply stays unshipped (it is goal-tier and non-blocking, so this never blocks the
  Linux x64 hard gate, which is satisfied by VST3 + CLAP + Standalone — ADR-011 Contract
  C6/C7); JUCE native LV2 export quality is uneven (ADR-011 Cons, line 70), and that
  risk is now un-hedged. Accepted: a goal-tier, Linux-only feature does not warrant a
  permanent second toolchain, and the gate guarantees we never ship a broken LV2 bundle.

This ADR reconciles the contradiction by choosing Option B. ADR-011's two-route
position loses on the dependency axis (it requires a manifest ADR-014 forbids) and wins
nothing on the gate axis (the validator decision is route-independent). ADR-014's
three-dependency manifest is the invariant that holds; ADR-011's everything-else — LV2
goal-tier, Linux-only-at-launch, `lv2lint`-gated, single shared engine, never blocking
the Linux gate, permanent AAX exclusion — is re-affirmed unchanged.

## Decision

**LV2 is produced by JUCE's native LV2 exporter ONLY.** The `clap-wrapper`
"re-emit the CLAP build as LV2" route in `plan/decisions/011-plugin-formats-wrappers.md`
(Decision, lines 166-168; Resolution, lines 60-61; Consequences, lines 194-195) is
**withdrawn**. There is exactly one LV2 build path: `juce_add_plugin` emits the LV2
target from the same shared host-agnostic `AudioProcessor` that produces VST3 / AU /
CLAP / Standalone, behind the existing `MW_BUILD_LV2` option
(`plan/decisions/014-build-toolchain-deps.md`, Decision line 152).

**No new dependency is introduced.** The `cmake/Dependencies.cmake` pin manifest
stays exactly as ADR-014 fixed it — **JUCE + Catch2 + clap-juce-extensions** (with CLAP
transitively SHA-pinned) — per `plan/decisions/014-build-toolchain-deps.md` Decision
(lines 181-186) and Contract C2 (line 285). `clap-wrapper` is **not** added to the
manifest, not vendored, and not built. This keeps ADR-014's single-file pin manifest /
GPLv3 license-provenance invariant intact.

**LV2 remains goal-tier, Linux-only at launch, and gated by `lv2lint`.** It ships from
the Linux x64 build only when `lv2lint` AND `lv2_validate` (lilv) are wired and green,
exactly as `plan/decisions/011-plugin-formats-wrappers.md` Contract C6 (line 238) and
the format/validator map (lines 150-156) require. LV2's absence never blocks the Linux
x64 hard gate, which is satisfied by VST3 + CLAP + Standalone (ADR-011 Contract C6/C7,
lines 238-239). Because LV2 is built from the same shared engine, it inherits the macOS
arm64 bit-exact bless reference verbatim and introduces no DSP fork (ADR-011 Decision,
lines 173-185); the wrapper is a thin host-contract adapter (LV2 atom ports drained into
the one pre-allocated, fixed-capacity, lock-free internal event buffer) and never
touches the RT no-alloc / no-lock invariant (ADR-011 Decision, lines 176-185; Contract
C9).

**AAX is permanently out of scope on every platform.** This re-affirms ADR-011's
exclusion (Decision, lines 159-161; Consequences, line 210; Contract C4, line 236):
AAX requires Avid PACE signing/licensing, which is incompatible with shipping
GPL-3.0-or-later binaries, and has no open validator, so it can satisfy neither the
license lock nor the "never build a format where no validator is wired" rule (ADR-011;
ADR-014 Contract C6). AAX is a deliberate, permanent exclusion, not a deferral — there
is no future condition under which it is reconsidered while the project is GPL-3.0.

The per-platform format scoping in `CMakePresets.json` is unchanged from ADR-014
(Decision, lines 192-201): VST3 + AU + CLAP + Standalone on macOS; VST3 + CLAP +
Standalone + LV2-goal on Linux; VST3 + CLAP + Standalone on Windows. No AAX cell exists
on any platform.

## Consequences

This commits us to:

- A **single** LV2 toolchain (JUCE native exporter); the LV2 backlog targets that path
  only and does not carry a `clap-wrapper` fallback.
- The ADR-014 pin manifest staying at three dependencies (JUCE + Catch2 +
  clap-juce-extensions); no manifest amendment, no fourth SPDX/provenance entry, one
  wrapper-bump exposure surface rather than two.
- Getting JUCE's native LV2 exporter green under `lv2lint` + `lv2_validate` for the full
  feature set (MPE-lite, host-synced arp / 100-step seq state round-trip, full
  automation) as the sole route to shipping LV2 — un-hedged goal-tier work.
- A hard, unconditional configure-time rejection of any AAX target on any platform
  (ADR-011 Contract C4), re-affirmed here.

This forecloses / makes harder:

- **No `clap-wrapper` LV2 fallback.** If JUCE's native LV2 export cannot reach
  `lv2lint`/`lv2_validate` green, LV2 stays unshipped rather than falling back to a
  CLAP-derived bundle. This is tolerable only because LV2 is goal-tier and non-blocking:
  the Linux x64 hard gate is met by VST3 + CLAP + Standalone, so deferred or absent LV2
  never blocks a release.
- **No AAX, permanently** — cedes the Pro Tools base; unavoidable under GPL-3.0 + PACE
  and the validator rule (re-affirmed from ADR-011, not newly introduced).
- Adding `clap-wrapper` later is no longer a documented option-on-file; it would require
  a new ADR superseding this decision and amending the ADR-014 manifest.

No new owner-ratification item: ADR-011 already raised the AAX-permanently-out-of-scope
and LV2-may-ship-Linux-only-at-launch ratification items (ADR-011 Consequences,
lines 218-224). This ADR introduces no new user-expectation or scope risk — it narrows
an internal toolchain choice (one LV2 route instead of two) and re-affirms existing
locks. The user-visible format set, platform tiers, and gating are unchanged.

## Contract

The backlog implements the following normative cases verbatim. "Configure" = CMake
configure step; "ship" = produce a distributable artifact. These supersede the LV2-route
language in ADR-011 wherever they conflict; all other ADR-011 and ADR-014 contract cases
remain in force.

| Format | Toolchain | Platforms | Validator | Tier |
| --- | --- | --- | --- | --- |
| VST3 | JUCE native exporter | macOS arm64, Linux x64, Windows x64 | `pluginval` + Steinberg `validator` | macOS bless / Linux hard gate / Windows goal |
| AU | JUCE native exporter | macOS arm64 only | `auval` + `pluginval` | macOS bless (macOS-only by construction) |
| CLAP | clap-juce-extensions (wraps shared `AudioProcessor`) | macOS arm64, Linux x64, Windows x64 | `clap-validator` (+ `pluginval` where it exercises CLAP) | macOS bless / Linux hard gate / Windows goal |
| Standalone | JUCE native exporter | macOS arm64, Linux x64, Windows x64 | headless smoke-launch (+ `pluginval` engine checks) | macOS bless / Linux hard gate / Windows goal |
| LV2 | JUCE NATIVE LV2 exporter ONLY (no `clap-wrapper`) | Linux x64 only at launch | `lv2lint` + `lv2_validate` (lilv) | goal-tier, non-blocking; ships only when green |
| AAX | none (permanently excluded) | none | none (no open validator) | out of scope — configure-time error on any platform |

| # | Case | Required behavior |
| --- | --- | --- |
| C1 | Configure the LV2 target | LV2 is emitted by `juce_add_plugin`'s native LV2 exporter from the shared `AudioProcessor`, behind `MW_BUILD_LV2`; no `clap-wrapper` is vendored, pinned, or invoked. |
| C2 | Inspect `cmake/Dependencies.cmake` for the LV2 path | Manifest contains exactly JUCE + Catch2 + clap-juce-extensions (CLAP transitive); `clap-wrapper` is absent. Adding it fails manifest review (ADR-014 C2). |
| C3 | Ship LV2 (Linux x64) | Permitted only when `lv2lint` AND `lv2_validate` are wired and green; otherwise LV2 is not built/shipped. LV2 absence never blocks the Linux x64 hard gate (re-affirms ADR-011 C6). |
| C4 | Linux x64 release gate | Must ship VST3 + CLAP + Standalone, each with its validator green; LV2 optional (re-affirms ADR-011 C7). |
| C5 | LV2 bundle output on macOS arm64 reference | The shared engine is bit-identical across all formats; LV2 inherits the macOS arm64 bless reference with no DSP fork (re-affirms ADR-011 C11). |
| C6 | Configure any AAX target on any platform | Configure-time error: AAX is unconditionally and permanently excluded (GPL-3.0 + PACE, no open validator) — re-affirms ADR-011 C4. |
| C7 | LV2 wrapper drains host parameter/automation/note (atom) events | Events drain into the one pre-allocated, fixed-capacity, lock-free internal buffer sized at `prepareToPlay`; no heap allocation and no lock on the audio thread (re-affirms ADR-011 C9). |
