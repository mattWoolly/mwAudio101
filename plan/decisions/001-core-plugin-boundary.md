<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 001: DSP core / plugin-shell boundary and real-time contract

Status: accepted
Date: 2026-06-17

## Context

The owner locked the tree split (`core/` = pure C++20, no JUCE; `plugin/` = the JUCE
shell) in the design spec (`docs/superpowers/specs/2026-06-17-mwaudio101-design.md` §3.1,
§3.2) and the real-time invariant "no heap alloc / no locks on the audio thread" (§1.8,
restated in `AGENTS.md` "Tests"). Those locks sit ABOVE this ADR and are re-affirmed here,
not reopened.

What the locks do NOT yet answer — and what ADR-001 must answer — is the *contract across
that line*: the exact processing API (sample/block surface) the shell uses to drive the
core, the lifecycle that makes the no-alloc/no-lock invariant real rather than aspirational,
how parameters/MIDI/transport cross without dragging JUCE types into the DSP, and how the
core is unit-tested fully headless.

Forces in play:

- The core holds the tone-defining, fidelity-locked DSP (`LadderFilter`, `Oscillator`,
  `Vca`) that the golden harness must bless on macOS arm64 and tolerance-compare elsewhere
  (spec §5). Anything nondeterministic the host could leak in poisons that harness.
- Linux x64 is a *co-required hard gate*, not a nicety (spec §1.8); core tests must build and
  run on a headless CI box with no GUI and no audio device.
- The research mandates CPU-heavy nonlinear paths — the IR3109 ladder requires >=2x
  oversampling for the `tanh` nonlinearity (`docs/research/10-dsp-modeling-techniques.md`
  §5.1, §3.6), and either a per-sample Newton/fixed-point solve (TPT) or a fixed-iteration
  Euler step (Huovilainen) (§3.7) — both of which are exactly the places a naive design
  silently allocates or spins unbounded on the audio thread.
- Per-voice analog drift (§9 of research 10 references seeded noise; cross-ref research 09)
  must coexist with bit-stable goldens, which forces drift to be explicit seeded state, not
  ambient nondeterminism.
- Five plugin formats (VST3/AU/CLAP/Standalone, LV2 goal-tier) must reuse one render path;
  format differences must live entirely in `plugin/`.

## Options considered

Both panelists converged on the same shape — a freestanding JUCE-free `core/` static
library driven by the shell over a POD seam, with a `prepare`/`process` split and a
no-alloc/no-lock guard test. They differed in emphasis, and each contributed a load-bearing
mechanism. There was no split vote; the decision is the union of their proposals with the
sharper of each enforcement mechanism adopted.

### Persona: RT-engineer

Advocated a typed *two-phase lifecycle* as the primary safety mechanism:
`prepare(sampleRate, maxBlockSize, maxVoices)` sizes every buffer, voice pool, oversampler
scratch and coefficient table once (non-RT), and a `noexcept process(BlockContext&)` that
consumes only borrowed pointers plus a lock-free immutable `ParamSnapshot`, a POD
`TransportInfo`, and a non-owning `MidiEventView`. Key additional mechanisms: (a) the core
sub-blocks internally at a fixed `kRenderBlock` (~32 frames) for cache locality and to bound
oversampler latency regardless of host block size; (b) APVTS atomics are read once per block
into an immutable snapshot, with control smoothing living *inside* the core so automation
never zippers and the SH-101's coarse control-loop cadence is modeled; (c) per-voice drift
is pre-seeded per-voice PRNG state so "analog randomness" is reproducible for goldens; (d)
`noexcept` on the hot path is load-bearing because it forbids the throwing paths that pull in
`operator new`.

- Pros: allocation becomes *structurally* impossible (sized in `prepare`, `process` is
  `noexcept` + sentinel-guarded), not merely discouraged; sub-blocking gives deterministic,
  cache-friendly, latency-bounded processing independent of host block size; seeded drift
  keeps goldens bit-stable while sounding non-static; one seam serves all five formats.
- Cons: worst-case up-front sizing (maxVoices x maxBlockSize x oversample) is a steady memory
  cost even at one voice (needs a sane voice cap); sub-blocking adds partial-block /
  param-tick bookkeeping; the POD-snapshot boundary forces an explicit marshalling layer in
  `plugin/`; the sentinel-allocator scaffolding must be scoped so it never ships in release.

### Persona: testability-architect

Advocated the *dependency-direction* argument as primary: testability is a function of who
depends on whom, so the boundary must be **build-enforced**, not conventional — a CI guard
that fails if any `core/` translation unit includes `<juce_*>` / `JUCE_`, plus the rule that
`core` never depends up. Seam: `prepare(...) noexcept` (the only place allocation/table-build
happens; idempotent on sample-rate change), `process(AudioBlockView, EventView) noexcept`
(pure render over the project's own POD views, never `juce::AudioBuffer`/`juce::MidiBuffer`),
and `reset() noexcept`. Parameters cross as a normalized `[0,1]`/enum snapshot; APVTS and
parameter IDs live ONLY in the shell, and the core maps normalized -> engineering units
against the centralized `(PI)` calibration table. Enforcement: an `AudioThreadGuard` RAII
that arms a custom global `operator new` and fails any test that allocates inside `process`;
`noexcept` hot paths so an escaped throw is caught as a test crash; bounded solver
iterations + FTZ/DAZ; a headless Catch2 binary linking `mwcore` only, with test tiers for
property/invariant, no-alloc/no-lock contract, determinism, the two-stage golden harness, and
a `prepare`-then-`process` lifecycle/fuzz pass. Sub-block splitting at event boundaries gives
sample-accurate automation without a per-sample virtual call.

- Pros: the no-alloc/no-lock owner lock becomes a *mechanically checkable* test, not a
  code-review opinion; the build guard keeps §3.1 honest across many agents over years; the
  same POD render path is exercised by tests and the shell, so blessed goldens ARE what ships
  (no test-only path); parameter inversion lets the `(PI)` calibration table be unit-tested
  directly; maximizes the safe parallelism the spec wants (core/shell/ui as three streams).
- Cons: boundary marshalling is a second representation to keep in sync (mitigated by a thin,
  tested adapter); the pure-POD seam forgoes JUCE conveniences (`dsp::AudioBlock`,
  `ProcessSpec`, `SmoothedValue`, `dsp::Oversampling`) inside the core, so some wheels are
  re-implemented; event-boundary sub-block splitting adds control-flow complexity; every new
  core module must respect `noexcept`/no-alloc, so the guard test is non-negotiable.

### Critiques adopted

- From RT-engineer: the **typed two-phase lifecycle** with `maxVoices`/`maxBlockSize`-sized
  storage and a `noexcept process`; **fixed internal sub-block render size** (latency-bounded,
  cache-friendly); **lock-free atomic-snapshot param handoff with in-core smoothing**;
  **seeded per-voice PRNG drift** for golden bit-stability; explicit FTZ/DAZ denormal flush.
- From testability-architect: the **CI "no JUCE in core" build guard** (makes §3.1 a compiler-
  /CI-enforced boundary); the **`AudioThreadGuard` no-alloc/no-lock contract test** armed
  around `process`; **`reset()`** in the seam; **parameter inversion** (normalized snapshot;
  APVTS/IDs only in the shell; normalized->engineering mapping against the `(PI)` table);
  **bounded solver iterations**; the **`prepare`-then-`process` lifecycle/fuzz test**.
- Reconciled tension on sub-blocking: RT-engineer wanted fixed `kRenderBlock`; testability-
  architect wanted splits at event boundaries. Adopted **both** — the core splits the host
  block at event sample-offsets, then renders each segment in fixed-size internal chunks
  (capped at `kRenderBlock`). This gives sample-accurate events AND a bounded, cache-stable
  oversampler working set. The control-rate param tick aligns to the chunk boundary.

## Decision

Partition the codebase so that `core/` is a freestanding C++20 static library (`mwcore`)
with **zero** link or include dependency on JUCE, and `plugin/` (`mwplugin`) is the JUCE
shell that *drives* it. The dependency points one way only: `plugin -> core`, `ui -> plugin`;
`core` never depends up. This is enforced at build time, not by convention: a CI guard fails
if any `core/` translation unit includes `<juce_*>` or references `JUCE_`, and `mwcore`'s
link interface must contain no JUCE target. This makes spec §3.1/§3.2 a CI-enforced boundary.

The processing contract is a small, value-typed, three-call engine surface — the single seam
shared identically by the shell and every test:

- `void prepare(double sampleRate, int maxBlockSize, int maxVoices) noexcept;` — the ONLY
  place allocation, table-building, oversampler scratch/ratio selection, voice-pool
  construction, and buffer sizing happen. Called off the audio thread from the shell's
  `prepareToPlay`. Idempotent and re-callable on sample-rate/blocksize change.
- `void process(const BlockContext&) noexcept;` — pure render; touches only pre-sized member
  storage. `BlockContext` is a POD aggregate of: a non-owning `AudioBlockView`
  (`float* const* channels; int numChannels; int numFrames`), an immutable `ParamSnapshot`,
  a POD `TransportInfo` (bpm, ppq, playing, sampleRate), and a `MidiEventView` (non-owning
  span of sample-offset-timestamped, host-decoded note/CC/MPE events). No `juce::AudioBuffer`,
  `juce::MidiBuffer`, or `APVTS` type ever crosses the seam.
- `void reset() noexcept;` — clears state to a known start, no allocation.

`process` splits the host block at event sample-offsets and renders each segment in fixed-size
internal chunks (`kRenderBlock`, a `(PI)` constant). Control-rate parameter ticks (one-pole /
linear smoothing, owned by the core) align to chunk boundaries, modeling the SH-101's coarse
control-loop cadence faithfully while audio-rate render runs underneath. Integer/deterministic
paths (sub-osc divider, arp/seq step counters, clock) are bit-exact by construction across
platforms (spec §5); the floating-point analog stages are blessed on macOS arm64 and
tolerance-compared (`max abs <= 1e-6`) elsewhere.

Parameters are inverted: the shell reads APVTS atomics once per block into a normalized
(`[0,1]` / typed-enum) `ParamSnapshot`; the core maps normalized -> engineering units
internally against the centralized `(PI)` calibration table. APVTS and parameter IDs live
ONLY in `plugin/`. This removes both audio-thread locks and automation zipper noise in one
mechanism and keeps the calibration table directly unit-testable.

Real-time invariants on the audio thread (`process`/`reset`), made mechanical rather than
hoped: no heap alloc, no locks/mutex, no syscalls, no unbounded loops, no thrown exceptions,
denormals flushed (FTZ/DAZ set at `process` entry). The whole hot path is `noexcept` so an
escaped throw calls `std::terminate` and is caught as a test crash. The nonlinear ladder's
>=2x oversampler and its decimator scratch are owned and sized in `prepare`
(`docs/research/10-dsp-modeling-techniques.md` §5.1); the implicit-loop solver (TPT Newton,
§3.7) or Huovilainen Euler step (§3.6) runs at a fixed maximum iteration count so worst-case
per-sample cost is constant. The 2x-vs-the-decimator IIR/FIR order/ripple choice is explicitly
deferred to a later ADR per research 10 §5.2 / §9.2 — this ADR only fixes that the oversampler
state is owned by the core and sized in `prepare`. The choice of ladder engine (TPT-Newton vs
Huovilainen-Euler, research 10 §3.6/§3.7, §9.4) is likewise a separate ADR with a CPU/fidelity
benchmark; both fit behind this seam unchanged. Per-voice drift uses pre-seeded per-voice PRNG
state, so "analog" randomness is reproducible and goldens stay bit-stable (research 10 §6,
cross-ref research 09).

FP discipline is pinned at the core build target: `-ffast-math` OFF, `-ffp-contract=off`
(`/fp:precise` on Windows), because the macOS-arm64 bless + Linux tolerance-compare depends on
it (spec §5; `AGENTS.md` "Tests"). `-fno-exceptions` is NOT used globally (JUCE needs
exceptions); the core relies on `noexcept` hot paths instead.

Headless testing: a Catch2 console binary links `mwcore` ONLY — no plugin, no JUCE, no audio
device — so it builds and runs on the Linux hard gate and the macOS bless box in milliseconds.
Test tiers: (1) per-module property/invariant tests (oscillator bounded in `[-1,1]`, ladder
stable for `k<4` and self-oscillating near `k=4` per research 10 §3.4, ADSR segment shapes);
(2) the `AudioThreadGuard` no-alloc/no-lock contract test wrapping a representative `process`
(a first-class acceptance checkbox per the owner lock and `AGENTS.md`); (3) determinism tests
(same seed + same events => bit-identical block on integer/deterministic paths); (4) the
two-stage golden harness driving the core directly with scripted `BlockContext` sequences,
blessed on macOS arm64, tolerance-compared elsewhere (spec §5); (5) a `prepare`-then-`process`
lifecycle/fuzz test (random valid block sizes `<= maxBlockSize`, random valid params) to catch
buffer-size and ramp-boundary bugs; (6) a per-block CPU-budget regression assertion in the
stress golden (max voices, high resonance, oversampling on) to catch CPU regressions in CI.
Per `AGENTS.md`, every `ctest -R/-L` carries `--no-tests=error` and test-case names begin with
the task tag word.

## Consequences

This commits us to:

- A POD marshalling/adapter layer in `plugin/` (MIDI/APVTS/transport -> `BlockContext`) — a
  thin, separately tested second representation that must stay in sync with the host.
- Re-implementing some JUCE DSP conveniences inside the core (oversampling, smoothing,
  audio-block iteration) rather than using `juce::dsp` — consistent with the circuit-accurate,
  our-own-`(PI)`-constants goal, and the price of a JUCE-free testable core.
- Worst-case memory sized up front in `prepare` (maxVoices x maxBlockSize x oversample scratch)
  even when one voice is sounding; a sane `maxVoices` cap is mandatory.
- A debug-only sentinel-allocator / `AudioThreadGuard` and an ASan/UBSan/TSan CI pass that must
  be carefully scoped so the global-`new` override never ships in release builds.
- Every new core module respecting `noexcept`/no-alloc and routing all allocation through
  `prepare`; the no-alloc guard test and the no-JUCE-in-core build guard are non-negotiable.

This forecloses / makes harder:

- Any feature that wants dynamic structure on the audio thread (e.g. a naive reverb that grows
  buffers at runtime) — it must be redesigned around a pre-allocated pool, not "just allocate."
- Letting the core read APVTS or host MIDI directly (the RT/testability sin this ADR forbids).
- Cheap reuse of host conveniences inside the core; the seam cost is paid on every block.

This ADR stays strictly inside the owner locks (spec §1.8, §3.1, §3.2, §5) and the research it
cites; it introduces no new user-facing feature scope or behavior promise beyond them. No owner
ratification item.

## Contract

Normative cases the backlog implements verbatim. "MUST" items are acceptance checkboxes.

| # | Condition / trigger | Required behavior | Enforcement |
|---|---|---|---|
| C1 | Any `core/` translation unit includes `<juce_*>` or references `JUCE_`; or `mwcore` link interface contains a JUCE target | CI build guard FAILS | CI "no-JUCE-in-core" guard |
| C2 | Host calls `prepareToPlay(sampleRate, maxBlockSize)` (and voice cap known) | Shell calls `core.prepare(sampleRate, maxBlockSize, maxVoices)`; ALL buffers/tables/voice-pool/oversampler scratch sized here; idempotent on re-call | lifecycle/fuzz test |
| C3 | Any heap allocation occurs during `process` or `reset` | Test FAILS | `AudioThreadGuard` (armed global `operator new`) |
| C4 | Any lock/mutex acquisition occurs during `process` or `reset` | Test FAILS | `AudioThreadGuard` lock sentinel |
| C5 | An exception escapes a hot-path (`process`/`reset`) call | `std::terminate` -> caught as a test crash (paths are `noexcept`) | `noexcept` + crash detection |
| C6 | Host block contains events at sample offsets | `process` splits at event offsets, renders each segment in fixed `kRenderBlock` chunks; events apply sample-accurately | property test |
| C7 | Parameter changes between/within blocks | Shell snapshots APVTS atomics once per block into normalized `ParamSnapshot`; core smooths internally (no zipper); core never reads `std::atomic` in tight loops | property test |
| C8 | Identical seed + identical `BlockContext` event/param sequence, deterministic/integer paths | Output is bit-identical run-to-run and across macOS/Linux | determinism test |
| C9 | Floating-point analog stages (ladder, VCA, oscillator) | Blessed on macOS arm64; tolerance-compared elsewhere at `max abs <= 1e-6` plus domain checks | golden harness (spec §5) |
| C10 | Nonlinear ladder path | Runs at >=2x oversampling; oversampler + decimator scratch owned by core and sized in `prepare`; implicit solver bounded to a fixed max iteration count | research 10 §5.1, §3.7; CPU-budget golden |
| C11 | `process` entry | FTZ/DAZ denormal flush set; self-oscillating/decay tails never enter denormal stall | property/CPU test |
| C12 | Core builds | FP flags pinned: `-ffast-math` OFF, `-ffp-contract=off` / `/fp:precise` | build config + golden reproducibility |
| C13 | Core unit-test binary | Links `mwcore` only — no JUCE, no plugin, no audio device; runs on headless Linux gate; every `ctest -R/-L` carries `--no-tests=error` | CI Linux gate |
| C14 | A given plugin format wrapper (VST3/AU/CLAP/Standalone/LV2) | Differs only in `plugin/`; the `core` render path is identical across all formats | code-review + shared golden |
