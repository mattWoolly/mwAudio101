// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/DriftModel.h — the drift / vintage-variance ORCHESTRATION engine
// (task 072). Owns the DriftState[kMaxVoices] array and orchestrates the three tiers:
//   Tier 1 — frozen per-instance trimmer calibration (drawn at construct / Re-roll);
//   Tier 2 — the shared/per-voice live thermal OU drift, advanced once per block;
//   Tier 3 + four variance spreads — frozen at note-on;
// then feeds each drifted target through a mandatory per-voice OnePoleSmoother so any
// block-rate or note-on step is de-zippered before the voice DSP reads it.
//
// Realizes docs/design/08:
//   §3.1/§3.2 (module layout / data-flow), §4 (Tier 1), §5.1/§5.2/§5.3 (Tier 2 +
//   T->pitch/cutoff mapping + warm-up), §6/§7 (Tier 3 + variance frozen at note-on),
//   §8.2 (DriftModel class surface, seeding, determinism), §8.3 (Re-roll, lock-free),
//   §9 (mandatory output smoothing), §11 (poly/unison: mono == voice 0, per-voice
//   Tier-2, shared warm-up chassis), §12 (real-time invariants).
// Contracts: ADR-009 §Decision 2/5/6, VV-13/14/15/16/17/18.
//
// REAL-TIME / DETERMINISM invariants (docs/design/08 §12; ADR-009 §Decision 5):
//   - the DriftState[kMaxVoices] array and every smoother coefficient are sized /
//     configured ONLY in prepare(); processBlock() and noteOn() are noexcept and
//     perform NO heap allocation and NO locks [§12.1, VV-16];
//   - the thermal OU integrator advances exactly ONCE per block (control rate); the
//     mapping T->cents and the smoother de-zipper read per sample [§12.2, VV-14];
//   - Tier-3 slop + the four variance draws are computed once at note-on [§12.3];
//   - Re-roll is consumed via a lock-free std::atomic<bool> at the block boundary
//     [§8.3, §12.5, VV-16];
//   - same instance_seed + same note/param input => bit-identical output across runs
//     and across re-prepare (macOS arm64 bless) [§8.2, §12.7, VV-17].
//
// OUT OF SCOPE (other tasks own these, per the task spec): the host-thread Age-macro
// mapping (VintageMacro, vintage-5); the param-schema atomic plumbing (the params are
// CONSUMED here as a POD set via setParams off the audio thread, mirroring the
// §5.4 parameter-inversion seam); and persisting instance_seed / seedLocked in
// <extras> (state-presets stream).

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "DriftState.h"
#include "ThermalState.h"
#include "Xorshift128p.h"
#include "calibration/DriftConstants.h"
#include "calibration/DriftModelConstants.h"
#include "calibration/ThermalConstants.h"
#include "voice/VoiceTypes.h"   // mw::kMaxVoices

namespace mw::dsp::drift {

// The drift control set the engine reads, snapshotted off the audio thread (host
// thread / prepareToPlay), mirroring the §5.4 parameter-inversion seam: the audio
// thread never reads a std::atomic in a tight loop — it reads this pre-stored POD.
// The schema (docs/design/06 §2) OWNS the IDs/ranges/defaults; these are the modeled
// engineering values behind them [docs/design/08 §10]. Engineering units, NOT
// normalized 0..1 (the normalized->engineering map is the schema/bridge's job):
//   driftDepthCents : mw101.drift.depth   (0..50 cents)
//   driftRate01     : mw101.drift.rate    (0..1 -> OU k via ThermalState)
//   slopCents       : mw101.tune.slop     (0..20 cents)
//   calSpread01     : mw101.vintage.cal_spread (0..1)
//   varCutoff01/varEnv01/varPw01/varGlide01 : mw101.var.* (0..1)
//   warmupTimeMin   : mw101.warmup.time   (0..30 min); useWarmup gates it
//   usePink         : optional 1/f component (off by default)
//   detuneAmt01     : mw101.vintage.detune_amt (0..1; scales per-voice spread under
//                     unison/poly only — no effect in mono == voice 0)
struct DriftParams {
    float driftDepthCents = 4.0f;    // mw101.drift.depth default
    float driftRate01     = 0.1f;    // mw101.drift.rate (already 0..1 here)
    float slopCents       = 2.5f;    // mw101.tune.slop default
    float calSpread01     = 0.25f;   // mw101.vintage.cal_spread default
    float varCutoff01     = 0.0f;
    float varEnv01        = 0.0f;
    float varPw01         = 0.0f;
    float varGlide01      = 0.0f;
    float warmupTimeMin   = 0.0f;    // mw101.warmup.time
    float detuneAmt01     = 0.0f;    // mw101.vintage.detune_amt
    bool  useWarmup       = false;   // warm-up off by default [§5.3, VV-5]
    bool  usePink         = false;   // 1/f off by default [§5.1]
};

class DriftModel {
public:
    DriftModel() noexcept = default;

    // The ONLY allocation/sizing/seeding site (off the audio thread). Sizes nothing
    // dynamically beyond the fixed std::array (which is by-value, so this is really a
    // configure step), sets every smoother time constant to kDriftSmoothMs, seeds the
    // per-voice PRNGs deterministically from instance_seed, and draws Tier-1 for all
    // voices. Idempotent / re-callable on sample-rate / block-size change [§8.2, §12.1].
    void prepare(double sampleRate, int blockSize, int numVoices) noexcept;

    // RT; noexcept; clears smoothers + thermal state to a known start; re-draws Tier-1
    // from the current seed. No allocation [§5.5 lifecycle parity].
    void reset() noexcept;

    // Host thread. Stores the new seed and arms the lock-free pendingReroll_ flag; the
    // re-draw is applied at the NEXT block boundary inside processBlock [§8.3, §12.5].
    void setInstanceSeed(std::uint64_t seed) noexcept;

    [[nodiscard]] std::uint64_t instanceSeed() const noexcept { return instanceSeed_; }

    // Host thread (or prepareToPlay). Snapshot the drift control set; processBlock reads
    // only this stored POD, never a std::atomic in the tight loop [§5.4 inversion].
    void setParams(const DriftParams& p) noexcept { params_ = p; }

    [[nodiscard]] const DriftParams& params() const noexcept { return params_; }

    // RT hot path entry called at NOTE-ON for a voice: draws Tier-3 slop + the four
    // variance spreads ONCE from the per-voice PRNG and arms the voice. noexcept; no
    // alloc/lock [§6, §7, §12.3]. noteHz is accepted for future octave-dependent slop
    // scaling; the v1 draw is octave-independent so it is currently unused.
    void noteOn(int voiceIndex, double noteHz) noexcept;

    // RT hot path. Consumes any pending Re-roll (lock-free), advances each active
    // voice's Tier-2 OU integrator exactly once (control rate), maps T->pitch/cutoff
    // cents, assembles the per-voice drifted targets (Tier1 + Tier2 + Tier3 + variance),
    // pushes them into the per-voice smoothers, then ticks the smoothers blockSize_
    // times so the per-voice accessors return a click-free, de-zippered value. noexcept;
    // no alloc/lock [§12.1, §12.2, §12.5, VV-14/15/16].
    void processBlock(int numActiveVoices) noexcept;

    // --- Per-voice smoothed outputs, read by the voice DSP (post-block) ------------
    // Additive cents on the VCO pitch CV (Tier1 tune + Tier2 driftCents + Tier3 slop).
    [[nodiscard]] float pitchOffsetCents(int v) const noexcept;
    // Additive cents-equiv on the VCF cutoff CV (Tier1 cutoffOffset + Tier2
    // cutoffDriftCents + variance cutoff). The VR-8 width SCALE is exposed separately.
    [[nodiscard]] float cutoffOffset(int v) const noexcept;
    // Additive duty fraction on the pulse width (variance PW).
    [[nodiscard]] float pwOffset(int v) const noexcept;
    // Multiplicative scale on the A/D/R time constants (variance env-time).
    [[nodiscard]] float envTimeScale(int v) const noexcept;
    // Multiplicative scale on the glide time constant (variance glide).
    [[nodiscard]] float glideTimeScale(int v) const noexcept;

    // VR-8 VCF width SCALE (multiplicative on the cutoff CV slope) for voice v — a
    // frozen Tier-1 value, exposed directly (it is not de-zippered: it never steps
    // mid-note) so the voice can apply it as a slope multiplier [§4.1].
    [[nodiscard]] float vcfWidthScale(int v) const noexcept;

    // Test / introspection: the raw shared thermal state of a voice (post-block).
    [[nodiscard]] float thermalValue(int v) const noexcept;

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // Seed the per-voice PRNG and re-draw its frozen Tier-1 calibration from the
    // current instanceSeed_. Used by prepare/reset and the Re-roll path.
    void seedAndDrawTier1(int v) noexcept;

    // Lock-free Re-roll consumption (atomic exchange), called at the RT-entry boundary
    // by BOTH processBlock and noteOn so a host-thread setInstanceSeed takes effect
    // deterministically before the next PRNG-advancing draw [§8.3, §12.5, VV-16].
    void consumePendingReroll() noexcept;

    // Apply the Tier-1 detune scaling under unison/poly: voice 0 is the instance
    // personality (full Tier-1); voices 1.. have their Tier-1 spread scaled by
    // detuneAmt01 (no effect in mono == voice 0) [§11, VV-11].
    [[nodiscard]] float detuneScaleFor(int v) const noexcept;

    // Per-voice frozen Tier-1 cal + PRNG + note-on offsets + smoothers (the §8.1 POD).
    std::array<DriftState, mw::kMaxVoices> voices_{};
    // Per-voice Tier-2 OU thermal integrators. The realized DriftState (task 065) carries
    // only a layout PLACEHOLDER for its `thermal` field (it predates ThermalState.h, task
    // 064); the orchestration engine owns the LIVE integrators here so each voice has its
    // own decorrelated OU walk while the warm-up chassis stays global [§5.4, §8.1, §11;
    // ADR-009 §Decision 6]. (DriftState.h is a sibling-owned shared file — not edited.)
    std::array<ThermalState, mw::kMaxVoices> thermal_{};
    DriftParams        params_{};
    double             sampleRate_   = 0.0;
    int                blockSize_    = 0;
    int                numVoices_    = 0;
    double             dtBlock_      = 0.0;        // blockSize_ / sampleRate_, seconds
    std::uint64_t      instanceSeed_ = 0;
    std::atomic<bool>  pendingReroll_{false};      // consumed lock-free in processBlock
    bool               prepared_     = false;
};

} // namespace mw::dsp::drift
