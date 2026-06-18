<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Oscillators: VCO, Sub-Oscillator & Noise

## 1. Scope and responsibilities

### 1.1 What this document owns

This is the single source of truth for the mwAudio101 oscillator section: the
single CEM3340-modeled VCO (anti-aliased sawtooth and variable-width pulse/PWM,
footages 16'/8'/4'/2', exponential 1V/oct pitch), the 4013-derived
sub-oscillator (diode-OR of two phase-locked squares: -1 oct square, -2 oct
square, -2 oct 25% pulse), and the xorshift white-noise generator. It defines
the C++ class/struct signatures, data layouts, the shared anti-aliasing core,
the numeric tables (footage, PWM, tuning), and the real-time invariants the
backlog implements.

The hardware ground truth is a single Curtis CEM3340 VCO (IC13) emitting only
saw + variable pulse, a 4013 dual flip-flop (IC17) sub divider clocked from the
sawtooth, and a reverse-biased-junction white-noise source (TR23)
[research/02 §1, §2.1, §3.1, §4.1]. The anti-aliasing strategy is PolyBLEP per
voice by default with a minBLEP HQ tier bound to the Quality enum [ADR-002,
ADR-018].

### 1.2 What this document does NOT own (references only)

- **Parameter IDs, ranges, defaults, and skews** are owned by the parameter
  schema, `docs/design/06 §3` (master index §3.0) (per ADR-008). This document
  REFERENCES parameter IDs (e.g. `mw101.vco.range`, `mw101.vco.pw`,
  `mw101.vco.pwm_depth`, `mw101.sub.mode`, `mw101.sub.level`,
  `mw101.noise.level`) and never re-mints them. Where a
  numeric range appears here it is the DSP-internal domain (the value the DSP
  consumes after the schema maps the normalized parameter), not the canonical
  schema range.
- **The single `mw101.quality` enum** (Eco/Standard/HQ) and its derivation table
  are owned by ADR-018 / `docs/design/06`. This document consumes the derived
  oscillator AA mode (PolyBLEP vs minBLEP) only.
- **Mixer summing into the filter, glide/portamento, LFO/MOD/bender CV
  generation, and the pitch CPU/key-assigner** are owned by their own design
  docs. This document defines the oscillator output contract and the CV input
  contract it consumes; it does not implement the modulation sources or the
  mix-to-filter stage. (Source-mixer topology is summarized in §9 for context
  only; the mixer design doc owns it.)
- **Oversampling** of the nonlinear filter/VCA path is owned by the filter
  design doc / ADR-004. Oscillators run at base sample rate and MUST NOT depend
  on filter oversampling [ADR-002 C7].

### 1.3 Files the backlog creates

| File | Contents |
| --- | --- |
| `core/dsp/PolyBlep.h` | Stateless closed-form PolyBLEP residual (header-only, inline). |
| `core/dsp/MinBlepTable.h` / `.cpp` | minBLEP residual table (64x Blackman) + per-voice ring-buffer applicator. |
| `core/dsp/Oscillator.h` / `.cpp` | CEM3340-modeled VCO (saw + variable pulse), exp pitch, drift model. |
| `core/dsp/SubOscillator.h` / `.cpp` | 4013 phase-locked divider, diode-OR, 3-way shape select. |
| `core/dsp/NoiseSource.h` / `.cpp` | xorshift white-noise PRNG, scaled to `[-1, 1)`. |
| `core/dsp/OscillatorSection.h` / `.cpp` | Per-voice owner aggregating VCO + sub + noise; shared phase. |
| `core/calibration/Calibration.h` | (Owned by backlog.) Central home for every `(PI)` constant below. |

## 2. Shared concepts and types

### 2.1 Phase accumulator and the dt convention

All edge-aware sources share the PolyBLEP convention from the ADR-002 Contract
and [research/10 §2.3]: phase `t` is in `[0, 1)`; `dt = freq / fs` is the
per-sample phase increment. One wrap per sample maximum is assumed; the dt clamp
in §4.4 guarantees this for the audio band.

The VCO owns the master phase accumulator. The sub-oscillator derives its
squares from the SAME accumulator (never an independent oscillator) so they are
exactly phase-locked at every footage [ADR-002 C4; research/02 §7.2].

### 2.2 Quality tier (consumed, not owned)

```cpp
// Mirrors the ADR-018 enum; the canonical definition lives in docs/design/06.
enum class OscAaMode { PolyBlep, MinBlepHq };
```

The oscillator section receives `OscAaMode` derived from `mw101.quality` per the
ADR-018 mapping (Eco -> PolyBlep, Standard -> PolyBlep, HQ -> MinBlepHq)
[ADR-018 Q-table]. The mode is set in `prepare()` / on structural
reconfiguration only, never per-sample on the audio thread [ADR-018 Q5].

### 2.3 Internal HQ auto-escalation (always on, not a parameter)

Independently of the tier, any voice whose VCO fundamental exceeds the
escalation threshold MUST switch from the closed-form PolyBLEP residual to the
minBLEP applicator for that voice while the condition holds. This is internal
model behavior keyed off pitch (the Valimaki limit), NOT a user parameter
[ADR-002 C9; ADR-018 Q6; research/10 §8 Table VIII].

- Escalation threshold: `kHqEscalationHz = 2000.0` Hz fundamental. `(PI)`
  rounded down from the cited 2135 Hz perceptual aliasing-free limit for
  2nd-order PolyBLEP at 44.1 kHz; centralized in `Calibration.h`. Source:
  [research/10 §8 Table VIII] (2135 Hz, NI=2).
- The threshold is sample-rate-relative in spirit but the cited figure is for
  44.1 kHz; the engine MAY scale it as `kHqEscalationHz * (fs / 44100.0)` so
  higher sample rates push aliases further out before escalating. `(PI)`
  (engineering convenience; centralize the choice in `Calibration.h`).
- Escalation is per voice and may toggle within a block; the applicator MUST
  remain allocation-free and lock-free (§3.3).

### 2.4 Real-time invariants (all sources)

- No heap allocation and no locks on any audio-thread path [ADR-002 C11;
  ADR-018 Q5].
- All per-sample render methods are `noexcept`.
- All buffers (the minBLEP table, the per-voice minBLEP correction ring buffer)
  are sized and allocated in `prepare()` and are read-only (table) or
  pre-sized (ring) on the audio thread.
- PolyBLEP is stateless; the minBLEP table is built once and shared read-only
  across voices [ADR-002 C8, C11].
- Render methods do not branch on the parameter store; control values are
  snapshotted/smoothed before the per-sample loop (smoothing policy owned by
  ADR-020 / `docs/design/06`).

## 3. Anti-aliasing core

### 3.1 PolyBLEP residual (`core/dsp/PolyBlep.h`)

Stateless, header-only, fully inlineable. Implements the ADR-002 Contract
residual verbatim.

```cpp
namespace mw101::dsp {

// Closed-form two-segment PolyBLEP residual.
// t  : phase in [0,1)
// dt : freq/fs (per-sample phase increment), dt > 0
// Returns the residual to SUBTRACT from a rising step / ADD per ADR-002 C1-C2.
[[nodiscard]] constexpr float polyBlep (float t, float dt) noexcept
{
    if (t < dt)                 // leading segment
    {
        const float x = t / dt;
        return 2.0f * x - x * x - 1.0f;       // 2*t' - t'^2 - 1
    }
    if (t > 1.0f - dt)          // trailing segment
    {
        const float x = (t - 1.0f) / dt;
        return x * x + 2.0f * x + 1.0f;        // t''^2 + 2*t'' + 1
    }
    return 0.0f;
}

} // namespace mw101::dsp
```

Residual formula traces to [ADR-002 Contract] and [research/10 §2.3, §8 Table].

### 3.2 minBLEP table (`core/dsp/MinBlepTable.h/.cpp`)

Built once in `prepare()`; read-only on the audio thread. Serves both as the
HQ-tier applicator and as the blessed A/B alias-suppression reference oracle for
the bit-exact bless [ADR-002 C8; research/10 §2.2, §5.1].

```cpp
namespace mw101::dsp {

class MinBlepTable
{
public:
    // Oversampling factor of the residual table (Blackman-windowed). [research/10 §5.1]
    static constexpr int kOversampling = 64;          // 64x  [research/10 §5.1]
    static constexpr int kZeroCrossings = 16;         // (PI) half-width in periods; Calibration.h
    // Total residual length in TABLE samples = 2*kZeroCrossings*kOversampling.

    // Built once, off the audio thread (prepareToPlay). Allocates here ONLY.
    void build();                                     // not noexcept; init-time only
    [[nodiscard]] bool isBuilt() const noexcept;

    // Sample the minimum-phase band-limited step residual at a fractional table
    // position. Pure read; noexcept; no allocation.
    [[nodiscard]] float residualAt (int tableIndex) const noexcept;
    [[nodiscard]] int   length() const noexcept;      // residual length in base samples

private:
    std::vector<float> residual_;                     // sized in build(); read-only after
};

} // namespace mw101::dsp
```

Window: Blackman; oversampling 64x [research/10 §5.1, §8]. `kZeroCrossings` is a
`(PI)` truncation length (number of sinc lobes retained each side) chosen to put
the stopband below the bless aliasing-floor gate; centralized in
`Calibration.h`.

### 3.3 Per-voice minBLEP applicator

Each voice owns a fixed-size correction accumulator (overlap-add of scheduled
band-limited steps). It is pre-sized in `prepare()` and is purely arithmetic on
the audio thread.

```cpp
namespace mw101::dsp {

class MinBlepApplicator
{
public:
    void prepare (const MinBlepTable& table, double sampleRate);  // sizes the ring
    void reset() noexcept;

    // Schedule a band-limited step of signed amplitude `amp` at fractional
    // sub-sample offset `frac` in [0,1). noexcept, no allocation.
    void scheduleStep (float amp, float frac) noexcept;

    // Pop the accumulated correction for the current sample and advance. noexcept.
    [[nodiscard]] float next() noexcept;

private:
    const MinBlepTable* table_ = nullptr;             // non-owning, read-only
    std::vector<float>  ring_;                         // pre-sized; circular accumulator
    int                 head_ = 0;
};

} // namespace mw101::dsp
```

Real-time invariant: `scheduleStep` / `next` allocate nothing and take no locks
[ADR-002 C11]. `ring_` length >= `MinBlepTable::length()`; sizing happens in
`prepare()`.

### 3.4 BLAMP is not used

No source uses PolyBLAMP/BLAMP. Every edge in the saw, the pulse, and the OR'd
sub is a true level step (C1-continuous discontinuity), which is exactly the
PolyBLEP/minBLEP case [ADR-002 C6; research/10 §2.4, §7]. The CEM3340 triangle
is not modeled (not panel-routed on the SH-101) [research/02 §2.3, §8.5], so no
slope-discontinuity correction is required anywhere in this section.

### 3.5 DPW exclusion

DPW is excluded from the audio path. It MAY exist only behind a compile-time
low-CPU fallback flag (e.g. `MW101_OSC_DPW_FALLBACK`), never the default
[ADR-002 C10; research/10 §2.5]. The default and HQ tiers are PolyBLEP and
minBLEP respectively.

## 4. VCO (`core/dsp/Oscillator.h/.cpp`)

### 4.1 Responsibilities

Model a single integrated oscillator equivalent to a CEM3340: one phase core
running at the pitch CV, producing a band-limited sawtooth and a band-limited
variable-width pulse (PWM), with on-die exponential pitch conversion and a small
intrinsic drift model [research/02 §2.1, §2.2, §7.1]. The VCO owns the master
phase accumulator that the sub-oscillator divides.

### 4.2 Class signature

```cpp
namespace mw101::dsp {

struct OscControls          // snapshot of smoothed/derived control values per block
{
    float pitchCvVolts;     // summed 1V/oct CV: key + range footage + tune + bend + LFO/MOD
    float pwmCvNorm;        // 0..1 -> pulse width per the PWM map (§4.5)
    OscAaMode aaMode;       // derived from mw101.quality (§2.2)
};

class Oscillator
{
public:
    void prepare (double sampleRate, const MinBlepTable& hqTable) noexcept;
    void reset() noexcept;                 // phase -> 0, applicators cleared

    void setControls (const OscControls& c) noexcept;   // per-block, not per-sample

    struct Output { float saw; float pulse; };

    // Render one sample. noexcept, no allocation. Advances the master phase.
    [[nodiscard]] Output renderSample() noexcept;

    // Phase access for the phase-locked sub-oscillator (§5).
    [[nodiscard]] float  phase() const noexcept;        // current t in [0,1)
    [[nodiscard]] bool   wrappedThisSample() const noexcept; // saw wrap edge this sample
    [[nodiscard]] double frequencyHz() const noexcept;  // current fundamental (for escalation)

private:
    double sampleRate_ = 0.0;
    double phase_ = 0.0;        // master accumulator [0,1)
    double dt_ = 0.0;           // freq/fs
    double freqHz_ = 0.0;
    float  duty_ = 0.5f;        // current pulse duty [kPwmDutyMin .. 0.5]
    OscAaMode aaMode_ = OscAaMode::PolyBlep;
    // drift state (§4.6)
    double warmupPhase_ = 0.0;
    float  scaleErr_ = 0.0f, offsetErr_ = 0.0f;
    // HQ applicators (per shape) used when aaMode_ == MinBlepHq OR escalated
    MinBlepApplicator sawBlep_, pulseBlep_;
    const MinBlepTable* hqTable_ = nullptr;
};

} // namespace mw101::dsp
```

### 4.3 Pitch: exponential 1V/oct converter (on-die model)

Pitch is generated as a digital 1V/oct CV summed into the frequency CV input;
the converter is the on-die CEM3340 exponential, so there is NO external VCO
tempco element [research/02 §2.2, §2.6, §2.8, §7.1]. The DSP maps CV volts to Hz:

```text
freqHz = kPitchRefHz * 2^( pitchCvVolts - kPitchRefVolts + footageOffsetV + driftScale )
```

- `kPitchRefHz = 442.0` Hz, the tuning reference A4 = 442 Hz at the 8' range with
  Transpose = Middle [research/02 §2.8, §6].
- `kPitchRefVolts` is the CV value (after footage offset) that corresponds to the
  reference pitch; chosen so 8' + Transpose Middle + 0-cent tune lands exactly on
  442 Hz. `(PI)` calibration anchor; centralize in `Calibration.h`.
- `footageOffsetV` comes from the footage table (§4.4); the octave switch is a CV
  offset, NOT a separate analog divider [research/02 §2.4, §7.1].
- The TUNE control contributes +/-50 cents (i.e. +/-50/1200 V) into
  `pitchCvVolts` [research/02 §2.8, §6]. Tune/bender/LFO summation is performed
  by the modulation/CV doc; this VCO consumes the summed `pitchCvVolts`.

Internal CV span for context: 0.415V to 5V internal, external CV-In jack 0-7V
[research/02 §2.8] — the modulation doc owns clamping; the VCO accepts the summed
volts.

### 4.4 Footage / range switch

The range switch has four positions implemented as CV octave offsets by the CPU,
NOT an analog divider [research/02 §2.4, §6]. The mapping below is the DSP octave
offset (in volts = octaves at 1V/oct) added to the pitch CV. The schema maps the
`mw101.vco.range` choice index to a footage; the DSP applies the offset.

| `mw101.vco.range` choice | Footage | Range data CV (hardware) | DSP octave offset (V) | Relative octave |
| --- | --- | --- | --- | --- |
| 0 | 16' | 1 V | +0.0 | lowest |
| 1 | 8' | 2 V | +1.0 | reference (A4=442) |
| 2 | 4' | 3 V | +2.0 | +1 oct |
| 3 | 2' | 4 V | +3.0 | +2 oct |

Source: footage positions and CV mapping [research/02 §2.4, §6]. The DSP offset
is the hardware range-data delta re-expressed so that 8' is the A4=442 reference
(offsets above are relative to 8' = 0; equivalently 16'/8'/4'/2' span -1/0/+1/+2
octaves about the reference). Transpose L/M/H adds +0/+1/+2 V on top, owned by
the key/CV doc [research/02 §7.1]; the VCO sees it pre-summed in `pitchCvVolts`.

> Frozen correction enforced: footage is 16'/8'/4'/2' (four positions), NOT
> 8'/16'/32'. There are NO 32'/64' registers [research/02 §2.4, §8.5].

dt clamp: `dt_ = min(freqHz_/fs, kDtMax)` with `kDtMax = 0.5` (Nyquist;
guarantees at most one wrap per sample). `(PI)` safety clamp; centralize in
`Calibration.h`.

### 4.5 Sawtooth and pulse generation

Per ADR-002 Contract C1-C3:

- **Saw (C1):** `value = (2*t - 1) - residual(t)`; one residual per wrap. In
  PolyBLEP mode the residual is `polyBlep(t, dt)`; in HQ/escalated mode the wrap
  schedules a minBLEP step of amplitude `-2` (the saw's reset jump) at the
  fractional wrap offset and `value = (2*t - 1) + sawBlep_.next()`.
- **Pulse / PWM (C2):** two INDEPENDENT BLEPs per period — `+residual` at the
  rising edge (phase 0), `-residual` at the falling edge placed at the
  duty-cycle phase. The naive pulse is `+1` for `t < duty`, else `-1`; PolyBLEP
  corrects both transitions, with the falling correction positioned at the duty
  phase so swept width tracks correctly [ADR-002 C2; research/02 §2.5;
  research/10 §2.3].

In HQ/escalated mode the same two transitions are scheduled into `pulseBlep_`
(amplitude `+2` rising, `-2` falling) at their fractional offsets.

### 4.6 PWM width model

The PWM source (ENV/MANUAL/LFO selection) is owned by the modulation doc; the
manual-width component is `mw101.vco.pwm_depth` (oscillator-owned; distinct from
the LFO->PWM amount `mw101.lfo.depth_pwm`). This VCO consumes a normalized
`pwmCvNorm` in `[0, 1]` (mapped by the schema from CEM3340 pin-5 0-5V semantics)
and converts it to duty. Pulse width sweeps from 50% (square) down toward a
practical ~5% minimum [research/02 §2.5, §7.1].

| Quantity | Value | Unit | Source |
| --- | --- | --- | --- |
| Max duty (square) | 0.50 | fraction | research/02 §2.5 |
| Min duty floor `kPwmDutyMin` | 0.05 | fraction | research/02 §2.5, §7.1 (AMSynths ~5%) |
| CV semantics | pin 5, 0-5V -> 0-100% PW (hardware) | V | research/02 §2.5 |

Mapping (DSP-internal): `duty = kPwmDutyMax - pwmCvNorm * (kPwmDutyMax -
kPwmDutyMin)` with `kPwmDutyMax = 0.5`, `kPwmDutyMin = 0.05`. So `pwmCvNorm = 0`
=> 50% (square), `pwmCvNorm = 1` => ~5%.

> `kPwmDutyMin = 0.05` is medium-confidence research (the AMSynths clone
> observation, not the Roland spec which states "50% to min/0%"). It is a
> calibration constant: centralize in `Calibration.h` so the floor can be tuned
> [research/02 §2.5, §8.2, §8.4].

Duty/dt overlap clamp (ADR-002 C3): clamp the effective duty so the two BLEPs
never overlap at the narrow-width / high-pitch extreme. Concretely:
`dutyClamped = clamp(duty, kPwmDutyMin, 0.5)` AND additionally require
`dutyClamped >= dt_` and `(1 - dutyClamped) >= dt_` so neither correction window
straddles the other [ADR-002 C3; research/10 §2.3]. The minimum effective duty is
therefore `max(kPwmDutyMin, dt_)`. `(PI)` overlap guard; centralize the policy in
`Calibration.h`.

### 4.7 Drift and stability model

The CEM3340 is intrinsically stable (same-die expo + tempco); model only a
SMALL, slow drift, NOT large free-running wander [research/02 §2.9, §7.1]:

- (a) warm-up transient: first-order settle of a tiny scale/offset error over
  tens of seconds from cold start.
- (b) small residual scale error (kT/q term not perfectly cancelled).
- (c) progressive top-octave sharpness unless HF tracking (pin 7) is modeled
  [research/02 §2.7, §7.1].

| Constant | Suggested value | Unit | Status | Source |
| --- | --- | --- | --- | --- |
| `kDriftScalePpmMax` | 50 | ppm | (PI), bounded by datasheet | research/02 §2.9 (+/-50 ppm) |
| `kDriftScaleErrPct` | 0.05 | % | (PI), bounded by datasheet | research/02 §2.9 (0.05% typ) |
| `kWarmupTauSec` | 30 | s | (PI) | research/02 §7.1 ("tens of seconds") |
| `kHfTrackEnable` | true | bool | (PI) | research/02 §2.7 |

All four are `(PI)` tuning values bounded by the CEM3340 datasheet figures and
MUST live in `Calibration.h`. The detailed per-voice variance distribution
(vintage variance) is owned by the variance design doc / ADR-009; this VCO
exposes the per-voice `scaleErr_`/`offsetErr_` seeds it consumes. Default build
MAY ship drift effectively at zero pending the variance doc; the hooks must
exist.

> Do NOT add an external VCO tempco element and do NOT place TH1 in the VCO
> path: the 3340 is self-compensated and TH1 belongs to the VCF model
> [research/02 §2.6, §7.1, §8.1].

## 5. Sub-oscillator (`core/dsp/SubOscillator.h/.cpp`)

### 5.1 Responsibilities

Model the sub as an EXACT integer divider of the VCO PHASE, never an independent
oscillator [ADR-002 C4; research/02 §3.4, §7.2]. It produces, selectable by a
3-position shape control matching the hardware S5 switch: -1 oct square (VCO/2),
-2 oct square (VCO/4), and -2 oct 25% pulse (diode-OR of the two squares). It
needs no separate tuning, glide, or drift — it inherits all VCO pitch/range
behavior [research/02 §3.7, §7.2].

### 5.2 Class signature

```cpp
namespace mw101::dsp {

enum class SubShape { OctDownSquare = 0, TwoOctDownSquare = 1, TwoOctDown25Pulse = 2 };

class SubOscillator
{
public:
    void prepare (double sampleRate, const MinBlepTable& hqTable) noexcept;
    void reset() noexcept;                 // divider state -> 0

    void setShape (SubShape s) noexcept;   // from mw101.sub.mode (schema-owned ID)
    void setAaMode (OscAaMode m) noexcept;

    // Drive from the master VCO each sample. `masterPhase` in [0,1); `wrapped`
    // true exactly on the sawtooth-wrap sample (the 4013 clock edge).
    // noexcept, no allocation.
    [[nodiscard]] float renderSample (float masterPhase, bool wrapped, double freqHz) noexcept;

private:
    bool q1_ = false;     // first flip-flop  (-1 oct), toggles each VCO cycle
    bool q2_ = false;     // second flip-flop (-2 oct), toggles each -1 oct cycle
    bool q1Prev_ = false; // for half-cycle phase of -2 oct pulse construction
    SubShape shape_ = SubShape::OctDownSquare;
    OscAaMode aaMode_ = OscAaMode::PolyBlep;
    double sampleRate_ = 0.0;
    MinBlepApplicator blep_;
    const MinBlepTable* hqTable_ = nullptr;
};

} // namespace mw101::dsp
```

### 5.3 Division mechanism (4013 model)

The 4013 is clocked from the VCO sawtooth/ramp on its single rising edge per VCO
cycle [research/02 §3.2, §3.4]:

- On each `wrapped == true` sample (one rising clock edge per VCO cycle), toggle
  `q1_`. The first flip-flop divides the VCO by 2 -> the -1 octave square.
- Toggle `q2_` on each rising edge of `q1_` (i.e. when `q1_` goes false->true).
  The second flip-flop divides again by 2 -> the -2 octave square.

This is standard binary division, exactly as the SH-101 circuit shows
[research/02 §3.4]. Because it is driven by the VCO wrap, the sub is always an
exact 1 or 2 octaves below whatever footage RANGE selects, with zero independent
drift [research/02 §3.7].

### 5.4 Diode-OR 25% pulse (Contract C5)

The -2 oct 25% pulse is `out = Q1 OR Q2` (logical OR of the two square outputs),
producing a signal high 75% of the time / low 25% of the time at the -2 oct
period [ADR-002 C5; research/02 §3.5, §7.2; research/10 §7]. This is a FIXED
diode-OR (D38/D39), not a continuous blend pot — VR15 only sets sub LEVEL
[research/02 §3.5, §8.2]. It is implemented as the OR, never as a separate PWM
oscillator, to match the harmonic spectrum exactly (the 25% duty has the
strongest 2nd harmonic, giving the characteristic -1/-2 oct blend)
[research/02 §3.5; research/10 §7].

Output mapping per shape (bipolar `[-1, +1]` before level):

| `mw101.sub.mode` / `SubShape` | Logic | Naive output |
| --- | --- | --- |
| 0 OctDownSquare (-1 oct) | `Q1` | `Q1 ? +1 : -1` |
| 1 TwoOctDownSquare (-2 oct) | `Q2` | `Q2 ? +1 : -1` |
| 2 TwoOctDown25Pulse (-2 oct) | `Q1 OR Q2` | `(Q1 || Q2) ? +1 : -1` (high 75% / low 25%) |

### 5.5 Edge-aware band-limiting and edge ordering

Every transition produced by the selected logic is a level step; PolyBLEP (or
minBLEP in HQ/escalated mode) is applied to EVERY resulting edge; no BLAMP
[ADR-002 C5, C6; research/10 §2.4, §7]. The edges occur at known fractional
sub-sample positions derived from the master phase and dt:

- The -1 oct square edges land at master-phase wraps (every VCO cycle the
  square toggles, i.e. an edge every 2 VCO cycles for a full -1 oct period).
- The -2 oct edges land at every other -1 oct edge.
- The 25% pulse edge pattern within one -2 oct period: the OR of two phase-locked
  squares yields a 4-segment pattern per -2 oct period. Edge ordering is
  load-bearing — incorrect ordering silently corrupts the 75/25 spectrum
  [ADR-002 Consequences; research/10 §9.2]. The implementation MUST derive each
  edge time from the SAME master accumulator and schedule the steps in temporal
  order; a unit test (§10) asserts the duty and harmonic ratio.

The fractional offset of each toggle within the current sample equals the
fraction of `dt` already traversed when the clocking phase crossed the toggle
threshold; reuse the VCO's wrap-fraction so the sub edges are sample-accurate and
drift-free relative to the saw wrap [research/02 §7.2].

### 5.6 Sub level and selection

The 3-way selection corresponds to the hardware S5 switch [research/02 §3.3].
The sub LEVEL (`mw101.sub.level`, schema-owned, hardware VR15 = 100k linear) is
applied by the source mixer (§9), not inside `SubOscillator`. The sub waveform is
clamped to the oscillator level before summing [research/02 §7.3].

## 6. Noise source (`core/dsp/NoiseSource.h/.cpp`)

### 6.1 Responsibilities

Flat WHITE noise (uniform power per Hz) — there is NO pink shaping
[research/02 §4.3, §7.3]. Generated from a fast integer PRNG scaled to the
half-open `[-1, 1)` range [research/10 §6].

### 6.2 Class signature

```cpp
namespace mw101::dsp {

class NoiseSource
{
public:
    void prepare (double sampleRate) noexcept;
    void reset (uint64_t seed) noexcept;   // per-voice seed; nonzero required

    // One white-noise sample in [-1, 1). noexcept, no allocation.
    [[nodiscard]] float renderSample() noexcept;

private:
    uint32_t state_ = 0x9E3779B9u;         // xorshift32 state; reseeded in reset()
    // Optional single-pole HF rolloff (§6.4); disabled by default.
    bool  hfRolloffEnabled_ = false;
    float lpfZ_ = 0.0f, lpfCoeff_ = 0.0f;
};

} // namespace mw101::dsp
```

### 6.3 PRNG and scaling

Default generator: **xorshift32** (the project's named default per ADR-002 scope
note and research/10 §6). Per-sample:

```cpp
uint32_t x = state_;
x ^= x << 13; x ^= x >> 17; x ^= x << 5;   // xorshift32
state_ = x;
// scale uint32 -> [-1, 1) via the fused multiply-add form (research/10 §6)
float out = (float) x * (2.0f / 4294967296.0f) - 1.0f;   // int*(2/2^32) - 1
```

- Range is the half-open `[-1, 1)`, NOT closed [research/10 §6, §9.3].
- `state_` MUST be reseeded nonzero per voice in `reset()` (xorshift cannot
  escape 0).

> Honest-label caveat carried from research: the cited primary source recommends
> a 64-bit LCG/PCG and explicitly "wouldn't use xorshift to create white noise";
> the GENERAL principle (fast integer PRNG + uniform distribution) is fully
> supported, the specific xorshift choice is contested [research/10 §6, §9.1].
> xorshift is adopted as the project default per the scope note; the generator is
> isolated behind `NoiseSource` so it can be swapped for a 64-bit LCG/PCG without
> touching callers. The choice is recorded as `(PI)` in `Calibration.h`.

### 6.4 Optional HF rolloff (off by default)

Optionally model the gentle analog op-amp rolloff at `f_c = 1/(2*pi*220k*240p) ~
3 kHz` (R86/C32) as a single-pole LPF [research/02 §4.2, §4.3, §7.3]. This is NOT
a pinking filter and MUST default OFF (the output is white). When enabled,
`lpfCoeff_` is computed in `prepare()` from `f_c`. `kNoiseHfRolloffHz = 3000.0`
is a `(PI)` derived value (centralize in `Calibration.h`).

## 7. Oscillator section owner (`core/dsp/OscillatorSection.h/.cpp`)

### 7.1 Responsibilities

Per-voice aggregate that owns one `Oscillator`, one `SubOscillator`, one
`NoiseSource`, sequences them so the sub and VCO share one phase, and emits the
four source signals for the mixer. It holds the references to the shared
`MinBlepTable`.

### 7.2 Class signature

```cpp
namespace mw101::dsp {

class OscillatorSection
{
public:
    // hqTable is built once by the engine and shared read-only across voices.
    void prepare (double sampleRate, const MinBlepTable& hqTable) noexcept;
    void reset (uint64_t noiseSeed) noexcept;

    struct Controls
    {
        OscControls vco;        // §4.2
        SubShape    subShape;
        OscAaMode   aaMode;     // derived from mw101.quality, applied to all sources
    };
    void setControls (const Controls& c) noexcept;   // per block

    struct Sources { float saw; float pulse; float sub; float noise; };

    // Render one sample: VCO first (advances master phase), then sub (phase-locked),
    // then noise. noexcept, no allocation. Per-voice HQ escalation (§2.3) is
    // applied here based on Oscillator::frequencyHz().
    [[nodiscard]] Sources renderSample() noexcept;

private:
    Oscillator    vco_;
    SubOscillator sub_;
    NoiseSource   noise_;
    const MinBlepTable* hqTable_ = nullptr;
};

} // namespace mw101::dsp
```

### 7.3 Per-sample ordering (load-bearing)

1. `auto v = vco_.renderSample();` — advances the master phase, records the wrap.
2. Determine effective AA mode for this sample: `aaMode_` OR escalated if
   `vco_.frequencyHz() > kHqEscalationHz(fs)` (§2.3).
3. `float s = sub_.renderSample(vco_.phase(), vco_.wrappedThisSample(),
   vco_.frequencyHz());` — uses the VCO's wrap edge as the 4013 clock so the sub
   is exactly phase-locked [research/02 §7.2].
4. `float n = noise_.renderSample();`
5. Return `{ v.saw, v.pulse, s, n }`. The mixer (§9) applies the four level
   sliders and sums.

The VCO MUST advance the phase exactly once per sample before the sub reads it,
so the sub's clock edge is consistent with the saw wrap within the same sample
[ADR-002 C4; research/02 §7.2].

## 8. Output contract

- All four source outputs (`saw`, `pulse`, `sub`, `noise`) are bipolar floats
  nominally in `[-1, +1]` (noise `[-1, 1)`), pre-level and pre-mix.
- The oscillator section emits raw band-limited sources; level scaling and
  summation are the mixer's responsibility (§9).
- The section runs at base sample rate only; it never oversamples and never
  reads the filter's oversample stride [ADR-002 C7].

## 9. Source mixer (context only — owned by the mixer design doc)

For context, the hardware Source Mixer sums Saw + Pulse + Sub + Noise, each
behind a 100k-linear front-panel level slider (Sub = VR15, Noise = VR16), into
the VCF input op-amp IC18; it is a simple LINEAR sum [research/02 §5, §7.3]. The
digital sub squares are clamped to the oscillator level before summing
[research/02 §7.3]. This document does NOT implement the mixer; the level
parameter IDs (`mw101.*.level`) and the summing topology are owned by the mixer
design doc and `docs/design/06`.

## 10. Acceptance hooks

Objectively-testable properties a backlog task's tests MUST verify:

- **PolyBLEP residual exactness:** `polyBlep(t, dt)` returns
  `2*(t/dt) - (t/dt)^2 - 1` for `t < dt`, `((t-1)/dt)^2 + 2*((t-1)/dt) + 1` for
  `t > 1-dt`, and `0` otherwise, to float tolerance [ADR-002 Contract;
  research/10 §2.3].
- **Saw construction:** band-limited saw equals `(2*t - 1) - polyBlep(t, dt)`
  sample-for-sample at a fixed frequency (C1) [ADR-002 C1].
- **PWM two-BLEP placement:** for a swept duty in [0.05, 0.5], the pulse has
  exactly two corrected transitions per period, with the falling correction at
  the duty-cycle phase; mean (DC) of the band-limited pulse tracks `2*duty - 1`
  within tolerance across the sweep [ADR-002 C2; research/02 §2.5].
- **PWM floor + overlap clamp:** effective duty never goes below
  `max(kPwmDutyMin, dt)`; the two BLEP windows never overlap at the 5%/high-pitch
  extreme [ADR-002 C3; research/02 §2.5].
- **Footage offsets:** 16'/8'/4'/2' produce exact octave ratios (frequency at 4'
  is 2x the 8' frequency, etc.); 8' + Transpose Middle + 0-cent tune = 442.0 Hz
  at A4 [research/02 §2.4, §2.8].
- **Sub phase-lock & drift-free:** sub fundamental is exactly VCO/2 (-1 oct) and
  VCO/4 (-2 oct) at every footage, with zero phase drift over a long run; sub
  edges are sample-aligned to the saw wrap [ADR-002 C4; research/02 §3.4, §7.2].
- **Sub 25% duty + diode-OR:** the OR'd pulse output is high 75% / low 25% of its
  -2 oct period, and its 2nd harmonic is the strongest harmonic (matching the
  diode-OR spectrum), distinguishing it from a naive 25% PWM oscillator only in
  exact edge timing/phase-lock [ADR-002 C5; research/02 §3.5; research/10 §7].
- **No BLAMP:** all edges treated as level steps; no slope-correction path is
  exercised [ADR-002 C6; research/10 §2.4].
- **HQ tier binding:** Eco/Standard use PolyBLEP, HQ uses minBLEP; mode set only
  in prepare/reconfiguration, never per-sample on the audio thread [ADR-018
  Q-table, Q5].
- **HQ auto-escalation:** a voice with VCO fundamental > `kHqEscalationHz`
  switches to the minBLEP applicator regardless of tier, and back below it;
  escalation is never exposed as a parameter [ADR-002 C9; ADR-018 Q6;
  research/10 §8].
- **Alias-suppression A/B oracle:** minBLEP-rendered saw/pulse/sub aliasing floor
  is below the bless gate; PolyBLEP-vs-minBLEP A/B is reproducible and bit-exact
  on macOS arm64 [ADR-002 C8; ADR-018 Q4].
- **Noise whiteness & range:** noise output is in `[-1, 1)` (half-open) and its
  spectrum is flat (white, no pinking) within tolerance over a long block; HF
  rolloff defaults OFF [research/02 §4.3; research/10 §6, §9.3].
- **Noise reseed:** distinct per-voice seeds produce decorrelated streams; a zero
  seed is rejected/avoided [research/10 §6].
- **DPW excluded:** no default-build code path renders DPW; it is reachable only
  behind the compile-time fallback flag [ADR-002 C10].
- **Real-time safety:** all `renderSample`/`scheduleStep`/`next` paths perform no
  heap allocation and take no locks (verified by an allocation/lock guard in
  tests) [ADR-002 C11; ADR-018 Q5].
- **`(PI)` centralization:** every `(PI)` constant in this doc is defined in
  `core/calibration/Calibration.h` and referenced (not duplicated) by the DSP
  sources.

## 11. References

- ADR-002 — Anti-aliased oscillator generation (saw / pulse / PWM / sub).
  `plan/decisions/002-oscillator-antialiasing.md`.
- ADR-018 — Quality-tier parameter registration.
  `plan/decisions/018-quality-tier-parameter.md`.
- ADR-008 — Parameter / state / preset schema (param-ID ownership; referenced).
  `plan/decisions/008-parameter-state-preset-schema.md`.
- ADR-004 — Oversampling strategy (filter path; referenced for tier mapping).
  `plan/decisions/004-oversampling-strategy.md`.
- research/02 — VCO (CEM3340), Sub-Oscillator & Noise.
  `docs/research/02-vco-suboscillator-noise.md`.
- research/10 — DSP Modeling Techniques (adopted theory).
  `docs/research/10-dsp-modeling-techniques.md`.
- `docs/design/06` — Parameter schema (owns parameter IDs/ranges/defaults; this
  doc references them).
