// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/DriveConstants.h — (PI) calibration constants for the FX Drive
// stage (task 091).
//
// Per AGENTS.md every invented constant is tagged (PI) and centralized in a
// calibration header that lives under core/calibration. To avoid serializing on the
// single Calibration.h orchestrator while the development fleet runs in parallel,
// this module's (PI) constants live in their own header and EXTEND the mw::cal
// namespace (adding mw::cal::drive); the orchestrator wires the include into
// Calibration.h later. This header #includes Calibration.h so the (PI) discipline
// (one home for every invented constant) holds and downstream code can include just
// this header. It introduces NO measured-spec assertions — there is no SH-101 oracle
// for the FX Drive curve or its oversampler [ADR-010 Consequences; ADR-017
// re-affirmed locks; docs/design/07-fx-section.md §1.2].
//
// These realize docs/design/07-fx-section.md §4.3 (asymmetric shaper), §4.4 (pre/de
// emphasis tilt), §4.5 (DC blocker), and §9 (anti-alias floor acceptance hook).

#pragma once

#include "Calibration.h"

namespace mw::cal::drive {

// --- Asymmetric waveshaper (§4.3) -------------------------------------------------
// The shaper is a single tasteful asymmetric memoryless nonlinearity:
//     y = tanh(kDrivePreGain * (x + kDriveBias)) - tanh(kDrivePreGain * kDriveBias)
// tanh is the physically-grounded saturation [research/10 §4]; the small DC bias
// before the shaper introduces asymmetry (even harmonics for analog-style grit), and
// the subtracted second term re-centers the curve so the bias contributes asymmetry
// without a large standing DC component before the DC blocker [§4.3].

// Pre-shaper DC bias for even-harmonic asymmetry. (PI) — §4.3 (suggested 0.10).
inline constexpr float kDriveBias = 0.10f; // (PI)

// Fixed shaper input scaling (applied around the bias). (PI) — §4.3 (suggested 1.0).
inline constexpr float kDrivePreGain = 1.0f; // (PI)

// Drive amount -> pre-gain mapping range [§4.6]. drive_amount in [0,1] maps to a
// linear-in-dB pre-gain across [0, kDriveMaxGainDb] dB into the shaper (more = more
// grit). (PI) — §4.6 (suggested 0..+36 dB). The mapping itself is in Drive.cpp; the
// CEILING is the (PI) constant.
inline constexpr float kDriveMaxGainDb = 36.0f; // (PI)

// Output makeup gain range [§4.6]. drive_output in [0,1] maps linear-in-dB across
// [kDriveOutMinDb, kDriveOutMaxDb]; 0.5 is ~0 dB unity. (PI) — §4.6 (-24..+12 dB).
inline constexpr float kDriveOutMinDb = -24.0f; // (PI)
inline constexpr float kDriveOutMaxDb = 12.0f;  // (PI)

// --- Pre/de-emphasis tilt (Tone, §4.4) --------------------------------------------
// Tone is a complementary one-pole tilt: pre-emphasis boosts highs INTO the shaper,
// de-emphasis is the inverse AFTER the shaper. At tone == 0.5 both stages are unity
// (flat) so a low-drive signal passes through near-linearly [§4.4; ADR-010 FX-5].

// Tilt pivot frequency (Hz) of the one-pole pre/de-emphasis shelf. (PI) — §4.4
// (suggested 700 Hz).
inline constexpr float kDriveTiltHz = 700.0f; // (PI)

// Maximum tilt magnitude (dB) at the tone extremes (tone=0 -> dark, tone=1 ->
// bright). tone maps linearly to a tilt gain in [-kDriveTiltMaxDb, +kDriveTiltMaxDb];
// tone=0.5 -> 0 dB (unity, flat) [§4.4]. (PI).
inline constexpr float kDriveTiltMaxDb = 12.0f; // (PI)

// --- DC blocker (§4.5) ------------------------------------------------------------
// Standard one-pole high-pass placed AFTER downsampling [§4.5; ADR-010 FX-5]:
//     y[n] = x[n] - x[n-1] + kDcBlockR * y[n-1]
// kDcBlockR is the pole radius at 48 kHz; Drive.cpp scales it for other sample rates
// so the corner stays at the same frequency [§4.5].
inline constexpr float kDcBlockR = 0.9975f; // (PI) — pole radius at 48 kHz

// Reference sample rate the kDcBlockR pole radius is defined at (used to scale the
// pole for other sample rates) [§4.5]. (PI).
inline constexpr double kDcBlockRefSampleRate = 48000.0; // (PI)

// --- Anti-alias floor (§9 acceptance hook) ----------------------------------------
// A full-scale sine into a hot Drive must produce in-band aliasing BELOW this fixed
// floor thanks to the dedicated 2x oversampler; the acceptance test also shows the 2x
// path beats the same shaper run at 1x [docs/design/07 §9; ADR-017 L2; research/10
// §5]. (PI) — the CONSERVATIVE acceptance ceiling, NOT a measured spec. A single 2x
// stage on a full-scale, maximally-driven tanh measures ~-30 dB worst-case in-band
// alias-to-fundamental on the macOS arm64 reference (vs ~-10 dB for the same shaper
// at 1x — a ~20 dB improvement); this floor sits a few dB above that worst case so
// the 2x path reliably clears it with margin while the 1x reference clearly fails it.
inline constexpr float kDriveAliasFloorDb = -25.0f; // (PI)

// --- Compile-time sanity on the (PI) discipline -----------------------------------
static_assert(kDcBlockR > 0.0f && kDcBlockR < 1.0f,
              "DriveConstants: kDcBlockR MUST be in (0,1) for a stable one-pole DC "
              "blocker [docs/design/07 §4.5].");
static_assert(kDriveAliasFloorDb < 0.0f,
              "DriveConstants: kDriveAliasFloorDb is a dB floor below 0 dBFS "
              "[docs/design/07 §9].");
static_assert(kDriveOutMinDb < kDriveOutMaxDb,
              "DriveConstants: output makeup range MUST be ordered [§4.6].");

} // namespace mw::cal::drive
