<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# DSP Modeling Techniques (adopted theory)

## 1. Scope and intent

This document records the canonical digital-signal-processing (DSP) literature that
mwAudio101 **adopts** as deliberate, citable engineering for emulating an SH-101-class
analog monosynth. It is an ADR-style "theory we adopt" reference: the algorithms and
equations here are established virtual-analog (VA) results, distinct from the
instrument-specific *circuit facts* (IR3109 filter, BA662 VCA, 4013 divider sub-osc),
which are documented elsewhere and are clone-derived/reverse-engineered. Where a circuit
fact appears in this document it is labeled as such and kept separate from the adopted
DSP theory [Stilson-Smith 1996, peer-reviewed; Huovilainen 2004, peer-reviewed;
Zavalishin, established VA theory] [R1][R6][R7].

The adopted stack is:

- Anti-aliased oscillators via residual-correction of trivial (modulo-counter)
  waveforms: BLIT/BLEP/minBLEP, the closed-form PolyBLEP, and the alias-reducing DPW
  family, with PolyBLAMP/BLAMP for first-derivative (triangle) discontinuities.
- The OTA 4-pole ladder modeled either as a zero-delay-feedback / Topology-Preserving-
  Transform (TPT) cascade with an implicitly-solved global feedback, or as Huovilainen's
  cascade of one-pole IIRs with embedded `tanh`.
- `tanh` saturation as the physically-correct memoryless nonlinearity for the OTA filter
  cells and the OTA VCA.
- Modest oversampling (2x typical for the nonlinear filter) with an IIR-vs-FIR resampling
  tradeoff.
- White noise from a fast integer PRNG scaled to `[-1,1)`.

> **Honest-labeling note.** None of the figures below are physical-unit measurements of
> a real SH-101. This project has made the decision to take **no bench measurements**;
> anything that would require a Bode plot, oscilloscope ADSR capture, or harmonic
> spectrum is an **OPEN VALIDATION GAP**, not a delivered fact. The numbers here are
> *literature constants and equations* for the algorithms we adopt.

## 2. Anti-aliased oscillator theory

### 2.1 The residual-correction paradigm (BLIT / BLEP / minBLEP)

BLIT/BLEP/minBLEP correct a trivial (aliased) waveform by inserting a band-limited
*residual* at each discontinuity, rather than synthesizing band-limited harmonics
directly [Brandt 2001, peer-reviewed; Stilson-Smith 1996, peer-reviewed] [R2][R1]. Brandt
(2001) shows hard-sync, sawtooth and square are synthesized by running a naive oscillator
and replacing each step discontinuity with a band-limited step: "don't just jump to the
new level, mix in a MinBLEP instead" [R2]. Terms:

- **BLIT** = a windowed-sinc band-limited impulse train (Stilson-Smith 1996) [R1].
- **BLEP** = the integral of a BLIT, i.e. a band-limited step whose final value is forced
  to 1; placing BLEPs directly avoids the leaky-integrator and DC-offset problems of
  integrating BLITs [R2].

This residual paradigm is the adopted theory for the saw and pulse cores [R2].

### 2.2 minBLEP (minimum-phase band-limited step)

minBLEP is the minimum-phase band-limited step: it concentrates the correction energy at
and after the discontinuity, eliminating both lookahead and a separate integration stage
[Brandt 2001, peer-reviewed] [R2]. It is formed by integrating the minimum-phase
band-limited impulse; "these improvements can be combined, for no lookahead and no
integration stage" [R2]. minBLEP applies when the waveform has **C1 continuity** — first
and higher derivatives essentially continuous across the discontinuity. Sawtooth, square,
and hard-synced saw/square qualify; triangle is only C2 and needs a band-limited ramp
(BLAMP); hard-synced sine has no `Cn` continuity and can only be approximated, with
residual aliasing falling ~6 dB/oct per corrected derivative [Brandt 2001, Sec. 6.3,
peer-reviewed] [R2]. Per-sample cost scales approximately with frequency (table lookups
scale with `f1`) [R2].

### 2.3 PolyBLEP (adopted default)

PolyBLEP is a **table-free, closed-form, two-segment polynomial residual** and is the
recommended low-cost choice for the saw / pulse / variable-PWM cores [practitioner
reference, verified against an independent implementation] [R3][R4]. Canonical closed form,
with `t` = phase in `[0,1)` and `dt = freq/fs`:

- For `t < dt`, with `t' = t/dt`: `blep = 2*t' - t'^2 - 1`.
- For `t > 1 - dt`, with `t'' = (t-1)/dt`: `blep = t''^2 + 2*t'' + 1`.
- Otherwise `0`.

Application:

- **Sawtooth:** `value = (2*t - 1) - polyBLEP(t)` [R4].
- **Square / pulse:** add `polyBLEP` at the rising edge, subtract `polyBLEP` at the
  falling edge. For 50 % duty the falling edge is at phase-fraction `(t + 0.5)`; for
  **variable PWM** the falling-edge correction is placed at the duty-cycle phase, so two
  independent BLEPs per period give a clean variable-width pulse [R3][R4].

PolyBLEP avoids oversampling because the fractional delay `dt` positions the discontinuity
exactly [R3][R4]. The leading-segment formula was verified verbatim against Martin Finke's
implementation (`t/=dt; return t+t - t*t - 1.0;`) [R4].

### 2.4 PolyBLAMP / BLAMP (slope-discontinuity correction)

PolyBLAMP / BLAMP (band-limited ramp) corrects **first-derivative (slope)** discontinuities
and is needed for triangle waveforms [Valimaki-Pekonen-Nam 2012, peer-reviewed; Brandt
2001, peer-reviewed] [R5][R2]. Valimaki-Pekonen-Nam (JASA 2012) treat the triangle as the
integral of a square; turning points (first-derivative discontinuities) are corrected with
integrated polynomial / B-spline ramp functions positioned by fractional delay, with the
triangle's turning-point fractional delays obtained at `P = 0.5` [R5]. For a
divider-derived sub-osc square / 25 %-pulse the edges are true level steps, so
(Poly)BLEP applies and BLAMP is **not** needed; BLAMP is the tool only if a triangle or
other slope-discontinuous shape is added [R5][R2].

### 2.5 DPW (Differentiated Polynomial Waveform) — fallback tier

DPW is the cheapest alias-reducing alternative: square the bipolar modulo-counter (giving a
piecewise-parabolic signal = the integral of the saw), then differentiate to recover a
sawtooth with attenuated aliasing [Valimaki 2005 / Valimaki-Huovilainen 2007,
peer-reviewed] [R8][R9]. Methods classify into band-limited (BLIT/BLEP/minBLEP/wavetable),
quasi-band-limited, and alias-reducing (DPW). DPW is the simplest (a few multiplies/adds
per sample) but its aliasing worsens at high fundamentals; DPW2 benefits from ~2x
oversampling/decimation prior to differentiation [R8][R9]. **PolyBLEP is generally
preferred over DPW when the discontinuity timing is known** — which it is in our
phase-accumulator oscillators — so DPW is adopted only as a low-CPU fallback tier [R8][R9].

## 3. The VA filter problem and adopted ladder models

### 3.1 The delay-free-loop problem

Naively cascading discretized integrators with a global feedback creates a delay-free
(instantaneous) loop that is not directly implementable; this is the central VA filter
problem [Stilson-Smith 1996, peer-reviewed; Zavalishin, established VA theory] [R1][R6].
Stilson-Smith: "The well known bilinear transform method ... yields a delay-free loop and
cannot be used without introducing an ad-hoc delay" [R1]. Zavalishin frames the same
issue: such one-pole filters "have a delay-free path from input to output ... when placed
in the feedback loop, the system [is] unrealizable" [R6]. Two adopted resolutions:

- (a) insert a unit delay in the loop (simple, but it detunes resonance/cutoff and couples
  the controls), or
- (b) solve the loop implicitly (ZDF / TPT).

### 3.2 Adopted high-fidelity approach: Zavalishin TPT / zero-delay feedback

The adopted high-fidelity filter is Zavalishin's **Topology-Preserving Transform (TPT)**
with zero-delay feedback: replace each analog integrator with a trapezoidal (bilinear)
integrator and solve the resulting delay-free loop instantaneously [Zavalishin, established
VA theory] [R6]. Zavalishin: "We will refer to the trapezoidal integrator replacement
method as the topology-preserving transform (TPT)." Trapezoidal integration *is* the
bilinear transform; it "warps the frequency range `[0,+inf)` into the zero-to-Nyquist
range, but otherwise doesn't change the frequency response at all," and exactly preserves
stability (left s-half-plane maps to the unit disk), unlike naive integrator replacement
which "overpreserves" stability [R6]. The 1-pole TPT block uses an integrator gain `g`; the
4-pole ladder is four identical 1-pole TPT lowpasses with one global feedback `k`, the loop
resolved without a unit delay [R6].

### 3.3 Cutoff prewarping

Cutoff must be **prewarped** to compensate the bilinear frequency warp; the integrator gain
is `g = tan(omega_c*T/2) = tan(pi*fc/fs)` [Zavalishin, established VA theory] [R6]. The
bilinear warp is `omega_a = (2/T)*tan(omega_d*T/2)`; using the raw `omega_c` directly
misplaces the cutoff, so the gain is prewarped to `g = tan(omega_c*T/2)` [R6]. Huovilainen's
alternative one-pole coefficient is the scaled impulse-invariant
`g = 1 - exp(-2*pi*fc/fs)` [Huovilainen 2004, eq. 21, peer-reviewed] [R7].

> **Citation-drift correction.** In the public Zavalishin PDF (rev 1.0.3) the ladder
> self-oscillation and `H(0)` results live in **Chapter 4 (eq. 4.1)**, not "ch.5 / eq.5.1,"
> and the `tan` prewarp is in sec. 3.8 (derivation) / sec. 3.9 (application). Section
> numbering shifts between revisions; the substance is correct [R6a].

### 3.4 Self-oscillation, resonance and bass droop

The 4-pole OTA/transistor ladder self-oscillates at global feedback **`k = 4`**; raising
`k` moves two poles toward the `jw` axis to produce the resonance peak, and DC gain droops
as `1/(1+k)` [Zavalishin, established VA theory; Stilson-Smith 1996, peer-reviewed]
[R6][R1]. Both primary sources agree. Zavalishin: `H(s) = 1/(k + (1+s)^4)`; "As `k` grows
from 0 to 4 ... two of the poles ... move toward the imaginary axis ... At `k=4` they hit
the imaginary axis ... and the filter becomes unstable [self-oscillates]," and
`H(0) = 1/(1+k)` so resonance drops the bass — "a general issue with ladder filter
designs" [R6]. Stilson-Smith independently: as feedback gain `k` approaches 4 the total
loop gain approaches 1 and the gain at resonance goes to infinity [R1].

> **Residual risk.** `k = 4` is the *dimensionless loop-gain* self-oscillation threshold in
> the normalized Stilson-Smith / Zavalishin model. It is **NOT** the SH-101's physical
> resonance-pot value or feedback-resistor ratio. The SH-101 uses an **IR3109 OTA ladder**,
> not a discrete-transistor Moog ladder, so mapping model `k` to the real circuit is a
> separate, circuit-specific step not verified here [theory/inference, unmeasured] [R6][R1].

### 3.5 Constant-Q (uncoupled controls)

Holding feedback `k` constant while sweeping cutoff gives approximately constant-Q with
uncoupled cutoff/resonance controls — a desirable property to preserve digitally
[Stilson-Smith 1996, peer-reviewed] [R1]. Their root-locus result: sweeping `wc` at fixed
`k` gives root-locus lines at a constant angle from the `jw` axis, so "`k` becomes a Q
control. Thus the Moog VCF has simple, uncoupled controls of corner frequency and
resonance" [R1]. The naive unit-delay-in-loop discretization breaks this uncoupling
(controls interact, Q rises at high freq), motivating either a separation table
(`k_actual = k_desired * Table(p)`) or the implicit ZDF solution [R1].

### 3.6 Adopted alternative: Huovilainen white-box ladder

Huovilainen's white-box model is the alternative adopted approach: **four cascaded one-pole
IIRs with a `tanh` nonlinearity embedded at each stage input and on the resonance feedback,
derived directly from the OTA/transistor differential-pair equation** [Huovilainen 2004,
peer-reviewed] [R7]. He derives the per-stage diff-eq
`dVc/dt = (Ictl/C)*[tanh(Vin/2Vt) - tanh(Vc/2Vt)]` from the differential-pair law
`Idiff = (It1+It2)*tanh((Vt1-Vt2)/2Vt)` (eq. 1), solves via forward Euler, and yields stages
of the form
`ya(n) = ya(n-1) + (Ictl/(C*Fs))*[tanh((x(n) - 4*r*yd(n-1))/2Vt) - Wa(n-1)]` with
`W{} = tanh(y/2Vt)`. Only **five `tanh` evaluations per sample** are needed (shared between
stages). The linearized small-signal coefficient is `g = 1 - exp(-2*pi*fc/fs)`. This
embeds the physical nonlinearity rather than bolting `tanh` onto a linear filter [R7].

### 3.7 Solving nonlinearities in the loop

Nonlinearities embedded in a ZDF/TPT loop create an instantaneously-nonlinear implicit
equation, solved per-sample by fixed-point / Newton iteration (or a pre-warped/clamped
explicit step) [Zavalishin, established VA theory; Huovilainen 2004, peer-reviewed]
[R6][R7]. Zavalishin: `tanh` elements inside the delay-free loop yield implicit equations
solved iteratively (Newton-Raphson) [R6]. Huovilainen sidesteps the implicit solve by using
forward Euler with oversampling, which "brings the Euler solution closer to the ideal
solution" and keeps the cascaded-one-pole structure; he rejects Runge-Kutta because it
needs inter-sample input evaluation, problematic for the resonance feedback path [R7]. The
engine choice is a tradeoff: **TPT + Newton** (accurate, costlier) vs **Huovilainen Euler +
oversampling** (cheaper, needs 2x).

### 3.8 Resonance feedback phase compensation

A unit delay in a naive resonance feedback path detunes the resonant frequency and changes
peak gain vs frequency; a **half-sample delay (two-sample average)** restores ~180-degree
feedback phase up to `Fs/4` [Huovilainen 2004, Sec. 5.2-5.3, peer-reviewed] [R7]. The loop
unit delay adds phase `4*p_stage + 180*f/Fs`, so resonance frequency drifts from cutoff and
peak attenuation is no longer exactly 3 dB. Adding a half-unit delay (realized by averaging
two samples) makes feedback phase ~180 deg at cutoff up to `Fs/4`; with 2x oversampling
(88.2 kHz) the tuning error is <10 % for `f < Fs/4`, and the residual is absorbed into a
single tuning/resonance-compensation table [R7]. This is the adopted resonance-tuning
compensation if the explicit (delayed) loop is used.

## 4. Nonlinearity: tanh as the physical OTA law

`tanh` is the physically-correct memoryless saturation for both the OTA filter cells and
the OTA VCA, because an OTA differential pair has `Idiff = Itail*tanh(Vin/2Vt)`
[Huovilainen 2004, eq. 1, peer-reviewed; AMSynths, clone/module reference] [R7][R10].
Huovilainen eq. 1 gives the differential-pair / OTA transconductor law
`It1 - It2 = (It1 + It2)*tanh((Vt1 - Vt2)/2Vt)`, valid for matched transistors, infinite
beta, and negligible Early effect [R7]. Modeling the VCA gain and filter saturation with
`tanh(x)` (or a fast polynomial/rational approximation) is therefore the physically-grounded
adopted nonlinearity; the `2Vt` scaling sets the saturation knee / "warmth" as a function of
input amplitude [R7].

> **Honest-labeling notes.**
>
> - The thermal voltage `Vt ~ 26 mV` (precisely 25.85 mV at 300 K, `kT/q`) is **standard
>   device physics, NOT a Huovilainen datum** — the paper only names `Vt` as "the so-called
>   thermal voltage of a transistor." Cite the `Vt` value to a semiconductor-physics
>   reference, not to eq. 1 [R7][R11].
> - The SH-101 VCA is the Roland **BA662**, whose internals are
>   **reverse-engineered (Open Music Labs) with no public datasheet** [reverse-engineered]
>   . The AMSynths page confirms it is "a custom made DC controlled variable transconductance
>   amplifier (or OTA)" but does **not** give the transfer equation, nor whether linearizing
>   diodes are used in the SH-101 VCA stage [clone-derived: AMSynths, partly inferred]
>   [R10]. So adopting `tanh` for the BA662 is theory-by-analogy to the generic OTA law, not
>   a measured BA662 transfer.
> - The SH-101 filter is the **IR3109** (IC14 in the schematic-reconciliation freeze); the
>   IR3109 electrical figures circulating in the community (drive currents, ~20 Vpp
>   self-oscillation) are **Alfa AS3109 clone / AMSynths-module figures, NOT
>   original-instrument measurements** [clone-derived: Alfa AS3109 / AMSynths,
>   presumed-equal].

## 5. Oversampling and resampling

### 5.1 Where oversampling is required

Modest oversampling is required for the nonlinear filter (Huovilainen: **2x typical**,
88.2 kHz), while table-based BLEP oscillators internally oversample the residual table
(~64x) and PolyBLEP needs **no oversampling at all** [Huovilainen 2004, peer-reviewed;
Valimaki-Pekonen-Nam 2012, peer-reviewed] [R7][R5]. Huovilainen: "Since there is a
non-linearity, oversampling must be used"; his figures use 2x (88.2 kHz) [R7]. For
oscillators, Valimaki-Pekonen-Nam built LUT-BLEP residual tables with an oversampling factor
of 64 and a Blackman window; PolyBLEP / B-spline correction avoids that table oversampling
because the fractional delay positions the discontinuity, and "oversampling that improves
the accuracy of the correction-function positioning does not have to be considered" [R5].
So the engine can run oscillators at base rate (PolyBLEP) but should oversample the
nonlinear filter/VCA path (>= 2x) [R7][R5].

### 5.2 IIR vs FIR decimator tradeoff

The IIR-vs-FIR resampling tradeoff: **IIR** (elliptic / polyphase halfband) is cheap but
phase-nonlinear; **FIR** (windowed-sinc, linear-phase) is costlier but preserves phase —
relevant when summing multiple oscillator / sub-osc outputs [established multirate DSP,
medium confidence] [R6][R12][R13]. Elliptic/polyphase IIR halfband designs cost ~2.5
multiplies per input sample (>5x cheaper than FIR) but have nonlinear phase that "may cause
distortion"; FIR can be designed linear-phase, trading added latency [R12][R13]. The
phase-linearity benefit being relevant when mixing multiple correlated sources is a sound
engineering inference (consistent with Zavalishin's warning that mixing outputs with phase
deviations "may easily lead to ... undesired results"), **not** a quoted constant from a
single VA paper — treat as a design decision, not a literature constant [R6].

## 6. Noise generation

Sample-rate white noise is generated with a **fast integer PRNG mapped to float `[-1,1)`**,
and a uniform distribution is acceptable for audio white noise [practitioner reference,
established practice, medium confidence] [R14][R15]. Conversion: divide a 32-bit integer by
`2^32` to get `[0,1)`, then scale by `*2 - 1` to get `[-1,1)`; the source folds this into a
single fused multiply-add `int*(2/2^32) - 1`, algebraically identical [R14]. Central-limit
summing of uniforms to approximate Gaussian is "terribly inefficient" for audio and usually
unnecessary, since perceptually uniform noise is indistinguishable as white noise [R14].

> **Honest-labeling correction.** The task brief and our adopted default name **xorshift**.
> The cited primary practitioner source (audiodev.blog) actually **recommends a 64-bit LCG /
> PCG and explicitly states it "wouldn't use xorshift to create white noise"** [R14]. So the
> *general principle* (fast integer PRNG + uniform distribution) is fully supported, but the
> specific **xorshift endorsement is contested by this source** [practitioner reference,
> disputed]. Adopt xorshift only with that caveat, or prefer a 64-bit LCG/PCG per the source.
> The float range is the half-open `[-1,1)`, not closed `[-1,1]` [R14].

## 7. Circuit-fact appendix (clone-derived, NOT DSP theory)

This material is included only to connect adopted DSP theory to the target instrument; it is
**reverse-engineered/clone-derived circuit fact**, kept separate from the citable DSP
literature above.

The SH-101 sub-oscillator divides the VCO with a **4013 dual flip-flop** to make `-1` and
`-2` octave squares, and a **diode-OR** of the two squares yields the 25 %/75 % pulse
[reverse-engineered: Electric Druid, consistent with service manual; medium confidence]
[R16][R17]. Electric Druid: the saw feeds a 4013 dual bistable; each flip-flop divides by 2
(`-1` oct, `-2` oct squares); "a couple of diodes as a diode-OR circuit. If either flip-flop
output is high, the OR's output is high. This gives a signal which is high 75 % of the time
and low 25 % of the time" [R16]. **DSP-modeling implication:** model the divider as exact
phase-locked subharmonic square generators clocked off the master phase, OR them
(`out = Q1 OR Q2`), and PolyBLEP every resulting edge (no BLAMP — edges are level steps).

> **Residual risk.** The exact boolean/timing alignment producing *precisely* 25 % was
> described in prose; no formal truth table or scope timing was found in the cited source.
> Electric Druid lists the 25 % pulse at `-2` octaves, produced by OR-ing the `-1` and `-2`
> oct squares; the resulting pulse fundamental period is the `-2` octave period. Confirm
> against the SH-101 service-manual schematic before implementing the divider+OR logic
> [community/reverse-engineered, partly inferred] [R16][R17].

## 8. Key parameters

All values are **literature constants / equations for the algorithms we adopt** — not
measurements of a physical SH-101.

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| Ladder self-oscillation feedback | `k = 4` | dimensionless loop gain | high | Zavalishin (eq. 4.1, ch. 4); Stilson-Smith 1996 [R6][R1] |
| Ladder DC gain droop with resonance | `H(0) = 1/(1+k)` | linear gain | high | Zavalishin ch. 4 [R6] |
| TPT cutoff prewarp (integrator gain) | `g = tan(pi*fc/fs) = tan(omega_c*T/2)` | dimensionless | high | Zavalishin sec. 3.8 [R6] |
| Huovilainen one-pole coefficient (small-signal) | `g = 1 - exp(-2*pi*fc/fs)` | dimensionless | high | Huovilainen 2004 (eq. 21) [R7] |
| OTA differential-pair nonlinearity | `Idiff = Itail*tanh(Vin/(2*Vt))` | current vs voltage; `Vt ~ 26 mV` thermal voltage (device physics, not Huovilainen) | high | Huovilainen 2004 (eq. 1); `Vt` value [R7][R11] |
| PolyBLEP residual, leading segment (`t<dt`) | `2*(t/dt) - (t/dt)^2 - 1` | dimensionless residual; `dt=freq/fs` | high | Martin Finke blog 018; pbat.ch sndkit [R4][R3] |
| PolyBLEP residual, trailing segment (`t>1-dt`) | `((t-1)/dt)^2 + 2*((t-1)/dt) + 1` | dimensionless residual | high | Martin Finke blog 018; pbat.ch sndkit [R4][R3] |
| Nonlinear-filter oversampling (Huovilainen) | `2x` (e.g. 88.2 kHz) | oversampling factor | high | Huovilainen 2004 (Sec. 5.3, Fig. 4-5) [R7] |
| Resonance feedback phase compensation | `+0.5`-sample delay (two-sample average) | sample delay | high | Huovilainen 2004 (Sec. 5.3) [R7] |
| 2nd-order PolyBLEP aliasing-free limit | ~2000 (fundamental 2135 Hz, NI=2) | Hz fundamental at `fs=44.1 kHz` | high | Valimaki-Pekonen-Nam 2012 (Table VIII) [R5] |
| 3rd-order B-spline perceptually aliasing-free limit | 7.8 | kHz fundamental at `fs=44.1 kHz` | high | Valimaki-Pekonen-Nam 2012 (abstract) [R5] |
| LUT-BLEP table oversampling factor | 64 (with Blackman window) | oversampling factor | high | Valimaki-Pekonen-Nam 2012 [R5] |
| minBLEP applicability | C1-continuous waveforms (saw, square) | continuity class | high | Brandt 2001 (Sec. 6.3) [R2] |
| 2nd-order leaky integrator pole (DC-blocking, for BLIT integration) | `c = 0.9992` (-0.5 dB at 20 Hz) | pole coeff at `fs=44.1 kHz` | high | Brandt 2001 (Sec. 5) [R2] |
| SH-101 sub-osc 25% pulse duty (clone-derived) | high 75 % / low 25 % via diode-OR of `-1`/`-2` oct squares | duty cycle | medium | Electric Druid sub-oscillator study [R16] |
| PRNG-to-float noise scaling | `int32 / 2^32 -> [0,1)`, then `*2 - 1 -> [-1,1)` | normalization | medium | audiodev.blog Random Numbers for Audio [R14] |

## 9. Confidence, disputes & honest labels

This section surfaces, plainly, every disputed / low-confidence item, correction, and
residual risk for this dimension. Nothing here is settled fact.

### 9.1 Disputed / contested items

- **xorshift endorsement is contested.** The brief and our adopted default say "xorshift,"
  but the cited primary source (audiodev.blog) recommends a **64-bit LCG / PCG** and
  explicitly says it "wouldn't use xorshift to create white noise" [R14]. The general
  principle (fast integer PRNG + uniform distribution) is supported; the specific xorshift
  choice is not endorsed by that source. [practitioner reference, disputed]

### 9.2 Medium-confidence items (adopt with caveat)

- **IIR-vs-FIR resampling tradeoff** is general multirate-DSP practice corroborated by
  reputable secondary sources, not a single peer-reviewed VA paper. Quantitative figures
  (e.g. ~2.5 mult/sample for elliptic IIR halfband) are source- and design-dependent
  [R6][R12][R13]. The order/ripple/transition-band choice for the 2x path is a **design
  decision requiring an ADR**, not a literature constant.
- **Noise PRNG + scaling** is engineering folklore (practitioner sources), not peer-reviewed
  VA literature; the float range is the half-open `[-1,1)`, not closed [R14][R15].
- **SH-101 sub-osc 25 % pulse** is reverse-engineered/clone-derived from Electric Druid,
  consistent with but not line-by-line cross-checked against the service-manual schematic in
  this pass [R16][R17].

### 9.3 Citation / attribution corrections (substance unchanged)

1. **`Vt ~ 26 mV` is NOT in Huovilainen.** Eq. 1 only names `Vt` as "the so-called thermal
   voltage of a transistor." The ~26 mV value (25.85 mV at 300 K, `kT/q`) is standard device
   physics and must be cited to a semiconductor-physics reference, not to Huovilainen eq. 1
   [R7][R11].
2. **Zavalishin section-number drift.** In the public PDF (rev 1.0.3) ladder self-oscillation
   at `k=4` and `H(0)=1/(1+k)` are in **Chapter 4 (eq. 4.1)**, not "ch. 5 / eq. 5.1"; the TPT
   `tan` prewarp is in sec. 3.8 (derivation) / sec. 3.9 (application). Re-cite by content
   [R6a].
3. **Valimaki 7.8 kHz** is for the **integrated third-order B-spline correction function**
   (the paper's headline best method), distinct from Table VIII's "B-spline PolyBLEP, NI=4 ->
   7845 Hz" row. Do not merge the two [R5].
4. **White-noise PRNG** — see 9.1; the source's xorshift caveat [R14].
5. **Noise scaling** uses a fused `int*(2/2^32) - 1` expression (algebraically identical to
   the two-step form) over a half-open `[-1,1)` range [R14].

### 9.4 Residual risks and open validation gaps

- **No IR3109-specific DSP model exists in the literature.** The adopted OTA-ladder theory
  (Zavalishin TPT, Huovilainen `tanh`-Euler) is **Moog-transistor-ladder-derived and applied
  by analogy** to the IR3109. Whether the IR3109's on-chip topology requires deviation from
  the generic `4 x LP1 + global-k` model is **unverified** [theory/inference, unmeasured].
- **`k=4` is a normalized-model threshold, not a physical SH-101 value.** The SH-101 uses an
  IR3109 OTA ladder, not a discrete-transistor Moog ladder; mapping model `k` to the real
  feedback path is a separate circuit-specific step not done here [R6][R1].
- **Diode-clipped resonance / self-oscillation clamp is not in the generic literature.** The
  FROZEN circuit facts note the VCF has diode-clipped resonance (Juno-6/60 topology, C47-C50
  = 240 pF integrator caps). How the IR3109 clamps self-oscillation amplitude/waveshape was
  **not** pinned to a primary source in this DSP survey and should be answered from the
  schematic/datasheet, not the Moog-ladder literature [open question].
- **BA662 VCA exact transfer is reverse-engineered.** Beyond the generic OTA `tanh`, the
  control-port exponential shape and presence/absence of linearizing diodes were not
  confirmed from a primary datasheet (none is public) [reverse-engineered: Open Music Labs;
  clone-derived: AMSynths, partly inferred] [R10].
- **TPT-vs-Huovilainen ladder is an open architectural decision** with a fidelity/CPU
  tradeoff; both are fully sourced but the choice (and a possible hybrid) needs a benchmark.
- **Valimaki limits (2135 Hz, 7.8 kHz) are "perceptually aliasing-free"** under that paper's
  specific perceptual metric, at `fs=44.1 kHz`, for sawtooth — not hard mathematical
  bandlimits; they shift with sample rate, waveform, and listening assumptions [R5].
- **PolyBLEP formulas** were confirmed from a practitioner source (Martin Finke); the
  pbat.ch co-citation was not separately opened, but the two-segment polynomial is the
  standard, widely-replicated form and Finke's code matches exactly [R4][R3].
- **NO physical-unit measurements exist (project decision).** Bode plots, ADSR oscilloscope
  curves, and harmonic spectra are **OPEN VALIDATION GAPS**, not delivered facts.
- **Software-only features do not apply here.** The hardware SH-101 has **no sine LFO, no
  32'/64' registers, no external audio input, and no MIDI/DCB**; those appear only in later
  software (e.g. Roland Cloud SH-01A). None of the adopted DSP theory above should be
  justified by a software-emulation artifact.

## 10. Design implications for mwAudio101

- **Oscillators (default PolyBLEP).** Generate trivial saw/pulse from a phase accumulator and
  correct each discontinuity with **PolyBLEP** (closed-form, table-free, no oversampling) as
  the default [R3][R4]. Keep **minBLEP** (Brandt) as the higher-fidelity option for hard-sync
  and as the reference [R2]; treat **DPW** only as a low-CPU fallback tier [R8][R9].
- **Variable PWM.** Place the falling-edge BLEP at the duty-cycle phase, giving two
  independent BLEPs per period [R3][R4].
- **Sub-oscillator.** Synthesize the `-1`/`-2` octave squares as exact subharmonics
  phase-locked to the master accumulator, combine with a logical OR to form the 25 % pulse,
  and PolyBLEP every resulting edge (no BLAMP — edges are level steps) [R16][R5].
- **Filter (IR3109 4-pole).** Implement the adopted OTA-ladder model as either:
  - (A) **Zavalishin TPT / ZDF** — four bilinear (trapezoidal) one-poles with prewarped
    `g = tan(pi*fc/fs)` and a single delay-free global feedback `k` (`k=4` = self-oscillation),
    solving the loop implicitly with a `tanh` nonlinearity (Newton iteration) [R6]; or
  - (B) **Huovilainen** — a cascade of one-pole IIRs with embedded `tanh` at each stage and on
    feedback, run at **2x oversampling** with a `+0.5`-sample (two-sample-average) feedback
    delay and a combined tuning/resonance-compensation table [R7].

  Record A-vs-B as an **ADR with a CPU/fidelity benchmark**; both reproduce the bass droop
  `H(0)=1/(1+k)`. Apply all ladder choices as **theory-by-analogy to the IR3109**, and verify
  chip-specific items (diode-clipped self-oscillation amplitude/waveshape, exact sub-osc OR
  alignment, BA662 linearizing diodes) against the service manual / datasheets before locking
  the spec.
- **VCA & saturation.** Model the BA662 VCA gain and filter-input saturation as
  `tanh(x/2Vt)` (or a fast polynomial/rational approximation sharing a lookup) [R7][R10].
  Label this as theory-by-analogy: the BA662 internals are reverse-engineered with no public
  datasheet.
- **Oversampling.** Run the nonlinear filter+VCA path oversampled (>= 2x) with a documented
  halfband decimator (ADR: IIR polyphase vs linear-phase FIR, sized to keep aliasing below
  the noise floor); oscillators can stay at base rate with PolyBLEP [R7][R5][R12][R13].
- **Noise.** Generate white noise from a per-voice fast integer PRNG scaled `int -> float` to
  `[-1,1)` (uniform is fine). Prefer a 64-bit LCG/PCG per the cited source, or xorshift only
  with the contested-endorsement caveat noted in 9.1 [R14][R15].

## 11. References

- [R1] Stilson, Smith. *Analyzing the Moog VCF with Considerations for Digital
  Implementation* (1996). <https://ccrma.stanford.edu/~stilti/papers/moogvcf.pdf>
- [R2] Brandt. *Hard Sync Without Aliasing* (ICMC 2001).
  <http://www.cs.cmu.edu/~eli/papers/icmc01-hardsync.pdf>
- [R3] pbat.ch — sndkit BLEP. <https://pbat.ch/sndkit/blep/>
- [R4] Martin Finke. *Making Audio Plugins Part 18: PolyBLEP Oscillator*.
  <https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/>
- [R5] Valimaki, Pekonen, Nam. *Perceptually informed synthesis of bandlimited classical
  waveforms using integrated polynomial interpolation* (JASA 2012).
  <https://mac.kaist.ac.kr/pubs/ValimakiPeknenNam-jasa2012.pdf>
- [R6] Zavalishin. *The Art of VA Filter Design* (rev 2.1.0).
  <https://www.discodsp.net/VAFilterDesign_2.1.0.pdf>
- [R6a] Zavalishin. *The Art of VA Filter Design* (rev 1.0.3, used for section-number
  reconciliation). <https://noisehack.com/research/VAFilterDesign_1.0.3.pdf>
- [R7] Huovilainen. *Non-linear digital implementation of the Moog ladder filter* (DAFx
  2004). <https://www.dafx.de/paper-archive/2004/P_061.PDF>
- [R8] Valimaki. *Oscillator and Filter Algorithms for Virtual Analog Synthesis* (Computer
  Music Journal 2005).
  <https://direct.mit.edu/comj/article/30/2/19/94705/Oscillator-and-Filter-Algorithms-for-Virtual>
- [R9] Valimaki, Huovilainen. *Alias-Suppressed Oscillators Based on Differentiated
  Polynomial Waveforms* (2007).
  <https://www.researchgate.net/publication/224557976_Alias-Suppressed_Oscillators_Based_on_Differentiated_Polynomial_Waveforms>
- [R10] AMSynths. *All About the BA662 Chip* (clone/module reference).
  <https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/>
- [R11] ElectricalVolt. *Thermal Voltage* (device-physics reference for `Vt ~ 26 mV`).
  <https://www.electricalvolt.com/thermal-voltage/>
- [R12] MathWorks. *IIR Polyphase Filter Design*.
  <https://www.mathworks.com/help/dsp/ug/iir-polyphase-filter-design.html>
- [R13] Wang, Reiss. *Decimation Filters* (AES 132, 2012).
  <https://www.eecs.qmul.ac.uk/~josh/documents/2012/WangReiss-AES1322012-DecimationFilters.pdf>
- [R14] audiodev.blog. *Random Numbers for Audio*.
  <https://audiodev.blog/random-numbers/>
- [R15] KVR Audio forum — white-noise PRNG discussion.
  <https://www.kvraudio.com/forum/viewtopic.php?t=564273>
- [R16] Electric Druid. *A Study of Sub-Oscillators*.
  <https://electricdruid.net/a-study-of-sub-oscillators/>
- [R17] Roland SH-101 Service Manual (PDF, for schematic confirmation).
  <https://electricdruid.net/wp-content/uploads/2025/05/SH101-Service-Manual.pdf>
