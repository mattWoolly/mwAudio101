// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/Oscillator.h — the CEM3340-modeled VCO master phase core: exponential
// 1V/oct pitch converter, 16'/8'/4'/2' footage CV offsets, dt Nyquist clamp, and a
// small intrinsic drift/stability model (hooks present, default near-zero) (task 029).
//
// Realizes docs/design/01-dsp-oscillators.md §4.1-§4.4, §4.7 (VCO responsibilities,
// class signature, exp pitch, footage, drift), §2.1 (phase accumulator / dt
// convention) and §10 (footage offsets, (PI) centralization, real-time safety).
//
// SCOPE (task 029): the phase core, the exp converter, the footage offsets, the dt
// clamp, and the drift hooks. The band-limited saw/pulse construction, the PWM map,
// the sub-oscillator divide, and the per-voice tune/bender/LFO CV summation are
// OUT OF SCOPE here (core-osc-4 / core-osc-5 / control-core). renderSample()
// therefore emits a RAW ramp saw (2*phase - 1) and a raw pulse; band-limiting is
// layered on by core-osc-4 [docs/design/01 §4.5; plan/backlog/029 Out-of-scope].
//
// Real-time invariants [docs/design/01 §2.4; docs/design/00 §9.1; ADR-002 C11]:
//   - renderSample()/phase()/wrappedThisSample()/frequencyHz()/dt()/reset()/
//     setControls() are noexcept and perform no heap allocation and take no locks.
//   - All sizing/allocation happens off the audio thread in prepare().
//   - Every (PI) pitch/footage/drift constant is read from the calibration header
//     (core/calibration/VcoConstants.h) and NEVER inlined here [§10].

#pragma once

#include "../calibration/VcoConstants.h"
#include "OscAaMode.h"   // canonical mw101::dsp::OscAaMode { PolyBlep, MinBlepHq } (task 031)

namespace mw101::dsp {

class MinBlepTable;   // fwd: the shared HQ residual table (band-limiting, core-osc-4)

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

    // Render one sample: advances the master phase ONCE and records the wrap. Emits
    // the RAW ramp saw / raw pulse (band-limiting is core-osc-4). noexcept, no alloc.
    [[nodiscard]] Output renderSample() noexcept;

    // Phase access for the phase-locked sub-oscillator (core-osc-5).
    [[nodiscard]] double phase() const noexcept { return phase_; }            // t in [0,1)
    [[nodiscard]] bool   wrappedThisSample() const noexcept { return wrapped_; }
    [[nodiscard]] double frequencyHz() const noexcept { return freqHz_; }     // current fundamental
    [[nodiscard]] double dt() const noexcept { return dt_; }                  // freq/fs, clamped

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
    // Recompute freqHz_/dt_ from the current controls + settled drift (§4.3/§4.4).
    void recompute() noexcept;

    double      sampleRate_ = 0.0;
    double      phase_      = 0.0;   // master accumulator in [0,1)
    double      dt_         = 0.0;   // freq/fs, clamped to kDtMax
    double      freqHz_     = 0.0;   // current fundamental
    bool        wrapped_    = false; // saw wrap edge occurred this sample
    OscControls controls_{};

    // Drift state (§4.7): a first-order warm-up settle factor in [0,1] scaling the
    // residual error, plus the per-voice seeds. warmupSettle_ == 0 at cold start,
    // -> 1 as the transient settles over ~kWarmupTauSec.
    double warmupSettle_ = 0.0;
    float  scaleErr_     = 0.0f;     // per-voice scale-error seed in [-1, 1]
    float  offsetErr_    = 0.0f;     // per-voice offset-error seed in [-1, 1]

    const MinBlepTable* hqTable_ = nullptr;   // band-limiting hook (core-osc-4)
};

} // namespace mw101::dsp
