<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Envelope (ADSR), LFO & VCA

## 1. Scope and ownership

### 1.1 What this document owns

This is the single source of truth for the mwAudio101 implementation of the
SH-101 modulation and amplitude DSP: the **one shared ADSR envelope generator**,
the **one LFO (Modulator)** with its four waveform cores and trigger/clock
coupling, and the **BA662A-class VCA** (OTA transconductance taper, ENV/GATE
amplitude source, LFO tremolo, anti-thump). It also owns the default
velocity-routing path into VCA level and VCF cutoff amount, as ratified by
[ADR-016] R-2.

Files this document authorizes the backlog to create (header/source pairs under
`core/dsp/` unless noted):

- `core/dsp/Envelope.h` / `Envelope.cpp` — ADSR generator (Section 2).
- `core/dsp/Lfo.h` / `Lfo.cpp` — single LFO / Modulator core (Section 3).
- `core/dsp/Vca.h` / `Vca.cpp` — BA662A-class VCA taper (Section 4).
- `core/dsp/ModRouting.h` / `ModRouting.cpp` — per-destination depth/velocity
  routing struct shared by this subsystem (Section 5).
- `core/dsp/OnePoleSmoother.h` — the shared `mw::dsp::OnePoleSmoother` one-pole
  de-zipper kind mandated by [ADR-020] S10 and [ADR-009] VV-15 (Section 6.4). This
  type is declared once in that header; docs 03/04/08 all REFERENCE that single
  declaration. This doc treats it as a dependency, not an invention, and does not
  re-declare or alias it.

### 1.2 What this document does NOT own (references only)

- **Parameter IDs, ranges, skews, defaults, smoothing class** are owned by the
  parameter schema in `docs/design/06` per [ADR-008] C7 / [ADR-008] §registry.
  This document REFERENCES proposed IDs (e.g. `mw101.env.attack`) and the numeric
  ranges from research, but doc 06 mints the canonical `APVTS` entries. Where a
  range here disagrees with doc 06, doc 06 wins.
- **All `(PI)` numeric constants** (curve-shape exponents, OTA tanh drive,
  velocity scaling, anti-thump fade) centralize in `core/calibration/Calibration.h`
  (created by the backlog per [ADR-008] §1 / [ADR-009]). This doc names each
  constant and gives its default value; the backlog places the literal there, not
  inlined at a DSP call site ([ADR-020] S13).
- **Control-rate tick / 6-bit DAC / S&H pitch glide / clock-edge model** are owned
  by [ADR-005] and the control-rate design doc. The envelope and LFO consume the
  control-rate tick defined there; the LFO does NOT own the arp/seq clock edge
  logic, only the rate that feeds it (Section 3.6).
- **VCF cutoff modeling**, **VCO pitch / PWM modeling**, and the **noise
  generator** are owned by their own design docs. This doc defines the
  *modulation contributions* the envelope/LFO send to those destinations and the
  *velocity contribution* to VCF cutoff amount, but never the filter or oscillator
  transfer functions. NOISE LFO mode CONSUMES the shared white-noise source; it
  does not define it (Section 3.5).
- **Drift / vintage variance** of times and rates is owned by [ADR-009]; this doc
  exposes the smoothed targets that drift writes to (Section 6.3), not the drift
  engine.

### 1.3 Honesty labels carried from research

Per [research/04 §1, §5] and [ADR-013] honesty discipline, the following are
**theory/inference, unmeasured** and MUST be modeled as tunable `(PI)` constants,
never asserted as measured SH-101 specs:

- Envelope **segment curve law** (exponential RC) [research/04 §2.6, §5.1].
- VCA **control law** (linear-in-current OTA; presence of an exp converter ahead
  of the BA662 is unconfirmed) [research/04 §4.3, §5.1].
- Any **OTA saturation / soft-clip** (argument from absence) [research/04 §4.7].

The following are FROZEN facts (high confidence) and MUST NOT be regressed: one
shared ADSR (no separate filter+amp EG) [research/04 §2.1]; ADSR ranges
[research/04 §2.2]; one LFO, 0.1-30 Hz, **four** waveforms, **no sine core**
[research/04 §3.1, §3.2]; RANDOM is CPU+DAC digital pseudo-S/H, not analog
[research/04 §3.4]; VCA is BA662A-class OTA with ENV/GATE source plus LFO tremolo
[research/04 §4.1, §4.4].

## 2. ADSR envelope generator

### 2.1 Responsibilities and invariants

`core/dsp/Envelope.h` implements **exactly one** ADSR generator per voice, shared
across all three destinations (VCF cutoff, VCA gain, VCO pulse width); there is no
separate filter envelope or amp envelope [research/04 §2.1, §6.1]. The envelope
produces a single normalized contour `[0, 1]`; per-destination depth scaling is
applied downstream by `ModRouting` (Section 5), not inside the envelope.

Real-time invariants ([ADR-001], [ADR-019] VT-01, [ADR-020] S14):

- All state is POD; coefficients are precomputed in `prepare(...)`; no heap
  allocation and no locks on the audio thread.
- `process()` and `processBlock()` are `noexcept` hot paths.
- The envelope advances on the [ADR-005]/[ADR-016] control-rate tick cadence and
  is sample-accurate at block boundaries; it never reads a wall-clock timer.

### 2.2 Class layout

```cpp
namespace mw101::dsp {

enum class EnvStage : uint8_t { Idle, Attack, Decay, Sustain, Release };

// GATE+TRIG / GATE / LFO trigger source (research/04 §2.3). The LFO mode is
// driven by Lfo's clock edge (Section 3.6); the envelope only consumes triggers.
enum class EnvTrigMode : uint8_t { GateTrig, Gate, Lfo };

struct EnvParams           // snapshot pushed from the control-rate update; POD
{
    float attackSec  = 0.003f;   // see Section 2.3 table for ranges
    float decaySec   = 0.060f;
    float sustain    = 0.7f;     // 0..1 fraction of attack peak
    float releaseSec = 0.100f;
    EnvTrigMode trig = EnvTrigMode::GateTrig;
    float curve      = 1.0f;     // (PI) shaping constant; see Section 2.4
};

class Envelope
{
public:
    void  prepare (double sampleRate, int controlRateDivider) noexcept;
    void  reset() noexcept;                       // -> Idle, level 0

    void  setParams (const EnvParams&) noexcept;  // called on control-rate tick

    // Triggering. gateOn=true on note-on; the trig mode decides retrigger.
    void  noteOn  (bool legato) noexcept;         // honors GATE vs GATE+TRIG
    void  noteOff() noexcept;                      // -> Release
    void  clockTrigger() noexcept;                // LFO-mode retrigger (Sect 3.6)

    // Hot path. Advances one control-rate tick worth of contour and returns the
    // current normalized level; the caller upsamples/holds across the block.
    float tick() noexcept;                         // returns level in [0,1]

    EnvStage stage()  const noexcept { return stage_; }
    bool     active() const noexcept { return stage_ != EnvStage::Idle; }
    float    level()  const noexcept { return level_; }

private:
    double sampleRate_      = 48000.0;
    int    ticksPerControl_ = 1;
    EnvStage stage_         = EnvStage::Idle;
    float  level_           = 0.0f;     // current normalized output
    float  target_          = 0.0f;     // stage asymptote (Section 2.4)
    float  coeff_           = 0.0f;     // per-tick one-pole coefficient
    float  sustain_         = 0.7f;
    float  curve_           = 1.0f;
    EnvTrigMode trig_       = EnvTrigMode::GateTrig;
    // precomputed per-stage coefficients refreshed in setParams()
    float  aCoeff_ = 0, dCoeff_ = 0, rCoeff_ = 0;
};

} // namespace mw101::dsp
```

### 2.3 Time / level ranges (from research, owned by doc 06)

All four ranges are verbatim from the service manual [research/04 §2.2, §2.7].
Doc 06 owns the canonical IDs/skews; the proposed IDs and ranges are:

| Proposed param ID | Stage | Range | Unit | Default | Skew | Source |
|---|---|---|---|---|---|---|
| `mw101.env.attack` | Attack time | 1.5 ms – 4 s | seconds | 3 ms `(PI)` default | logarithmic | [research/04 §2.2] |
| `mw101.env.decay` | Decay time | 2 ms – 10 s | seconds | 60 ms `(PI)` default | logarithmic | [research/04 §2.2] |
| `mw101.env.sustain` | Sustain level | 0 – 100% | fraction of attack peak | 0.7 `(PI)` default | linear | [research/04 §2.2] |
| `mw101.env.release` | Release time | 2 ms – 10 s | seconds | 100 ms `(PI)` default | logarithmic | [research/04 §2.2] |

Notes:

- Decay and Release **share the same 2 ms – 10 s range** [research/04 §2.2]; they
  are independent parameters but identical range/skew.
- Sustain is a **level (fraction of the attack peak)**, not a time; it is clamped
  to `[0, 1]` [research/04 §2.2].
- The default times above are out-of-box `(PI)` patch values, NOT spec minima.
  Per doc 06 §11, INIT defaults live in the INIT patch (doc 06 / [ADR-016]);
  `Calibration.h` holds only the shaping constants (curve law, overshoot, time
  scale — Section 2.4), not these per-parameter INIT defaults.
- System 100 "Model 101" ranges (Attack 0.4 ms–3 s, etc.) MUST NOT be used
  [research/04 §2.8].

### 2.4 Segment curve law — (PI) shaping constant

The segment shape is **inferred, unmeasured theory** (exponential RC charge /
discharge) [research/04 §2.6, §5.1]. The implementation models each stage as a
one-pole approach toward a per-stage target, with the curve exposed as a single
tunable shaping constant so it is honest about being inference, not spec.

Per-stage one-pole coefficient for a time `T` (the 1/e-ish time constant scaled to
hit the snap threshold within `T`) at control-tick rate `fc = sampleRate /
ticksPerControl`:

```
coeff = exp( -1 / ( max(T, Tmin) * fc * kEnvTimeScale ) )
level += (target - level) * (1 - coeff)
```

- **Attack** target is set ABOVE 1.0 (asymptotic, "snappy") to the calibration
  constant `kEnvAttackOvershoot`; the stage transitions to Decay when `level >=
  1.0` (clamped to 1.0) [research/04 §2.6].
- **Decay** target = `sustain`; transitions to Sustain when within snap threshold.
- **Sustain** holds `level = sustain` (no movement) while gated.
- **Release** target = 0.0; transitions to Idle at the snap threshold.
- The snap-to-target threshold is shared with the de-zipper policy so the integer
  stage bookkeeping is deterministic across platforms ([ADR-020] S10, S12).

Calibration constants (placed in `core/calibration/Calibration.h`; all `(PI)`):

| Constant | Purpose | Default | Tag |
|---|---|---|---|
| `kEnvAttackOvershoot` | Attack asymptote above unity (snappy charge) | 1.25 | (PI) |
| `kEnvTimeScale` | Maps a user "time" to the 1/e constant so the audible segment ≈ the labeled time | 0.20 | (PI) |
| `kEnvCurve` | Default `EnvParams::curve` shaping exponent (1.0 = near-RC) | 1.0 | (PI) |
| `kEnvSnapThreshold` | Level distance at which a stage snaps to target / advances | 1.0e-4 | (PI) |
| `kEnvTimeMin` | Floor on any segment time to bound the coefficient | 1.0e-4 s | (PI) |

The `curve` field MAY be exposed later as a user "curve" control; for v1 it
defaults to `kEnvCurve` and is not user-facing unless doc 06 mints an ID. The
curve law being configurable is the required honest stance [research/04 §2.6].

### 2.5 Trigger state machine (GATE+TRIG / GATE / LFO)

Three trigger modes [research/04 §2.3, §6.1]:

- **GateTrig**: every new note-on (incl. legato) fires a fresh Attack — trills /
  rapid restarts. `noteOn(legato=*)` always restarts from Attack.
- **Gate**: one shot per held gate; legato does NOT retrigger. `noteOn(legato=true)`
  is ignored while already non-Idle; only the initial gate starts Attack.
- **Lfo**: the LFO clocks the envelope; `clockTrigger()` (called from `Lfo` on its
  cycle edge, Section 3.6) restarts Attack while a key is held, independent of new
  key presses.

Open behavioral gap (carry as `(PI)` choice, document in code): on a GateTrig
retrigger, whether the envelope snaps to 0 before re-attacking or re-attacks from
the current level is **not formally specified** [research/04 §5.3]. v1 re-attacks
from the **current level** (no discontinuity / no click); this is a `(PI)` choice
and noted as an open validation gap, not a measured fact.

## 3. LFO (Modulator)

### 3.1 Responsibilities and invariants

`core/dsp/Lfo.h` implements **one** LFO ("MODULATOR / LFO·CLK"), rate **0.1–30 Hz**
[research/04 §3.1]. It is a single-selection source (one waveform active at a
time, not simultaneous outputs) [research/04 §3.2]. It is discrete circuitry,
independent of the VCO/VCF emulations [research/04 §3.6]. The same instance
provides the rate that drives the arp/seq clock (the *edge logic* is owned by the
control-rate / arp doc, [ADR-005]); this doc owns only the oscillator core and the
per-destination value it emits.

Real-time invariants are identical to Section 2.1 (POD state, coefficients in
`prepare`, `noexcept` hot path, control-rate cadence, no alloc/locks).

### 3.2 Waveform selector — four positions, NO sine core

The selector has **four** positions on the original hardware [research/04 §3.2].
The smooth ("sine"-symbol) position is a **triangle rounded toward sine** by a
fixed shaper, NOT a pure sine and NOT a separate sine core [research/04 §3.3].

```cpp
namespace mw101::dsp {

// FOUR positions only. Do NOT add Sine/Saw: the six-position selector is a
// software-reissue (SH-01A) artifact, not 1982 hardware (research/04 §3.2).
enum class LfoShape : uint8_t {
    SmoothTri = 0,   // triangle, fixed-shaped "rounded toward sine"
    Square    = 1,
    Random    = 2,   // CPU+DAC digital pseudo sample/hold (research/04 §3.4)
    Noise     = 3    // white noise from the audio-path generator (research/04 §3.5)
};

} // namespace mw101::dsp
```

Doc 06 owns the parameter; proposed ID `mw101.lfo.shape` (choice, 4 entries,
default `SmoothTri`). This is a stepped/choice selector: per [ADR-020] S7 it is
**NOT value-smoothed**; an audible switch uses the [ADR-005] click-safe crossfade
(Section 6.5).

### 3.3 LFO class layout

```cpp
namespace mw101::dsp {

class Lfo
{
public:
    void  prepare (double sampleRate, int controlRateDivider) noexcept;
    void  reset() noexcept;                        // phase -> 0, S/H reload

    void  setRateHz (float hz) noexcept;           // clamped to [0.1, 30] Hz
    void  setShape  (LfoShape) noexcept;
    void  resetPhaseOnKey() noexcept;              // clock-reset-on-keypress hook

    // Hot path: advance one control-rate tick. Returns bipolar value in [-1, 1]
    // for SmoothTri/Square/Random; Noise returns the bandlimited noise sample.
    float tick() noexcept;

    // True for one tick on the H->L cycle edge; consumers (Envelope LFO-trigger,
    // arp/seq clock owned elsewhere) read this. Edge logic for arp lives in the
    // control-rate doc; we only flag the oscillator's own cycle boundary.
    bool  cycleEdge() const noexcept { return edge_; }

    float value() const noexcept { return value_; }

    // White-noise source is injected, never owned here (research/04 §3.5, §1.2).
    void  setNoiseSource (const float* sharedNoiseSample) noexcept;

private:
    double sampleRate_   = 48000.0;
    int    ticksPerCtl_  = 1;
    float  phase_        = 0.0f;        // [0,1)
    float  phaseInc_     = 0.0f;        // per-tick increment from rate
    LfoShape shape_      = LfoShape::SmoothTri;
    float  value_        = 0.0f;
    bool   edge_         = false;
    float  shReg_        = 0.0f;        // Random sample/hold register
    uint64_t rngState_   = 0;           // POD PRNG for Random (seeded, det.)
    const float* noiseSample_ = nullptr;
};

} // namespace mw101::dsp
```

### 3.4 Rate range and phase

| Proposed param ID | Range | Unit | Default | Skew | Source |
|---|---|---|---|---|---|
| `mw101.lfo.rate` | 0.1 – 30 | Hz | 5 Hz `(PI)` patch default | logarithmic / "exponential-ish feel" | [research/04 §3.1, §6.2] |

- `setRateHz` clamps to `[0.1, 30]`; the disputed 0.35 Hz minimum is a clone
  artifact and MUST NOT be used [research/04 §3.1, §5.1].
- `phaseInc_ = rateHz / fc` where `fc = sampleRate / ticksPerControl`.
- `kLfoRateSkew` `(PI)` shapes the pot taper (exp-ish feel); doc 06 may instead
  encode the skew on the `APVTS` parameter — if so, the engine receives Hz
  directly and `kLfoRateSkew` is unused.

### 3.5 Waveform cores

- **SmoothTri** [research/04 §3.3]: native triangle from phase, then a fixed
  shaper rounds it toward sine. Model the shaper as a tunable blend
  `out = lerp(tri, sineApprox(tri), kLfoSmoothShape)` with `kLfoSmoothShape` a
  `(PI)` constant (default toward "rounded," not pure sine). Label as
  "rounded toward sine," never as mathematically pure sine.
- **Square** [research/04 §3.2]: `out = (phase < 0.5) ? +1 : -1`. (Edge-jump zipper
  is bounded by the S3 PWM-class smoother only on the *PWM destination*, not on the
  raw LFO; the square LFO itself is intentionally hard-edged.)
- **Random** [research/04 §3.4]: a sample/hold register `shReg_` reloaded with a
  **uniform** pseudo-random value on each LFO cycle edge (digital, CPU+DAC style),
  NOT an analog noise S&H. Range internally `[-1, 1]` (mapped from the original
  0..+10 V 6-bit pseudo-S/H). The PRNG is a seeded POD generator so the value
  stream is deterministic for the golden harness.
- **Noise** [research/04 §3.5]: routes the **shared** white-noise sample injected
  via `setNoiseSource` (same source as the audio mixer, full-bandwidth). A fixed
  `(PI)` ~16 kHz / ~-3 dB one-pole on the modulation bus (`kModBusLpHz`) is applied
  to all modulation signals per [research/04 §3.5]; this LPF is part of
  `ModRouting` (Section 5), not duplicated per shape.

Calibration constants (`Calibration.h`, all `(PI)`):

| Constant | Purpose | Default | Tag |
|---|---|---|---|
| `kLfoSmoothShape` | Triangle→sine rounding blend (0 = triangle, 1 = sine) | 0.85 | (PI) |
| `kLfoRateSkew` | Rate pot taper if not encoded in doc 06 skew | 0.3 | (PI) |
| `kModBusLpHz` | Fixed modulation-bus low-pass corner | 16000 Hz | (PI) |

### 3.6 LFO-driven destinations and clock coupling

Routing is **fixed with per-destination depths**, not a matrix [research/04 §3.6,
§6.2]. The LFO value feeds (all owned/scaled by `ModRouting`, Section 5):

- **VCO pitch** (single MOD depth) — vibrato. Contribution sent to the VCO doc.
- **Pulse width** (own ENV / MANUAL / LFO source switch + PWM depth) — PWM
  destination owned by the VCO doc; this doc supplies the LFO value when the PWM
  source is LFO.
- **VCF cutoff** (own MOD depth, alongside ENV depth and Key Follow 0–100%) —
  contribution sent to the VCF doc.
- **VCA / gate tremolo** via the GATE+TRIG / GATE / LFO selector (Section 4.4).
- **Envelope LFO-trigger**: when `EnvTrigMode::Lfo`, `Lfo::cycleEdge()` drives
  `Envelope::clockTrigger()` (Section 2.5).

Clock coupling: the same LFO rate is the master arp/seq clock; the **edge advance
logic, clock-reset-on-keypress, and EXT CLK IN override (+2.5 V) belong to
[ADR-005] / the control-rate doc** [research/04 §3.6]. This doc exposes only
`cycleEdge()` and `resetPhaseOnKey()` as hooks. Exact mod-depth constants
(V/oct → VCO, Hz/V → VCF, %/V → PWM) are **measurement-required open gaps**
[research/04 §3.6, §5.3] and are carried as `(PI)` depth scalings in Section 5.

## 4. VCA (BA662A-class OTA)

### 4.1 Responsibilities and invariants

`core/dsp/Vca.h` is a **linear-control-current OTA** model of the BA662A (IC15)
[research/04 §4.1, §4.3, §6.3]: `out = taper(control) * in`, where the control is a
summing node of the selected amplitude source (ENV or GATE) plus an LFO tremolo
contribution. The BA662 internal architecture is reverse-engineered, not
datasheet-documented [research/04 §4.2] — the model is behavioral (transfer taper),
NOT a transistor-level netlist. Same RT invariants as Section 2.1.

### 4.2 Class layout

```cpp
namespace mw101::dsp {

// VCA amplitude source switch (research/04 §4.4). HOLD is an AMSynths-clone
// extension and is NOT confirmed on original hardware (research/04 §4.4, §5.3);
// v1 models the documented ENV/GATE pair only. Do not add HOLD without an ADR.
enum class VcaMode : uint8_t { Env = 0, Gate = 1 };

class Vca
{
public:
    void  prepare (double sampleRate) noexcept;
    void  reset() noexcept;

    void  setMode (VcaMode) noexcept;
    void  setDrive (float driveNorm) noexcept;     // optional OTA character (Sect 4.5)

    // control = amplitude source (ENV level or GATE 0/1) summed with LFO tremolo,
    // velocity already folded in by ModRouting (Section 5). Range [0,1].
    // Returns the gained sample; processBlock variant for the hot loop.
    float process (float in, float control) noexcept;
    void  processBlock (float* buffer, const float* control, int n) noexcept;

private:
    double sampleRate_ = 48000.0;
    VcaMode mode_      = VcaMode::Env;
    float  drive_      = 0.0f;
    // anti-thump state (Section 4.6)
    float  offsetNull_ = 0.0f;
    float  gateFade_   = 0.0f;        // smoothed gate open/close
    float  gateFadeCoeff_ = 0.0f;
};

} // namespace mw101::dsp
```

Doc 06 owns the mode parameter; proposed ID `mw101.vca.mode` (choice ENV/GATE,
default `Env`). It is a stepped selector ([ADR-020] S7): not value-smoothed; the
ENV↔GATE switch is made click-safe by the anti-thump gate fade (Section 4.6) and,
if needed, the [ADR-005] crossfade.

### 4.3 Control law / taper — (PI)

The OTA transconductance is **linearly proportional to control current** (standard
OTA theory) [research/04 §4.3]. Whether the SH-101 inserts an exp converter ahead
of the BA662 is **unconfirmed** [research/04 §4.3, §5.1], so the taper is a
**tunable** centralized constant set:

```
taper(control) = pow(clamp(control, 0, 1), kVcaTaperExp)   // dB-curve shaping
out = tanh(kVcaOtaDrive * taper(control) * in) / tanh(kVcaOtaDrive)  // OTA tanh
```

- `kVcaTaperExp` selects the perceptual curve: 1.0 = linear-in-current (raw OTA
  decay shape), > 1.0 = pre-shaped toward dB-linear (as if an exp converter were
  present). Default leans toward a musical exponential while staying labeled as a
  tunable, not a spec [research/04 §4.3, §6.3].
- The `tanh` is the OTA soft taper; with `kVcaOtaDrive` low the VCA stays in the
  OTA's linear window (matching typical Roland input attenuation) [research/04
  §4.7]. There is **no documented dedicated saturation stage** — drive is exposed
  as an optional character control, not as documented original behavior
  [research/04 §4.7, §5.1].

Calibration constants (`Calibration.h`, all `(PI)`):

| Constant | Purpose | Default | Tag |
|---|---|---|---|
| `kVcaTaperExp` | Control→gain curve exponent (1.0 linear-OTA … >1 dB-ish) | 2.0 | (PI) |
| `kVcaOtaDrive` | OTA tanh drive (linear window vs. soft compression) | 1.0 | (PI) |
| `kVcaAntiThumpMs` | Gate open/close fade time (anti-thump) | 2.0 ms | (PI) |
| `kVcaOffsetNull` | Residual DC offset nulled at gate transition | 0.0 | (PI) |

### 4.4 Amplitude sources: ENV / GATE + LFO tremolo

The control input is assembled by `ModRouting` (Section 5) and passed to
`Vca::process` [research/04 §4.4, §6.3]:

- **Env**: control follows the ADSR contour (Section 2).
- **Gate**: control = flat full level for the gate duration (organ-style; envelope
  shape bypassed).
- **LFO tremolo**: the LFO contribution is summed onto the control regardless of
  mode (SH-101-specific; MC-202 omits it) [research/04 §4.4]. Velocity (when ON)
  scales the control per Section 5.

### 4.5 Output level / drive placement

- Full-scale output is referenced to **0 dBm (~0.775 Vrms)** at the OUTPUT jack
  [research/04 §4.6]. Internal headroom references the **actual rails (+15 V main,
  +14 V, ±5 V)** — NOT ±15 V [research/04 §4.6, §5.2].
- Any OTA drive/nonlinearity is an **optional character parameter**
  (`mw101.vca.drive` if doc 06 mints it), defaulting to the linear window
  [research/04 §4.7]. Global drive *placement* in the chain is governed by
  [ADR-017]; this doc only provides the VCA's own tanh taper, not the FX-drive
  stage.

### 4.6 Anti-thump (low-offset selection)

The BA662**A** low-offset grade was chosen so the VCA opens without an audible
thump [research/04 §4.5, §6.3]. Model:

- Null residual DC offset at the gate open/close transition (`kVcaOffsetNull`,
  default 0 — i.e. clean by default).
- Apply a short one-pole fade (`gateFade_`, time `kVcaAntiThumpMs`) on the
  control at gate edges so onset/offset is clean rather than a click. This is the
  click-safe mechanism for the ENV↔GATE switch and for note on/off, sharing the
  one-pole smoother kind ([ADR-020] S10, level/amplitude class S4 conceptually).

## 5. Modulation routing, depths & velocity

### 5.1 Responsibilities

`core/dsp/ModRouting.h` is the fixed-routing combiner [research/04 §3.6]: it
scales the one envelope and the one LFO value by per-destination depths and folds
in the default velocity routing. It owns NO parameter IDs (doc 06) and NO
destination DSP (VCO/VCF docs). It is a POD struct of depth scalars plus the
mod-bus LPF state.

```cpp
namespace mw101::dsp {

struct ModDepths    // values set per control-rate tick from de-zippered params
{
    float lfoToPitch  = 0.0f;   // VCO MOD depth        (V/oct (PI) scaled)
    float lfoToCutoff = 0.0f;   // VCF MOD depth        (Hz/V  (PI) scaled)
    float lfoToPw     = 0.0f;   // PWM depth (LFO src)  (%/V   (PI) scaled)
    float lfoToVca    = 0.0f;   // tremolo depth
    float envToCutoff = 0.0f;   // VCF ENV depth
    float envToPw     = 0.0f;   // PWM depth (ENV src)
    float keyFollow   = 0.0f;   // VCF Key Follow 0..1
};

struct VelocityRouting    // ADR-016 R-2: ON by default
{
    bool  enabled       = true;          // out-of-box ON (ADR-016 R-2)
    float toVcaAmount   = 1.0f;          // velocity -> VCA level  (PI) scale
    float toCutoffAmount= 1.0f;          // velocity -> VCF cutoff (PI) scale
};

struct ModBus      // POD, sized in prepare
{
    float lpState = 0.0f;   // kModBusLpHz one-pole on mod signals (research/04 §3.5)
    float lpCoeff = 0.0f;
};

} // namespace mw101::dsp
```

### 5.2 Velocity routing (default ON per ADR-016)

Per [ADR-016] R-2 and [research/04 referenced docs/08], velocity is **ON by
default** and routes to **VCA level + VCF cutoff amount** — the documented physical
nodes — additively, not as invented structure. The faithful no-velocity behavior
is one switch away (doc 06 mints the on/off control; default ON).

- **VCA level**: `vcaControl = baseAmp * lerp(1.0, velNorm, toVcaAmount * velEnabled)`
  where `baseAmp` is the ENV/GATE source level plus LFO tremolo. So at full
  velocity the level is unchanged; at low velocity it scales down by
  `toVcaAmount`.
- **VCF cutoff amount**: velocity adds to the cutoff modulation amount sent to the
  VCF doc: `cutoffMod += velNorm * toCutoffAmount * velEnabled`. The actual cutoff
  transfer is the VCF doc's; this doc supplies the additive velocity contribution
  only.

Velocity scaling constants are `(PI)` and live in `Calibration.h`:

| Constant | Purpose | Default | Tag |
|---|---|---|---|
| `kVelToVca` | Default `VelocityRouting::toVcaAmount` | 0.7 | (PI) |
| `kVelToCutoff` | Default `VelocityRouting::toCutoffAmount` | 0.5 | (PI) |
| `kVelCurve` | Velocity input curve (0..127 → 0..1) shaping | 1.0 | (PI) |

### 5.3 Depth-scaling open gaps

The exact modulation-depth constants (V/oct, Hz/V, %/V) and the true LFO minimum
frequency are **measurement-required open gaps** [research/04 §3.6, §5.3]; the
depth scalars above are `(PI)` defaults in `Calibration.h` and MUST be tagged as
modeling choices, never asserted as measured SH-101 amounts.

## 6. Cross-cutting: smoothing, control rate, real-time

### 6.1 De-zipper policy (ADR-020)

Continuous params in this subsystem opt into the per-class one-pole de-zipper;
stepped selectors are NOT smoothed. Mapping to [ADR-020] classes (the time
constants are owned by the calibration table; the class is declared per param in
doc 06's registry):

| This-doc param (proposed ID) | ADR-020 class | Default τ | Mechanism |
|---|---|---|---|
| `mw101.lfo.depth_pitch` / `mw101.lfo.depth_cutoff` (LFO depths) | S2 fast sonic | ~10 ms `(PI)` | one-pole de-zipper |
| `mw101.env.*` ADSR knob values | S2 fast sonic | ~10 ms `(PI)` | one-pole de-zipper |
| `mw101.lfo.depth_pwm` (LFO→PWM depth) | S2 fast sonic | ~10 ms `(PI)` | one-pole de-zipper |
| `mw101.vco.pwm_depth` (manual PWM width) | S3 PW/PWM | ~5 ms `(PI)` | one-pole de-zipper |
| `mw101.vca.level` / `mw101.vel.depth` (velocity amount) | S4 level/amp | ~15 ms `(PI)` | one-pole de-zipper |
| `mw101.lfo.rate` | S2 fast sonic | ~10 ms `(PI)` | one-pole de-zipper |
| `mw101.lfo.shape`, `mw101.vca.mode`, `mw101.key.trigger_priority` | S7 stepped | n/a | NOT smoothed; [ADR-005] crossfade where audible |

The envelope CONTOUR and the LFO VALUE themselves are NOT de-zippered — they are
generated signals, not parameter targets. De-zippering applies to the *knob
values* feeding `setParams` / depths ([ADR-020] S5/S6 distinction). The anti-thump
fade (Section 4.6) is a DSP click-guard at the gate edge, not a param de-zipper.

### 6.2 Control-rate cadence

The envelope and LFO advance on the [ADR-005]/[ADR-016] control-rate tick
(`ticksPerControl` passed to `prepare`), sample-accurate at block boundaries,
driven by a sample counter inside `process` — never a wall-clock timer or
background thread [ADR-005] C. Param de-zippers advance on the same tick
([ADR-020] S11). The block-boundary update bookkeeping is CLASS-EXACT and MUST be
bit-identical on macOS arm64 and Linux x64 ([ADR-020] S12, [ADR-013]); the
generated audio value is CLASS-FP.

### 6.3 Drift / vintage variance

Time/rate targets exposed by `EnvParams` and `Lfo::setRateHz` are the de-zippered
targets that the [ADR-009] drift engine writes to; the [ADR-009] mandatory
per-voice one-pole output smoother (VV-15, ~5–20 ms) sits downstream. This doc
does NOT implement drift; it provides the settable targets and relies on the
shared one-pole smoother kind so there is no second smoother flavor [ADR-020] S10.

### 6.4 Shared one-pole smoother kind

`mw::dsp::OnePoleSmoother` (declared once in `core/dsp/OnePoleSmoother.h`) is the
single one-pole exponential smoother with a deterministic snap-to-target
threshold, shared with [ADR-009] VV-15 and mandated by [ADR-020] S10. Its
canonical interface is fields `{ float current, target, coeff; }` with methods
`setTimeConstant(float ms, double sr)`, `setTarget(float)`, and
`float tick() noexcept`. This doc (alongside docs 04 and 08) **REFERENCES** that
single declaration — it is a dependency, not re-declared and not aliased (no
`OnePoleLpf`) here. See `core/dsp/OnePoleSmoother.h` for the authoritative
definition.

### 6.5 Click-safe switching

Audible selector switches (`mw101.lfo.shape` when sounding, `mw101.vca.mode`) use
the [ADR-005] click-safe CV crossfade (precompute both branches, blend, branchless
hot path, no allocation on switch) [ADR-020] S7. The VCA ENV↔GATE transition is
additionally guarded by the anti-thump fade (Section 4.6).

## Acceptance hooks

Objectively-testable properties a backlog task's tests MUST verify:

- **Single shared envelope**: there is exactly one `Envelope` instance per voice
  feeding VCF cutoff, VCA gain, and PWM; no separate filter/amp EG exists
  [research/04 §2.1].
- **ADSR ranges honored**: Attack maps 1.5 ms–4 s; Decay and Release each map
  2 ms–10 s and share an identical range; Sustain is a level clamped to `[0,1]`,
  not a time [research/04 §2.2]. (Canonical ranges asserted against doc 06.)
- **Envelope monotonic stages**: Attack rises monotonically to a clamped 1.0,
  Decay falls toward sustain, Sustain holds, Release falls to 0; the stage
  machine reaches Idle within the snap threshold of the labeled Release time.
- **Trigger modes**: GateTrig retriggers on legato; Gate does not; Lfo retriggers
  on `Lfo::cycleEdge()` while held [research/04 §2.3].
- **LFO has exactly four shapes**, no Sine and no Saw enumerators exist; selecting
  `SmoothTri` produces a triangle-rounded-toward-sine (not a pure sine)
  [research/04 §3.2, §3.3].
- **LFO rate clamped to [0.1, 30] Hz**; 0.35 Hz is never the enforced minimum
  [research/04 §3.1].
- **Random is digital uniform S/H**: the value changes only on cycle edges, is
  uniform, and is deterministic for a fixed seed (golden-reproducible)
  [research/04 §3.4].
- **Noise uses the shared white-noise source** (injected), not a private
  generator, and the mod bus applies the fixed `kModBusLpHz` LPF [research/04 §3.5].
- **VCA gain is monotonic in control**, `process(in, 0) == 0`, and at full control
  the gain matches the calibrated full-scale; no audible thump on a 0→1 gate edge
  (energy in the first `kVcaAntiThumpMs` is bounded) [research/04 §4.3, §4.5].
- **VCA ENV vs GATE**: ENV mode follows the ADSR contour; GATE mode holds a flat
  level for the gate duration [research/04 §4.4].
- **Velocity ON by default** routes to VCA level and VCF cutoff amount; turning the
  velocity switch OFF removes both contributions (faithful pole) [ADR-016] R-2.
- **All listed `(PI)` constants resolve from `core/calibration/Calibration.h`**,
  not inlined at call sites ([ADR-020] S13, [ADR-008]).
- **De-zipper class correctness**: a continuous param (e.g. env time, LFO depth)
  de-zippers a step input; a stepped selector (`mw101.lfo.shape`) does NOT smear
  through wrong indices ([ADR-020] S7, S12 paired positive/negative test).
- **Real-time safety**: `Envelope`, `Lfo`, `Vca` perform no heap allocation and no
  locks in `process`/`tick`; hot paths are `noexcept`; all buffers/state sized in
  `prepare` ([ADR-001], [ADR-019] VT-01, [ADR-020] S14).
- **Control-rate determinism**: envelope/LFO advance on the control-rate tick and
  block-boundary bookkeeping is bit-identical on macOS arm64 and Linux x64
  ([ADR-020] S12, [ADR-013]).

## References

ADRs (normative contracts):

- [ADR-001] core/plugin boundary, `noexcept process` seam —
  `plan/decisions/001-core-plugin-boundary.md`
- [ADR-005] control-rate model / 6-bit CV / control tick / crossfade —
  `plan/decisions/005-control-rate-cpu-authenticity.md`
- [ADR-008] parameter / state / preset schema (owns param IDs, calibration table) —
  `plan/decisions/008-parameter-state-preset-schema.md`
- [ADR-009] vintage variance / drift, one-pole output smoother (VV-15) —
  `plan/decisions/009-vintage-variance-model.md`
- [ADR-013] testing / golden calibration harness (CLASS-EXACT / CLASS-FP) —
  `plan/decisions/013-testing-golden-calibration-harness.md`
- [ADR-016] owner ratifications (velocity ON → VCA level + VCF cutoff, R-2) —
  `plan/decisions/016-owner-ratifications-2026-06-18.md`
- [ADR-017] plugin latency / PDC / drive placement —
  `plan/decisions/017-plugin-latency-pdc-and-drive-placement.md`
- [ADR-019] voice-rendering threading model (single-threaded `process`) —
  `plan/decisions/019-voice-rendering-threading-model.md`
- [ADR-020] parameter smoothing / de-zipper policy —
  `plan/decisions/020-parameter-smoothing-policy.md`

Research (cited factual ground truth):

- [research/04] ADSR Envelope, LFO & VCA —
  `docs/research/04-envelope-lfo-vca.md` (Sections 2, 3, 4, 5, 6 cited throughout)

Cross-document ownership (referenced, not redefined here):

- `docs/design/06` — parameter schema: canonical IDs, ranges, skews, defaults,
  smoothing class ([ADR-008] C7).
- `core/calibration/Calibration.h` — the single `(PI)` calibration table (created
  by the backlog).
