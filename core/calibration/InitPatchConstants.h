// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/InitPatchConstants.h — the (PI) INIT-patch overlay values the
// out-of-box default patch (core/state/InitPatch.h, task 021) reads from
// (docs/design/06 §11, §3.10; ADR-016).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h: it #includes that root and APPENDS into the same
// mw::cal subsystem namespaces, so the two compose additively. The INIT patch
// references these named constants so NO (PI) INIT value is inlined at the patch's
// build site [docs/design/06 §3.10, §11; ADR-008 §1; ADR-020 S13].
//
// EVERY constant here is (PI) — a *pragmatic invention* / tunable out-of-box pole,
// NOT a measured spec. ADR-016 §11 fixes the SURFACE (which knobs the INIT patch
// moves and in which direction); the precise low / low-mid magnitudes are PI. The
// per-parameter `defaultValue` in §3 is NOT changed by these — INIT is a patch
// OVERLAY, so re-tuning a value here is a one-line local edit and is NOT a migration
// event [docs/design/06 §11; ADR-016 Contract].

#pragma once

#include "Calibration.h"
// The velocity-depth low-mid pole is the SAME value as the vel.depth param default
// (mw::cal::paramdefault::kVelDepth), which lives in this sibling calibration header;
// we alias it rather than mint a second constant so the two surfaces cannot drift.
#include "ParamDefsConstants.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// §11 — INIT-patch (out-of-box, ADR-016) overlay magnitudes that have no measured
// oracle and are flagged (PI) in the §11 table. The DIRECTION/surface of each is
// owner-locked by ADR-016; only the magnitude lives here.
//
// Note the velocity-depth INIT overlay reuses the existing param-default value
// (mw::cal::paramdefault::kVelDepth, "low-mid") rather than minting a second
// constant — §11 sets `vel.depth` low-mid, which is the same low-mid pole the §3.3
// param default already encodes; the INIT patch still EXPLICITLY carries it because
// `vel.enable` flips to true (a patch action), and a single source for that value
// keeps the two surfaces from drifting apart [docs/design/06 §11; §3.3].
// ---------------------------------------------------------------------------
namespace initpatch {
    // mw101.vintage.age INIT overlay — "low" subtle-age pole. The PARAMETER default
    // stays 0 (in tune on load, §3.3); the INIT PATCH moves age LOW and enables
    // drift — a patch choice, not a parameter-default change [docs/design/06 §11;
    // ADR-016 R-4]. (PI) tunable magnitude.
    inline constexpr float kVintageAgeLow = 0.15f;  // (PI) — subtle, low age

    // mw101.tune.a4 INIT reference — A4 = 440 Hz out of the box; the "hardware-
    // accurate" 442 Hz pole is a labelled factory preset, not the INIT default
    // [docs/design/06 §11; ADR-012 C21-C22]. Not strictly (PI) (it is the contract
    // value), centralized here so the patch builder reads it by name like the rest.
    inline constexpr float kTuneA4Hz = 440.0f;  // contract value, §11

    // mw101.vel.depth INIT overlay — the §11 "low-mid" velocity pole. Aliased to the
    // single param-default source so the two never drift [docs/design/06 §11; §3.3].
    inline constexpr float kVelDepthLowMid = paramdefault::kVelDepth;  // (PI) low-mid
}

} // namespace mw::cal
