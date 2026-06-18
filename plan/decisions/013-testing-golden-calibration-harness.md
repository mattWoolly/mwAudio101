<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 013: Testing strategy — golden/regression and calibration harness

Status: accepted
Date: 2026-06-17

## Context

We must define the test architecture for mwAudio101: Catch2 unit tests with
silent-pass-prevention rules, a golden/regression harness (two-stage comparer,
guarded bless with provenance), the bit-exact-vs-FP-tolerance compare policy
across platforms, calibration-tool self-tests, and the cross-cutting invariants
(license headers, no-audio-thread allocation) baked into one `ctest` graph.

The single force that dominates this ADR is the calibration-oracle decision:
mwAudio101 is **modeled from documented circuit behavior** and **holds no
physical SH-101 unit and takes no bench measurements as the source of truth**
(docs/research/13-validation-gaps-and-disputes.md §1.1). Recordings, if any, are
a secondary local-only cross-check and are **never** the oracle. The
hardware-measurement gaps in §5 (ADSR curve law §5.1, filter Bode/phase/resonance
§5.2, oscillator/sub-osc/noise spectra §5.3, drift §5.4, DSP-vs-circuit fidelity
§5.6) are **permanent** under this policy, not TODOs. The structural consequence
is unavoidable: **a golden file can only prove "the DSP still does what it did
when blessed" — it can NEVER prove "this matches a real SH-101."** The test
architecture must encode that truth so no test can silently launder a guess into
a regression-protected "fact."

This ADR sits under, and re-affirms (does not reverse), these owner-locked
decisions (plan/ORCHESTRATION.md "Decisions locked with the project owner"):

- **Circuit-accurate modeling from documented circuit behavior; NO physical-unit
  oracle** (recordings = secondary, local-only).
- **macOS arm64 = reference/bless platform and bit-exact**; **Linux x64 =
  co-required hard gate**; **Windows x64 = goal**.
- **Real-time safe: no heap allocation and no locks on the audio thread.**
- **GPL-3.0-or-later** on every source file.
- **JUCE / C++20 / CMake**; formats VST3, AU (macOS), CLAP, Standalone; LV2 =
  goal-tier.
- Feature scope = faithful mono SH-101 path plus modern essentials (poly/unison,
  oversampling, per-voice drift, Chorus/Delay/Drive, host-synced arp + 100-step
  seq, full automation, MPE-lite, ~64 presets).

Two locked decisions collide and force the compare design. "macOS arm64 =
bit-exact" and "Linux x64 = co-required hard gate" cannot both be satisfied by a
single bit-exact cross-platform compare, because the adopted DSP stack is
wall-to-wall transcendentals — `tanh`, `tan`, `exp` in the TPT/Huovilainen ladder
and the OTA VCA (docs/research/10-dsp-modeling-techniques.md §3.2, §3.6, §4) —
whose last-ULP results, SIMD reduction order, and FMA contraction differ between
arm64 and x64 libm. A naive bit-exact gate on Linux would emit constant false
regressions, training reviewers to ignore red — the worst silent-pass outcome.
By contrast, the integer/control logic where the load-bearing citable facts live
— sequencer per-step byte format and note-priority decode
(docs/research/13-validation-gaps-and-disputes.md §2.9, §4.6), the 4013 divider
diode-OR sub-osc edges (docs/research/10-dsp-modeling-techniques.md §7), and the
fixed-seed integer PRNG noise stream
(docs/research/10-dsp-modeling-techniques.md §6) — IS reproducible bit-for-bit
across both architectures.

This ADR also depends on still-open engine choices: ZDF/TPT-vs-Huovilainen (ADR
003) and the IIR-vs-FIR decimator and oversampling factor (ADR 004), both flagged
as open A/B decisions (docs/research/10-dsp-modeling-techniques.md §5.2, §9.4;
docs/research/13-validation-gaps-and-disputes.md §6.1). The harness must let those
A/Bs be decided on reproducible identical-stimulus renders, not vibes.

## Options considered

The panel split on ONE axis: how the golden corpus is partitioned. Everything
else (Catch2 silent-pass prophylaxis, two-stage comparer, guarded bless +
MANIFEST + BLESS_REASON, planted-answer + disjoint cal/val self-tests, ctest
license-header and no-audio-thread-alloc gates) was convergent across both
personas.

### Persona: QA-adversarial — silent-pass prevention, FP divergence, regression integrity

Advocated a three-layer ctest graph partitioned by **platform tier**: macOS arm64
= bit-exact (it is the bless platform), Linux x64 = documented FP-tolerance hard
gate, Windows x64 = same tolerance tier (goal). Goldens keyed by {render-graph
hash, sample rate, block size, seed}; tolerance derived per-signal from a
documented noise floor, committed alongside the golden. Layer 1: Catch2 with
`--no-tests=error`, per-module name-prefix discovery assertions, a ban on bare
`REQUIRE(true)`, and a mandatory paired positive/negative property assert on every
numeric DSP test (e.g. "k=4 self-oscillates" paired with "k=3.9 does NOT") so a
stubbed-to-constant implementation fails. Layer 3: a guarded `bless` tool refusing
to run without a non-empty `BLESS_REASON`, appending to a checked-in
`MANIFEST.toml` that binds each artifact to its honesty-label provenance
(docs/research/13-validation-gaps-and-disputes.md §2-§8 — e.g. "self-osc
amplitude: clone-derived AM8101 @ ±12V, NOT measured"). Layer 4: planted-answer +
disjoint cal/val + a deliberately-wrong negative-control fixture for the
calibration harness. Plus a CPU-budget regression test rendering a worst-case
patch (full poly + unison + 2x oversampling + Newton ladder) under a committed
wall-time ceiling, with oversample factor and ladder engine pinned in the
MANIFEST.

- Pros: directly honors the "arm64 bless + bit-exact" lock; the honesty-label
  provenance binding is the strongest defense of the no-oracle policy; the
  CPU-budget golden catches a fidelity change that blows the RT budget as a
  regression rather than a user underrun; paired positive/negative asserts kill
  stubbed-constant DSP.
- Cons: making the WHOLE compare platform-tiered means even the integer/control
  logic — which IS achievable bit-exact on Linux — would be tolerance-compared
  there, throwing away a deterministic, zero-ambiguity gate on exactly the paths
  that carry the citable facts (sequencer bytes, divider OR, PRNG).

Critiques adopted: the silent-pass prophylaxis set; the guarded bless +
MANIFEST + BLESS_REASON + honesty-label provenance binding (this is the project's
single biggest integrity control given the no-oracle policy); the paired
positive/negative property-assert rule; the planted-answer + disjoint cal/val +
negative-control calibration self-tests; the CPU-budget regression golden with
engine/oversample pinned in the MANIFEST; both cross-cutting invariants as ctest
gates with a documented, reviewed one-time warm-up carve-out for lazy init.

### Persona: DSP-determinism — bit-exact integer paths, FP discipline, golden corpus design

Advocated the same three layers but partitioned the golden corpus by
**determinism CLASS, not platform tier**. CLASS-EXACT (integer / integer-derived:
phase-accumulator wrap, divider/4013 OR-logic edge indices, sequencer byte layout
and note-priority decode, fixed-seed PRNG stream, arp step ordering, param-smooth
block boundaries, CC/param mapping) is SHA-256 hash-compared and must be IDENTICAL
on macOS arm64 AND Linux x64. CLASS-FP (the nonlinear audio path: TPT/Huovilainen
tanh ladder, PolyBLEP residuals, oversampled VCA) gets a two-stage comparer —
Stage 1 cheap RMS/envelope/max-abs-err fast-reject; Stage 2 (only on Stage-1 fail)
windowed-FFT NMSE-in-dB plus an alias-floor metric below the perceptual limit
(~2135 Hz NI=2 / 7.8 kHz B-spline, docs/research/10-dsp-modeling-techniques.md §8)
— with per-corpus tolerance stored IN the manifest, never a global `#define`.
Added: bless runs ONLY on macOS arm64 (Linux blesses rejected); a compile-time +
runtime **FP-discipline flag gate** asserting `-ffast-math`,
`-funsafe-math-optimizations`, `-freciprocal-math` are OFF and `-ffp-contract=off`
is pinned for the DSP TU, with the proof recorded in the manifest; goldens tagged
by engine ({ZDF|Huov}, oversample factor) so the open ADR-003/ADR-004 A/Bs run on
identical stimuli without cross-contaminating blesses; a checked-in
`ctest --print-labels` snapshot diffed in CI so a deleted test fails loudly.

- Pros: maximizes the bit-exact surface exactly where it is achievable AND where
  the citable facts live, giving a zero-tolerance deterministic gate across both
  hard-gate platforms; the FP-discipline gate makes CLASS-EXACT actually true
  (without it, bit-exactness is a lie and FP tolerances drift silently between
  toolchains); engine-tagging makes the open ladder/decimator ADRs decidable by
  reproducible A/B.
- Cons: corpus-by-class is upfront classification discipline — a mis-classified FP
  path placed in CLASS-EXACT is permanently red on Linux; a toolchain bump can
  invalidate the whole CLASS-EXACT corpus and force a mass re-bless (the manifest
  records the toolchain, but the churn is real).

Critiques adopted: the determinism-CLASS partition as the spine of the corpus
(this resolves the split — see Decision); arm64-only bless; the FP-discipline
flag gate as a first-class ctest invariant; per-corpus tolerance in the manifest
(no global tolerance constant); engine/oversample-tagged goldens for the open
ADR-003/004 A/Bs; the checked-in label-snapshot diff; the two-stage comparer with
spectral/alias-floor Stage 2.

### Resolution of the split

Both personas are right about their respective halves. The platform-tier framing
(QA-adversarial) is correct for the FP analog path; the determinism-class framing
(DSP-determinism) is correct because it recovers a bit-exact cross-platform gate
on the integer/control path that a pure platform-tier scheme would needlessly
relax. We adopt **determinism-CLASS as the primary partition** and make the
platform tier a property OF each class: CLASS-EXACT is bit-exact on BOTH arm64 and
Linux (hash-compared); CLASS-FP is bit-exact only on the arm64 bless platform and
FP-tolerance-banded on Linux/Windows. This honors every word of the locked
compare policy while giving the citable-fact paths the strongest possible gate.
QA-adversarial's honesty-label provenance and CPU-budget golden are layered on top
unchanged.

## Decision

Adopt a single `ctest` graph with three layers plus cross-cutting invariants, and
partition the golden corpus by **determinism class** with platform behavior a
property of the class.

**Layer 1 — Catch2 unit tests, silent-pass-hardened.** Register every test
executable with `catch_discover_tests` run under `--no-tests=error` (or ctest
`FAIL_REGULAR_EXPRESSION` on "No tests ran") so an empty/mis-linked/mis-filtered
binary FAILS instead of passing green. Every test carries a subsystem tag
(`[vco][vcf][vca][env][seq][prng][arp][rt][cal]`) and a name-prefix selector
(`mw101.unit.*`, `mw101.golden.*`, `mw101.cal.*`); CI asserts each prefix has >=1
discovered test, and a checked-in `ctest --print-labels` snapshot is diffed so a
deleted/renamed suite shows up as a failing diff, not silence. A clang-query/grep
lint bans bare `REQUIRE(true)` and empty-body tests. **Every numeric DSP assert
MUST be paired with a negative/property control** so a stubbed-to-constant
implementation fails (e.g. ladder self-oscillates at `k=4` but NOT at `k=3.9`,
docs/research/10-dsp-modeling-techniques.md §3.4).

**Layer 2 — Two-stage golden/regression comparer, corpus partitioned by
determinism class.**

- **CLASS-EXACT** — integer or integer-derived paths, SHA-256 hash-compared, must
  be IDENTICAL on macOS arm64 AND Linux x64 (any diff fails): phase-accumulator
  wrap; 4013 divider diode-OR sub-osc edge-sample indices
  (docs/research/10-dsp-modeling-techniques.md §7,
  docs/research/13-validation-gaps-and-disputes.md §4.3); sequencer per-step byte
  layout and note-priority decode
  (docs/research/13-validation-gaps-and-disputes.md §2.9, §4.6); fixed-seed
  integer PRNG stream (docs/research/10-dsp-modeling-techniques.md §6 — prefer
  64-bit LCG/PCG; xorshift only with the §9.1 contested-endorsement caveat); arp
  step ordering; parameter-smoothing block boundaries; CC/automation param
  mapping.
- **CLASS-FP** — the nonlinear audio path (TPT/Huovilainen `tanh` ladder, PolyBLEP
  residuals, oversampled VCA, Drive). Stage 1 = cheap scalar fingerprint
  (per-buffer RMS, peak, envelope, max-abs-err) gated on tolerance; Stage 2 (run
  only on Stage-1 flag or `--full`) = full sample-vector compare plus windowed-FFT
  NMSE-in-dB and an alias-floor metric below the perceptual limit
  (docs/research/10-dsp-modeling-techniques.md §8 — ~2135 Hz NI=2 / 7.8 kHz
  B-spline). Compare tier is bit-exact on arm64 (the bless platform), and
  FP-tolerance-banded on Linux x64 (hard gate) and Windows x64 (goal). The
  tolerance is **per-corpus in the manifest**, derived from a documented noise
  floor — never a global `#define` or magic number. Goldens are versioned binary
  blobs (raw f32/WAV + sidecar JSON) keyed by {render-graph hash, engine tag,
  oversample factor, sample rate, block size, seed}.

Goldens are **engine-tagged** ({ZDF|Huov}, oversample factor) so the open
ADR-003 (ladder engine) and ADR-004 (decimator / oversampling) A/Bs run on
identical stimuli without cross-contaminating blesses
(docs/research/10-dsp-modeling-techniques.md §9.4).

**Layer 3 — Guarded bless + provenance MANIFEST.** `bless` is a separate tool,
never a test side-effect, and **runs only on macOS arm64** (Linux/Windows blesses
rejected, per the locked reference-platform decision). It refuses unless
`BLESS_REASON` is set non-empty. Each blessed artifact appends to a checked-in
`MANIFEST.toml` recording: artifact SHA-256, blesser identity, ISO date, commit
SHA, `BLESS_REASON`, exact render parameters {engine, oversample factor, sample
rate, fs/seed, block size}, corpus class, the tolerance band (CLASS-FP),
compiler+version, the FP-flag proof (`-ffast-math` off, `-ffp-contract=off`), the
arm64 reference host id, and the **honesty-label provenance** of what the artifact
claims (docs/research/13-validation-gaps-and-disputes.md §2-§8 — e.g. "self-osc
amplitude: clone-derived AM8101 @ ±12V, NOT measured §4.1"; "ADSR curve law:
theory/inference §5.1"; "sequencer byte format: community disassembly, partly
inferred §4.6"). CI fails if a golden's hash is absent from MANIFEST, or if a
MANIFEST entry has no corresponding test. This makes "why did the golden change"
an auditable, reviewable diff and carries the ledger's honesty labels into the
chain of custody so a guess can never silently harden into a "regression-protected
fact."

**Layer 4 — Calibration-tool self-tests.** The calibration harness
(variance/drift fit, k-mapping, tempco — docs/research/13-validation-gaps-and-disputes.md
§5.4, §6.1) is tested with: (a) **planted-answer** fixtures (synthesize a signal
from known circuit-model parameters, run the calibrator, assert recovery within
tolerance — catches a calibrator that "succeeds" by echoing its input); (b)
**disjoint cal/val split** (parameters/seeds used to FIT are never the ones used
to VALIDATE — detects overfitting / memorization); (c) a deliberately-**wrong**
planted fixture the calibrator MUST reject (negative control). These run offline,
off the audio thread. Since there is no physical oracle, planted answers are the
only oracle we can manufacture.

**Cross-cutting invariants, baked into ctest as gates (not advisories) because
they are owner-locked:**

- **License-header check** — every source carries `SPDX-License-Identifier:
  GPL-3.0-or-later` (matching the headers in docs/research/*.md and the existing
  ADRs).
- **No-audio-thread-allocation guard** — a fixture installs a `processBlock`-scope
  sentinel (override global `new`/`malloc`/`free` and pthread mutex hooks, or run
  under an RT-safety harness) and FAILS on any heap alloc or lock taken during
  `processBlock`, exercised under stress (block-size sweep, sample-rate change,
  voice-steal, mid-block preset recall, automation storm), with a documented and
  code-reviewed **one-time warm-up carve-out** for lazy init that must never
  become a blanket exemption. This pins the no-heap/no-lock lock against the
  adopted stack's per-voice PRNG state, oversampling buffers, Newton-iteration
  scratch, and `tanh`/compensation tables — all must be preallocated at
  `prepareToPlay` (docs/research/10-dsp-modeling-techniques.md §3.7, §5.1).
- **FP-discipline flag gate** — a compile-time + runtime assertion that
  `-ffast-math`, `-funsafe-math-optimizations`, `-freciprocal-math` are OFF and
  `-ffp-contract=off` is pinned for the DSP TU, with the proof recorded in the
  bless manifest. Without this, CLASS-EXACT is a lie and CLASS-FP tolerances drift
  silently between toolchains.
- **CPU-budget regression golden** — render a worst-case patch (full poly + unison
  + 2x oversampling + Newton-iterated ladder) and assert wall-time per block stays
  under a committed ceiling, with engine and oversample factor pinned in the
  MANIFEST, so a fidelity change (e.g. Huovilainen-Euler -> TPT+Newton) that blows
  the RT budget is caught as a regression, not discovered as a user underrun
  (docs/research/10-dsp-modeling-techniques.md §3.6, §3.7).

Why this and not a single global compare: the no-oracle policy
(docs/research/13-validation-gaps-and-disputes.md §1.1, §5) makes every golden
self-referential, so provenance discipline (Layer 3) is the only thing standing
between "documented circuit behavior" and silently shipping a wrong constant as
gospel; and the arm64-bit-exact / Linux-hard-gate lock can only be honored
honestly by maximizing the bit-exact surface (CLASS-EXACT on both platforms) while
giving the transcendental FP path explicit manifested tolerances.

## Consequences

This commits us to:

- Real harness engineering before the engine fully stabilizes: a `bless` tool, a
  MANIFEST validator, a two-stage comparer with an FFT/NMSE Stage 2, an
  allocator/lock sentinel, per-prefix discovery + label-snapshot gates, and the
  FP-flag gate. This harness code must itself be tested (it can have silent
  passes too); the planted-answer self-tests partly cover the calibration side.
- Correctly **classifying every new golden** as CLASS-EXACT or CLASS-FP at
  authoring time; a mis-classified FP path placed in CLASS-EXACT will be
  permanently red on Linux.
- Roughly **doubling unit-test authoring effort** versus naive single-output
  asserts (paired positive/negative controls, planted-answer fixtures).
- Pinning `-ffp-contract=off` and fast-math off in the DSP TU, which slightly
  raises per-sample cost versus an FMA-fused build but is mandatory for
  determinism and is verified, not assumed.
- Storing goldens as binary blobs in the repo — they grow history weight; we will
  need Git LFS or a render-on-demand-from-seed strategy to avoid bloating clone
  size.

It forecloses / makes harder:

- A single cross-platform bit-exact gate (deliberately abandoned for CLASS-FP —
  it would be permanently red on Linux given transcendental libm divergence,
  docs/research/10-dsp-modeling-techniques.md §3.2, §3.6, §4).
- "Quick" goldens blessed as a test side-effect: bless is arm64-only, tool-gated,
  and requires a reviewed MANIFEST diff with `BLESS_REASON`.
- Toolchain churn is real: an Xcode/compiler/libm bump can force a mass re-bless
  of CLASS-EXACT (toolchain recorded in the manifest, but the churn remains), and
  without discipline a re-bless can degrade into a rubber-stamp — mitigated, not
  eliminated, by `BLESS_REASON` review.

What this harness can and cannot prove (state plainly in any accuracy claim):
because there is no physical oracle
(docs/research/13-validation-gaps-and-disputes.md §1.1, §5.6), goldens validate
**self-consistency and topology-faithfulness, NOT measured SH-101 fidelity**. A
wrong-but-stable render passes forever; this harness detects "we changed the DSP,"
never "we modeled the circuit wrong." The CLASS-FP tolerance band is a judgement
call (too tight -> Linux flaps red on legitimate libm differences; too loose ->
real small regressions slip through) and needs periodic re-derivation as the DSP
changes; the manifest makes it honest, not automatic.

Owner ratification item: this ADR formalizes that no test in the project asserts
measured SH-101 fidelity, and that "accuracy" claims in marketing/UI must be
worded as "modeled from documented circuit behavior," carrying the ledger honesty
labels (clone-derived / reverse-engineered / theory-inference) into the shipped
provenance. This is downstream of the locked no-oracle decision but has direct
user-expectation impact and should be explicitly acknowledged by the owner.

## Contract

Normative cases the backlog implements verbatim. "Reference" = the macOS arm64
bless platform.

| Case | Condition | Required outcome |
| --- | --- | --- |
| C1 empty/mis-filtered test binary | a test executable discovers zero tests | ctest FAILS (`--no-tests=error` / "No tests ran" regex) — never green |
| C2 missing module coverage | a name-prefix (`vco.*`/`vcf.*`/`vca.*`/`env.*`/`seq.*`/`prng.*`/`arp.*`/`cal.*`) has 0 discovered tests | ctest FAILS the per-prefix discovery assertion |
| C3 deleted/renamed suite | committed `ctest --print-labels` snapshot differs from current | ctest FAILS on the label-snapshot diff |
| C4 stubbed-constant DSP | a numeric DSP test lacks its paired negative/property control, or the control does not fail on a constant stub | lint/test FAILS |
| C5 CLASS-EXACT compare | integer/control golden (seq bytes, divider OR edges, PRNG stream, arp order, param-smooth boundaries, CC map) on arm64 OR Linux x64 | SHA-256 must match bit-for-bit; any diff FAILS |
| C6 CLASS-FP compare on Reference (arm64) | nonlinear-path golden, arm64 | bit-exact vs blessed artifact; any diff FAILS |
| C7 CLASS-FP compare on Linux x64 (hard gate) | nonlinear-path golden, Linux | must fall inside the per-corpus manifest tolerance band; outside band FAILS |
| C8 CLASS-FP compare on Windows x64 (goal) | nonlinear-path golden, Windows | same tolerance tier as C7; reported, goal-gated |
| C9 two-stage trigger | Stage-1 scalar fingerprint within tolerance | Stage 2 (full vector + FFT/NMSE + alias-floor) SKIPPED unless Stage 1 flags or `--full` |
| C10 bless platform guard | `bless` invoked on Linux/Windows | bless REFUSES (arm64-only) |
| C11 bless reason guard | `bless` invoked without non-empty `BLESS_REASON` | bless REFUSES |
| C12 MANIFEST completeness | a golden file hash not present in `MANIFEST.toml` | CI FAILS |
| C13 MANIFEST orphan | a `MANIFEST.toml` entry with no corresponding test | CI FAILS |
| C14 honesty-label provenance | a blessed artifact whose claim derives from a ledger §2-§8 labelled fact lacks that label in its MANIFEST entry | CI FAILS |
| C15 calibration planted-answer | calibrator run on a signal synthesized from known params | recovered params within tolerance; else FAILS |
| C16 calibration disjoint cal/val | fit on parameter/seed set A, validate held-out error on disjoint set B | held-out error within tolerance; else FAILS (overfit detected) |
| C17 calibration negative control | calibrator run on a deliberately-wrong planted fixture | calibrator MUST reject it; acceptance FAILS |
| C18 license header | any source file missing `SPDX-License-Identifier: GPL-3.0-or-later` | ctest FAILS |
| C19 no-audio-thread alloc/lock | any heap alloc or lock taken during `processBlock` (outside the documented one-time warm-up carve-out), under the stress fixture | ctest FAILS |
| C20 FP-discipline flags | `-ffast-math`/`-funsafe-math-optimizations`/`-freciprocal-math` not OFF, or `-ffp-contract` not `off`, in the DSP TU | compile-time + runtime assertion FAILS |
| C21 CPU budget | worst-case patch (full poly + unison + 2x oversampling + Newton ladder) per-block wall-time exceeds the committed ceiling | ctest FAILS (RT-budget regression) |
| C22 engine-tag isolation | a CLASS-FP golden compared across a different engine tag ({ZDF\|Huov}) or oversample factor than it was blessed under | compare REFUSES (no cross-engine contamination) |
