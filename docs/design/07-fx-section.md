<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# FX Section: Drive, Chorus, Delay

## 1. Scope and contract framing

### 1.1 What this document owns

This document specifies the **post-voice FX section** of mwAudio101: a
fixed-order, mono-input serial chain **Drive -> Chorus -> Delay** that runs
**once on the mono voice sum** (after poly/unison/drift, after the per-voice
oversampled voice output) and produces a stereo output [ADR-010]. It owns the
DSP topology, class/file layout, per-FX algorithms, mono-to-stereo widening,
bypass semantics, the FX contribution to plugin delay compensation (PDC), and
the real-time invariants of the chain.

It defines concrete C++ signatures, data layouts, numeric tables, and the
acceptance properties a backlog task must verify.

### 1.2 What this document does NOT own (references only)

- **Parameter IDs, ranges, units, skews, defaults, automatable flags** are owned
  by the parameter schema, **docs/design/06 §2** (the APVTS contract per
  [ADR-008]). This document REFERENCES those parameter IDs by name and gives the
  *DSP-facing* interpretation (what the normalized value means to the algorithm);
  it never re-mints an ID, range, or skew. Where a table here lists a "suggested
  range," that is advisory input to doc 06 and is non-normative until doc 06
  registers it.
- **The blessed mono voice path** (oscillators -> sub -> mixer -> IR3109 VCF ->
  BA662 VCA -> drift -> per-voice oversampled output) is owned by the voice
  design docs. The FX section consumes its mono output and is explicitly
  **outside the modeled-signal-path / bit-exact contract** [ADR-010 Decision;
  ADR-017].
- **The `setLatencySamples` / host PDC call** is owned by `plugin/` per
  [ADR-001] and [ADR-017 L4]. This document specifies only the FX section's
  *deterministic group-delay contribution* and exposes it as a queryable
  constant; `plugin/` sums it with the per-voice zone delay and reports the
  worst-case constant.
- **Parameter smoothing policy** (smoothing time constants, the smoother type)
  is governed by [ADR-020]; this document states *which* params are smoothed and
  the smoothing semantics required for click-free operation, deferring the
  default time constants to doc 06 / ADR-020.
- **The single calibration table** `core/calibration/Calibration.h` (created by
  the backlog per [ADR-008]) centralizes every invented constant tagged `(PI)`
  below. This document names the constants; the backlog defines them there.

### 1.3 Normative anchors

The behavioral contract of this subsystem is fixed by [ADR-010] Contract rows
FX-1..FX-14 and [ADR-017] Contract rows L1..L11. Where this document and those
ADR rows could appear to differ, the ADR rows win and this document is the
implementation of them. The cultural justification for *why* these three FX and
this order exist is [research/11 §4.2, §4.3, §4.5, §4.7, §4.8, §7.1].

## 2. Module layout and responsibilities

### 2.1 Files created by the backlog

| File | Contents |
| --- | --- |
| `core/dsp/fx/FxChain.h` / `.cpp` | `FxChain` — owns the three stages, the dry/wet sum, the global Mono Output collapse, the worst-case latency report, and the master bypass early-out. |
| `core/dsp/fx/Drive.h` / `.cpp` | `Drive` — asymmetric waveshaper with its own 2x oversampler, pre/de-emphasis tilt, DC blocker. |
| `core/dsp/fx/Chorus.h` / `.cpp` | `Chorus` — Juno-style BBD, two anti-phase LFO-modulated fractional-delay lines panned hard L/R. |
| `core/dsp/fx/Delay.h` / `.cpp` | `Delay` — single mono core to stereo, fractional read, feedback-path damping LPF + gentle saturation, tempo sync, optional ping-pong. |
| `core/dsp/fx/FractionalDelayLine.h` | Header-only `FractionalDelayLine` — preallocated ring buffer + interpolated read used by `Chorus` and `Delay`. |
| `core/dsp/fx/FxOversampler2x.h` / `.cpp` | `FxOversampler2x` — the dedicated post-voice 2x up/down halfband pair used ONLY by `Drive` [ADR-017 L2]. Distinct instance from the per-voice voice oversampler [ADR-017 §A2]. |
| `core/dsp/fx/FxParams.h` | POD `FxParams` snapshot struct (plain values pulled from the APVTS on the message/control thread, read lock-free on the audio thread). |

All FX DSP lives under `core/` (no JUCE host types) per [ADR-001]; the
`plugin/` layer maps APVTS parameters (IDs from doc 06) into `FxParams` and
calls `FxChain`.

### 2.2 Ownership graph

```text
plugin/  (owns APVTS, doc 06 IDs)
  -> builds FxParams snapshot (message/control thread)
  -> FxChain
       -> Drive   (FxOversampler2x + waveshaper + tilt + DC block)
       -> Chorus  (2x FractionalDelayLine, anti-phase LFOs)
       -> Delay   (1x FractionalDelayLine core, feedback LPF + sat)
```

The chain is **fixed**; there is no runtime reorder or parallel routing in v1
[ADR-010 FX-2; Consequences].

## 3. Chain-level interface (`FxChain`)

### 3.1 Class signature

```cpp
namespace mw::fx {

// POD snapshot, published by plugin/ on the control thread, consumed lock-free
// on the audio thread. All fields are plain values; parameter IDs/ranges that
// feed these come from docs/design/06 §2.
struct FxParams; // defined in §7

class FxChain
{
public:
    FxChain() noexcept;

    // Allocate all ring buffers / oversampler state at max size. Called off the
    // audio thread. After this returns, process() performs no allocation.
    void prepare (double sampleRate, int maxBlockSize) noexcept; // alloc allowed here only

    void reset() noexcept;                       // clear all state, no alloc

    // Publish a new parameter snapshot. Called from the control thread; the
    // audio thread reads the active snapshot lock-free (double-buffer / atomic
    // pointer swap). Never blocks.
    void setParams (const FxParams& p) noexcept;

    // Process one block. mono == the post-voice mono voice sum (single channel,
    // numSamples). out == stereo (2 x numSamples). RT hot path: noexcept, no
    // alloc, no lock. See §3.3 for the dry-sum / bypass rules.
    void process (const float* mono, float* const* out, int numSamples) noexcept;

    // Deterministic FX group delay in samples at the prepared sample rate.
    // CONSTANT for the instance lifetime; computed in prepare(). Consumed by
    // plugin/ and summed with the per-voice zone delay for setLatencySamples.
    // This is the FX contribution to L2 only; the Delay/Chorus musical delay is
    // NOT included (ADR-017 L3).
    [[nodiscard]] int getLatencySamples() const noexcept;

private:
    Drive  drive_;
    Chorus chorus_;
    Delay  delay_;
    // worst-case pad delay line for the dry/short path alignment (see §6.3)
    FractionalDelayLine dryPad_;
    // ... atomic snapshot storage, scratch buffers (preallocated in prepare)
};

} // namespace mw::fx
```

### 3.2 Real-time invariants (chain-wide) [ADR-010 FX-10; ADR-017 L10]

- `process()`, `setParams()`, `getLatencySamples()`, `reset()` are `noexcept`
  and perform **no heap allocation and take no locks**.
- All ring buffers, oversampler state, scratch buffers, and the dry-pad delay
  line are allocated and sized to their **maximum** in `prepare()` (max delay
  time, max chorus depth, max block size). `process()` only moves read/write
  indices.
- Parameter updates cross the thread boundary via an atomic snapshot
  (double-buffered `FxParams`; the audio thread reads the published pointer with
  `std::memory_order_acquire`). No mutex.
- Every feedback / recirculating path runs under flush-to-zero (a
  `juce::ScopedNoDenormals`-equivalent set once per block in `plugin/`, plus a
  defensive denormal flush in the `Delay` feedback) [ADR-010 FX-10].
- Feedback gain is hard-clamped `< 1.0` before use [ADR-010 FX-8].

### 3.3 Dry/wet, mono-to-stereo, and bypass rules

These implement [ADR-010] FX-1, FX-3, FX-4, FX-9 verbatim.

1. **Dry path is strictly mono through Drive** and is summed **equally to L/R**.
   With all FX off, `out[0][n] == out[1][n] == drySample` (after the constant
   pad of §6.3), which is FX-off bit-exact mono [ADR-010 FX-1, FX-4; ADR-017 L6].
2. **Stereo is born only inside Chorus and Delay wet content** [ADR-010 FX-4].
   Drive is mono-in/mono-out; it never widens.
3. **Master FX bypass** (`FxParams::masterBypass`, mapped from the doc 06 master
   bypass param) and the **all-blocks-bypassed** condition both early-out: the
   chain copies the (padded) mono dry equally to L/R and runs **no FX DSP**
   (~0 cost) [ADR-010 FX-1].
4. **Per-block bypass** (`Drive.on`, `Chorus.mode == Off`, `Delay.on`) is a true
   early-out that skips that block's `process` entirely; the block adds ~0 CPU
   and is a pass-through of its input. It is **not** a dry/wet=0 mix
   [ADR-010 FX-3].
5. **Global Mono Output** (`FxParams::monoOutput`): when ON, after the full
   stereo chain, force `m = 0.5*(L+R); L = R = m` so the result is a
   phase-coherent mono sum regardless of Chorus/Delay Width [ADR-010 FX-9].
   Required to mitigate anti-phase chorus cancellation under host mono fold-down
   [ADR-010 Consequences].

### 3.4 Processing order (one block)

```text
process(mono, out, N):
  1. snap = published FxParams (acquire)
  2. dry  = dryPad_.processBlock(mono, N)      // constant worst-case alignment (§6.3)
  3. if snap.masterBypass OR all-blocks-off:
        out[L] = out[R] = dry ; return         // FX-1 early-out, ~0 cost
  4. m = dry                                    // mono working buffer
  5. if drive_.on:   m = drive_.process(m, N)   // mono->mono, oversampled (§4)
  6. (L,R) = (m, m)                             // dry summed equally L/R (FX-4)
  7. if chorus_.mode != Off: chorus_.process(m, L, R, N)  // adds stereo wet (§5.1)
  8. if delay_.on:           delay_.process(L, R, N)      // stereo in/out (§5.2)
  9. if snap.monoOutput: collapse L,R -> 0.5*(L+R)        // FX-9
```

Note: `dryPad_` (the §6.3 alignment delay) is always applied so the reported
latency is constant whether or not Drive is engaged [ADR-017 L5, L8].

## 4. Drive stage

### 4.1 Topology and placement

The FX Drive is a **single post-voice stage** running once on the mono sum with
its **own dedicated 2x oversampling** [ADR-017 §A2, L2, L9]. It is NOT per-voice
and does NOT reuse the voice oversampler — it has its own up/down halfband pair
so the harmonics it generates are band-limited before Chorus/Delay can fold them
back in [ADR-017 Decision; research/10 §5, §5.1]. Drive is the documented acid
"overdrive for grit" sweetener [research/11 §4.3], not a 303-distortion clone
(the "blip"/extra-character idioms are general-practice, not sourced SH-101 fact
— [research/11 §4.6]).

Inside the 2x zone the signal flow is:

```text
upsample 2x -> pre-emphasis tilt -> input gain (Drive) -> asymmetric
waveshaper -> de-emphasis tilt -> downsample 2x -> DC blocker -> output makeup
```

The DC blocker runs **after** the downsampler (it is a level/offset correction,
not a band-limiting concern) [ADR-010 FX-5].

### 4.2 Class signature

```cpp
class Drive
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept; // alloc here only
    void reset() noexcept;
    void setParams (const FxParams::DriveP& p) noexcept;

    // mono in-place; returns the processed mono pointer. noexcept hot path.
    float* process (float* mono, int numSamples) noexcept;

    bool on = false;                              // mapped from doc 06 mw101.fx.drive_enable
    [[nodiscard]] int latencySamples() const noexcept; // 2x halfband group delay

private:
    FxOversampler2x os_;       // dedicated post-voice 2x pair (ADR-017 L2)
    float tiltState_ = 0.0f;   // one-pole pre/de-emphasis tilt state
    float dcX1_ = 0.0f, dcY1_ = 0.0f; // DC blocker state
    // smoothed gains for Drive / Output
};
```

### 4.3 Waveshaper (asymmetric)

The shaper is a single tasteful asymmetric memoryless nonlinearity. `tanh` is
the physically-grounded saturation [research/10 §4]; asymmetry is introduced by
a small DC bias before the shaper (removed afterward by the DC blocker), which
produces even harmonics for analog-style grit.

`(PI)` shaper definition — centralize in `core/calibration/Calibration.h`:

```text
y = tanh(kDrivePreGain * (x + kDriveBias)) - tanh(kDrivePreGain * kDriveBias)
```

The second term re-centers the curve so the bias contributes asymmetry without a
large standing DC component before the blocker. `kDrivePreGain` and `kDriveBias`
are pragmatic inventions (no SH-101 oracle for the FX Drive — [ADR-010
Consequences; ADR-017 re-affirmed locks]).

| Constant | Meaning | Suggested value | Tag |
| --- | --- | --- | --- |
| `kDriveBias` | pre-shaper DC bias for even-harmonic asymmetry | 0.10 (normalized) | (PI) |
| `kDrivePreGain` | fixed shaper input scaling | 1.0 | (PI) |
| `kDriveOSFactor` | Drive oversampling factor | 2 (fixed) | [ADR-017 L2] |

### 4.4 Pre/de-emphasis tilt (Tone)

Tone is a complementary one-pole tilt: pre-emphasis boosts highs into the
shaper, de-emphasis is the inverse after the shaper, so harmonic generation is
biased toward brighter or darker grit without a static EQ on the dry sound. At
`tone = 0.5` the pre and de stages are unity (flat) [ADR-010 FX-5].

`(PI)` tilt pivot frequency `kDriveTiltHz` (suggested 700 Hz) and the
tone->tilt-gain map centralize in `Calibration.h`.

### 4.5 DC blocker

Standard one-pole high-pass: `y[n] = x[n] - x[n-1] + R*y[n-1]`, with `(PI)`
`kDcBlockR` (suggested 0.9975 at 48 kHz, scaled by sample rate). Placed after
downsampling [ADR-010 FX-5].

### 4.6 Parameters (DSP interpretation; IDs/ranges owned by doc 06 §2)

| Param ID (doc 06) | DSP role | Suggested range / unit | Suggested skew | Suggested default |
| --- | --- | --- | --- | --- |
| `mw101.fx.drive_amount` | input gain into the shaper (more = more grit) | 0..1 -> 0..+36 dB pre-gain | mild log toward more gain | 0.0 (clean) |
| `mw101.fx.drive_tone` | pre/de-emphasis tilt; 0.5 = flat | 0..1 (0=dark, 1=bright) | linear | 0.5 |
| `mw101.fx.drive_output` | post makeup gain | 0..1 -> -24..+12 dB | linear-in-dB | 0.5 (~0 dB) |
| `mw101.fx.drive_enable` | per-block bypass (early-out) | bool | — | false [ADR-010 FX-13] |

All ranges/skews/defaults above are advisory to doc 06 and become normative only
when doc 06 §2 registers them [ADR-008].

### 4.7 Drive RT invariants

- The 2x oversampler is the ONLY allocation source; sized in `prepare()`.
- `process()` is `noexcept`, in-place on the caller's mono buffer.
- When `on == false`, `Drive::process` is not called at all (chain early-outs in
  §3.4 step 5), so a bypassed Drive costs ~0 [ADR-010 FX-3]. Its latency,
  however, is still counted toward the constant pad [ADR-017 L2, L8].

## 5. Chorus and Delay stages

### 5.1 Chorus (Juno-style BBD)

#### 5.1.1 Topology

Two **anti-phase** LFO-modulated fractional-delay lines, panned **hard L/R**,
fed from the (mono) Drive output. The two LFOs are 180 degrees out of phase so
the stereo image opens; the wet content is what creates stereo [ADR-010 FX-6;
research/11 §4.5, §7.1]. This is the primary, identity-legitimate widener,
justified by the shared IR3109 filter family with the Juno-6/60 and Jupiter
lineage [research/11 §4.2, §5; ADR-010 Decision]. (No TB-303 filter descriptor
is implied; the "same filter as the 303" claim is FALSE and FROZEN —
[research/11 §6.1].)

Modes I / II / I+II select LFO rate/depth presets (Juno-style); Mode Off is the
per-block early-out [ADR-010 FX-6].

#### 5.1.2 Class signature

```cpp
class Chorus
{
public:
    enum class Mode { Off = 0, I = 1, II = 2, IandII = 3 };

    void prepare (double sampleRate, int maxBlockSize) noexcept; // alloc here only
    void reset() noexcept;
    void setParams (const FxParams::ChorusP& p) noexcept;

    // Mono drive output in `mono`; mixes wet stereo into L/R (which already hold
    // the dry mono per §3.4 step 6). noexcept hot path.
    void process (const float* mono, float* L, float* R, int numSamples) noexcept;

    Mode mode = Mode::Off;
    [[nodiscard]] int latencySamples() const noexcept; // 0 (musical delay, ADR-017 L3)

private:
    FractionalDelayLine lineL_, lineR_;  // sized to max base+depth in prepare
    float lfoPhaseL_ = 0.0f, lfoPhaseR_ = 0.5f; // anti-phase (0.5 cycle apart)
    // smoothed rate / depth / width / mix
};
```

#### 5.1.3 Modulation and width

- Each line's read offset = `kChorusBaseDelayMs + depth*kChorusDepthMs*lfo(phase)`.
  The LFO is a triangle (or sine) at `rate`. The right LFO phase is the left
  phase + 0.5 cycle (anti-phase) [ADR-010 FX-6].
- **Width** scales the L/R separation. `width = 0` => the two taps are summed to
  the center and added equally to L and R: a true centered mono collapse
  [ADR-010 FX-6, FX-9 mitigation]. `width = 1` => full hard-panned anti-phase.
- **Mix** crossfades dry vs wet within the chorus stage (the chain dry is already
  in L/R; Chorus adds `mix * wet`).
- The Chorus modulation delay is **intended musical delay** and does NOT
  contribute to reported PDC [ADR-017 L3]; `latencySamples()` returns 0.

#### 5.1.4 `(PI)` calibration constants (centralize in `Calibration.h`)

| Constant | Meaning | Suggested value | Tag |
| --- | --- | --- | --- |
| `kChorusBaseDelayMs` | center delay of each BBD line | 7.5 ms | (PI) |
| `kChorusDepthMs` | max modulation excursion at depth=1 | 4.0 ms | (PI) |
| `kChorusModeIRateHz` | Mode I LFO rate | 0.5 Hz | (PI) |
| `kChorusModeIIRateHz` | Mode II LFO rate | 0.83 Hz | (PI) |
| `kChorusModeIDepth` | Mode I depth scalar | 0.6 | (PI) |
| `kChorusModeIIDepth` | Mode II depth scalar | 1.0 | (PI) |

These are taste-calibrated against Juno/BBD literature with no physical-unit
oracle [ADR-010 Consequences]. I+II combines both rate/depth pairs.

#### 5.1.5 Chorus parameters (DSP interpretation; IDs/ranges owned by doc 06 §2)

| Param ID (doc 06) | DSP role | Suggested range / unit | Suggested default |
| --- | --- | --- | --- |
| `mw101.fx.chorus_mode` | Off / I / II / I+II (choice, order fixed by doc 06) | enum | Off |
| `mw101.fx.chorus_rate` | LFO rate override (when not mode-locked) | 0.05..10 Hz, log skew | mode default |
| `mw101.fx.chorus_depth` | modulation excursion scalar | 0..1 | mode default |
| `mw101.fx.chorus_width` | stereo separation; 0 = centered mono | 0..1 | 1.0 |
| `mw101.fx.chorus_mix` | dry/wet of the chorus stage | 0..1 | 0.5 |

### 5.2 Delay (tempo-syncable)

#### 5.2.1 Topology

A **single mono delay core** read to a **stereo output**, with fractional
(interpolated) read, a one-pole damping LPF plus gentle saturation in the
feedback path, optional ping-pong, and tempo sync [ADR-010 FX-7, FX-8]. Delay is
last and conservative; it is not a documented SH-101 circuit idiom but serves the
defining trigger-synced sequencer/arp riff performance idiom (e.g. "Voodoo Ray")
[research/11 §4.7, §4.8; ADR-010 Decision].

```text
input (stereo from chorus) -> mono-sum into write -> core ring buffer
read tap(s) (fractional) -> damping LPF -> gentle saturation -> * feedback(<1)
   -> back to write
wet routed to L/R (ping-pong alternates taps); Width scales L/R spread; Mix sums
```

#### 5.2.2 Class signature

```cpp
class Delay
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept; // alloc here only
    void reset() noexcept;
    void setParams (const FxParams::DelayP& p) noexcept;

    // Stereo in-place (L/R already carry dry + chorus wet). noexcept hot path.
    void process (float* L, float* R, int numSamples) noexcept;

    bool on = false;
    [[nodiscard]] int latencySamples() const noexcept; // 0 (musical delay, ADR-017 L3)

private:
    FractionalDelayLine core_;     // sized to kDelayMaxMs in prepare
    float dampStateL_ = 0.0f, dampStateR_ = 0.0f; // one-pole LPF state
    SmoothedValue smoothedDelaySamples_;          // pointer-glide for time changes
    // cached tempo-sync conversion (recomputed only on tempo/division change)
};
```

#### 5.2.3 Tempo sync [ADR-010 FX-7]

- When `sync == ON`, delay time = host-BPM note division from the set
  `{1/4, 1/8, 1/8., 1/8T, 1/16, 1/16T}` and their dotted/triplet variants. The
  ms-equivalent is computed as `delayMs = (60000 / bpm) * beatsPerDivision`.
- The conversion is **cached** and recomputed only when the host tempo OR the
  selected division changes — never per sample [ADR-010 FX-7].
- When `sync == OFF`, delay time is the free `mw101.fx.delay_time` value.
- The host BPM is supplied via `FxParams::hostBpm` (sourced by `plugin/` from the
  `AudioPlayHead`; the FX core never touches host types per [ADR-001]).

#### 5.2.4 Feedback, damping, saturation, ping-pong

- **Feedback** is clamped to `[0, kDelayMaxFeedback]` with
  `kDelayMaxFeedback < 1.0` so the loop cannot diverge [ADR-010 FX-8, FX-10].
- **Damping/Tone** is a one-pole low-pass in the feedback path (BBD-style high
  loss per repeat); `damp` maps to its cutoff.
- **Gentle saturation** in the feedback path is a soft `tanh`-style clip to keep
  repeats from building up harshly; shares the saturation policy of
  [research/10 §4].
- **Ping-pong** (`mw101.fx.delay_pingpong`): when ON, alternate the wet tap routing
  L->R->L so echoes bounce across the stereo field. When OFF, wet is equal on
  both channels scaled by Width.
- **Width = 0** => centered mono wet [ADR-010 FX-8].
- The Delay musical time does NOT contribute to reported PDC [ADR-017 L3];
  `latencySamples()` returns 0.

#### 5.2.5 Click-free time changes [ADR-010 FX-11]

Delay-time changes (sync division change, free-time knob move, or tempo change)
**pointer-glide** the read position via a `SmoothedValue` ramp to the new delay
in samples (or crossfade between two taps), so no zipper/click occurs. All other
Delay params are smoothed per [ADR-020].

#### 5.2.6 `(PI)` calibration constants (centralize in `Calibration.h`)

| Constant | Meaning | Suggested value | Tag |
| --- | --- | --- | --- |
| `kDelayMaxMs` | max delay buffer length (sizes ring in prepare) | 2000 ms | (PI) |
| `kDelayMaxFeedback` | hard feedback clamp (< 1.0; doc 06 ceiling) | 0.95 | (PI) |
| `kDelayDampHzMin` / `Max` | feedback LPF cutoff range | 1.5 kHz .. 18 kHz | (PI) |
| `kDelaySatDrive` | feedback saturation pre-gain | 1.2 | (PI) |
| `kDelayTimeGlideMs` | pointer-glide ramp time for time changes | 30 ms | (PI) |

#### 5.2.7 Delay parameters (DSP interpretation; IDs/ranges owned by doc 06 §2)

| Param ID (doc 06) | DSP role | Suggested range / unit | Suggested default |
| --- | --- | --- | --- |
| `mw101.fx.delay_enable` | per-block bypass (early-out) | bool | false |
| `mw101.fx.delay_sync` | tempo sync on/off | bool | false |
| `mw101.fx.delay_division` | note division (choice; order owned by doc 06) | enum {1/4,1/8,1/8.,1/8T,1/16,1/16T} | 1/8 |
| `mw101.fx.delay_time` | free time when sync off | 1..2000 ms, log skew | 350 ms |
| `mw101.fx.delay_feedback` | recirculation (clamped < 1.0) | 0..1 -> 0..0.95 | 0.35 |
| `mw101.fx.delay_damp` | feedback LPF cutoff | 0..1 (0=dark) | 0.5 |
| `mw101.fx.delay_width` | stereo spread; 0 = centered mono | 0..1 | 1.0 |
| `mw101.fx.delay_mix` | dry/wet of the delay stage | 0..1 | 0.3 |
| `mw101.fx.delay_pingpong` | ping-pong routing on/off | bool | false |

### 5.3 `FractionalDelayLine` (shared helper)

```cpp
class FractionalDelayLine
{
public:
    void prepare (int maxLengthSamples) noexcept;   // alloc here only
    void reset() noexcept;
    void write (float x) noexcept;                  // push one sample
    [[nodiscard]] float read (float delaySamples) const noexcept; // interpolated
    // block helper used by dryPad_ for fixed-integer alignment delay
    void processBlock (const float* in, int n) noexcept;
private:
    std::vector<float> buf_;  // sized once in prepare; never reallocated
    int writeIndex_ = 0;
};
```

Interpolation is at least linear; cubic/Lagrange is permitted for the
chorus/delay read (the `(PI)` choice of interpolation order centralizes in
`Calibration.h`). The buffer is power-of-two-masked or modulo-indexed; sized to
the maximum of (`kDelayMaxMs`, chorus base+depth, dry pad) in `prepare()` so no
runtime allocation ever occurs [ADR-010 FX-10].

## 6. Latency / PDC contribution

### 6.1 What the FX section contributes

Per [ADR-017]:

- **Drive 2x oversampling group delay** (its own up/down halfband pair):
  **CONTRIBUTES** to reported PDC, and is **always counted** toward the worst
  case even when Drive is bypassed [ADR-017 L2, L8]. `Drive::latencySamples()`
  returns this fixed group delay.
- **Chorus modulation delay and Delay musical time**: **DO NOT contribute**
  [ADR-017 L3]. Their `latencySamples()` return 0. A host must not
  PDC-compensate an effect's musical delay.

`FxChain::getLatencySamples()` returns the **constant** FX contribution =
`Drive::latencySamples()` (the only contributing FX source), independent of
whether Drive is on/off [ADR-017 L2, L5, L8].

### 6.2 Where the report is made

`plugin/` (not `core/`) calls `setLatencySamples` per [ADR-001] and
[ADR-017 L4]. It reports a **single constant** = worst-case total =
(per-voice oversampled-zone group delay [ADR-017 L1]) + (`FxChain::getLatency
Samples()` [ADR-017 L2]), computed once in `prepare`, **invariant** to FX bypass,
Quality tier, and build-to-build [ADR-017 L4, L5, L7, L11]. This document owns
only the FX term; the sum and the report belong to `plugin/`.

### 6.3 Constant-pad / delay alignment

Because Drive's latency is always counted (even bypassed), and the dry/FX-off
path would otherwise be shorter, the FX section keeps a **fixed integer delay
line** (`FxChain::dryPad_`) on the dry/short path so the FX-off output sits at
exactly the declared worst-case offset [ADR-017 L5, L6, B2]. This makes FX-off
bit-exact at the fixed offset and keeps the golden reference comparing
like-for-like [ADR-017 L6].

`dryPad_` length = `getLatencySamples()` samples, applied in §3.4 step 2 on every
block (bypassed or not). It is sized in `prepare()`; the pad delay introduces no
audible artifact (it is a pure integer delay) [ADR-017 L10].

## 7. `FxParams` snapshot layout

```cpp
struct FxParams
{
    struct DriveP  { bool on; float amount, tone, output; };
    struct ChorusP { int mode; float rate, depth, width, mix; }; // mode: enum int
    struct DelayP  { bool on, sync, pingpong;
                     int division; float timeMs, feedback, damp, width, mix; };

    bool   masterBypass = true;   // engine default OFF -> bypass (ADR-010 FX-13)
    bool   monoOutput   = false;  // global Mono Output collapse (ADR-010 FX-9)
    double hostBpm      = 120.0;  // from plugin/ AudioPlayHead (ADR-001)

    DriveP  drive  {};
    ChorusP chorus {};
    DelayP  delay  {};
};
```

`FxParams` is a trivially-copyable POD. `plugin/` fills it from the APVTS (IDs
from doc 06 §2) on the control thread and publishes it via `FxChain::setParams`,
which stores it into a double buffer the audio thread reads lock-free. No field
here re-defines a parameter ID or range — these are decoded values [ADR-008].

## 8. Defaults and preset policy [ADR-010 FX-13]

- The **engine/global default and the INIT patch are FX OFF / dry**:
  `masterBypass == true`, `drive.on == false`, `chorus.mode == Off`,
  `delay.on == false`. A reviewer hears the bare blessed mono voice first
  [ADR-010 Decision; research/11 §7.1].
- **Factory presets default to dry-first** voicing. FX may be engaged
  per-preset **only where the research prescribes it** — specifically the
  **"PWM / Strings"** category, defined as a **mono PWM + chorus stylization**
  [research/11 §4.5, §7.1; ADR-010 FX-13]. Such presets MUST remain labelled as
  a mono stylization [research/11 §4.5].
- FX state is stored per preset [ADR-010 FX-13]; the preset format and storage
  are owned by doc 06 / [ADR-008], not here.
- **No reverb in v1** [ADR-010 FX-14].

## 9. Acceptance hooks

A backlog task implementing this subsystem MUST have tests verifying:

- **FX-off bit-exact (mono).** With `masterBypass` ON (or all three blocks
  bypassed), `out[L][n] == out[R][n]` and the stream is sample-identical to the
  blessed mono voice at the declared worst-case offset (golden compare on macOS
  arm64; Linux x64 hard gate) [ADR-010 FX-1; ADR-017 L6].
- **Order is fixed.** No code path applies Chorus or Delay before Drive; the
  chain order is Drive -> Chorus -> Delay [ADR-010 FX-2].
- **Per-block bypass is a true early-out.** A bypassed block (`drive.on=false`,
  `chorus.mode=Off`, `delay.on=false`) skips its `process` and its output equals
  its input bit-for-bit (no dry/wet=0 residue) and adds ~0 CPU [ADR-010 FX-3].
- **Dry is mono; stereo is born only in Chorus/Delay.** With Drive on but Chorus
  Off and Delay off, `out[L] == out[R]` (Drive cannot widen) [ADR-010 FX-4].
- **Drive is anti-aliased.** A full-scale sine into a hot Drive produces
  in-band aliasing below a fixed floor `(PI) kDriveAliasFloorDb` thanks to the
  dedicated 2x oversampler; compare against the same shaper run at 1x to show the
  oversampler reduces aliasing [ADR-017 L2; research/10 §5].
- **Width=0 collapses to centered mono.** For Chorus and for Delay, setting
  `width=0` yields `out[L] == out[R]` [ADR-010 FX-6, FX-8].
- **Mono Output forces phase-coherent mono.** With `monoOutput=ON` and any
  Chorus/Delay width, `out[L] == out[R]` at the chain output [ADR-010 FX-9].
- **Tempo sync is correct and cached.** With `sync=ON`, the realized delay equals
  `(60000/bpm)*beatsPerDivision` ms within one sample of fractional read; the
  conversion is recomputed only on tempo/division change, never per sample
  [ADR-010 FX-7].
- **Feedback cannot diverge.** With `feedback=1.0` requested, the applied
  feedback is clamped `< 1.0` (`kDelayMaxFeedback`) and the output stays bounded
  over a long impulse-fed run [ADR-010 FX-8].
- **Click-free time changes.** Stepping `delay.time`/division at full feedback
  produces no sample discontinuity above a fixed threshold (pointer-glide /
  crossfade) [ADR-010 FX-11].
- **RT-safety.** `prepare/reset/process/setParams/getLatencySamples` perform no
  heap allocation and take no locks (allocation-tracking / lock-detection test);
  feedback paths flush denormals [ADR-010 FX-10; ADR-017 L10].
- **Latency is constant and FX-side contribution is correct.**
  `FxChain::getLatencySamples()` returns the Drive 2x group delay only; it is
  invariant to `drive.on`, `masterBypass`, and per-block bypass; Chorus/Delay
  `latencySamples()` return 0 [ADR-017 L2, L3, L5, L8].
- **Defaults are OFF/dry.** A freshly constructed `FxChain` / INIT `FxParams` has
  `masterBypass=true`, `drive.on=false`, `chorus.mode=Off`, `delay.on=false`
  [ADR-010 FX-13].

## 10. References

- [ADR-001] Core/plugin boundary (`prepare`, `setLatencySamples` lives in
  `plugin/`).
- [ADR-008] Parameter / state / preset schema (owns parameter IDs, ranges,
  skews, defaults; the `core/calibration/Calibration.h` single calibration
  table).
- [ADR-010] Built-in FX section (Chorus / Delay / Drive) — fixed-order topology,
  Contract FX-1..FX-14.
- [ADR-017] Plugin latency (PDC) policy & Drive placement — Drive is post-voice
  with its own 2x oversampler; constant worst-case-padded latency; Contract
  L1..L11.
- [ADR-020] Parameter smoothing policy (smoothing time constants, smoother type).
- docs/design/06 (parameter schema) — owns all FX parameter IDs/ranges
  referenced here.
- docs/research/10-dsp-modeling-techniques.md §4 (`tanh` nonlinearity), §5,
  §5.1, §5.2 (oversampling / IIR-vs-FIR decimator).
- docs/research/11-cultural-influence.md §4.2 (IR3109 filter lineage; TB-303
  correction), §4.3 (acid overdrive idiom), §4.5 (PWM "Strings" = mono PWM +
  chorus stylization), §4.7, §4.8 (sequencer/arp riff, Voodoo Ray), §6.1
  (FROZEN: no TB-303 filter descriptor), §7.1 (preset taxonomy).
