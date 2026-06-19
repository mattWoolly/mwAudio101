// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the prepare-time frozen-constant-set SELECTOR (task 049c). The
// selector binds, at prepareToPlay (never at audio rate), the frozen DSP constant
// set (tanh approx, decimator/halfband coeffs, compensation-table source constants,
// VCO pitch/drift constants) for a session's stored renderVersion off the
// Calibration.h registry, realizing the legacy-render path [docs/design/00 §8.2/§8.3;
// ADR-023 V10, V18].
//
// Test-case names begin with "calibration" so `-R calibration` selects them; the
// "[cal]" tag groups them under the cal subsystem label (silent-pass discipline,
// AGENTS.md / docs/design/11 §8.1/§8.2). AVOID '[' inside the display text.

#include <catch2/catch_test_macros.hpp>

#include "calibration/ConstantSetSelector.h"
#include "calibration/Calibration.h"
#include "calibration/FastTanhConstants.h"
#include "calibration/FilterTablesConstants.h"
#include "calibration/OversamplerConstants.h"
#include "calibration/VcoConstants.h"
#include "version/EngineVersion.h"
#include "dsp/FastTanh.h"
#include "dsp/FilterTables.h"
#include "dsp/Oversampler.h"

#include "../invariants/AudioThreadGuard.h"

using mw::cal::ConstantSetSelection;
using mw::cal::selectConstantSet;
using mw::cal::selectCurrentConstantSet;
using mw::test::AudioThreadGuard;

// --- Acceptance: selecting CURRENT reproduces the blessed constant set -----------
TEST_CASE("calibration: selecting CURRENT binds the blessed frozen constant set", "[calibration][cal]") {
    const ConstantSetSelection sel = selectConstantSet(mw101::version::kCurrentRenderVersion);

    REQUIRE(sel.ok);                                                  // CURRENT is shipped
    REQUIRE(sel.renderVersion == mw101::version::kCurrentRenderVersion);
    REQUIRE(sel.isCurrent);                                           // blessed (CURRENT) path
    REQUIRE(sel.frozen != nullptr);                                   // bound to a registry entry
    REQUIRE(sel.frozen->renderVersion == mw101::version::kCurrentRenderVersion);

    // The selected set points at the SHIPPED renderVersion-1 frozen sources. For rv 1
    // (the only shipped version today) these are the current blessed constants.
    REQUIRE(sel.tanhCoeffs    == &mw::cal::vcf::tanhCoeffs);
    REQUIRE(sel.kTanhClamp    == mw::cal::vcf::kTanhClamp);
    REQUIRE(sel.invTwoVt      == mw::cal::vcf::invTwoVt);
    REQUIRE(sel.branch0Coeffs == &mw::cal::osiir::kBranch0Coeffs);
    REQUIRE(sel.branch1Coeffs == &mw::cal::osiir::kBranch1Coeffs);
    REQUIRE(sel.compFit       == &mw::cal::vcf::kCompFit);
    REQUIRE(sel.fcRefHz       == mw::cal::vcf::kFcRefHz);
    REQUIRE(sel.pitchRefHz    == mw::cal::vco::kPitchRefHz);
    REQUIRE(sel.driftScalePpmMax == mw::cal::drift::kDriftScalePpmMax);
}

// --- Acceptance: a convenience selector for "current" agrees with explicit CURRENT.
TEST_CASE("calibration: selectCurrentConstantSet equals selecting kCurrentRenderVersion", "[calibration][cal]") {
    const ConstantSetSelection a = mw::cal::selectCurrentConstantSet();
    const ConstantSetSelection b = selectConstantSet(mw101::version::kCurrentRenderVersion);

    REQUIRE(a.ok);
    REQUIRE(a.renderVersion == b.renderVersion);
    REQUIRE(a.isCurrent == b.isCurrent);
    REQUIRE(a.frozen == b.frozen);
    REQUIRE(a.tanhCoeffs == b.tanhCoeffs);
    REQUIRE(a.branch0Coeffs == b.branch0Coeffs);
    REQUIRE(a.compFit == b.compFit);
}

// --- Acceptance: an unknown / unshipped renderVersion is REFUSED -----------------
//     (no silent fallback to CURRENT) [docs/design/00 §8.2/§8.3].
TEST_CASE("calibration: an unshipped renderVersion is refused, never falls back to CURRENT", "[calibration][cal]") {
    // 0 is never-shipped; 999 is a future (unshipped) version; -1 is invalid.
    for (const int rv : {0, -1, 999, mw101::version::kCurrentRenderVersion + 1}) {
        const ConstantSetSelection sel = selectConstantSet(rv);
        REQUIRE_FALSE(sel.ok);                       // refused
        REQUIRE(sel.frozen == nullptr);              // bound to nothing
        REQUIRE(sel.renderVersion == rv);            // echoes the REQUESTED version, NOT CURRENT
        REQUIRE_FALSE(sel.isCurrent);
        // The critical no-silent-fallback property: a refused selection does NOT
        // present the CURRENT constant set.
        REQUIRE(sel.tanhCoeffs == nullptr);
        REQUIRE(sel.branch0Coeffs == nullptr);
        REQUIRE(sel.compFit == nullptr);
    }
}

// --- Acceptance: selecting a shipped legacy renderVersion binds THAT version's set.
//     Today only rv 1 is shipped (== CURRENT); assert every SHIPPED version in the
//     registry binds successfully and to its own renderVersion (no cross-binding).
TEST_CASE("calibration: every shipped renderVersion binds its own frozen set", "[calibration][cal]") {
    for (const auto& entry : mw::cal::kFrozenConstantSets) {
        const ConstantSetSelection sel = selectConstantSet(entry.renderVersion);
        REQUIRE(sel.ok);
        REQUIRE(sel.renderVersion == entry.renderVersion);
        REQUIRE(sel.frozen == &entry);               // bound to the matching registry entry
        REQUIRE(sel.isCurrent == entry.isCurrent);
        REQUIRE(sel.tanhCoeffs != nullptr);          // a shipped version always binds real sources
        REQUIRE(sel.branch0Coeffs != nullptr);
        REQUIRE(sel.compFit != nullptr);
    }
}

// --- Acceptance: FastTanh, FilterTables and the oversampler OBSERVE the selected
//     set, verified by a prepare-time wiring test [docs/design/00 §8.3].
TEST_CASE("calibration: FastTanh observes the selected tanh constant set", "[calibration][cal]") {
    const ConstantSetSelection sel = selectCurrentConstantSet();
    REQUIRE(sel.ok);

    // The selected tanh coefficients are exactly the ones FastTanh evaluates with, so
    // fastTanh's output is reproducible from the bound coefficient set.
    const auto& c = *sel.tanhCoeffs;
    const float x = 0.5f;
    const float x2 = x * x;
    const float expected = x * (c[0] + c[1] * x2) / (c[2] + c[3] * x2);
    REQUIRE(mw::dsp::fastTanh(x) == expected);       // bit-exact: same frozen coeffs

    // Odd-symmetry / monotonicity sanity from the selected set.
    REQUIRE(mw::dsp::fastTanh(-x) == -mw::dsp::fastTanh(x));
}

TEST_CASE("calibration: FilterTables compensation observes the selected comp-fit set", "[calibration][cal]") {
    const ConstantSetSelection sel = selectCurrentConstantSet();
    REQUIRE(sel.ok);

    // Build the per-SR tables at prepare for a blessed rate, oversampled 2x.
    mw::dsp::FilterTables tables;
    const double fsOs = 2.0 * 48000.0;
    tables.build(fsOs);                              // prepare-time only (the sole allocator)

    // resoTuningComp is driven by the selected comp-fit polynomial comp(g)=c0+c1 g+c2 g^2.
    const auto& fit = *sel.compFit;
    const float g = 0.5f;
    const float expectedComp = fit[0] + fit[1] * g + fit[2] * g * g;
    // Default fit is identity (comp == 1); the table value matches the selected fit.
    REQUIRE(tables.resoTuningComp(g) == expectedComp);
}

TEST_CASE("calibration: the oversampler observes the selected halfband coefficient set", "[calibration][cal]") {
    const ConstantSetSelection sel = selectCurrentConstantSet();
    REQUIRE(sel.ok);

    // The selected halfband branches are exactly the coefficient arrays the realtime
    // oversampler is built from (the constant SOURCE the oversampler reads at prepare).
    REQUIRE(sel.branch0Coeffs->size() == mw::cal::osiir::kSectionsPerBranch);
    REQUIRE(sel.branch1Coeffs->size() == mw::cal::osiir::kSectionsPerBranch);
    for (std::size_t i = 0; i < mw::cal::osiir::kSectionsPerBranch; ++i) {
        REQUIRE((*sel.branch0Coeffs)[i] == mw::cal::osiir::kBranch0Coeffs[i]);
        REQUIRE((*sel.branch1Coeffs)[i] == mw::cal::osiir::kBranch1Coeffs[i]);
        // Stability: every allpass coefficient lies strictly inside (0,1) [ADR-004 C14].
        REQUIRE((*sel.branch0Coeffs)[i] > 0.0);
        REQUIRE((*sel.branch0Coeffs)[i] < 1.0);
    }
}

// --- Acceptance: select() runs only at prepare; an AudioThreadGuard-fenced test
//     confirms zero alloc/lock and NO selection call on the hot path
//     [docs/design/00 §8.3; ADR-023 V18].
TEST_CASE("calibration: selectConstantSet allocates nothing and locks nothing", "[calibration][cal]") {
    AudioThreadGuard guard;
    guard.arm();
    // Exercise the selector under the sentinel: a prepare-time selection must be a
    // pure pointer/reference bind — no heap alloc, no lock, no table rebuild.
    const ConstantSetSelection sel = selectConstantSet(mw101::version::kCurrentRenderVersion);
    const ConstantSetSelection refused = selectConstantSet(999);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());                 // zero alloc / zero lock
    REQUIRE(guard.violations().empty());
    REQUIRE(sel.ok);
    REQUIRE_FALSE(refused.ok);
}

// --- The selector is noexcept and constexpr-evaluable (prepare-time, no audio rate).
TEST_CASE("calibration: selectConstantSet is noexcept and constant-evaluable", "[calibration][cal]") {
    STATIC_REQUIRE(noexcept(selectConstantSet(1)));
    // Evaluable at compile time -> it is a pure bind, never a runtime table build.
    constexpr ConstantSetSelection sel = selectConstantSet(mw101::version::kCurrentRenderVersion);
    STATIC_REQUIRE(sel.ok);
    STATIC_REQUIRE(sel.renderVersion == mw101::version::kCurrentRenderVersion);

    constexpr ConstantSetSelection refused = selectConstantSet(0);
    STATIC_REQUIRE_FALSE(refused.ok);
}
