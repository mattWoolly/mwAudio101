<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 003: IR3109 4-pole VCF modeling method

Status: accepted
Date: 2026-06-17

## Context

The IR3109 is THE tone-defining module of mwAudio101. We must choose how to model its
four-pole OTA low-pass so it reproduces three SH-101 signatures: diode-clipped resonance,
output-side Q behavior, and clean self-oscillation, while staying stable under fast
cutoff/resonance modulation. The candidate backbones are:

- **Zavalishin TPT / zero-delay-feedback (ZDF)** — four trapezoidal one-poles with a
  prewarped gain `g = tan(pi*fc/fs)` and a single delay-free global feedback `k`, with the
  in-loop `tanh` resolved by an iterative (Newton / fixed-point) solve
  (docs/research/10-dsp-modeling-techniques.md §3.2, §3.3, §3.7).
- **Huovilainen** — four cascaded one-pole IIRs with `tanh` embedded at each stage and on
  the feedback, forward-Euler, run at 2x oversampling, with a `+0.5`-sample
  (two-sample-average) feedback-phase compensation and a tuning/resonance-compensation
  table (docs/research/10-dsp-modeling-techniques.md §3.6, §3.8).
- A hybrid.

This decision sits under, and is bounded by, several owner-locked decisions
(plan/ORCHESTRATION.md "Decisions locked with the project owner"), which it re-affirms and
does not reverse:

- **Circuit-accurate analog modeling from documented circuit behavior**, with NO
  physical-unit oracle (recordings are secondary, local-only cross-checks).
- **Real-time safe: no heap allocation and no locks on the audio thread.**
- **macOS arm64 = reference/bless and bit-exact**; Linux x64 = co-required hard gate;
  Windows x64 = goal.
- **JUCE / C++20 / CMake**, GPL-3.0-or-later.
- Feature scope includes **poly/unison, oversampling, and per-voice drift**, so the filter
  runs as many independent per-voice instances every sample.

A pervasive constraint from the research: there is no IR3109-specific VA model in the
literature; both backbones are Moog-transistor-ladder theory applied to the IR3109 **by
analogy** (docs/research/10-dsp-modeling-techniques.md §9.4), and the project holds NO
measured Bode plot, resonance-vs-control curve, or self-oscillation amplitude
(docs/research/03-filter-ir3109.md §9.1). The two genuine SH-101 circuit facts that drive
fidelity here are FEEDBACK-PATH and OUTPUT-PATH features, not in-ladder features
(docs/research/03-filter-ir3109.md §4.2, §4.3).

## Options considered

### Persona: authenticity-DSP — "Huovilainen-core, SH-101-topology"

Advocated a Huovilainen cascade of four one-pole OTA cells with `tanh` at each
transconductor (g = 1 - exp(-2*pi*fc/fs)), re-wired to the SH-101's documented topology:
explicit global inverting feedback of the 4th-stage output, a **diode-clamp shaper in the
feedback path** as the primary resonance limiter, **output-side make-up gain** summed into
VCA drive, the `+0.5`-sample feedback-phase compensation, and 2x oversampling. TPT is kept
only as an offline linear reference oracle.

- Pros: structure matches the documented external four-OTA cascade 1:1
  (docs/research/03-filter-ir3109.md §2.2); exposes explicit, separable feedback and output
  nodes so the diode clamp and Q comp go exactly where the hardware puts them (§4.2, §4.3);
  avoids the explicitly-forbidden single in-ladder OTA soft-clip (§4.2, §9.3); fixed,
  data-independent per-sample cost (no Newton iteration count variance).
- Cons: forward Euler is less numerically pristine than TPT and needs the compensation
  table; the explicit one-sample loop needs the half-sample trick and the table must be
  regenerated per sample rate; 2x oversampling roughly doubles per-voice filter CPU; the
  per-stage saturation knee is a clone-derived/AS3109 tunable assumption (§5.2, §9.2).

### Persona: Performance (CPU budget for poly/unison, oversampling, stability)

Advocated the same Huovilainen cascade at fixed 2x oversampling as the per-voice production
filter, on the grounds that poly + unison + per-voice drift means dozens of independent
nonlinear 4-pole filters per sample, and the dominant risk is data-dependent solver cost.
Huovilainen's forward-Euler gives a fixed, branch-light, SIMD-friendly cost (4 one-poles +
~5 shared `tanh`/sample) with no iteration loop, so worst case = average case — critical
when patches get hot (high resonance, self-oscillation). The two tone-defining SH-101
features land OUTSIDE the loop (output-side scalar Q comp; one memoryless feedback
waveshaper), so they cost almost nothing and force no implicit solver.

- Pros: provisioned worst-case cost with no CPU spikes; deterministic identical results
  across the macOS reference and Linux hard-gate (no convergence-tolerance divergence);
  only 2x OS, the documented sweet spot (docs/research/10-dsp-modeling-techniques.md §5.1).
- Cons: 2x oversampling is mandatory, not optional, and is a real per-voice tax; cutoff/Q
  are only approximately uncoupled via the half-sample-delay + table, not exactly like TPT;
  gives up textbook ZDF accuracy a fidelity-first reviewer might want for the tone-defining
  module.

### Persona: stability-QA (numerical stability under fast modulation, self-osc amplitude, denormals)

Advocated a **TPT/ZDF skeleton** because ZDF has no loop delay, so cutoff and resonance stay
uncoupled by construction under fast LFO/envelope sweeps (docs/research/10-dsp-modeling-techniques.md
§3.5), avoiding the detune/zipper/Q-creep the half-sample-delay Huovilainen path fights with
its table (§3.8). Crucially, it agreed the SH-101 limiter is a diode clamp in the feedback
path and that Q comp is output-side (docs/research/03-filter-ir3109.md §4.2, §4.3), and
argued the self-oscillation amplitude should be the fixed point of that explicit feedback
clamp rather than a knife-edge `k=4` balance — bounded by construction and insensitive to
coefficient rounding. To stay real-time-safe it proposed a SINGLE damped fixed-point step
(a guaranteed contraction), not iterate-to-tolerance Newton, plus FTZ/DAZ, an anti-denormal
bias on every integrator state, and `fc` clamping.

- Pros: cutoff/Q uncoupled under fast modulation; self-oscillation amplitude bounded by the
  explicit diode-clamp fixed point; trapezoidal integrators exactly preserve stability into
  the unit disk (§3.2); `tan` prewarp keeps cutoff accurate across 10 Hz-20 kHz with no
  per-rate table.
- Cons: the delay-free loop with an embedded nonlinearity plus the feedback-clamp
  interaction is the subtlest code in the synth and the hardest to make bit-identical across
  platforms/compilers for the bless target; a single fixed-point step is itself an
  approximation whose damping factor needs validation against a resonance-vs-k curve we have
  no oracle for; `tan()` is costlier than `1-exp()` and needs Nyquist guarding; still needs
  2x OS, so it does not win on raw CPU.

### The split and how it resolved

The panel split 2-1 on the structural backbone: authenticity-DSP and Performance both chose
the Huovilainen cascade as the shipping engine; stability-QA chose a TPT/ZDF skeleton. All
three agreed unanimously on the SH-101-specific topology: a diode-clamp limiter in the
feedback path (not in-ladder OTA soft-clip) and an output-side scalar Q comp, at 2x
oversampling.

It resolves in favor of the **Huovilainen cascade**, decided by the owner locks rather than
by raw DSP elegance:

1. **Real-time safety / "no surprises on the audio thread"** is an owner lock and a hard
   gate. Forward-Euler Huovilainen has provably fixed per-sample cost. The TPT path is
   real-time-safe ONLY if reduced to a single fixed-point step; but stability-QA itself
   conceded that step is an approximation requiring validation against a curve we have no
   oracle for. Once the implicit solve is degraded to one fixed step, TPT loses its main
   theoretical-accuracy advantage while keeping the harder-to-verify code.
2. **macOS arm64 bit-exact bless** is an owner lock. A pure forward-Euler cascade with
   versioned `tanh` approximation, table, and decimator coefficients is far easier to make
   bit-identical across compilers/platforms than an implicit-solve-plus-feedback-clamp
   interaction with a damping constant.
3. **Poly/unison + per-voice drift** is in scope, so per-voice cost predictability (worst
   case = average case) directly protects the buffer deadline. This is Performance's
   decisive point and it is unrebutted.
4. The fidelity argument is symmetric: with NO measured Bode plot or constant-Q oracle
   (docs/research/03-filter-ir3109.md §9.1; docs/research/10-dsp-modeling-techniques.md
   §9.4), TPT's "exact uncoupling" is accuracy we cannot bless against, whereas the residual
   Huovilainen tuning error (<10% below Fs/4 at 2x) is below the threshold we could ever
   verify and is absorbable into the calibration table (§3.8).

Critiques adopted from stability-QA (folded into the chosen engine):

- **Diode clamp as the amplitude governor, not a knife-edge `k`.** Model self-oscillation
  amplitude as the fixed point of the explicit feedback diode-clamp shaper so it is bounded
  by construction and insensitive to coefficient rounding under fast resonance slews
  (docs/research/03-filter-ir3109.md §4.2; docs/research/10-dsp-modeling-techniques.md §9.4).
- **Denormal handling.** Enable FTZ + DAZ in the audio-thread CSR and add a tiny
  anti-denormal bias (or periodic flush) to every integrator state; assert no subnormals in
  a drive-to-silence test. (Self-oscillation decay tails are a classic denormal trap.)
- **`fc` clamping** to a stable range (e.g. [10 Hz, ~0.45*fs_os]) and control-rate slew of
  cutoff/resonance CV to avoid zipper without adding loop delay.

Critiques NOT adopted: the TPT skeleton itself (loses real-time determinism and
bit-exactness margin for accuracy we cannot bless), and the in-loop fixed-point `tanh`
solve. The cutoff/Q-uncoupling concern stability-QA raised against Huovilainen is mitigated,
not ignored, by the half-sample compensation + table at 2x.

## Decision

Ship the **Huovilainen-core cascade wired to the SH-101's documented topology** as the
real-time per-voice filter:

1. **Four cascaded one-pole OTA cells**, each a `tanh` transconductor feeding a one-pole
   integrator with small-signal coefficient `g = 1 - exp(-2*pi*fc/fs)`, `tanh` embedded at
   each stage per Huovilainen (docs/research/10-dsp-modeling-techniques.md §3.6, §4). This
   matches the documented external four-OTA cascade of the IR3109
   (docs/research/03-filter-ir3109.md §2.1, §2.2).
2. **Resonance = global INVERTING feedback** of the 4th-stage output to the input, with the
   `+0.5`-sample (two-sample-average) feedback-phase compensation to hold ~180 deg at cutoff
   up to Fs/4 (docs/research/10-dsp-modeling-techniques.md §3.8;
   docs/research/03-filter-ir3109.md §2.2, §4.1). Self-oscillation emerges as loop gain
   reaches unity (`k -> 4` in the normalized model), producing a near-pure sine
   (docs/research/10-dsp-modeling-techniques.md §3.4; docs/research/03-filter-ir3109.md §4.4).
3. **Primary resonance limiter = a diode-clamp shaper in the FEEDBACK path**, NOT an
   in-ladder OTA soft-clip. The clamp reduces loop level as soon as it conducts and is the
   amplitude governor for self-oscillation (docs/research/03-filter-ir3109.md §4.2, §9.3).
4. **Q compensation = OUTPUT-side make-up gain** that rises with the resonance control and
   is summed into the VCA drive, NOT a boost of the filter input
   (docs/research/03-filter-ir3109.md §4.3).
5. **2x oversampling (88.2 kHz @ 44.1)** on the nonlinear filter+VCA path, decimated by a
   polyphase IIR halfband; oscillators stay at base rate
   (docs/research/10-dsp-modeling-techniques.md §5.1, §5.2).
6. **Real-time-safe construction:** forward-Euler (no Newton, no data-dependent iteration);
   a single shared fast rational/polynomial `tanh` approximation (~5 evals/sample); the
   cutoff/resonance/feedback-phase compensation table precomputed at `prepareToPlay` into
   preallocated state (sample-rate-dependent), read-only at audio rate; FTZ/DAZ +
   anti-denormal bias; clamped prewarped `g` and feedback `k`.
7. **TPT/ZDF is retained ONLY as the offline linear reference oracle** for the small-signal
   response and as the basis of the macOS arm64 bit-exact bless cross-check
   (docs/research/10-dsp-modeling-techniques.md §3.2). It is never the shipping engine.

Per-stage buffer-drive asymmetry (stage 4 hardest) and the saturation knee are AS3109
clone-derived and are treated as tunable, `(PI)`-tagged calibration constants, not delivered
facts (docs/research/03-filter-ir3109.md §5.2, §9.2). All ladder theory is applied as
theory-by-analogy to the IR3109 (docs/research/10-dsp-modeling-techniques.md §9.4); the
`k=4` threshold is a normalized loop-gain value, not the SH-101's physical resonance-pot
value (§3.4 residual risk).

## Consequences

Commits us to:

- A bespoke filter engine (diode-clamp feedback shaper, output-side Q comp, half-sample
  feedback compensation, comp table) that must be unit-tested against the linear TPT
  reference oracle to catch silent topology errors, since we have no physical-unit oracle.
- A mandatory 2x-oversampled nonlinear filter+VCA path with a polyphase IIR halfband
  decimator; this is the heaviest per-voice DSP block and multiplies under poly/unison. A
  CPU-budget gate is required: benchmark max-poly + max-unison at 2x on macOS arm64
  (reference) and Linux x64 (hard gate) with headroom at a 64-sample buffer / 48 kHz before
  locking the engine. Higher oversampling (4x) is gated behind a quality switch for
  offline/bless renders only.
- Versioned, frozen constants (the `tanh` approximation, decimator coefficients, and
  compensation-table contents) as the basis of macOS arm64 bit-exactness; the table must be
  regenerated per sample rate at `prepareToPlay`.
- A per-voice preallocated filter-state layout (no heap, no locks on the audio thread),
  FTZ/DAZ set on the audio thread, and anti-denormal bias on every integrator state.

Makes harder / forecloses:

- Exact ZDF constant-Q uncoupling under fast modulation: we accept approximate uncoupling
  via the half-sample-delay + comp table; residual Q-rise/detune may appear approaching
  Fs/4 even at 2x. Mitigated by the table and `fc` clamping, but it is a known weak spot.
- Cleanly retuning the saturation "voice" independently of structure: `tanh` is embedded in
  the topology, so the knee/`2Vt` scaling is less separable than in a linear-filter +
  bolted-on-nonlinearity design.
- Switching engines later is costly: golden corpora and the bit-exact bless baseline will be
  generated against this engine, so a future change requires a superseding ADR and rebless.

Owner ratification item: none. This ADR stays within the locked decisions (circuit-accurate
documented-behavior modeling, real-time-safe, bit-exact macOS bless, poly/unison scope) and
re-affirms them. The clone-derived per-stage saturation constants and the absence of any
measured response curve are pre-existing OPEN VALIDATION GAPS already disclosed in the
research (docs/research/03-filter-ir3109.md §9.1, §9.6), not new scope or user-expectation
risk introduced here.

## Contract

Normative behavior the backlog implements verbatim. "OS" = oversampled rate (2x base).

| ID | Condition | Required behavior |
| --- | --- | --- |
| F-01 | Filter topology | Four cascaded one-pole OTA cells, each with `tanh` at the transconductor input; small-signal coefficient `g = 1 - exp(-2*pi*fc/fs_os)`. 24 dB/oct total. |
| F-02 | Integration method | Forward Euler. NO Newton / iterate-to-tolerance solver. Per-sample work is fixed and data-independent. |
| F-03 | Resonance feedback | Global INVERTING feedback of stage-4 output to stage-1 input, with `+0.5`-sample (two-sample-average) phase compensation. |
| F-04 | Resonance limiter | A diode-clamp waveshaper on the FEEDBACK signal is the primary amplitude limiter. NO in-ladder OTA soft-clip as the primary limiter. |
| F-05 | Self-oscillation | As feedback gain reaches unity (normalized `k -> 4`) the filter self-oscillates; output is a near-pure sine whose amplitude is the fixed point of the F-04 diode clamp (bounded by construction, not by a knife-edge `k`). |
| F-06 | Q compensation | OUTPUT-side make-up gain rising with the resonance control, summed into VCA drive. The filter INPUT level is NOT boosted with resonance. |
| F-07 | Bass droop | DC gain droops as `H(0) = 1/(1+k)` (inherent to the cascade). |
| F-08 | Cutoff range / scaling | `fc` exponential in summed CV, 1 V/oct, clamped to [10 Hz, ~0.45*fs_os]; never exceeds the prewarp/stability guard. |
| F-09 | Oversampling | Nonlinear filter+VCA path runs at fixed 2x; decimated by a polyphase IIR halfband. Oscillators run at base rate. 4x is offline/bless-quality only, behind a quality switch. |
| F-10 | `tanh` evaluation | A single shared fast rational/polynomial `tanh` approximation (~5 evals/sample), no `std::tanh` on the audio thread. |
| F-11 | Real-time safety | Compensation/tuning table precomputed at `prepareToPlay` into preallocated state, read-only at audio rate; per-voice filter state preallocated. No heap allocation, no locks on the audio thread. |
| F-12 | Denormals | FTZ + DAZ enabled on the audio thread; anti-denormal bias (or periodic flush) on every integrator state. A drive-to-silence test asserts no subnormals. |
| F-13 | Reference oracle | A TPT/ZDF linear model is the offline reference for small-signal response and the macOS arm64 bit-exact bless cross-check. It is NOT the shipping engine. |
| F-14 | Determinism / bless | The `tanh` approximation, decimator coefficients, and compensation-table contents are versioned, frozen constants; results are bit-identical on macOS arm64. |
| F-15 | Tunable constants | Per-stage buffer-drive asymmetry and the saturation knee (`2Vt` scaling) are `(PI)`-tagged calibration constants in the central table, not hard facts. |
