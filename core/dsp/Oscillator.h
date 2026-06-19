// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Oscillator.h — the CEM3340-modeled VCO: master phase core (exponential
// 1V/oct pitch converter, 16'/8'/4'/2' footage CV offsets, dt Nyquist clamp, small
// intrinsic drift/stability model; task 029) PLUS the band-limited saw + variable-
// width pulse (PWM) construction with PolyBLEP (default) / minBLEP (HQ + auto-
// escalated) anti-aliasing and the PWM width map + duty/dt overlap clamp (task 030).
//
// Realizes docs/design/01-dsp-oscillators.md §4.1-§4.4, §4.7 (VCO responsibilities,
// class signature, exp pitch, footage, drift), §4.5 (saw + pulse construction),
// §4.6 (PWM width map + overlap clamp), §2.1 (phase / dt convention), §2.2-§2.3 (AA
// mode + HQ auto-escalation) and §10, plus ADR-002 C1-C3, C7, C9.
//
// SCOPE: the phase core + exp converter + footage + dt clamp + drift hooks (029) and
// the band-limited shape construction (030). The sub-oscillator divide, the noise
// source, the source mixer, and the per-voice tune/bender/LFO CV summation remain
// OUT OF SCOPE here (core-osc-5/6/7, control-core). renderSample() emits the band-
// limited saw and the variable-width pulse only [docs/design/01 §4.5, §4.6].
//
// Real-time invariants [docs/design/01 §2.4; docs/design/00 §9.1; ADR-002 C11]:
//   - renderSample()/phase()/wrappedThisSample()/frequencyHz()/dt()/duty()/
//     effectiveAaMode()/reset()/setControls() are noexcept and perform no heap
//     allocation and take no locks.
//   - All sizing/allocation happens off the audio thread in prepare() (the minBLEP
//     applicator rings are pre-sized there from the shared read-only table).
//   - The AA mode is selected in prepare()/setControls() only (the per-sample HQ
//     auto-escalation is keyed off the per-block fundamental, not a per-sample
//     parameter read) [§2.2-§2.3; ADR-018 Q5].
//   - Every (PI) pitch/footage/drift/PWM/escalation constant is read from a
//     calibration header (VcoConstants.h / VcoShapeConstants.h) and NEVER inlined [§10].

#pragma once

#include "../calibration/VcoConstants.h"
#include "../calibration/VcoShapeConstants.h"   // PWM map + HQ-escalation (PI) (task 030)
#include "OscAaMode.h"   // canonical mw101::dsp::OscAaMode { PolyBlep, MinBlepHq } (task 031)
#include "MinBlepTable.h"   // per-voice MinBlepApplicator (HQ / escalated band-limiting)

namespace mw101::dsp {

// Anti-aliasing mode, derived from mw101.quality (Eco/Standard -> PolyBlep,
// HQ -> MinBlepHq) [docs/design/01 §2.2; ADR-018 Q-table]. The CANONICAL definition
// is the shared mw101::dsp::OscAaMode in OscAaMode.h (task 031) — included above and
// consumed here so the VCO and the sub-oscillator share ONE type (no ODR violation).
// The phase core stores the selected mode for core-osc-4 to consume; this task does
// not branch on it per sample. Set in prepare()/setControls() (per block), never
// per-sample [ADR-018 Q5].

// Snapshot of smoothed/derived control values, supplied PER BLOCK (not per sample)
// [docs/design/01 §4.2]. pitchCvVolts is the already-summed 1V/oct CV
// (key + tune + bend + LFO/MOD); the footage offset and drift are applied INSIDE the
// converter. The tune/bender/LFO summation is owned by control-core; this VCO
// consumes the pre-summed volts [plan/backlog/029 Out-of-scope].
struct OscControls
{
    float     pitchCvVolts = 0.0f;             // summed 1V/oct CV (pre-footage, pre-drift)
    Footage   footage      = Footage::Eight;   // 16'/8'/4'/2' octave-offset switch (§4.4)
    float     pwmCvNorm    = 0.0f;             // 0..1 pulse-width CV (PWM map: core-osc-4)
    OscAaMode aaMode       = OscAaMode::PolyBlep;
};

class Oscillator
{
public:
    // Off-the-audio-thread setup; the ONLY place allocation may happen. `hqTable` is
    // the shared, read-only minBLEP table built by the engine (used by core-osc-4 for
    // HQ/escalated band-limiting); it may be nullptr while band-limiting is unwired.
    void prepare (double sampleRate, const MinBlepTable* hqTable) noexcept;

    // Clear phase/drift transient to a known start. No allocation. [docs/design/00 §5.5]
    void reset() noexcept;

    // Per-block (not per-sample) control update: recomputes freq/dt from the exp
    // converter against the footage offset + settled drift. noexcept, no allocation.
    void setControls (const OscControls& c) noexcept;

    struct Output { float saw; float pulse; };

    // Render one sample: advances the master phase ONCE, records the wrap, and emits
    // the band-limited saw (2*t-1 - PolyBLEP, or trivial ramp + minBLEP correction in
    // HQ/escalated mode) and the variable-width pulse (two independent BLEPs/period,
    // rising at phase 0, falling at the duty phase) [§4.5; ADR-002 C1-C2].
    // noexcept, no allocation.
    [[nodiscard]] Output renderSample() noexcept;

    // Phase access for the phase-locked sub-oscillator (core-osc-5).
    [[nodiscard]] double phase() const noexcept { return phase_; }            // t in [0,1)
    [[nodiscard]] bool   wrappedThisSample() const noexcept { return wrapped_; }
    [[nodiscard]] double frequencyHz() const noexcept { return freqHz_; }     // current fundamental
    [[nodiscard]] double dt() const noexcept { return dt_; }                  // freq/fs, clamped

    // Effective (clamped) pulse duty in [max(kPwmDutyMin, dt), kPwmDutyMax]: the PWM
    // width map (§4.6) after the duty/dt overlap clamp (ADR-002 C3). Recomputed per
    // block in setControls(); pure read here.
    [[nodiscard]] double duty() const noexcept { return duty_; }

    // The AA mode actually applied this block: the requested tier OR minBLEP if the
    // fundamental escalated above kHqEscalationHz [§2.2-§2.3; ADR-002 C9]. Selected in
    // prepare()/setControls(), never flipped per sample on the audio thread.
    [[nodiscard]] OscAaMode effectiveAaMode() const noexcept { return effectiveAaMode_; }

    // --- Drift / stability model hooks (§4.7) -----------------------------------
    // The per-voice scale/offset error SEEDS the VCO consumes (the detailed variance
    // distribution is owned by the variance doc / ADR-009). Default zero => drift is
    // effectively off, but the hooks exist. Seeds are normalized in [-1, 1]; the
    // calibration ceilings (kDriftScalePpmMax etc.) bound their physical effect.
    void setDriftSeeds (float scaleErrSeed, float offsetErrSeed) noexcept;
    [[nodiscard]] float scaleErrSeed() const noexcept { return scaleErr_; }
    [[nodiscard]] float offsetErrSeed() const noexcept { return offsetErr_; }

    // Test/host hook: fast-forward the warm-up first-order settle to fully steady
    // state (off the audio thread). The audio-thread path advances it per block.
    void settleDriftForTest() noexcept { warmupSettle_ = 1.0; }

private:
    // Recompute freqHz_/dt_ from the current controls + settled drift (§4.3/§4.4),
    // then the band-limited SHAPE state: the PWM duty (map + overlap clamp, §4.6) and
    // the effective AA mode after HQ auto-escalation (§2.3).
    void recompute() noexcept;
    void recomputeShape() noexcept;

    double      sampleRate_ = 0.0;
    double      phase_      = 0.0;   // master accumulator in [0,1)
    double      dt_         = 0.0;   // freq/fs, clamped to kDtMax
    double      freqHz_     = 0.0;   // current fundamental
    bool        wrapped_    = false; // saw wrap edge occurred this sample
    OscControls controls_{};

    // --- Band-limited shape state (§4.5, §4.6, §2.3) ----------------------------
    // Effective (clamped) pulse duty: PWM map then the dt overlap clamp so neither
    // BLEP window straddles the other (ADR-002 C3). Default 0.5 (square).
    double    duty_            = static_cast<double> (mw::cal::vco::kPwmDutyMax);
    // The AA mode actually applied this block (requested tier OR escalated minBLEP).
    OscAaMode effectiveAaMode_ = OscAaMode::PolyBlep;

    // Per-voice minBLEP overlap-add applicators, one per shape, used when the
    // effective mode is MinBlepHq (HQ tier OR auto-escalated). Pre-sized in prepare()
    // from the shared read-only table; purely arithmetic on the audio thread
    // [docs/design/01 §3.3, §4.5; ADR-002 C8, C11].
    //
    // Saw: the trivial ramp (2*t-1) already carries the -2 reset DC, so the HQ saw adds
    // the RESIDUAL only — we track the applicator's accumulated DC (sawBlepLevel_) and
    // subtract it from sawBlep_.next() so just the band-limited residual is added.
    // Pulse: piecewise-constant, so pulseBlep_.next() IS the band-limited pulse (it owns
    // the held DC, driven by scheduleStep of the +2 rising / -2 falling deltas; the same
    // applicator contract the sub-oscillator uses) [docs/design/01 §4.5; ADR-002 C2].
    MinBlepApplicator sawBlep_;
    MinBlepApplicator pulseBlep_;
    double            sawBlepLevel_ = 0.0;     // running DC scheduled into sawBlep_
    float             pulseNaive_   = 1.0f;    // current naive pulse level (+1 high / -1 low)

    // Drift state (§4.7): a first-order warm-up settle factor in [0,1] scaling the
    // residual error, plus the per-voice seeds. warmupSettle_ == 0 at cold start,
    // -> 1 as the transient settles over ~kWarmupTauSec.
    double warmupSettle_ = 0.0;
    float  scaleErr_     = 0.0f;     // per-voice scale-error seed in [-1, 1]
    float  offsetErr_    = 0.0f;     // per-voice offset-error seed in [-1, 1]

    const MinBlepTable* hqTable_ = nullptr;   // band-limiting hook (core-osc-4)
};

} // namespace mw101::dsp
