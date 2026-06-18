<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Vintage Variance / Analog-Drift Model

## 1. Scope and contract anchoring

### 1.1 What this document owns

This is the single source of truth for the **drift / variance subsystem** of
mwAudio101: the DSP that gives the synth its unit-to-unit "personality," its
live thermal wander, its per-note tuning slop, and the four frozen variance
spreads, plus the persisted per-instance seed and the host-thread **Vintage
(Age)** macro that fronts them. It defines concrete C++ class/struct/function
signatures, data layouts, numeric ranges, defaults, real-time invariants, and
acceptance hooks for a fresh agent to implement.

The model is the realization of the three-tier decision in [ADR-009]:
**Tier 1** frozen per-instance trimmer calibration, **Tier 2** one shared
thermal state driving VCO and VCF together, **Tier 3** per-note-on slop; plus
four frozen-at-note-on variance spreads. Every numeric figure here is a
**tunable default, not a measured spec** [ADR-009 §8; research/09 §1, §7, §8.3].

### 1.2 What this document does NOT own (cross-references only)

- **Parameter IDs, ranges, units, skews, defaults, automatable flags** are
  owned by the parameter schema (docs/design/06 §2; realized per [ADR-008]).
  This doc REFERENCES those IDs and states the modeled semantics behind each;
  it never re-mints an ID. The contract table in §10 maps each modeled control
  to its schema ID (`mw101.vintage.*`, `mw101.drift.*`, `mw101.var.*`).
- **All `(PI)` invented constants** (band widths, time constants, smoother
  length, exp2 table size) centralize in the single calibration table
  `core/calibration/Calibration.h` (created by the backlog per [ADR-008]
  §Decision 1). This doc names each constant and its proposed default; the
  table is authoritative for the value.
- **Voice pool, `kMaxVoices`, unison/poly allocation, note priority** are owned
  by docs/design/06 / [ADR-006]; this doc consumes `kMaxVoices` and the voice
  index, and is fed note-on / mode events. `kMaxVoices` is a compile-time
  constant `>= maxPoly x maxUnison`, `maxUnison` capped at 8 [ADR-006 C16].
- **The 6-bit DAC / control-tick / MODERN-vs-VINTAGE control macro** is owned
  by [ADR-005]; drift sits on the CV *after* that path and is orthogonal to it.
- **The exp2/cents->ratio fast path** is shared DSP; the contract here is only
  that drift MUST use it, never `std::exp` per sample (§7.4).
- **Filter cutoff / resonance topology** is owned by [ADR-003]; this doc only
  *adds an offset* to the cutoff CV and reads the shared thermal coefficient.

### 1.3 Honesty-label requirement

Every control carries a UI/docs tag — **VR-anchored (documented)** or
**analog-modeling embellishment** — per [ADR-009 §7] and [ADR-016 §5]
(honesty labels everywhere). The tag column is normative; see §10. The
Tier-1 trimmer set-points are documented; the band *widths*, the pink/OU drift,
the warm-up curve, and the four variance spreads are embellishment.

## 2. Three statement classes (provenance discipline)

Per research/09 §1, every claim is one of three classes. The implementer MUST
preserve this distinction in code comments and UI tags:

| Class | Meaning | Examples in this subsystem |
| --- | --- | --- |
| Documented SH-101 fact | Service manual / chip identity | VR-2/VR-7/VR-9/VR-8 set-points; VR-7 = 442 Hz; cutoff offset uncalibrated [research/09 §2.1, §2.3] |
| Theory / inference | Converter physics, RC tolerance | kT/q -3300 ppm/degC tempco; env-time RC spread [research/09 §3.1, §3.4] |
| Modeling practice | DSP-forum / Diva consensus | pink/OU drift, Box-Muller slop, per-voice decorrelation, Diva Trimmers layout [research/09 §4, §5] |

Software-emulation artifacts (Sine LFO, 32'/64', external in, MIDI/DCB) are NOT
hardware and MUST NOT be driven by this subsystem [research/09 §8.4].

## 3. Module layout and responsibilities

### 3.1 Files the backlog creates

| File | Responsibility |
| --- | --- |
| `core/dsp/drift/DriftModel.h/.cpp` | Owns the `DriftState[kMaxVoices]` array; orchestrates Tier 1/2/3; block-rate update + per-sample interpolation; shared thermal state |
| `core/dsp/drift/DriftState.h` | POD per-voice state struct (PRNG, integrators, smoother, frozen note-on offsets, frozen Tier-1 calibration) |
| `core/dsp/drift/Xorshift128p.h` | POD `xorshift128+` PRNG + Gaussian (Box-Muller) and cubic shaping helpers; header-only, `constexpr`-friendly, no `std::random` |
| `core/dsp/drift/ThermalState.h` | Shared scalar OU/1-f temperature integrator + warm-up transient |
| `core/dsp/OnePoleSmoother.h` | (created by the backlog; canonical `mw::dsp::OnePoleSmoother`) — mandatory per-target one-pole de-zipper smoother; this doc REFERENCES it, never re-declares it |
| `core/dsp/drift/VintageMacro.h/.cpp` | Host-thread Age-macro -> target mapping (NOT audio-thread) |
| `core/calibration/Calibration.h` | (created by [ADR-008] backlog) home of every `(PI)` constant named below |

### 3.2 Data-flow summary

```
host thread:  Age macro --> VintageMacro::apply() --> smoothed param targets (APVTS)
                                                          |
construct:    instance_seed (from <extras>) --+           |
note-on:      voice_index --------------------+--> per-voice PRNG seed       |
                                                          v                  v
audio thread (per block):  ThermalState.tick() --> shared T(t)              DriftModel.processBlock()
                                                          |                  | reads param atomics
                           per voice: Tier1 frozen + Tier2(T) + Tier3 slop --+--> OnePoleSmoother --> CV offsets
                                                                                  (pitch cents, cutoff, PW, envScale, glideScale)
```

The macro path is host-thread only and writes already-smoothed targets, costing
the audio thread nothing [ADR-009 §7; ADR-009 §Decision 7].

## 4. Tier 1 — frozen per-instance calibration

### 4.1 Mechanism

On instance construction (and on Re-roll), a deterministic PRNG seeded from the
persisted `instance_seed` draws **fixed offsets** that perturb the *documented*
factory trimmer set-points within their service-manual tolerance bands. These
offsets are constant for the instance's life ("frozen trimmers") and persist
with the preset [ADR-009 §Decision 1; research/09 §2.1, §6.3].

The four perturbed set-points (only the 8 enumerated trimmers are touched; VR-4
is excluded, function unconfirmed [research/09 §2.2, §7.1]):

| Set-point | Documented nominal | Modeled perturbation | Band (PI default) |
| --- | --- | --- | --- |
| VR-7 / VR-9 VCO Tune | 442 Hz [research/09 §2.1, §9] | additive cents offset to global pitch | `kCalBandTuneCents` = +/-6 cents (PI) |
| VR-2 D/A Tune | 0 V +/-1 mV [research/09 §2.1, §9] | additive cents offset (coupled w/ Tune) | `kCalBandDacCents` = +/-2 cents (PI) |
| VR-8 VCF Width | F5 cycle = 2x F4 [research/09 §2.1] | **scale** multiplier on cutoff CV slope, NOT an offset | `kCalBandVcfScale` = +/-1.5 % (PI) |
| Cutoff offset | **uncalibrated on hardware** [research/09 §2.3] | additive offset to cutoff CV; **widest** band | `kCalBandCutoffOffset` = +/-180 cents-equiv (PI) |

VR-8 is a *scale*, never an offset [ADR-009 §Decision 1; research/09 §2.1]. The
cutoff offset legitimately gets the most generous band because the IR3109 has
only the VR-8 width trim and no per-unit cutoff-offset calibration
[research/09 §2.3]. The set-points are documented (VR-anchored); the band widths
are `(PI)` and live in `Calibration.h`.

### 4.2 Signatures

```cpp
struct CalibrationDraw {            // frozen per voice (and per instance for voice 0)
    float tuneCents      = 0.0f;    // VR-7/VR-9 + VR-2 combined, additive
    float vcfWidthScale  = 1.0f;    // VR-8, multiplicative on cutoff slope
    float cutoffOffset   = 0.0f;    // uncalibrated cutoff offset, additive (cents-equiv)
};

// drawn once at construct / Re-roll (voice 0 = instance personality) and once
// per note-on for voices 1..N under unison/poly. spread01 = cal.spread param.
CalibrationDraw drawCalibration(Xorshift128p& rng, float spread01) noexcept;
```

`cal.spread` (schema `mw101.vintage.cal_spread`) is a 0..100 % width multiplier
on every band [ADR-009 VV-6]. The Re-roll button reseeds the personality (§8).

## 5. Tier 2 — live thermal drift (shared, correlated)

### 5.1 Mechanism

ONE shared scalar temperature state `T(t)` drives BOTH VCO and VCF through the
same kT/q `-3300 ppm/degC` coefficient — they wander *together*, never as two
independent random walks [ADR-009 §Decision 2, VV-13; research/09 §3.1, §3.3,
§6.4]. Sign convention: the converter scale and OTA transconductance carry the
*negative* coefficient; the compensating resistor the *positive* — same
mechanism [research/09 §3.1, §8.2]. Because the CEM3340 is on-die
temperature-compensated, **drift depth defaults small** — the SH-101 is the
stable end of the vintage spectrum [research/09 §3.2; ADR-009 §Decision 2].

`T(t)` is a **bounded leaky-integrated Gaussian (Ornstein-Uhlenbeck)**,
optionally summed with a fixed-coefficient 1/f (Voss-McCartney/Kellet) component,
plus an optional exponential **Warm-Up** transient that decays a shared extra
offset from "cold" [ADR-009 §Decision 2; research/09 §5, §6.2]. The OU update is
`T += -k*T*dt + sigma*sqrt(dt)*N(0,1)`, run **once per block** (control rate),
clamped to `+/-kDriftClampCents` so it can never run away.

### 5.2 Mapping T(t) to pitch and cutoff

`driftCents = T * drift.depth` applied to VCO pitch; `cutoffDriftCents =
T * drift.depth * kVcfDriftRatio` applied to cutoff. `kVcfDriftRatio` (PI,
default 1.0, the "same coefficient" assumption [research/09 §3.3]) lets the
inferred filter tempco be retuned without code change; the equivalence is theory,
not a measured IR3109 spec [research/09 §3.3, §8.3].

### 5.3 Warm-Up transient

`warmup.time` (schema `mw101.warmup.time`) is **off by default** — it is the
least authentic element and ships off, tagged embellishment [ADR-009
§Consequences; research/09 §6.2, §8.3]. When on, an extra offset
`Twarm = kWarmupCents * exp(-t / tau)` is added to the shared `T(t)`, decaying
over a user-set 0-30 min; under unison/poly the warm-up *chassis* term stays
**global** (one curve for all voices) while each voice keeps its own OU
integrator [ADR-009 §Decision 6].

### 5.4 Signatures

```cpp
struct ThermalState {
    float T          = 0.0f;   // bounded OU state, cents-domain normalized [-1,1]
    float pinkState[7]{};      // optional Voss-McCartney rows (off by default)
    double warmupSec = 0.0;    // elapsed warm time; <0 == warm-up disabled
    void  reset(bool cold) noexcept;            // cold==true seeds warm-up offset
    // dtBlock in seconds; rate01 = drift.rate param, usePink/useWarmup flags.
    void  tick(Xorshift128p& rng, float rate01, double dtBlock,
               bool usePink, bool useWarmup, float warmupTimeMin) noexcept;
    float value() const noexcept { return T; }  // shared, read by all voices
};
```

`ThermalState` is owned by `DriftModel` and is a single instance for mono /
voice 0; under unison/poly each voice owns its own OU integrator (decorrelated,
same statistics) so stacked voices beat naturally, while the warm-up chassis
term is shared [ADR-009 §Decision 6, VV-18].

## 6. Tier 3 — per-note-on slop

A Gaussian (Box-Muller) or cubic `(2u-1)^3` tuning offset is latched **once at
each note-on**, independent of `T(t)` and never recomputed per sample
[ADR-009 §Decision 3, VV-4; research/09 §5, §6.1]. Range 0-20 cents, default
2.5 cents (`tune.slop`, schema `mw101.tune.slop`). Cubic-vs-Gaussian is a
labelled taste choice, default Gaussian; selectable via `kSlopShape` (PI).

```cpp
// latched at note-on from the per-voice PRNG; gaussian() returns N(0,1).
inline float drawSlopCents(Xorshift128p& rng, float slopCents) noexcept {
    return slopCents * rng.gaussian();   // or rng.cubic() per kSlopShape
}
```

## 7. Variance spreads (frozen at note-on)

### 7.1 Mechanism

The four spreads are **per-voice offsets frozen at note-on** — NOT continuous
modulation — drawn from the per-voice PRNG [ADR-009 §Decision 4, VV-7..VV-10;
research/09 §6.4]. A held note does not wander in cutoff/PW within the note;
promoting cutoff variance to a continuous path later is a new ADR
[ADR-009 §Consequences].

| Spread | Schema ID | Domain | Apply rule | Band (PI default) |
| --- | --- | --- | --- | --- |
| Cutoff | `mw101.var.cutoff` | native (cutoff CV) | **additive** offset; **widest** band (uncalibrated [research/09 §2.3]) | `kVarCutoffCents` = +/-300 cents-equiv (PI) |
| Env time | `mw101.var.env_time` | A/D/R time constants | **multiplicative** `(1 + spread*band)` | `kVarEnvBand` = +/-5..20 % [research/09 §3.4, §6.4] (PI exact %) |
| PW | `mw101.var.pw` | native (pulse width) | **additive** offset | `kVarPwFrac` = +/-4 % duty (PI) |
| Glide | `mw101.var.glide` | glide time constant | **multiplicative** `(1 + spread*band)` | `kVarGlideBand` = +/-15 % (PI) |

Cutoff and PW add in the parameter's native domain; env-time and glide multiply
the time constant [ADR-009 §Decision 4]. The env-time `+/-5..20 %` magnitude is
an unverified RC-tolerance heuristic, explicitly labelled embellishment
[research/09 §3.4, §8.2; ADR-009 §8].

### 7.2 Signatures

```cpp
struct NoteOnOffsets {              // all frozen once per note-on
    float slopCents    = 0.0f;      // Tier 3
    float varCutoff    = 0.0f;      // additive, cents-equiv
    float varEnvScale  = 1.0f;      // multiplier on A/D/R
    float varPw        = 0.0f;      // additive duty fraction
    float varGlideScale= 1.0f;      // multiplier on glide
};

NoteOnOffsets drawNoteOn(Xorshift128p& rng,
                         float slopCents, float varCutoff01,
                         float varEnv01, float varPw01, float varGlide01) noexcept;
```

## 8. Per-voice state, seeding and determinism

### 8.1 DriftState layout

```cpp
struct DriftState {                 // POD; one per voice; pre-sized [kMaxVoices]
    Xorshift128p    rng;            // seeded from instance_seed ^ mix(voice_index)
    CalibrationDraw cal;            // Tier 1, frozen at construct / Re-roll
    ThermalState    thermal;        // Tier 2 OU integrator (own per voice)
    NoteOnOffsets   noteOn;         // Tier 3 + variance, frozen at note-on
    mw::dsp::OnePoleSmoother smPitch, smCutoff, smPw, smEnv, smGlide;  // canonical smoother, §9
    bool            active = false;
};
```

### 8.2 Seeding and determinism

Per-voice PRNGs derive deterministically from `instance_seed + voice_index`
(implementation: `seed = splitmix64(instance_seed ^ goldenMix(voice_index))`),
so the same input yields **bit-identical** output on the macOS arm64 bless gate
while voices decorrelate [ADR-009 §Decision 5, VV-17]. `instance_seed` is a
64-bit value persisted in the `<extras>` subtree — it is **NOT** an
`AudioProcessorParameter` (no automation lane, not in host parameter count)
[ADR-008 C8]. It is visible in the UI, re-rollable, and lockable.

```cpp
class DriftModel {
public:
    void  prepare (double sampleRate, int blockSize, int numVoices) noexcept; // allocates here ONLY
    void  setInstanceSeed (uint64_t seed) noexcept;     // host thread; sets pendingReroll flag
    void  noteOn  (int voiceIndex, double noteHz) noexcept;  // draws Tier 3 + variance
    void  processBlock (int numActiveVoices) noexcept;  // RT hot path; noexcept; no alloc/lock
    // per-voice smoothed outputs, read by the voice DSP each sample:
    float pitchOffsetCents (int v) const noexcept;
    float cutoffOffset     (int v) const noexcept;
    float pwOffset         (int v) const noexcept;
    float envTimeScale     (int v) const noexcept;
    float glideTimeScale   (int v) const noexcept;
private:
    std::array<DriftState, kMaxVoices> voices_;   // fixed, pre-sized
    uint64_t          instanceSeed_ = 0;
    std::atomic<bool> pendingReroll_{false};      // consumed lock-free in processBlock
};
```

### 8.3 Re-roll, visible seed, lockable

- **Visible:** the 64-bit seed is rendered in the Trimmers page (hex/decimal).
- **Re-roll:** a button generates a new seed on the host thread; `DriftModel`
  consumes it via the lock-free `pendingReroll_` flag and re-draws Tier 1 at the
  next block boundary [ADR-009 §Decision 5, VV-12, VV-16].
- **Lockable:** a `seedLocked` bool in `<extras>` — when true, INIT / preset
  load preserves the current seed instead of overwriting it (lets a user keep
  one "unit" across patches). Seed + lock travel with the preset
  [ADR-009 §Decision 1; ADR-008 C8].

Persisted per-instance seeds may make two instances of the same preset sound
subtly different on A/B recall — owner-ratified acceptable, mitigated by the
visible seed + Re-roll + lock affordance [ADR-009 §Consequences,
owner-ratification item].

## 9. Output smoothing (mandatory de-zipper)

Every drifted target passes through a **mandatory per-voice one-pole output
smoother** so any block-rate or note-on step is de-zippered; zipper noise is
structurally impossible regardless of the upstream step
[ADR-009 §Decision 5, VV-15]. This is independent of, and additional to, the
schema-level parameter smoothing in [ADR-020].

This subsystem uses the canonical smoother **`mw::dsp::OnePoleSmoother`**
(`core/dsp/OnePoleSmoother.h`, fields `{float current, target, coeff;}`, methods
`setTimeConstant(float ms, double sr)`, `setTarget(float)`, `float tick() noexcept`)
shared with docs/design/03 and docs/design/04. This doc **REFERENCES** that single
declaration; it does **not** re-declare a smoother or alias. Per-voice usage:

```cpp
// smPitch/smCutoff/... are mw::dsp::OnePoleSmoother instances (see §8.1).
sm.setTimeConstant(kDriftSmoothMs, sampleRate);  // in prepare(); never reallocated
sm.setTarget(driftedTargetForVoice);             // block-rate / note-on step
float smoothed = sm.tick();                      // per-sample de-zipper read
```

Smoother time `kDriftSmoothMs` = 8 ms (PI), within the mandated 5-20 ms window
[ADR-009 VV-15]. Set via `setTimeConstant` in `prepare`; never reallocated.

## 10. Control set and schema mapping

All ranges/defaults below are OWNED by the schema (docs/design/06 §2; [ADR-008]
C1-C5) and reproduced here only to state modeled semantics. The numeric values
match the [ADR-009] Contract table verbatim. Tags are normative [ADR-009 §7;
ADR-016 §5].

| Modeled control | Schema ID (doc 06) | Range | Default | Tag | Modeled semantics |
| --- | --- | --- | --- | --- | --- |
| Vintage (Age) macro | `mw101.vintage.age` | 0-100 % | 0 | embellishment | Host-thread mapping scaling group; writes smoothed targets; in tune on load [VV-1] |
| Drift depth | `mw101.drift.depth` | 0-50 cents | 4 | embellishment | OU/pink amplitude, block-rate [VV-2] |
| Drift rate | `mw101.drift.rate` | 0.01-1 Hz | 0.1 | embellishment | Effective drift bandwidth (OU `k`) [VV-3] |
| Tuning slop | `mw101.tune.slop` | 0-20 cents | 2.5 | embellishment | Gaussian/cubic, latched per note-on [VV-4] |
| Warm-up time | `mw101.warmup.time` | off, 0-30 min | off | embellishment | Decaying offset on shared T(t); off by default [VV-5] |
| Cal spread | `mw101.vintage.cal_spread` | 0-100 % | 25 | VR-anchored (band = embellishment) | Width multiplier on Tier-1 draws [VV-6] |
| Cutoff variance | `mw101.var.cutoff` | 0-100 % | 0 | VR-anchored uncalibrated; widest band | Frozen-at-note-on cutoff offset, native domain [VV-7] |
| Env-time variance | `mw101.var.env_time` | 0-100 % -> +/-5..20 % | 0 | embellishment | Frozen multiplier on A/D/R [VV-8] |
| PW variance | `mw101.var.pw` | 0-100 % | 0 | embellishment | Frozen PW offset [VV-9] |
| Glide variance | `mw101.var.glide` | 0-100 % | 0 | embellishment | Frozen glide multiplier [VV-10] |
| Detune amount | `mw101.vintage.detune_amt` | 0-100 % | (preset) | embellishment | Scales per-voice spread under unison/poly only [VV-11] |
| Instance seed + Re-roll + lock | `<extras>` (not a param) | 64-bit | seeded at construct | VR-anchored | Persisted; perturbs VR-2/7/9/8; Re-roll reseeds; lockable [VV-12; ADR-008 C8] |

### 10.1 Vintage (Age) macro semantics

The Age macro is a **host-visible parameter** that scales the group; it is a
host-thread preset mapping (`VintageMacro::apply`) that writes
already-smoothed targets, so it costs the audio thread nothing
[ADR-009 §Decision 7]. Automation contract (normative): **both the macro AND its
targets are host-visible and automatable; preset diffs record macro + targets**
[ADR-009 §Consequences, owner-ratification item]. The macro -> target curve is a
`(PI)` table `kAgeCurve` in `Calibration.h`.

### 10.2 INIT-patch default (ADR-016)

The `vintage.age` **parameter** default is **0** (in tune on load)
[ADR-009 VV-1; research/09 §6, §10.2]. The shipped **INIT patch** is a separate
artifact that sets the Age macro **LOW (not zero)** so the first note is "analog
and alive," unmistakably still in tune [ADR-016 §Decision 4, R-4]. This is a
*patch* default authored against the modern poles; it does NOT change any
parameter default or any drift DSP [ADR-016 §Consequences].

## 11. Poly / unison behavior

- **Mono SH-101 path** = ONE instance personality (Tier 1, voice 0) + ONE shared
  `T(t)` + per-note-on slop. Mono is simply voice index 0; there is **no
  separate code path** [ADR-009 §Decision 6, VV-18].
- **Unison / poly** = each voice gets its own Tier-1 draw and its own Tier-2 OU
  integrator (decorrelated, same statistics) so stacked voices beat naturally;
  the warm-up *chassis* term stays global; Tier-3 slop is already per-note
  [ADR-009 §Decision 6].
- A single `vintage.detune_amt` scales per-voice Tier-1/Tier-2 spread under
  unison/poly only (no effect in mono) [ADR-009 VV-11].
- Voice pool, `kMaxVoices`, allocation and note priority are owned by
  docs/design/06 / [ADR-006]; this subsystem consumes the voice index and
  active-voice count.

## 12. Real-time invariants

1. All PRNG / thermal / smoother / frozen-offset state lives in a fixed
   `std::array<DriftState, kMaxVoices>` allocated **only** in
   `prepare(...)`; `processBlock` and `noteOn` are `noexcept` and perform **no
   heap allocation and no locks** [ADR-009 §Decision 5, VV-16].
2. Drift integrators and the thermal model run **once per block** (control rate)
   and interpolate to sample rate; sub-Hz drift never runs at sample rate
   [ADR-009 §Decision 5, VV-14].
3. Tier-3 slop and the four variance draws are computed **once at note-on**,
   never per sample [ADR-009 VV-14].
4. cents->ratio uses the shared fast `exp2` approximation / interpolated table,
   never `std::exp` per sample [ADR-009 §Decision 5]. PRNG is `xorshift128+`
   (POD, no `std::random` heap/locking).
5. Re-roll is consumed via a lock-free `std::atomic<bool>` flag at the next block
   boundary [ADR-009 §Decision 5, VV-16].
6. FTZ/DAZ + an explicit denormal flush guard the OU/pink integrators during long
   silence [ADR-009 §Decision 5].
7. Determinism: `instance_seed + voice_index` derivation yields bit-identical
   output for identical input on the macOS arm64 bless gate [ADR-009 VV-17].

## 13. Open validation gaps (carried forward)

These are inherited from [ADR-009 §Consequences] and research/09 §7/§8.3 and MUST
remain labelled, not silently hardened into "facts":

- No bench-measured SH-101 drift/warm-up curves; depths/curves are
  practitioner/forum anecdote [research/09 §7.3, §8.3].
- IR3109 tempco is inferred from general OTA theory, not a datasheet; the "same
  -3300 ppm/degC" filter equivalence is theory (`kVcfDriftRatio` is the retuning
  knob) [research/09 §3.3, §8.3].
- VR-4's function is unconfirmed — only the 8 enumerated trimmers are perturbed
  [research/09 §2.2, §7.1].
- Per-node original tolerance classes unconfirmed; env-time `+/-5..20 %` is an
  unverified heuristic [research/09 §3.5, §8.2].
- Warm-up is the least authentic element — ships off by default, embellishment
  [ADR-009 §Consequences].
- Labelled taste/validation choices (NOT blockers): cubic-vs-Gaussian slop;
  block-rate+interp vs continuous 1/f; single-pole vs true Voss-McCartney pink
  [ADR-009 §Decision DSP persona].

## Acceptance hooks

Objectively-testable properties a backlog task's tests MUST verify:

- **Determinism / bless:** with a fixed `instance_seed` and identical note input,
  `processBlock` output is bit-identical across runs and across re-`prepare`
  with the same args (macOS arm64) [VV-17, §12.7].
- **Voice decorrelation:** for the same seed, voices `i != j` produce
  statistically independent Tier-1 draws and Tier-2 trajectories (cross-correlation
  near zero over a long block) [§11, VV-18].
- **Tier-2 correlation:** within one voice, VCO and VCF drift are perfectly
  correlated (both `= T(t)` scaled), never independent walks; setting
  `kVcfDriftRatio=0` removes cutoff drift while pitch drift persists [VV-13, §5.2].
- **No-alloc / no-lock hot path:** an allocation/lock detector confirms
  `processBlock` and `noteOn` allocate nothing and take no lock after `prepare`
  [VV-16, §12.1].
- **Frozen-at-note-on:** within a single held note, `varCutoff`/`varPw`/slop are
  constant (the smoother settles to a fixed value); only Tier-2 drift moves
  [VV-4, VV-7..VV-10, §7.1].
- **Bounded drift:** `T(t)` and total pitch/cutoff offset stay within the clamp
  (`kDriftClampCents`) for arbitrarily long runs; no runaway, no denormals after
  long silence [§5.1, §12.6].
- **Zero defaults => in tune:** with all schema defaults (`age=0`, all `var.*=0`,
  `tune.slop` still applies per its default) and seed drawn, the *Age=0* path
  with variance off yields zero added offset beyond the documented Tier-1
  personality; with the whole macro at 0 the instrument is in tune on load
  [VV-1, §10.2].
- **Smoother de-zipper:** an instantaneous target step at a block boundary
  produces a continuous, click-free output ramp of ~`kDriftSmoothMs`; no sample
  discontinuity [VV-15, §9].
- **Re-roll lock-free:** Re-roll changes the personality at the next block
  boundary without a glitch and without touching a lock; `seedLocked=true`
  preserves the seed across INIT/preset load [VV-12, §8.3].
- **VR-8 is a scale, not an offset:** changing `cal_spread` modulates the cutoff
  CV *slope* multiplier, leaving the cutoff *offset* path independent [§4.1].
- **Cutoff band is widest:** for equal spread settings, the cutoff offset band is
  the most generous of the variance set [VV-7, §4.1, §7.1].
- **Block-rate update:** the OU/pink integrators advance exactly once per block;
  per-sample reads are interpolations, not new noise draws [VV-14, §12.2].
- **Mono == voice 0:** the mono path is bit-identical to running voice index 0 of
  the unison/poly path with one voice [VV-18, §11].
- **Seed not a host param:** `instance_seed`, `seedLocked`, and the sequencer/arp
  state are absent from the host parameter list and round-trip via `<extras>`
  [ADR-008 C8, §8.2].
- **Honesty tags present:** every control exposes its VR-anchored vs
  embellishment tag; warm-up defaults off [ADR-009 §7; ADR-016 §5, §10, §5.3].

## References

- [ADR-009] Vintage variance / analog-drift model
  (`plan/decisions/009-vintage-variance-model.md`).
- [ADR-016] Owner ratifications — out-of-box defaults
  (`plan/decisions/016-owner-ratifications-2026-06-18.md`).
- [ADR-008] Parameter / state / preset schema and versioning
  (`plan/decisions/008-parameter-state-preset-schema.md`) — parameter IDs,
  `<extras>` non-parameter state, calibration-table location.
- [ADR-006] Voice architecture, polyphony and unison
  (`plan/decisions/006-voice-poly-unison-model.md`) — `kMaxVoices`, voice index.
- [ADR-005] Control-rate model and 6-bit CV quantization
  (`plan/decisions/005-control-rate-cpu-authenticity.md`) — orthogonal CV path.
- [ADR-003] Filter modeling method
  (`plan/decisions/003-filter-modeling-method.md`) — cutoff/resonance topology.
- [ADR-020] Parameter smoothing policy
  (`plan/decisions/020-parameter-smoothing-policy.md`) — schema-level smoothing.
- research/09 Vintage Variance & Analog-Drift Model
  (`docs/research/09-vintage-variance-drift.md`) — cited factual ground truth.
- docs/design/06 — parameter schema (owns all `mw101.*` IDs/ranges; referenced).
