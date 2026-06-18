// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the Envelope/LFO/VCA (PI) calibration constants block
// (task 049). Test-case NAMES begin with "envlfovca_calib" so `-R envlfovca_calib`
// selects them (catch_discover_tests registers test-case names, not tags). The
// [cal] tag keeps these within the existing calibration label snapshot so no shared
// snapshot/aggregate file needs editing.
//
// These are (PI) TUNABLE DEFAULTS — no measured-spec assertion. The values trace to
// docs/design/03 §2.4/§3.5/§4.3/§5.2 default tables and ADR-020 S13 (centralized,
// never inlined at a DSP call site).

#include <catch2/catch_test_macros.hpp>

#include "calibration/EnvLfoVcaConstants.h"

// --- §2.4 Envelope shaping constants -----------------------------------------
TEST_CASE("envlfovca_calib_values: envelope shaping (PI) defaults match design 03 sec 2.4",
          "[cal]") {
    using namespace mw::cal::env;
    REQUIRE(kEnvAttackOvershoot == 1.25f);   // attack asymptote above unity
    REQUIRE(kEnvTimeScale       == 0.20f);   // user-time -> 1/e constant map
    REQUIRE(kEnvCurve           == 1.0f);    // default shaping exponent (near-RC)
    REQUIRE(kEnvSnapThreshold   == 1.0e-4f); // stage snap/advance distance
    REQUIRE(kEnvTimeMin         == 1.0e-4f); // floor on any segment time (seconds)
}

// --- §3.5 LFO shape/rate/mod-bus constants -----------------------------------
TEST_CASE("envlfovca_calib_values: LFO shape/rate/mod-bus (PI) defaults match design 03 sec 3.5",
          "[cal]") {
    using namespace mw::cal::lfo;
    REQUIRE(kLfoSmoothShape == 0.85f);  // triangle->sine rounding blend
    REQUIRE(kLfoRateSkew    == 0.3f);   // rate pot taper (if not encoded in doc 06)
    REQUIRE(kModBusLpHz     == 16000.0f); // fixed modulation-bus low-pass corner (Hz)
}

// --- §4.3 VCA taper / anti-thump constants -----------------------------------
TEST_CASE("envlfovca_calib_values: VCA taper/anti-thump (PI) defaults match design 03 sec 4.3",
          "[cal]") {
    using namespace mw::cal::vca;
    REQUIRE(kVcaTaperExp   == 2.0f);  // control->gain curve exponent
    REQUIRE(kVcaOtaDrive   == 1.0f);  // OTA tanh drive
    REQUIRE(kVcaAntiThumpMs == 2.0f); // gate open/close fade time (ms)
    REQUIRE(kVcaOffsetNull == 0.0f);  // residual DC nulled at gate transition
}

// --- §5.2 Velocity-routing constants -----------------------------------------
TEST_CASE("envlfovca_calib_values: velocity-routing (PI) defaults match design 03 sec 5.2",
          "[cal]") {
    using namespace mw::cal::vel;
    REQUIRE(kVelToVca    == 0.7f);  // default VelocityRouting::toVcaAmount
    REQUIRE(kVelToCutoff == 0.5f);  // default VelocityRouting::toCutoffAmount
    REQUIRE(kVelCurve    == 1.0f);  // velocity input curve shaping
}

// --- Whole-block presence/count guard ----------------------------------------
// Asserts all 15 named constants resolve from the header (compile-time use) and are
// finite (negative control against a NaN/inf typo). This is the "15 named constants
// exist" acceptance check made objective; it does not assert any measured spec.
TEST_CASE("envlfovca_calib_values: all 15 named constants resolve and are finite",
          "[cal]") {
    const float all[] = {
        mw::cal::env::kEnvAttackOvershoot, mw::cal::env::kEnvTimeScale,
        mw::cal::env::kEnvCurve,           mw::cal::env::kEnvSnapThreshold,
        mw::cal::env::kEnvTimeMin,
        mw::cal::lfo::kLfoSmoothShape,     mw::cal::lfo::kLfoRateSkew,
        mw::cal::lfo::kModBusLpHz,
        mw::cal::vca::kVcaTaperExp,        mw::cal::vca::kVcaOtaDrive,
        mw::cal::vca::kVcaAntiThumpMs,     mw::cal::vca::kVcaOffsetNull,
        mw::cal::vel::kVelToVca,           mw::cal::vel::kVelToCutoff,
        mw::cal::vel::kVelCurve,
    };
    constexpr int kExpectedCount = 15;
    REQUIRE(static_cast<int>(sizeof(all) / sizeof(all[0])) == kExpectedCount);

    int finiteCount = 0;
    for (float v : all) {
        // v == v rejects NaN; the |v| bound rejects inf without including <cmath>.
        REQUIRE(v == v);
        REQUIRE(v < 1.0e30f);
        REQUIRE(v > -1.0e30f);
        ++finiteCount;
    }
    REQUIRE(finiteCount == kExpectedCount);
}
