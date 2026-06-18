<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 019: Voice-rendering threading model

Status: accepted
Date: 2026-06-18

## Context

ADR-006 fixes a compile-time-sized, preallocated `Voice` pool (`Voice[kMaxVoices]`,
`kMaxVoices >= maxPoly x maxUnison`, `maxUnison` capped at 8) whose worst-case CPU is
"active voices x per-voice oversampled cost + shared FX" (ADR-006 §4, C16-C17). ADR-003
makes each voice's IR3109 ladder a mandatory 2x-oversampled nonlinear block -- "the heaviest
per-voice DSP block" -- which "multiplies under poly/unison" (ADR-003 Consequences, F-09).
ADR-001 puts the whole render behind a single `void process(const BlockContext&) noexcept`
seam that sub-blocks internally at a fixed `kRenderBlock` and forbids heap allocation, locks,
syscalls, unbounded loops, and thrown exceptions on the audio thread (ADR-001 Decision; C3,
C4, C5).

None of those ADRs say WHO runs the per-voice loop: a single thread inside `process`, or a
fan-out across a worker thread pool. With up to `maxPoly x maxUnison` independent
2x-oversampled nonlinear voices, multi-threading is an obvious latent temptation, so the gap
needs an explicit owner. This ADR fills it.

Owner-locked decisions this touches (re-affirmed, not reopened):

- **Real-time safe: no heap allocation and no locks on the audio thread.** A worker pool's
  natural coordination primitives (mutex/condition-variable wakeups, work-stealing deques,
  futures) are exactly the locks and unbounded waits the lock forbids; honoring the lock with
  threads requires lock-free, bounded, wait-free-on-the-audio-side machinery that is far
  harder to get right and to prove.
- **macOS arm64 = reference/bless and FP bit-exact; Linux x64 = co-required hard gate
  (integer bit-exact, FP tolerance-banded per design spec §5).** Bless requires that the same
  input produce the same output sample-for-sample on the reference target. Summing N voices in
  a nondeterministic completion order changes the floating-point reduction order, which is not
  associative, so a thread pool silently breaks FP bit-exactness on the bless target unless
  the reduction is forced into a fixed order anyway -- which removes most of the parallel win.
- **JUCE / C++20 / CMake; RT-safe; poly/unison + oversampling + per-voice drift in scope.**

This ADR depends on and reconciles ADR-001 (the `noexcept process` seam, `kRenderBlock`
chunking, the `AudioThreadGuard` no-alloc/no-lock contract test, the per-block CPU-budget
regression assertion in the stress golden -- C10), ADR-003 (the mandatory 2x-oversampled
per-voice ladder and its required CPU-budget gate), and ADR-006 (the fixed preallocated pool,
the active-voice list so idle slots cost nothing, and worst-case CPU framing). It cites the
design spec §5 (bless/tolerance rules) and `docs/research/10-dsp-modeling-techniques.md` §5.1
(2x oversampling sweet spot).

## Options considered

### Option A: Single-threaded voice rendering inside `process` (recommended)

The audio thread iterates the active-voice list (ADR-006) and renders every voice for the
current `kRenderBlock` chunk in one thread, accumulating into the block mix, then runs shared
FX (Chorus/Delay/Drive) -- all within the one `noexcept process` call. No worker threads, no
cross-thread handoff, no audio-thread waits.

- Pros: trivially satisfies the no-lock invariant -- there is nothing to synchronize, so the
  `AudioThreadGuard` lock sentinel (ADR-001 C4) can never trip from voice rendering.
  Deterministic by construction: voices render and sum in fixed index order, so the FP
  reduction order is fixed and bit-exact on macOS arm64 and tolerance-stable on Linux (ADR-001
  C8, C9; spec §5). No wakeup latency, no thread-pool warm-up, no priority-inversion or
  denormal-stall-on-a-worker surprises. Simplest to write, read, test, and fuzz; one code path
  for mono, unison, and poly. The cost model stays the exact linear "active voices x per-voice
  cost + FX" that ADR-006 already budgets, so the ADR-001 C10 / ADR-003 CPU-budget gate
  measures precisely what ships.
- Cons: per-voice cost cannot be amortized across cores, so the worst case (max poly x max
  unison x 2x oversampling) lands entirely on one audio-callback thread; if that exceeds the
  buffer deadline at the budgeted buffer/sample-rate, single-threading alone cannot rescue it
  and the only levers are cheaper DSP, a lower voice cap, or (later) Option B. This is the one
  real risk and it is exactly what the CPU-budget gate exists to catch.

### Option B: Multi-threaded voice rendering via a real-time worker pool

A fixed pool of high-priority worker threads (sized in `prepare`, ADR-001 C2) splits the
active voices; the audio thread fans out per-chunk work, the workers render disjoint voice
subsets into per-worker scratch, and the audio thread reduces the partials in a fixed order.
Coordination must be lock-free and bounded (e.g. atomic counters + spin with a bounded backoff,
never a blocking mutex/condvar) to respect the lock.

- Pros: spreads the heaviest block (2x-oversampled ladders) across cores, raising the
  achievable max poly x unison before the buffer deadline; the only option that actually buys
  more simultaneous voices when single-threaded CPU is the binding constraint.
- Cons: directly stresses the no-lock owner lock -- any blocking primitive violates it, and the
  lock-free alternative (bounded spin / wait-free SPSC handoff) is subtle, easy to regress, and
  hard to prove under the `AudioThreadGuard` (ADR-001 C4). It threatens bless: nondeterministic
  worker completion means the audio thread MUST reduce partials in a fixed voice-index order to
  keep FP bit-exactness on macOS arm64 (spec §5; ADR-001 C9), which serializes the final sum
  and erodes the speedup. Worker threads can themselves hit denormal stalls or scheduler
  preemption, reintroducing exactly the per-callback jitter ADR-003 F-12 and ADR-001 C11 work
  to remove. It is the most complex path to write, fuzz, and keep deterministic across the two
  gate platforms, and the per-block CPU-budget regression assertion (ADR-001 C10) becomes
  harder to interpret. None of this is justified until the CPU-budget gate proves single-thread
  cannot hold the deadline.

### How this resolves

There is no contradiction between prior ADRs to reconcile -- this is a genuine gap. The forces
decide it cleanly. The no-lock invariant and the bit-exact/tolerance-banded bless gate both
push hard toward Option A: Option A satisfies them by construction, whereas Option B can only
satisfy them by adding lock-free machinery AND forcing a fixed-order final reduction that
removes most of its own benefit. ADR-006 already frames worst-case CPU as a single linear sum
and ADR-001 already mandates a per-block CPU-budget regression assertion, so we have a
mechanical trigger to know if and when single-threading is insufficient -- we are not guessing.
Multi-threading is a real-but-unproven optimization with a poor risk/reward against the locks
for v1; we defer it behind that gate rather than pay its complexity speculatively.

## Decision

For v1, **per-voice rendering is single-threaded inside `process`**. The audio-callback thread
walks the ADR-006 active-voice list and renders every active voice for each `kRenderBlock`
chunk (ADR-001) in one thread, accumulating into the block mix in fixed voice-index order,
then runs the shared FX chain -- all inside the single `void process(const BlockContext&)
noexcept` call (ADR-001 Decision). No worker threads, no thread pool, no cross-thread voice
handoff, and therefore no synchronization primitive of any kind participates in voice
rendering.

This is chosen because:

1. **It satisfies the no-lock owner lock by construction** (ADR-001 §"Real-time invariants",
   C4): with no second thread there is nothing to lock, so the `AudioThreadGuard` lock sentinel
   cannot trip from the voice loop and the invariant is structural, not hoped-for.
2. **It is deterministic and bless-stable by construction.** Fixed voice-index render-and-sum
   order fixes the (non-associative) floating-point reduction order, so output is FP bit-exact
   on macOS arm64 and within the tolerance band on Linux x64 (design spec §5; ADR-001 C8, C9;
   ADR-006 C18). A thread pool would force a fixed-order reduction anyway to keep this, negating
   most of its win.
3. **It matches the already-budgeted cost model.** ADR-006's worst-case CPU is exactly "active
   voices x per-voice oversampled cost + shared FX" on one thread; single-threaded rendering IS
   that model, so the ADR-001 C10 per-block CPU-budget regression assertion and the ADR-003
   max-poly + max-unison + 2x CPU-budget gate measure precisely the path that ships.
4. **It is the simplest path to write, fuzz, and keep bit-stable** across the macOS bless box
   and the Linux hard gate -- one render path for mono, unison, and poly, exercised identically
   by tests and the shell (ADR-001 §"Headless testing").

**Reconsideration trigger (normative).** Multi-threaded voice rendering (Option B) is revisited
ONLY if the CPU-budget gate -- the ADR-003 max-poly + max-unison at 2x benchmark and the
ADR-001 C10 per-block CPU-budget regression assertion, measured with headroom at the budgeted
buffer size / sample rate on BOTH the macOS arm64 reference and the Linux x64 hard gate -- FAILS
and the failure cannot be recovered by cheaper per-voice DSP or a lower voice cap. Any such move
to multi-threading must come via a **superseding ADR** that re-proves the no-lock invariant
(lock-free, bounded, no audio-thread blocking; `AudioThreadGuard`-clean per ADR-001 C4) and
re-proves determinism/bless (fixed-order partial reduction; FP bit-exact on macOS arm64 and
tolerance-banded on Linux per spec §5), and that triggers a rebless of the golden corpora.

This stays strictly within the owner locks (RT-safe no-lock; bit-exact macOS bless +
tolerance-banded Linux gate; JUCE/C++20/CMake; poly/unison + oversampling + drift scope) and
introduces no new user-facing behavior.

## Consequences

Commits us to:

- A single-threaded voice loop inside the one `noexcept process` call, summing voices in fixed
  index order before shared FX; the `AudioThreadGuard` no-alloc/no-lock contract test (ADR-001
  C3, C4) covers it with nothing extra to special-case.
- Treating the ADR-003 / ADR-001 C10 CPU-budget gate as the single, mechanical decision point
  for voice-render parallelism: it must benchmark max poly x max unison at 2x with headroom at
  the budgeted buffer/sample-rate on macOS arm64 AND Linux x64 before the engine locks, and the
  per-block CPU-budget regression assertion must keep guarding it in CI.
- Carrying the entire worst-case voice load on the audio-callback thread; the voice cap
  (`kMaxVoices`, ADR-006) and per-voice DSP cost are the available headroom levers in v1.

Makes harder / forecloses:

- No multi-core amortization of voice cost in v1; if a future patch/host demands more
  simultaneous voices than one thread can render within the deadline, the answer is a superseding
  ADR (Option B) plus a rebless, not a runtime toggle.
- Adopting threads later is deliberately costly: goldens are blessed against the single-threaded
  fixed-order sum, so a threaded reduction that changes FP ordering forces a rebless even if it
  is numerically "better".

Owner ratification item: none. This ADR resolves an internal implementation gap entirely within
the locked RT-safety, bless/gate, and feature-scope decisions; it adds no user-expectation or
scope risk. The latent risk that single-thread CPU is insufficient at extreme poly x unison is
not new -- it is the pre-existing CPU-budget gate already required by ADR-001 (C10) and ADR-003.

## Contract

Normative behavior the backlog implements verbatim. "active-voice list" is per ADR-006;
"`process`" and "`kRenderBlock`" are per ADR-001.

| ID | Condition | Required behavior |
| --- | --- | --- |
| VT-01 | Voice rendering thread | All active voices for a `kRenderBlock` chunk render on the audio-callback thread inside the single `process(const BlockContext&) noexcept` call. NO worker threads, NO thread pool, NO cross-thread voice handoff in v1. |
| VT-02 | Voice mix order | Active voices are summed into the block mix in fixed voice-index order (deterministic), before the shared FX chain. |
| VT-03 | Synchronization | Voice rendering uses NO mutex, condition variable, futures, atomics-as-locks, or any blocking/spinning cross-thread primitive. The `AudioThreadGuard` lock sentinel (ADR-001 C4) must never trip from the voice loop. |
| VT-04 | Determinism / bless | Identical input produces FP bit-exact output on macOS arm64 and tolerance-banded output on Linux x64 (design spec §5; ADR-001 C8, C9; ADR-006 C18). Fixed render/sum order is the mechanism. |
| VT-05 | Cost model | Worst-case CPU = active voices x per-voice 2x-oversampled cost + shared FX, on one thread (ADR-006; ADR-003 F-09). The ADR-001 C10 per-block CPU-budget regression assertion guards this in CI. |
| VT-06 | Reconsideration trigger | Multi-threaded voice rendering is reconsidered ONLY if the ADR-003 max-poly + max-unison @ 2x CPU-budget gate (with headroom, on macOS arm64 AND Linux x64) FAILS and cannot be recovered by cheaper DSP or a lower voice cap. |
| VT-07 | Path to change | Any move to multi-threaded voice rendering requires a SUPERSEDING ADR that re-proves the no-lock invariant (lock-free, bounded, audio-thread non-blocking) and determinism/bless (fixed-order partial reduction), and triggers a rebless of the golden corpora. |
