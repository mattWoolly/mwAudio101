// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/DriftModel.cpp — implementation of the drift orchestration engine
// (task 072). See DriftModel.h for the contract / design-doc anchors.

#include "DriftModel.h"

#include <algorithm>

namespace mw::dsp::drift {

namespace {

// Configure one per-voice smoother bank to the mandatory de-zipper time constant. The
// realized canonical smoother (core/params/Smoother.h) ticks once per process() call,
// so we drive it at SAMPLE RATE (tickRateHz == sampleRate, one process() == one
// sample) and tick blockSize_ times per block — that makes a continuous click-free
// ramp of ~kDriftSmoothMs regardless of the upstream block-rate / note-on step
// [docs/design/08 §9, VV-15]. timeConstant is in SECONDS.
void prepareSmoother(mw::params::OnePoleSmoother& sm, double sampleRate) noexcept {
    sm.prepare(static_cast<double>(mw::cal::drift::kDriftSmoothMs) / 1000.0, sampleRate);
}

} // namespace

void DriftModel::prepare(double sampleRate, int blockSize, int numVoices) noexcept {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 1.0;
    blockSize_  = blockSize > 0 ? blockSize : 1;
    numVoices_  = std::clamp(numVoices, 1, mw::kMaxVoices);
    dtBlock_    = static_cast<double>(blockSize_) / sampleRate_;

    pendingReroll_.store(false, std::memory_order_relaxed);

    // Configure every voice: smoother coefficients (only here, never reallocated),
    // seed the per-voice PRNG, re-draw Tier-1, and clear note-on / thermal state.
    for (int v = 0; v < mw::kMaxVoices; ++v) {
        DriftState& s = voices_[static_cast<std::size_t>(v)];
        prepareSmoother(s.smPitch,  sampleRate_);
        prepareSmoother(s.smCutoff, sampleRate_);
        prepareSmoother(s.smPw,     sampleRate_);
        prepareSmoother(s.smEnv,    sampleRate_);
        prepareSmoother(s.smGlide,  sampleRate_);
        seedAndDrawTier1(v);
        s.noteOn  = NoteOnOffsets{};   // identity until a note-on draws
        thermal_[static_cast<std::size_t>(v)].reset(/*cold=*/false);
        s.active  = false;
        // Reset smoothers to the resting identity target (no transient on first block).
        s.smPitch.reset(0.0);
        s.smCutoff.reset(0.0);
        s.smPw.reset(0.0);
        s.smEnv.reset(1.0);
        s.smGlide.reset(1.0);
    }
    prepared_ = true;
}

void DriftModel::reset() noexcept {
    // Clear to a known start; re-draw Tier-1 from the current seed; no allocation.
    for (int v = 0; v < mw::kMaxVoices; ++v) {
        DriftState& s = voices_[static_cast<std::size_t>(v)];
        seedAndDrawTier1(v);
        s.noteOn = NoteOnOffsets{};
        thermal_[static_cast<std::size_t>(v)].reset(/*cold=*/false);
        s.active = false;
        s.smPitch.reset(0.0);
        s.smCutoff.reset(0.0);
        s.smPw.reset(0.0);
        s.smEnv.reset(1.0);
        s.smGlide.reset(1.0);
    }
    pendingReroll_.store(false, std::memory_order_relaxed);
}

void DriftModel::setInstanceSeed(std::uint64_t seed) noexcept {
    // Host thread: store the new seed and arm the lock-free Re-roll flag. The actual
    // Tier-1 re-draw is applied at the next block boundary inside processBlock [§8.3].
    instanceSeed_ = seed;
    pendingReroll_.store(true, std::memory_order_release);
}

void DriftModel::seedAndDrawTier1(int v) noexcept {
    DriftState& s = voices_[static_cast<std::size_t>(v)];
    s.rng.seed(seedFromInstance(instanceSeed_, v));
    // Voice 0 = instance personality (full Tier-1); voices 1.. scale by detuneAmt01
    // under unison/poly (no effect in mono == voice 0) [§11, VV-11].
    s.cal = drawCalibration(s.rng, params_.calSpread01 * detuneScaleFor(v));
}

float DriftModel::detuneScaleFor(int v) const noexcept {
    // Voice 0 is always the full instance personality; the detune amount only spreads
    // the ADDITIONAL voices under unison/poly [§11, VV-11].
    if (v == 0) return 1.0f;
    return params_.detuneAmt01;
}

void DriftModel::consumePendingReroll() noexcept {
    // Lock-free Re-roll consumption at an RT-entry boundary [§8.3, §12.5, VV-16].
    // Consumed by BOTH processBlock and noteOn so a host-thread setInstanceSeed takes
    // effect deterministically before the next note-on DRAW (which advances the per-voice
    // PRNG), regardless of whether note-on or the next block comes first. A single
    // atomic exchange => no lock, no alloc.
    if (pendingReroll_.exchange(false, std::memory_order_acquire)) {
        for (int v = 0; v < mw::kMaxVoices; ++v)
            seedAndDrawTier1(v);   // re-draw the frozen Tier-1 personality from new seed
    }
}

void DriftModel::noteOn(int voiceIndex, double /*noteHz*/) noexcept {
    if (voiceIndex < 0 || voiceIndex >= mw::kMaxVoices) return;
    // Apply any pending Re-roll FIRST so the note-on draw uses the current seed's PRNG.
    consumePendingReroll();
    DriftState& s = voices_[static_cast<std::size_t>(voiceIndex)];
    // Draw Tier-3 slop + the four variance spreads ONCE, from the per-voice PRNG. The
    // variance bands are scaled by detuneAmt01 for voices 1.. (per-voice spread, §11).
    const float ds = detuneScaleFor(voiceIndex);
    s.noteOn = drawNoteOn(s.rng,
                          params_.slopCents,
                          params_.varCutoff01 * ds,
                          params_.varEnv01    * ds,
                          params_.varPw01     * ds,
                          params_.varGlide01  * ds);
    // Reset the per-voice thermal integrator for this fresh note (warm-up disabled by
    // default; reset(false) keeps it off). The OU walk starts from rest per note.
    thermal_[static_cast<std::size_t>(voiceIndex)].reset(/*cold=*/false);
    s.active = true;
}

void DriftModel::processBlock(int numActiveVoices) noexcept {
    // --- Consume any pending Re-roll lock-free at the block boundary [§8.3, §12.5]. ---
    consumePendingReroll();

    const int n = std::clamp(numActiveVoices, 0, mw::kMaxVoices);

    for (int v = 0; v < n; ++v) {
        DriftState& s = voices_[static_cast<std::size_t>(v)];

        // --- Tier 2: advance the per-voice OU thermal integrator ONCE per block
        //     (control rate) [§5.1, §12.2, VV-14]. Each voice owns its own integrator
        //     so stacked voices decorrelate / beat naturally [§11, VV-18].
        ThermalState& th = thermal_[static_cast<std::size_t>(v)];
        th.tick(s.rng, params_.driftRate01, dtBlock_,
                params_.usePink, params_.useWarmup, params_.warmupTimeMin);
        const float T = th.value();

        // --- Map the shared thermal state to pitch + cutoff cents [§5.2, VV-13]:
        //     both = T*depth scaled — perfectly correlated, never two random walks.
        const float driftCents       = T * params_.driftDepthCents;
        const float cutoffDriftCents = driftCents * mw::cal::drift::kVcfDriftRatio;

        // --- Assemble the drifted targets (Tier1 frozen + Tier2 block-rate + Tier3/
        //     variance frozen-at-note-on), then push each into its per-voice smoother.
        const float pitchTarget  = s.cal.tuneCents + driftCents + s.noteOn.slopCents;
        const float cutoffTarget = s.cal.cutoffOffset + cutoffDriftCents + s.noteOn.varCutoff;
        const float pwTarget     = s.noteOn.varPw;
        const float envTarget    = s.noteOn.varEnvScale;
        const float glideTarget  = s.noteOn.varGlideScale;

        s.smPitch.setTarget(static_cast<double>(pitchTarget));
        s.smCutoff.setTarget(static_cast<double>(cutoffTarget));
        s.smPw.setTarget(static_cast<double>(pwTarget));
        s.smEnv.setTarget(static_cast<double>(envTarget));
        s.smGlide.setTarget(static_cast<double>(glideTarget));

        // --- De-zipper: tick each smoother once per SAMPLE (blockSize_ ticks) so a
        //     block-rate / note-on step ramps continuously over ~kDriftSmoothMs; the
        //     accessors below read the settled-toward value [§9, VV-15]. No alloc/lock.
        for (int i = 0; i < blockSize_; ++i) {
            s.smPitch.process();
            s.smCutoff.process();
            s.smPw.process();
            s.smEnv.process();
            s.smGlide.process();
        }
    }
}

// --- Per-voice smoothed accessors (read by the voice DSP, post-block) --------------

float DriftModel::pitchOffsetCents(int v) const noexcept {
    return static_cast<float>(voices_[static_cast<std::size_t>(v)].smPitch.current());
}
float DriftModel::cutoffOffset(int v) const noexcept {
    return static_cast<float>(voices_[static_cast<std::size_t>(v)].smCutoff.current());
}
float DriftModel::pwOffset(int v) const noexcept {
    return static_cast<float>(voices_[static_cast<std::size_t>(v)].smPw.current());
}
float DriftModel::envTimeScale(int v) const noexcept {
    return static_cast<float>(voices_[static_cast<std::size_t>(v)].smEnv.current());
}
float DriftModel::glideTimeScale(int v) const noexcept {
    return static_cast<float>(voices_[static_cast<std::size_t>(v)].smGlide.current());
}
float DriftModel::vcfWidthScale(int v) const noexcept {
    return voices_[static_cast<std::size_t>(v)].cal.vcfWidthScale;
}
float DriftModel::thermalValue(int v) const noexcept {
    return thermal_[static_cast<std::size_t>(v)].value();
}

} // namespace mw::dsp::drift
