// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/dsp/drift/ThermalState.h — the shared scalar Ornstein-Uhlenbeck thermal
// drift integrator with optional fixed-coefficient Voss-McCartney pink and an
// optional exponential warm-up transient, advanced ONCE PER BLOCK (task 064).
//
// Realizes docs/design/08 §5.1 (bounded OU + clamp, no runaway), §5.3 (warm-up
// transient, off by default, decays toward zero over warmup.time), §5.4 (the
// ThermalState struct layout/signatures), §12.6 (bounded drift, FTZ/DAZ + explicit
// denormal flush) and ADR-009 §Decision 2 / VV-13 (ONE shared T(t) drives both VCO
// and VCF — never two independent walks) and VV-5 (warm-up off by default).
//
// Class discipline (docs/design/08 §12):
//   - POD: trivially-copyable / standard-layout so it lives by value inside the
//     pre-allocated DriftState[kMaxVoices] array [§8.1, §12.1]; no heap, no locks.
//   - tick() / reset() are noexcept; tick() runs at *control rate* (once per block),
//     so the transcendentals it uses (sqrt for the OU diffusion term, exp for the
//     warm-up curve) are block-rate, never per-sample [§5.1, §5.3, §12.2].
//   - an explicit denormal flush on the integrator state guards long silence in
//     addition to the engine-level FTZ/DAZ [§12.6; ADR-001 C11].
//
// OUT OF SCOPE here (owned by sibling tasks, per the task spec): the PRNG itself
// (consumes vintage-1's Xorshift128p), the mapping of T to pitch/cutoff cents
// (vintage-4), and per-voice ownership/decorrelation orchestration (vintage-4).
//
// All numeric figures resolve in core/calibration/ThermalConstants.h and are
// TUNABLE DEFAULTS, NOT measured SH-101 specs.

#pragma once

#include "../../calibration/ThermalConstants.h"
#include "Xorshift128p.h"

#include <cmath>

namespace mw::dsp::drift {

// Shared scalar temperature state per docs/design/08 §5.4. ONE instance drives both
// VCO and VCF (Tier-2 correlation, VV-13); under unison/poly each voice owns its own
// integrator while the warm-up chassis term stays global [§5.4, §11; ADR-009
// §Decision 6]. value() is read by all consumers; the cents/pitch/cutoff mapping is
// applied downstream (vintage-4), not here.
//
// POD aggregate: every data member is public so the struct stays standard-layout AND
// trivially-copyable. The §5.4 fields (T, pinkState, warmupSec) are the contract
// surface; ouState/pinkCounter are implementation bookkeeping (the bounded OU
// integrator in isolation, and the Voss-McCartney row selector) and are zero in
// every reset state.
struct ThermalState {
    // Total shared state read by all consumers, cents-domain normalized [-1,1]
    // nominal [§5.4]. T = clamped OU integrator (ouState) + warm-up offset; value()
    // returns it directly. The OU integrator alone is clamped to +/-kDriftClampCents
    // (no runaway); the warm-up transient sits ON TOP as a decaying offset [§5.3].
    float    T = 0.0f;

    // Optional Voss-McCartney pink rows (off by default) [§5.4]. Summed (scaled) into
    // the OU increment when usePink is true; left at zero otherwise.
    float    pinkState[mw::cal::drift::kPinkRows]{};

    // Elapsed warm time in seconds; < 0 means warm-up DISABLED (the default) [§5.4].
    double   warmupSec = -1.0;

    // The bounded OU integrator in isolation (the mean-reverting random walk), clamped
    // to +/-kDriftClampCents each tick. Kept separate from T so the warm-up offset
    // never compounds into the OU dynamics. Bookkeeping, not part of the §5.4 surface.
    float    ouState = 0.0f;

    // Voss-McCartney row-selector counter (bookkeeping). Zero in every reset state so
    // two ThermalStates reset the same way are bit-identical.
    unsigned pinkCounter = 0u;

    // Reset to a known start [§5.4]. cold==true seeds the warm-up offset (a "cold"
    // unit) and arms the transient (warmupSec=0); cold==false leaves warm-up disabled
    // (warmupSec<0). All integrator/pink state is cleared either way.
    void reset(bool cold) noexcept {
        ouState     = 0.0f;
        pinkCounter = 0u;
        for (float& row : pinkState) row = 0.0f;
        if (cold) {
            warmupSec = 0.0;                  // armed; transient starts at full offset
            T = mw::cal::drift::kWarmupCents;  // cold start = full warm-up offset, OU=0
        } else {
            warmupSec = -1.0;                 // disabled
            T = 0.0f;
        }
    }

    // Advance the shared thermal state by one block. dtBlock is the block duration in
    // seconds; rate01 = drift.rate param in [0,1]; usePink/useWarmup are the optional
    // component flags; warmupTimeMin is the user warm-up time (minutes), used only
    // when useWarmup is true [§5.4]. Exactly one OU step per call (control rate,
    // VV-14). noexcept; no heap, no locks.
    void tick(Xorshift128p& rng, float rate01, double dtBlock,
              bool usePink, bool useWarmup, float warmupTimeMin) noexcept {
        namespace c = mw::cal::drift;

        const double dt = (dtBlock > 0.0) ? dtBlock : 0.0;

        // --- OU mean-reversion rate k (1/s) from rate01 (log-linear over VV-3 range).
        const float  r01 = clamp01(rate01);
        const double k   = logLerp(c::kOuRateMinHz, c::kOuRateMaxHz, r01);

        // --- OU increment on the isolated integrator: -k*T*dt + sigma*sqrt(dt)*N(0,1).
        double ou = static_cast<double>(ouState);
        const double sqrtDt = std::sqrt(dt);
        const double noise  = static_cast<double>(rng.gaussian());
        ou += -k * ou * dt + static_cast<double>(c::kOuSigma) * sqrtDt * noise;

        // --- Optional Voss-McCartney pink, summed into the OU state (off by default).
        if (usePink) {
            ou += static_cast<double>(c::kPinkGain) * static_cast<double>(nextPink(rng));
        }

        // Clamp the OU integrator so it can never run away [§5.1, §12.6].
        ouState = clampSym(static_cast<float>(ou), c::kDriftClampCents);

        // --- Advance elapsed warm time and recompute this block's warm-up offset.
        float warmOffset = 0.0f;
        if (useWarmup) {
            if (warmupSec < 0.0) warmupSec = 0.0;   // arm on first enabled tick
            else                 warmupSec += dt;    // advance elapsed warm time
            warmOffset = static_cast<float>(
                warmOffsetFor(warmupSec, /*useWarmup=*/true, warmupTimeMin));
        } else {
            warmupSec = -1.0;                         // keep disabled
        }

        // --- Total shared state = clamped OU integrator + decaying warm-up offset.
        T = ouState + warmOffset;
        flushDenormals();
    }

    // Shared accessor read by all consumers [§5.4]. Total T (OU + warm-up offset).
    float value() const noexcept { return T; }

private:
    static constexpr float clamp01(float v) noexcept {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    static constexpr float clampSym(float v, float bound) noexcept {
        return v < -bound ? -bound : (v > bound ? bound : v);
    }
    // Geometric (log-linear) interpolation between two positive endpoints.
    static double logLerp(float lo, float hi, float t) noexcept {
        const double l = std::log(static_cast<double>(lo));
        const double h = std::log(static_cast<double>(hi));
        return std::exp(l + (h - l) * static_cast<double>(t));
    }

    // Warm-up offset Twarm = kWarmupCents * exp(-elapsed/tau) for a given elapsed warm
    // time [§5.3]. tau is derived so the offset has decayed to kWarmupSettleFrac at
    // the user's set warmup.time: exp(-warmTime/tau) = settleFrac. Returns 0 when
    // warm-up is disabled (elapsed < 0) or off.
    static double warmOffsetFor(double elapsedSec, bool useWarmup, float warmupTimeMin) noexcept {
        namespace c = mw::cal::drift;
        if (!useWarmup || elapsedSec < 0.0) return 0.0;
        const double warmTimeSec = static_cast<double>(warmupTimeMin) * 60.0;
        const double denom = std::log(1.0 / c::kWarmupSettleFrac); // > 0
        const double tau   = (warmTimeSec > c::kWarmupTauFloorSec)
                                 ? (warmTimeSec / denom)
                                 : c::kWarmupTauFloorSec;
        return static_cast<double>(c::kWarmupCents) * std::exp(-elapsedSec / tau);
    }

    // One Voss-McCartney pink sample from the seven rows. Each tick, the row selected
    // by the lowest changed bit of an incrementing counter is re-drawn from U[-1,1);
    // the output is the mean of all rows. Fixed-coefficient, bounded, POD state.
    float nextPink(Xorshift128p& rng) noexcept {
        const unsigned prev = pinkCounter++;
        unsigned diff = prev ^ pinkCounter;          // bits that flipped this tick
        int row = 0;
        while ((diff & 1u) == 0u && row < mw::cal::drift::kPinkRows - 1) {
            diff >>= 1;
            ++row;
        }
        pinkState[row] = 2.0f * rng.nextFloat01() - 1.0f;  // U[-1,1)

        float sum = 0.0f;
        for (float v : pinkState) sum += v;
        return sum / static_cast<float>(mw::cal::drift::kPinkRows);
    }

    // Explicit denormal flush on the integrator state, in addition to FTZ/DAZ, so a
    // long silent tail never stalls in subnormals [§12.6; ADR-001 C11].
    void flushDenormals() noexcept {
        const float floorMag = mw::cal::drift::kDenormalFloor;
        if (std::fabs(ouState) < floorMag) ouState = 0.0f;
        if (std::fabs(T) < floorMag)       T = 0.0f;
        for (float& row : pinkState)
            if (std::fabs(row) < floorMag) row = 0.0f;
    }
};

} // namespace mw::dsp::drift
