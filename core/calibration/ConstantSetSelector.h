// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ConstantSetSelector.h — the prepare-time FROZEN constant-set
// SELECTOR keyed by renderVersion (task 049c, the legacy-render path).
//
// At prepareToPlay (NEVER at audio rate), select the frozen DSP constant set — the
// tanh approximation, the decimator/halfband coefficients, the compensation-table
// SOURCE constants, and the VCO pitch/drift constants — for the session's stored
// renderVersion, chosen off the core/calibration/Calibration.h frozen-constant-set
// registry. This realizes the legacy-render path: a pinned old renderVersion
// reproduces that version's audio by binding that version's frozen set
// [docs/design/00 §8.2/§8.3; ADR-023 V10, V18].
//
// Per the parallel-fleet conflict-avoidance rule this is a NEW per-module header: it
// #includes the shared Calibration.h (the registry) and the per-module frozen-constant
// headers (FastTanh 033, FilterTables 035, Oversampler 036, VCO 029) and binds them
// into one view; it does NOT edit Calibration.h. The orchestrator wires any
// cross-module aggregation later.
//
// Discipline this header enforces (the §8.2/§8.3 contract made mechanical):
//   * Selecting CURRENT yields the blessed constant set [ADR-023 V10].
//   * Selecting a SHIPPED legacy renderVersion binds that version's frozen set; today
//     only renderVersion 1 is shipped (== CURRENT) so it binds the rv-1 sources. The
//     selector is the single extension point: a future shipped rv adds its frozen
//     source bundle to bundleFor() and the registry retains its entry.
//   * An unknown / unshipped renderVersion is REFUSED (ok == false, all source
//     pointers null) — NEVER a silent fallback to CURRENT [docs/design/00 §8.2/§8.3].
//   * Selection is a pure compile-time-evaluable pointer/reference bind: no
//     transcendental, no table build, no heap alloc, no lock — RT-safe by
//     construction [docs/design/00 §9.1 RT-6; ADR-023 V18; ADR-003 §F-11].
//
// mwcore is JUCE-free.

#pragma once

#include <array>
#include <cstddef>

#include "Calibration.h"               // the per-renderVersion FrozenConstantSet registry (005b)
#include "FastTanhConstants.h"         // tanh approx coeffs feeding FastTanh (033)
#include "FilterTablesConstants.h"     // compensation-table SOURCE constants feeding FilterTables (035)
#include "OversamplerConstants.h"      // decimator/halfband coeffs feeding the oversampler (036)
#include "VcoConstants.h"              // VCO pitch/drift constants (029)
#include "../version/EngineVersion.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// ConstantSetSelection — the prepare-time VIEW of one renderVersion's frozen DSP
// constant set. It carries NO owning storage: every member is a pointer/value into
// the frozen, immutable constant headers, bound once at prepare and frozen for the
// block-processing lifetime [docs/design/00 §8.3; ADR-023 V10, V18].
//
// On a REFUSED selection (unknown/unshipped renderVersion) `ok` is false and every
// source pointer is null — there is no silent fallback to CURRENT [§8.2/§8.3].
// ---------------------------------------------------------------------------
struct ConstantSetSelection {
    // True iff the requested renderVersion is a SHIPPED, retained version. A refused
    // selection leaves this false and binds nothing.
    bool ok = false;

    // The renderVersion that was REQUESTED. On a refusal this echoes the request
    // (NOT CURRENT) so a caller cannot mistake a refusal for a CURRENT bind.
    int renderVersion = 0;

    // True iff the bound version is CURRENT (the blessed path) [ADR-023 V10].
    bool isCurrent = false;

    // The matching registry entry (the §8.3 frozen-constant-set), or nullptr on refusal.
    const FrozenConstantSet* frozen = nullptr;

    // --- tanh approximation (FastTanh / 033) -------------------------------------
    const std::array<float, 4>* tanhCoeffs = nullptr;   // {num0,num1,den0,den1}
    float kTanhClamp = 0.0f;                            // validity-range clamp
    float invTwoVt   = 0.0f;                            // OTA knee scaler

    // --- decimator / halfband coefficients (Oversampler / 036) -------------------
    const std::array<double, mw::cal::osiir::kSectionsPerBranch>* branch0Coeffs = nullptr;
    const std::array<double, mw::cal::osiir::kSectionsPerBranch>* branch1Coeffs = nullptr;

    // --- compensation-table SOURCE constants (FilterTables / 035) ----------------
    const std::array<float, 3>* compFit = nullptr;      // comp(g)=c0+c1 g+c2 g^2
    float fcRefHz = 0.0f;                                // cutoff at CV=0 V

    // --- VCO pitch / drift constants (029) ---------------------------------------
    double pitchRefHz       = 0.0;
    double driftScalePpmMax = 0.0;
};

namespace detail {

// Bind the frozen DSP source bundle for ONE shipped renderVersion. This is the single
// extension point: when a future audio-altering bless ships renderVersion N, its
// version-tagged frozen headers are bound here under `case N`. Today only
// renderVersion 1 is shipped, and its frozen sources are the current blessed headers
// (FastTanhConstants / FilterTablesConstants / OversamplerConstants / VcoConstants)
// [docs/design/00 §8.3; ADR-023 V10].
//
// The caller has ALREADY verified the renderVersion is in the registry (so this only
// ever runs for a shipped version); a request that reaches the default arm would be a
// registry/selector desync and is treated as a refusal (ok stays false).
constexpr void bindBundleFor(int renderVersion, ConstantSetSelection& s) noexcept {
    switch (renderVersion) {
        case 1:
            // renderVersion 1 == CURRENT: the blessed frozen constant set.
            s.tanhCoeffs       = &mw::cal::vcf::tanhCoeffs;
            s.kTanhClamp       = mw::cal::vcf::kTanhClamp;
            s.invTwoVt         = mw::cal::vcf::invTwoVt;
            s.branch0Coeffs    = &mw::cal::osiir::kBranch0Coeffs;
            s.branch1Coeffs    = &mw::cal::osiir::kBranch1Coeffs;
            s.compFit          = &mw::cal::vcf::kCompFit;
            s.fcRefHz          = mw::cal::vcf::kFcRefHz;
            s.pitchRefHz       = mw::cal::vco::kPitchRefHz;
            s.driftScalePpmMax = mw::cal::drift::kDriftScalePpmMax;
            s.ok               = true;
            return;
        default:
            // No frozen bundle wired for this version: refuse (no silent fallback).
            return;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// selectConstantSet — choose the frozen constant set for a stored renderVersion.
//
// Called ONLY from prepare/prepareToPlay (the constexpr/noexcept signature documents
// that it is a pure bind; it is never invoked on the audio thread) [§8.3; ADR-023
// V18]. Steps:
//   1. Look the renderVersion up in the §8.3 registry (Calibration.h). A miss
//      (unknown / unshipped / future / invalid) is REFUSED with no fallback [§8.2].
//   2. For a shipped version, bind that version's frozen DSP source bundle.
// The returned view is a set of pointers/values into immutable constant headers — no
// allocation, no lock, no table rebuild [§9.1 RT-6].
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr ConstantSetSelection selectConstantSet(int renderVersion) noexcept {
    ConstantSetSelection s{};
    s.renderVersion = renderVersion;   // echo the REQUEST; never silently rewritten to CURRENT

    // (1) Registry lookup — only SHIPPED, retained renderVersions are present. A miss
    // is a refusal: a pinned version this build cannot reproduce must NOT silently
    // render as CURRENT [docs/design/00 §8.2/§8.3; ADR-023 V10, §Consequences].
    const FrozenConstantSet* entry = frozenSetFor(renderVersion);
    if (entry == nullptr) {
        return s;   // ok == false, all source pointers null
    }
    s.frozen    = entry;
    s.isCurrent = entry->isCurrent;

    // (2) Bind the frozen DSP source bundle for this shipped version. bindBundleFor
    // sets ok = true on success; a registry/selector desync (no bundle wired) leaves
    // ok false — still a refusal, never a CURRENT fallback.
    detail::bindBundleFor(renderVersion, s);
    return s;
}

// Convenience: select the CURRENT (blessed) constant set. New/blank sessions and new
// factory presets render at CURRENT [ADR-023 V9].
[[nodiscard]] constexpr ConstantSetSelection selectCurrentConstantSet() noexcept {
    return selectConstantSet(mw101::version::kCurrentRenderVersion);
}

} // namespace mw::cal
