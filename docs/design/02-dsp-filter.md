<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# IR3109 4-Pole VCF (LadderFilter)

This document is the single source of truth for the mwAudio101 voltage-controlled
filter (VCF): the Huovilainen four-stage one-pole OTA cascade wired to the SH-101's
documented topology (inverting global feedback, diode-clamp resonance limiter,
output-side make-up Q), oversampled 2x per voice, with a TPT/ZDF linear model as the
offline reference oracle. Backlog tasks cite it by section number.

It is bounded by, and must not contradict, [ADR-003] (filter modeling method, the
normative `F-01..F-15` contract), [ADR-004] (oversampling strategy, the per-voice
nonlinear zone), and [ADR-018] (the single `mw101.quality` tier control). The cited
factual ground truth is [research/03] (IR3109 circuit facts) and [research/10] (adopted
DSP theory).

## 1. Scope, ownership, and boundaries

### 1.1 In scope

This module owns the IR3109 4-pole lowpass DSP and everything inside the resonance loop:

- The four cascaded one-pole OTA cells with embedded `tanh` ([ADR-003] F-01;
  [research/10 §3.6]).
- The global inverting resonance feedback with the `+0.5`-sample (two-sample-average)
  phase compensation ([ADR-003] F-03; [research/10 §3.8]).
- The diode-clamp resonance limiter in the feedback path ([ADR-003] F-04;
  [research/03 §4.2]).
- The output-side make-up Q gain (the scalar value, computed here; it is *applied* at the
  VCA drive node — see §1.2) ([ADR-003] F-06; [research/03 §4.3]).
- Cutoff/CV mapping from a summed control voltage to the prewarped per-stage coefficient
  `g` ([ADR-003] F-08; [research/03 §3.1]).
- The per-sample-rate compensation/tuning table and its regeneration in `prepare`
  ([ADR-003] F-11, F-14).
- The fast shared `tanh` approximation ([ADR-003] F-10).
- The TPT/ZDF linear reference oracle (offline only) ([ADR-003] F-13; [research/10 §3.2]).

### 1.2 Out of scope (referenced, never redefined)

- **Parameter IDs / ranges / defaults / skews** are owned by the schema doc (the ADR-008
  `ParamDefs` registry, documented in docs/design/06 §2). This doc REFERENCES the IDs
  (`mw101.vcf.cutoff`, `mw101.vcf.resonance`, `mw101.vcf.kbd_track`, `mw101.vcf.env_mod`,
  `mw101.vcf.lfo_mod`, `mw101.quality`) and never re-mints them ([ADR-008]; [ADR-018]
  Q1-Q2). The numeric ranges in §6 below are the *DSP-internal* ranges the engine clamps
  to, not the user-facing parameter schema; the schema doc is authoritative for the
  user-facing surface.
- **The 2x oversampling zone (up/downsample, halfband kernels, per-voice scratch
  buffers)** is owned by the oversampling design doc (docs/design covering [ADR-004]). This
  module runs at the oversampled rate `fs_os` *inside* that zone and assumes its
  `prepare` is called with `fs_os`; it does not implement the resampler. See §2.2.
- **The BA662 VCA / `tanh` drive and the Drive module** are downstream stages in the same
  oversampled zone, owned by the VCA/amp design doc. This module emits the output-side Q
  make-up gain scalar (§5.3) for that stage to apply; it does NOT apply VCA gain itself
  ([ADR-004] zone stages 6-7; [research/03 §4.3]).
- **The summed cutoff CV** (CUTOFF knob + keyboard x KeyFollow + ENV x EnvDepth +
  LFO x ModDepth) is assembled by the modulation/voice layer and handed to this module as
  a single value per control block; this module owns only the CV -> `g` mapping
  ([research/03 §3.4, §10]).
- **The `mw101.quality` enum -> oversample factor mapping** is owned by [ADR-018] (Eco=1x,
  Standard=2x default/blessed, HQ=4x). This module is told its `fs_os` and regenerates its
  table accordingly; it does not own the enum.
- **Parameter smoothing** of cutoff/resonance is owned by the smoothing policy
  ([ADR-020]); this module consumes already-smoothed per-block control values.

### 1.3 Real-time invariants (apply to the whole module)

- No heap allocation and no locks on the audio thread. All state and the compensation
  table are preallocated/regenerated in `prepare()` ([ADR-003] F-11; [ADR-004] C15).
- All audio-rate hot paths (`processSample`, `processBlock`) are `noexcept`.
- Per-sample work is fixed and data-independent: forward Euler, no Newton / iterate-to-
  tolerance solver, no branches whose count depends on signal ([ADR-003] F-02).
- FTZ + DAZ are enabled on the audio thread; every integrator state carries an
  anti-denormal bias ([ADR-003] F-12).
- No `std::tanh`, no `std::tan`, no `std::exp` on the audio thread (table/approx only)
  ([ADR-003] F-10).
- Frozen, versioned constants (the `tanh` approximation, the table contents) so results
  are bit-identical on macOS arm64 ([ADR-003] F-14; [ADR-018] Q4).

## 2. Files and module responsibilities

### 2.1 File layout (created by the backlog)

| File | Responsibility |
| --- | --- |
| `core/dsp/LadderFilter.h` / `.cpp` | The shipping per-voice Huovilainen cascade + SH-101 topology. The class `mw::dsp::LadderFilter` (§3). |
| `core/dsp/FilterTables.h` / `.cpp` | Per-sample-rate coefficient/compensation tables; `mw::dsp::FilterTables` (§7). Built in `prepare`, read-only at audio rate. |
| `core/dsp/FastTanh.h` | Header-only shared `tanh` approximation `mw::dsp::fastTanh` and `fastTanhRange` (§4). |
| `core/dsp/LadderReferenceTPT.h` / `.cpp` | The offline TPT/ZDF linear oracle `mw::dsp::LadderReferenceTPT` (§8). NOT compiled into the audio hot path; test/calibration target only. |
| `core/calibration/Calibration.h` | (Owned by the schema/calibration backlog, [ADR-008].) Holds every `(PI)`-tagged constant this doc tags: the `tanh` knee `2Vt`, per-stage drive asymmetry, the comp-table polynomial fit constants, the resonance->k and resonance->makeup curves. This module READS those constants; it does not define them inline. See §9. |

### 2.2 Where this module sits in the signal graph

```text
[summed cutoff CV]  [resonance ctrl]  [keyfollow note]
        |                  |                 |
        v                  v                 v
   +-------------------------------------------------+
   | (oversampled zone, fs_os = factor * fs_host)    |   <- zone owned by ADR-004 doc
   |   x[n] --> LadderFilter::processSample --------------> y[n] (to VCA drive node)
   |              |  (4-stage tanh cascade,                |
   |              |   inverting fb + diode clamp)          |
   |              +--> makeUpGain scalar  ------------------> (to VCA drive, applied there)
   +-------------------------------------------------+
```

`LadderFilter` is instantiated once per voice ([ADR-003] context; [ADR-004] C9). Its
`processSample` is called at `fs_os` from inside the per-voice nonlinear zone, between the
oscillator/mixer (base rate, upsampled into the zone) and the BA662 VCA drive (§1.2). The
upsample/downsample and halfband decimation are NOT this module's responsibility.

## 3. `LadderFilter` class

### 3.1 Public interface

```cpp
namespace mw::dsp {

class LadderFilter {
public:
    LadderFilter() noexcept = default;

    // Off-audio-thread setup. Regenerates the per-SR table (FilterTables) into
    // preallocated storage and resets state. fsOsHz is the OVERSAMPLED rate
    // (factor * host rate). maxBlockOs is the max oversampled block length, used to
    // size any internal scratch. Allocates here ONLY. (ADR-003 F-11, F-14; ADR-004 C15)
    void prepare(double fsOsHz, int maxBlockOs) noexcept;

    // Drop all integrator/feedback memory to the anti-denormal bias; keep coefficients.
    void reset() noexcept;

    // --- Control-rate setters (call once per control block, NOT per sample) ---

    // Summed, smoothed cutoff CV in volts (1 V/oct), referenced so that
    // cv == 0 V maps to the reference cutoff fcRefHz (see calibration, §6/§9).
    // Internally clamped and mapped to the prewarped coefficient g. (F-08; research/03 §3.1)
    void setCutoffCv(float cutoffVolts) noexcept;

    // Alternatively set cutoff directly in Hz (used by the reference/tests and by callers
    // that pre-resolve CV->Hz). Clamped to [fcMinHz, fcMaxHz] (§6). (F-08)
    void setCutoffHz(float fcHz) noexcept;

    // Normalized resonance control in [0, 1]; 1.0 = onset of self-oscillation.
    // Maps to normalized loop gain k in [0, ~4] via the calibrated curve (§5.1). (F-05)
    void setResonance(float reso01) noexcept;

    // --- Audio-rate hot path (fs_os) ---

    // Process one oversampled sample through the 4-pole cascade with inverting
    // feedback + diode clamp. Returns the stage-4 output. noexcept, no alloc. (F-01..F-05)
    float processSample(float x) noexcept;

    // Block form over the oversampled buffer; equivalent to a processSample loop.
    void processBlock(float* samplesOs, int numSamplesOs) noexcept;

    // The output-side make-up gain scalar for the CURRENT resonance setting, to be applied
    // by the downstream VCA drive node (NOT applied here). Rises with resonance. (F-06)
    [[nodiscard]] float makeUpGain() const noexcept { return makeUpGain_; }

    // Current normalized loop gain k (for tests / reference cross-check).
    [[nodiscard]] float loopGainK() const noexcept { return k_; }

private:
    // ... see §3.2 ...
};

} // namespace mw::dsp
```

### 3.2 State layout (per voice, preallocated)

```cpp
private:
    // Coefficients (control-rate; updated by setters)
    double fsOs_      = 88200.0; // oversampled rate
    float  g_         = 0.0f;    // per-stage one-pole coeff = 1 - exp(-2*pi*fc/fs_os)  (F-01)
    float  k_         = 0.0f;    // normalized loop gain in [0, kMax]; k->4 = self-osc  (F-05)
    float  makeUpGain_= 1.0f;    // output-side Q make-up scalar (F-06)

    // Integrator states (one per stage) + their saturated outputs W{} = tanh(y/2Vt)
    // (Huovilainen, research/10 §3.6). Carry anti-denormal bias (F-12).
    float  y_[4]  = { kAntiDenorm, kAntiDenorm, kAntiDenorm, kAntiDenorm };
    float  w_[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Feedback-phase compensation history: the two-sample average that realizes the
    // +0.5-sample delay (research/10 §3.8; F-03).
    float  fbPrev_ = kAntiDenorm; // stage-4 output one sample ago (for the 2-sample avg)

    const FilterTables* tables_ = nullptr; // read-only at audio rate (F-11)

    static constexpr float kAntiDenorm = 1.0e-20f; // anti-denormal bias (F-12) (PI)
```

The struct is trivially copyable and contains no pointers to heap-owned data except the
read-only `tables_`. A voice's `LadderFilter` is a member of the voice object, so it is
preallocated with the voice pool (no per-note allocation).

## 4. The `tanh` nonlinearity (FastTanh.h)

### 4.1 Why `tanh`

`tanh` is the physically-correct memoryless OTA differential-pair law
`Idiff = Itail * tanh(Vin / 2Vt)` ([research/10 §4], [research/10 §3.6] eq. 1). It is
embedded at each stage transconductor input and on the feedback signal (Huovilainen
white-box model). Note this is *theory-by-analogy*: the IR3109 has no published transfer
curve and `2Vt` is a device-physics constant, not an SH-101 measurement
([research/10 §4 honest-labeling]; [research/03 §9.1]).

### 4.2 Approximation contract

A single shared fast rational/polynomial approximation is used; `std::tanh` MUST NOT
appear on the audio thread ([ADR-003] F-10; [research/10 §10 "fast polynomial/rational
approximation"]). Budget: ~5 evaluations per oversampled sample (4 stages share intermediate
results + 1 feedback), matching Huovilainen's "only five `tanh` evaluations per sample"
([research/10 §3.6]).

```cpp
namespace mw::dsp {

// Fast, frozen, branch-light tanh approximation. Versioned: the coefficients are part of
// the bit-exact bless contract (F-14). Must be odd-symmetric and monotone on the working
// range, saturating to +/-1. Reference implementation: a Pade-style rational
// x*(27 + x^2) / (27 + 9*x^2) clamped for |x| beyond the validity range to +/-1.  (PI)
[[nodiscard]] inline float fastTanh(float x) noexcept;

// Saturating transconductor with the OTA knee folded in: fastTanh(x * invTwoVt).
// invTwoVt = 1 / (2*Vt), the (PI) saturation-knee scaler from Calibration.h (§9).
[[nodiscard]] inline float fastTanhKnee(float x, float invTwoVt) noexcept;

} // namespace mw::dsp
```

(PI) The choice of the specific rational approximation and its coefficients is a pragmatic
invention (no research source prescribes a particular fast `tanh`); the `2Vt` knee scaler
and the rational coefficients centralize in `core/calibration/Calibration.h` (§9). Required
properties (not the exact polynomial) are normative in §10 acceptance hooks: odd symmetry,
monotonicity, saturation to +/-1, and max absolute error vs `std::tanh` over the working
input range below a fixed bound.

## 5. Topology and per-sample algorithm

### 5.1 Resonance mapping (control -> normalized loop gain k)

The normalized resonance control `reso01 in [0,1]` maps to the dimensionless loop gain
`k in [0, kMax]`. Self-oscillation onset is at `k = 4` in the normalized Stilson-Smith /
Zavalishin model ([research/10 §3.4], [research/10 §8]). `reso01 = 1.0` MUST reach onset.

```text
k = kMax * resonanceCurve(reso01)            // resonanceCurve(1) = 1.0
kMax = 4.0                                   // normalized self-osc threshold (research/10 §3.4)
```

(PI) `resonanceCurve` (the taper from control to `k`) is a pragmatic invention: the SH-101
resonance-pot-to-feedback law is unmeasured ([research/03 §9.1]; `k=4` is a normalized model
value, NOT the SH-101 pot value — [research/10 §3.4 residual risk], [ADR-003] Decision note).
Default taper is a unit-output exponential/`x^p` curve; its exponent `kResoCurveExp` lives in
Calibration.h (§9). Self-oscillation amplitude is governed by the diode clamp (§5.4), NOT by
a knife-edge `k`, so a small overshoot of `reso01` past 1.0 is bounded by construction
([ADR-003] F-05; [ADR-003] Decision "diode clamp as the amplitude governor").

### 5.2 Cutoff -> coefficient mapping

```text
fc_clamped = clamp(fc, fcMinHz, fcMaxHz)            // §6, F-08
g = 1 - exp(-2*pi*fc_clamped / fs_os)               // Huovilainen small-signal coeff (research/10 §3.6, eq. 21; F-01)
```

`fc` is exponential in the summed CV at 1 V/oct ([research/03 §3.1, §3.3]). The
CV->Hz conversion is `fc = fcRefHz * 2^(cutoffVolts)` (1 V/oct; [research/03 §3.1]). To
keep the audio thread free of `std::exp`, BOTH the `2^v` CV map and the `1 - exp(...)`
coefficient are resolved through the per-SR table (§7); the setters do a table lookup +
interpolation, not a transcendental call ([ADR-003] F-10, F-11).

### 5.3 Output-side make-up Q (the scalar)

OTA 4-pole filters lose passband level as resonance rises; the SH-101 compensates on the
OUTPUT side by routing the non-inverted phase-splitter copy to the VCA drive — it does NOT
boost the filter input ([research/03 §4.3]; [ADR-003] F-06). This module computes the
scalar and exposes it via `makeUpGain()`; the VCA stage applies it ([ADR-004] zone stage 6).

```text
makeUpGain_ = 1 + makeUpDepth * resonanceCurve(reso01)   // rises with resonance
```

(PI) `makeUpDepth` (and the inherent bass-droop compensation it offsets) is a pragmatic
invention: the SH-101 output-boost gain-vs-resonance law is unmeasured ([research/03 §4.3,
§9.1]). It centralizes in Calibration.h (§9). The filter INPUT level MUST NOT be scaled with
resonance ([ADR-003] F-06).

### 5.4 Diode-clamp feedback limiter

The PRIMARY resonance limiter is a memoryless diode-clamp waveshaper on the FEEDBACK
signal, NOT an in-ladder OTA soft-clip ([ADR-003] F-04; [research/03 §4.2, §9.3]). It
"reduces the level as soon as it starts to conduct," keeping self-oscillation a fairly clean
sine ([research/03 §4.2]). It is the amplitude governor: self-oscillation amplitude is the
fixed point of this clamp ([ADR-003] F-05).

```cpp
// Symmetric diode clamp to ground: soft below the threshold, hard-limiting above.
// (PI) shape; vClamp threshold from Calibration.h (§9).
[[nodiscard]] inline float diodeClamp(float fb, float vClamp) noexcept;
```

(PI) The diode-clamp shape and threshold `vClamp` are pragmatic inventions: the exact
clipping-diode part/threshold and feedback-node values are reverse-engineered and unmeasured
([research/03 §4.2, §9.3, §9.6]). They centralize in Calibration.h (§9). A `tanh`-with-larger-
knee or a polynomial soft-clip are both acceptable realizations; the normative requirement is
that the clamp lives in the FEEDBACK path and bounds the loop, and that it — not a knife-edge
`k` — sets the self-oscillation amplitude ([ADR-003] F-04, F-05).

### 5.5 Per-sample processing (forward Euler, the hot path)

`processSample(x)` runs at `fs_os` and is `noexcept`, allocation-free, fixed-cost. The
half-sample feedback-phase compensation (the two-sample average) holds feedback phase ~180
degrees at cutoff up to `Fs/4` ([research/10 §3.8]; [ADR-003] F-03). Pseudocode (normative
structure; the `(PI)` constants are read from Calibration.h via `tables_`):

```text
processSample(x):
    # 1. Feedback-phase compensation: +0.5-sample delay = average of the last two
    #    stage-4 outputs (research/10 §3.8, F-03).
    fbComp = 0.5f * (y_[3] + fbPrev_)

    # 2. Diode-clamp the inverting feedback (F-04). Inverting => subtract from input.
    fb = diodeClamp(k_ * fbComp, vClamp)        # k_ in [0, 4]

    # 3. Stage 1 input: tanh transconductor with the OTA knee (research/10 §3.6, eq. for ya).
    in0 = fastTanhKnee(x - fb, invTwoVt)        # global inverting feedback (F-03)
    # forward-Euler one-pole: y += g * (in0 - tanh(y/2Vt))
    y_[0] += g_ * (in0 - w_[0]);  w_[0] = fastTanhKnee(y_[0], invTwoVt)
    y_[1] += g_ * (w_[0] - w_[1]); w_[1] = fastTanhKnee(y_[1], invTwoVt)
    y_[2] += g_ * (w_[1] - w_[2]); w_[2] = fastTanhKnee(y_[2], invTwoVt)
    y_[3] += g_ * (w_[2] - w_[3]); w_[3] = fastTanhKnee(y_[3], invTwoVt)

    # 4. Update feedback history for the next sample's two-sample average.
    fbPrev_ = y_[3]

    # 5. Anti-denormal bias maintained by adding kAntiDenorm into each y_ accumulation
    #    (or periodic flush); FTZ/DAZ set on the audio thread (F-12).
    return y_[3]
```

Notes:

- 24 dB/oct total: four cascaded one-poles ([research/03 §2.2]; [ADR-003] F-01).
- DC gain droops as `H(0) = 1/(1+k)` inherently ([ADR-003] F-07; [research/10 §3.4]); the
  output-side make-up gain (§5.3) offsets this perceptually at the VCA, NOT by boosting the
  filter input.
- The `tanh` of `y_` (the `w_[i]`) is computed once per stage per sample and reused next
  sample, giving the ~5-evals/sample budget (4 stages + 1 input transconductor;
  [research/10 §3.6]).

### 5.6 Self-oscillation behavior

As `k -> 4` the loop gain reaches unity and the cascade self-oscillates, producing a
near-pure sine at the cutoff frequency ([research/10 §3.4]; [research/03 §4.4]; [ADR-003]
F-05). Amplitude is bounded by the §5.4 diode clamp's fixed point (insensitive to coefficient
rounding under fast resonance slews), NOT by a precise `k = 4` balance ([ADR-003] Decision;
[ADR-003] F-05). With FTZ/DAZ + anti-denormal bias, the decay tail to silence produces no
subnormals ([ADR-003] F-12; [research/03 self-oscillation decay denormal trap note in ADR]).

## 6. Numeric ranges, units, and defaults

DSP-internal ranges the engine clamps to. User-facing parameter ranges/defaults/skews are
owned by the schema doc (docs/design/06 §2; [ADR-008]); the values below are what the DSP
hot path enforces.

| Quantity | Symbol | Range / value | Unit | Default | Source |
| --- | --- | --- | --- | --- | --- |
| Cutoff frequency (clamped) | `fc` | `[fcMinHz, fcMaxHz]` = `[10, min(20000, 0.45*fs_os)]` | Hz | reference `fcRefHz` (PI) | [research/03 §3.1]; [ADR-003] F-08 |
| Cutoff CV scaling | — | exponential, 1 V/oct (`fc = fcRefHz * 2^v`) | V/oct | — | [research/03 §3.1, §3.3] |
| Reference cutoff (CV = 0) | `fcRefHz` | (PI) calibration constant | Hz | ~1000 (PI) | [research/03 §3.1] (VR8 procedure @ ~1 kHz); §9 |
| Per-stage coefficient | `g` | `1 - exp(-2*pi*fc/fs_os)`, in `[0, ~1)` | dimensionless | derived | [research/10 §3.6 eq.21]; [ADR-003] F-01 |
| Resonance control | `reso01` | `[0, 1]` | normalized | 0 | [research/03 §4.4]; [ADR-003] F-05 |
| Normalized loop gain | `k` | `[0, kMax]`, `kMax = 4` | dimensionless | 0 | [research/10 §3.4]; [ADR-003] F-05 |
| Resonance taper exponent | `kResoCurveExp` | (PI) | dimensionless | (PI) | §5.1, §9 |
| Make-up Q depth | `makeUpDepth` | (PI) | dimensionless | (PI) | [research/03 §4.3]; §9 |
| OTA knee scaler | `invTwoVt = 1/(2*Vt)` | (PI); `Vt ~ 25.85 mV` device physics | 1/V | (PI) | [research/10 §4]; §9 |
| Diode-clamp threshold | `vClamp` | (PI) | (model units) | (PI) | [research/03 §4.2]; §9 |
| Per-stage drive asymmetry | `driveAsym[4]` | (PI); AS3109-derived (stage1/3 ~0.6, stage2 ~1.0, stage4 ~1.3 mA ratios) | ratio | (PI) | [research/03 §5.2, §9.2]; [ADR-003] F-15; §9 |
| Anti-denormal bias | `kAntiDenorm` | `1e-20` | — | `1e-20` | (PI); [ADR-003] F-12 |
| Oversample factor | — | 1x / 2x (default, blessed) / 4x | — | 2x | [ADR-004] C10; [ADR-018] Q4 |
| Oversampled rate | `fs_os` | `factor * fs_host` | Hz | `2 * fs_host` | [ADR-004] C10 |
| Prewarp/stability guard | — | `fc` never exceeds `0.45 * fs_os` | Hz | — | [ADR-003] F-08 |

Notes:

- `fcMaxHz = min(20000, 0.45*fs_os)`: the documented audio range tops at 20 kHz
  ([research/03 §3.1]); the `0.45*fs_os` term is the stability/prewarp guard ([ADR-003]
  F-08). At 2x/44.1 kHz (`fs_os = 88.2 kHz`) the guard is well above 20 kHz, so 20 kHz binds.
- `driveAsym[4]` (stage-4 drives hardest) is a SECONDARY high-fidelity detail and is
  clone-derived; it scales the per-stage `fastTanhKnee` input and defaults to all-ones (no
  asymmetry) until calibrated ([research/03 §5.2]; [ADR-003] F-15).

## 7. FilterTables (per-sample-rate, regenerated in prepare)

### 7.1 Responsibility

`FilterTables` precomputes, at `prepare(fsOsHz, ...)`, everything that would otherwise need
a transcendental on the audio thread, plus the residual tuning compensation ([ADR-003] F-11,
F-14; [research/10 §3.8]). It is built off the audio thread into preallocated storage and is
read-only at audio rate. It MUST be regenerated whenever `fs_os` changes (i.e. on host SR
change or on a `mw101.quality` factor change that alters `fs_os`).

### 7.2 Interface

```cpp
namespace mw::dsp {

class FilterTables {
public:
    void build(double fsOsHz) noexcept;            // off-thread; fills preallocated arrays

    // Map summed cutoff CV (volts, 1 V/oct) -> prewarped coeff g, via interpolated table.
    // Folds fc = fcRefHz * 2^v and g = 1 - exp(-2*pi*fc/fs_os) into one lookup. (§5.2)
    [[nodiscard]] float cvToG(float cutoffVolts) const noexcept;

    // Map cutoff Hz -> g directly (for setCutoffHz / reference).
    [[nodiscard]] float hzToG(float fcHz) const noexcept;

    // Residual feedback-tuning compensation factor for the current g (research/10 §3.8).
    // Multiplies k or g to absorb the <10% (at 2x) half-sample tuning error. (§7.3)
    [[nodiscard]] float resoTuningComp(float g) const noexcept;

    [[nodiscard]] double sampleRateOs() const noexcept { return fsOs_; }

private:
    double fsOs_ = 0.0;
    static constexpr int kTableSize = 1024;        // (PI) resolution; frozen for bless
    std::array<float, kTableSize> gByCv_  {};      // CV-domain g table
    std::array<float, kTableSize> compByG_{};      // tuning-comp table
};

} // namespace mw::dsp
```

### 7.3 Compensation table contents

(PI) The compensation table that absorbs the residual half-sample tuning error (`<10%`
below `Fs/4` at 2x; [research/10 §3.8]; [ADR-003] F-14) is a pragmatic invention in its exact
fit: research gives the existence and magnitude bound of the error, not a closed-form
correction polynomial. The fit constants centralize in `core/calibration/Calibration.h`
(§9). The table contents are frozen/versioned and part of the bit-exact bless contract
([ADR-003] F-14; [ADR-018] Q4). Real-time invariant: built only in `prepare`, never written
at audio rate ([ADR-003] F-11).

## 8. TPT/ZDF reference oracle (offline only)

### 8.1 Purpose

The TPT/ZDF linear model is the offline reference for the small-signal (low-resonance,
linear) response and the basis of the macOS arm64 bit-exact bless cross-check ([ADR-003]
F-13; [research/10 §3.2]). It is NEVER the shipping engine and is NOT compiled into the
audio hot path. It exists so the bespoke Huovilainen engine can be unit-tested against a
known-correct linear ladder to catch silent topology errors, since the project holds no
physical-unit oracle ([ADR-003] Consequences; [research/03 §9.1]).

### 8.2 Interface

```cpp
namespace mw::dsp {

// Zavalishin TPT 4-pole ladder: four trapezoidal one-poles, prewarped g = tan(pi*fc/fs),
// single delay-free global feedback k, solved instantaneously (LINEAR: no embedded tanh).
// Reference/test only. (research/10 §3.2, §3.3)
class LadderReferenceTPT {
public:
    void prepare(double fsHz) noexcept;
    void reset() noexcept;
    void setCutoffHz(double fcHz) noexcept;     // g = tan(pi*fc/fs)  (research/10 §3.3)
    void setResonanceK(double k) noexcept;      // k in [0,4); k=4 self-osc (research/10 §3.4)
    [[nodiscard]] double processSample(double x) noexcept; // double precision for the oracle
};

} // namespace mw::dsp
```

The oracle uses the prewarp `g = tan(pi*fc/fs)` ([research/10 §3.3]) and the delay-free ZDF
solve; it reproduces the analytic `H(0) = 1/(1+k)` bass droop and the `k=4` self-oscillation
threshold ([research/10 §3.4]). It is run at base rate (no oversampling needed for a linear
model) and in double precision for accuracy as a comparison baseline.

## 9. (PI) constants — centralization in Calibration.h

Every pragmatic-invention constant tagged `(PI)` in this doc is a single named entry in
`core/calibration/Calibration.h` ([ADR-008]; [ADR-003] F-15). This module reads them; it
does NOT hardcode them at the use site. The single calibration table makes them unit-testable
and retunable without touching DSP code.

| Constant | Meaning | Default | Source / why (PI) |
| --- | --- | --- | --- |
| `vcf::fcRefHz` | Cutoff at CV = 0 V | ~1000 | VR8 procedure references ~1 kHz, exact ref is engineering choice ([research/03 §3.1]) |
| `vcf::invTwoVt` | OTA knee `1/(2*Vt)` | from `Vt~25.85 mV` | device physics not SH-101 datum ([research/10 §4]) |
| `vcf::kMax` | Normalized self-osc loop gain | 4.0 | model value, not SH-101 pot value ([research/10 §3.4]) |
| `vcf::kResoCurveExp` | Resonance taper exponent | (PI) | SH-101 reso law unmeasured ([research/03 §9.1]) |
| `vcf::makeUpDepth` | Output-side Q make-up depth | (PI) | output-boost law unmeasured ([research/03 §4.3]) |
| `vcf::vClamp` | Diode-clamp threshold | (PI) | clipping-diode part/node reverse-engineered ([research/03 §4.2, §9.3]) |
| `vcf::tanhCoeffs` | FastTanh rational coefficients | (PI) | no research-prescribed approx (§4.2) |
| `vcf::driveAsym[4]` | Per-stage drive asymmetry | {1,1,1,1} | AS3109 clone-derived ([research/03 §5.2]) |
| `vcf::compFit` | Tuning-comp table fit constants | (PI) | research gives bound not closed form ([research/10 §3.8]) |
| `vcf::antiDenorm` | Anti-denormal bias | 1e-20 | numerical hygiene ([ADR-003] F-12) |

## 10. Acceptance hooks

Objectively-testable properties a backlog task's tests MUST verify. Each maps to one or more
[ADR-003] contract rows.

- **Topology / slope (F-01):** At low resonance the magnitude response rolls off at
  24 dB/oct (4-pole); measured slope one octave above cutoff is `-24 +/- tolerance` dB/oct.
- **Forward Euler / fixed cost (F-02):** `processSample` performs no Newton iteration; a
  cycle/branch count is data-independent across input amplitudes (no signal-dependent loop).
- **Inverting feedback + phase comp (F-03):** Resonant peak frequency tracks cutoff within
  the `<10%` (at 2x, below `Fs/4`) tuning-error bound from [research/10 §3.8]; removing the
  two-sample average measurably worsens peak-frequency detune (proves the comp is wired).
- **Diode clamp is the limiter (F-04, F-05):** With the diode clamp disabled the loop is
  unbounded/clips to rails; with it enabled the self-oscillation output amplitude converges to
  a fixed point that is insensitive (within tolerance) to small `k` perturbations and to
  coefficient rounding. No in-ladder OTA soft-clip acts as the primary limiter.
- **Self-oscillation is a near-pure sine (F-05):** At `reso01 = 1` (k -> 4) with zero input,
  the output is a sine at ~cutoff with THD below a fixed bound; its frequency tracks cutoff.
- **Output-side make-up only (F-06):** `makeUpGain()` increases monotonically with
  `reso01`; the filter INPUT scaling is invariant to resonance (input gain is constant). The
  make-up is NOT applied inside `processSample` (it is exposed for the VCA).
- **Bass droop (F-07):** DC gain equals `1/(1+k)` within tolerance vs the TPT oracle across
  a sweep of `k`.
- **Cutoff range / scaling (F-08):** A 1 V CV step doubles `fc` (1 V/oct); `fc` is clamped to
  `[10 Hz, min(20 kHz, 0.45*fs_os)]` and never exceeds the guard at any CV.
- **Oversampling integration (F-09):** With factor = 2x, alias products at full drive +
  self-oscillation sit below the [ADR-004] C12 self-referential floor (~-90 to -100 dBFS)
  measured against a higher-oversampled run of this same engine.
- **`tanh` approximation (F-10):** `fastTanh` is odd-symmetric, monotone, saturates to
  +/-1, has max absolute error vs `std::tanh` over the working range below a fixed bound, and
  no `std::tanh`/`std::tan`/`std::exp` symbol is reachable from `processSample`/setters.
- **Real-time safety (F-11):** `prepare`/`reset`/`processSample`/`processBlock` allocate no
  heap and take no lock at audio rate (verified by an allocation/lock guard in tests);
  `FilterTables::build` is the only allocator and runs only in `prepare`.
- **Denormals (F-12):** A drive-to-silence (self-oscillation decay) test asserts no subnormal
  floats appear in any integrator state or output once FTZ/DAZ + anti-denormal bias are on.
- **Reference cross-check (F-13):** At low resonance (linear regime) `LadderFilter` matches
  `LadderReferenceTPT` magnitude/phase within a stated tolerance band across the cutoff range.
- **Determinism / bless (F-14):** Given fixed inputs, fixed `fs_os`, and the frozen
  constants, `processBlock` output is bit-identical on repeated runs and on macOS arm64; the
  `tanh` coefficients and table contents are versioned constants.
- **Tunable constants (F-15):** Every `(PI)` constant in §9 is read from `Calibration.h`; no
  `(PI)` numeric literal appears inline in `LadderFilter.cpp`/`FastTanh.h`/`FilterTables.cpp`.

## 11. References

ADRs (normative contracts):

- [ADR-003] plan/decisions/003-filter-modeling-method.md — Huovilainen-core cascade wired to
  SH-101 topology; contract `F-01..F-15`.
- [ADR-004] plan/decisions/004-oversampling-strategy.md — per-voice 2x nonlinear zone;
  contract C1-C16.
- [ADR-018] plan/decisions/018-quality-tier-parameter.md — single `mw101.quality` enum
  (Eco/Standard/HQ), Standard = 2x default/blessed.
- [ADR-008] plan/decisions/008-parameter-state-preset-schema.md — parameter registry and the
  single `(PI)` calibration table (referenced for IDs and `Calibration.h`).
- [ADR-020] plan/decisions/020-parameter-smoothing-policy.md — smoothing of cutoff/resonance
  (referenced; upstream of this module).

Research docs (cited factual ground truth):

- [research/03] docs/research/03-filter-ir3109.md — IR3109 topology, cutoff/CV scaling,
  diode-clipped resonance, output-side Q, self-oscillation, clone-derived figures.
- [research/10] docs/research/10-dsp-modeling-techniques.md — adopted DSP theory: Huovilainen
  cascade (§3.6), TPT/ZDF oracle (§3.2-§3.3), `k=4` self-osc / `H(0)=1/(1+k)` (§3.4),
  half-sample feedback compensation (§3.8), `tanh` OTA law (§4), 2x oversampling (§5.1).
