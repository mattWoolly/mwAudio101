// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ThermalConstants.h — (PI) calibration constants for the shared
// scalar thermal drift integrator core/dsp/drift/ThermalState (task 064).
//
// Per the parallel-fleet conflict-avoidance rule, this module's (PI) constants land
// in a dedicated header that #includes (and extends the mw::cal namespaces of) the
// shared core/calibration/Calibration.h, rather than editing that orchestrator file
// directly [docs/design/06 §3.10; docs/design/00 §8.3; ADR-008 §1; AGENTS.md
// "(PI) centralization"]. Every constant here is (PI) — a *pragmatic invention*,
// NOT a measured SH-101 spec — and is referenced (never duplicated) by
// core/dsp/drift/ThermalState so no DSP call site inlines a literal.
//
// Source contracts: docs/design/08 §5.1 (bounded OU + clamp), §5.3 (warm-up
// transient), §5.4 (ThermalState layout), §12.6 (bounded drift, no denormals);
// ADR-009 §Decision 2 / VV-13 (one shared T(t)), VV-5 (warm-up off by default).
// All numeric values are TUNABLE DEFAULTS, not measured SH-101 specs [ADR-009 §8].

#pragma once

#include "Calibration.h"

namespace mw::cal::drift {

// --- Ornstein-Uhlenbeck integrator (Tier-2 live thermal drift) ---------------
//
// T(t) is a bounded leaky-integrated Gaussian: T += -k*T*dt + sigma*sqrt(dt)*N(0,1),
// advanced once per block (control rate), clamped to +/-kDriftClampCents so it can
// never run away [docs/design/08 §5.1; ADR-009 §Decision 2]. T is carried in a
// cents-domain *normalized* unit ([-1,1] nominal); the cents/pitch/cutoff mapping
// (driftCents = T * drift.depth) belongs to vintage-4 and is OUT OF SCOPE here.

// Hard symmetric clamp on the shared thermal state T (normalized cents domain). The
// OU process is mean-reverting and statistically stays well inside this, but the
// clamp makes runaway *structurally* impossible for arbitrarily long runs
// [docs/design/08 §5.1, §12.6]. (PI) — runaway guard, not a measured fact.
inline constexpr float kDriftClampCents = 1.5f;

// OU mean-reversion rate k (1/seconds) at the two ends of the drift.rate param
// (VV-3: drift.rate spans 0.01-1 Hz). k sets the drift bandwidth: larger k => the
// state reverts faster / wanders at a higher rate. rate01 in [0,1] maps log-linearly
// between these so the perceptual sweep is even. (PI) — drift-bandwidth defaults.
inline constexpr float kOuRateMinHz = 0.01f;   // drift.rate = 0 % end (slowest wander)
inline constexpr float kOuRateMaxHz = 1.0f;    // drift.rate = 100 % end (fastest wander)

// OU diffusion amplitude sigma (per-sqrt-second) in the normalized T domain. Chosen
// so the *stationary* standard deviation of T, sigma/sqrt(2k), is a small fraction
// of the clamp (the SH-101 is the stable end of the vintage spectrum because the
// CEM3340 is on-die temperature compensated [docs/design/08 §5.1; ADR-009
// §Decision 2]). (PI) — drift-depth default, not a measured fact.
inline constexpr float kOuSigma = 0.20f;

// Denormal-flush floor: |T| and each pink row below this magnitude are flushed to
// exactly 0 after the block update, so the integrators never enter a denormal CPU
// stall during long silence even independently of the hardware FTZ/DAZ mode
// [docs/design/08 §12.6; ADR-001 C11]. (PI) — FP-domain guard.
inline constexpr float kDenormalFloor = 1.0e-15f;

// --- Voss-McCartney / Kellet 1/f (pink) component (off by default) -----------
//
// Optional fixed-coefficient pink summed into the OU increment. pinkState[7] holds
// the seven update rows (§5.4). The summed rows are scaled by this gain before being
// added so the pink component is a *subtle* colouring, not a second random walk
// [docs/design/08 §5.1; ADR-009 §Decision 2]. (PI) — pink-blend default.
inline constexpr int   kPinkRows = 7;          // == sizeof(ThermalState::pinkState)
inline constexpr float kPinkGain = 0.05f;      // (PI) — pink contribution scale

// --- Warm-Up transient (off by default) --------------------------------------
//
// When enabled, an extra offset Twarm = kWarmupCents * exp(-t / tau) is ADDED to the
// shared T(t), decaying from "cold" toward zero over the user-set warmup.time
// (0-30 min) [docs/design/08 §5.3; ADR-009 VV-5]. Warm-up is the least authentic
// element and ships OFF by default (warmupSec < 0 means disabled) [ADR-009
// §Consequences]. (PI) — warm-up shape defaults, not measured facts.

// Cold-start magnitude of the warm-up offset, normalized T domain (same units as the
// OU state). Positive so a "cold" unit starts sharp/flat then settles. (PI).
inline constexpr float kWarmupCents = 0.50f;

// The exponential decays so that after the full user warmup.time the offset has
// fallen to this small fraction of kWarmupCents (i.e. effectively warm). tau is then
// derived per-tick from warmupTimeMin so the curve always lands "warm" at the user's
// chosen time: tau = warmupTimeSec / ln(1/kWarmupSettleFrac). (PI) — settle target.
inline constexpr double kWarmupSettleFrac = 0.05;   // 5 % residual at the set time

// Floor on the derived warm-up time constant (seconds) so a warmupTimeMin of 0 (or a
// tiny value) cannot divide-by-zero / produce a non-finite tau; below it the warm-up
// is treated as already-warm. (PI) — numeric guard.
inline constexpr double kWarmupTauFloorSec = 1.0e-3;

} // namespace mw::cal::drift
