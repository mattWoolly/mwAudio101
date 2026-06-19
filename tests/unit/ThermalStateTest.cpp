// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/ThermalStateTest.cpp — Layer-1 unit tests for the shared scalar
// thermal drift integrator core/dsp/drift/ThermalState (task 064).
//
// Test-case names begin with "vintage_thermal" so `ctest -R vintage_thermal`
// selects them (silent-pass discipline, AGENTS.md). Covers every Acceptance
// criterion of plan/backlog/064:
//   - T bounded within +/-kDriftClampCents over arbitrarily long runs, no runaway
//     (docs/design/08 §5.1, §12.6)
//   - no denormals / NaN after long silence, with an FTZ/DAZ guard (§12.6)
//   - warm-up OFF by default; when on, decays toward zero over warmupTimeMin
//     (§5.3, ADR-009 VV-5)
//   - tick advances state exactly once per call (block rate), deterministic for a
//     fixed seed (§5.4, VV-14)
//
// All figures here are reproducibility/statistical anchors, NOT measured specs.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "dsp/drift/ThermalState.h"
#include "calibration/ThermalConstants.h"

using mw::dsp::drift::ThermalState;
using mw::dsp::drift::Xorshift128p;
namespace cd = mw::cal::drift;

namespace {

// Whether a float is a finite, non-denormal (or exactly zero) value.
bool isCleanFloat(float v) {
    if (!std::isfinite(v)) return false;
    return v == 0.0f || std::fabs(v) >= std::numeric_limits<float>::min();
}

} // namespace

// --- POD discipline: lives by value in DriftState[kMaxVoices] (§8.1, §12.1) ----

TEST_CASE("vintage_thermal ThermalState is a trivially-copyable standard-layout POD",
          "[vintage_thermal]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ThermalState>);
    STATIC_REQUIRE(std::is_standard_layout_v<ThermalState>);
    STATIC_REQUIRE(std::is_nothrow_default_constructible_v<ThermalState>);
    // The §5.4 pink-row array width matches the centralized (PI) constant.
    STATIC_REQUIRE(sizeof(ThermalState{}.pinkState) / sizeof(float) == cd::kPinkRows);
}

// --- Bounded drift: no runaway over an arbitrarily long run (§5.1, §12.6) ------

TEST_CASE("vintage_thermal T stays within the clamp over a very long run - no runaway",
          "[vintage_thermal]") {
    ThermalState th;
    th.reset(/*cold=*/false);
    Xorshift128p rng(0xC0FFEE0DDF00D123ULL);

    // Long run at a fast drift rate (worst case for excursion). dtBlock ~ 32 frames
    // at 48 kHz; ~2 million blocks ~= many hours of audio.
    const double dtBlock = 32.0 / 48000.0;
    float maxAbs = 0.0f;
    for (int i = 0; i < 2'000'000; ++i) {
        th.tick(rng, /*rate01=*/1.0f, dtBlock,
                /*usePink=*/false, /*useWarmup=*/false, /*warmupTimeMin=*/0.0f);
        REQUIRE(std::isfinite(th.value()));
        maxAbs = std::max(maxAbs, std::fabs(th.value()));
        REQUIRE(std::fabs(th.value()) <= cd::kDriftClampCents);
    }
    // The clamp is a hard bound; confirm the process actually moves (not stuck at 0).
    REQUIRE(maxAbs > 0.0f);
}

TEST_CASE("vintage_thermal T stays bounded even with pink and a slow rate",
          "[vintage_thermal]") {
    ThermalState th;
    th.reset(false);
    Xorshift128p rng(0x1234ABCD5678EF90ULL);

    const double dtBlock = 64.0 / 44100.0;
    for (int i = 0; i < 500'000; ++i) {
        th.tick(rng, /*rate01=*/0.0f /*slowest k*/, dtBlock,
                /*usePink=*/true, /*useWarmup=*/false, 0.0f);
        REQUIRE(std::fabs(th.value()) <= cd::kDriftClampCents);
        REQUIRE(std::isfinite(th.value()));
    }
}

// --- No denormals / NaN after long silence, FTZ/DAZ guard (§12.6) --------------

TEST_CASE("vintage_thermal no denormals or NaN after long silence with the denormal flush",
          "[vintage_thermal]") {
    ThermalState th;
    th.reset(false);
    Xorshift128p rng(0xDEADBEEF00C0FFEEULL);

    // Kick the state, then run with zero diffusion drive by feeding dt that decays the
    // OU state toward zero (rate high, no fresh excitation is impossible since gaussian
    // always draws — but the flush must catch any subnormal the decay produces).
    const double dtBlock = 16.0 / 96000.0;
    for (int i = 0; i < 1'000'000; ++i) {
        th.tick(rng, 0.5f, dtBlock, /*usePink=*/true, /*useWarmup=*/false, 0.0f);
    }
    // Every stored float must be finite and either exactly zero or normal (>= FLT_MIN).
    REQUIRE(isCleanFloat(th.T));
    REQUIRE(isCleanFloat(th.value()));
    REQUIRE(isCleanFloat(th.ouState));
    for (float row : th.pinkState) REQUIRE(isCleanFloat(row));

    // Drive the integrator to a sub-floor magnitude and step with dt==0 so the OU
    // increment is identically zero (no fresh diffusion); the explicit flush must then
    // zero the residual subnormal, independent of hardware FTZ/DAZ.
    th.ouState = cd::kDenormalFloor * 0.5f;   // sub-floor magnitude
    th.T       = cd::kDenormalFloor * 0.5f;
    th.tick(rng, 1.0f, /*dtBlock=*/0.0, /*usePink=*/false, /*useWarmup=*/false, 0.0f);
    REQUIRE(th.ouState == 0.0f);              // flushed to exactly zero, no subnormal
    REQUIRE(isCleanFloat(th.T));
}

// --- Warm-up OFF by default; ON decays toward zero over warmupTimeMin (§5.3, VV-5)

TEST_CASE("vintage_thermal warm-up is off by default - no offset added when useWarmup is false",
          "[vintage_thermal]") {
    // Two states fed identical streams; one ticks with warm-up flag false. A fresh
    // (non-cold) reset has warmupSec < 0 (disabled), and useWarmup=false keeps it so.
    ThermalState th;
    th.reset(false);
    REQUIRE(th.warmupSec < 0.0);          // disabled marker after a non-cold reset

    Xorshift128p rng(0xABCDEF0123456789ULL);
    const double dtBlock = 32.0 / 48000.0;

    // With warm-up off, the very first ticks start from ~0 (pure OU around the mean),
    // never near the kWarmupCents cold offset.
    th.tick(rng, 0.1f, dtBlock, /*usePink=*/false, /*useWarmup=*/false, 30.0f);
    REQUIRE(th.warmupSec < 0.0);          // still disabled
    REQUIRE(std::fabs(th.value()) < cd::kWarmupCents); // no big cold offset present
}

TEST_CASE("vintage_thermal cold reset arms the warm-up offset at full magnitude",
          "[vintage_thermal]") {
    ThermalState th;
    th.reset(/*cold=*/true);
    REQUIRE(th.warmupSec == Catch::Approx(0.0));
    // Cold start: T begins at the full warm-up offset (OU == 0).
    REQUIRE(th.value() == Catch::Approx(cd::kWarmupCents));
}

TEST_CASE("vintage_thermal warm-up offset decays toward zero over warmupTimeMin",
          "[vintage_thermal]") {
    // Disable OU diffusion contribution to isolate the warm-up curve: use a frozen
    // (zero-variance) probe by comparing the warm-only component across two states —
    // one with warm-up on, one off — fed the SAME rng so the OU parts are identical.
    Xorshift128p rngOn(0x55AA55AA55AA55AAULL);
    Xorshift128p rngOff(0x55AA55AA55AA55AAULL);

    ThermalState on;  on.reset(/*cold=*/true);   // warm-up armed, starts at full offset
    ThermalState off; off.reset(/*cold=*/false); // warm-up disabled

    const double dtBlock = 1.0;          // 1 second per block to march warm time fast
    const float  warmMin = 1.0f;         // 1-minute warm-up window

    // Sample the warm-up *offset* = (on - off) at three milestones along the curve.
    auto runTo = [&](double targetSec) {
        // Advance both states in lockstep until `on` has accumulated >= targetSec.
        while (on.warmupSec < targetSec) {
            on.tick (rngOn,  0.1f, dtBlock, false, /*useWarmup=*/true,  warmMin);
            off.tick(rngOff, 0.1f, dtBlock, false, /*useWarmup=*/false, warmMin);
        }
        return static_cast<double>(on.value()) - static_cast<double>(off.value());
    };

    const double offsetEarly = runTo(1.0);    // ~1 s in
    const double offsetMid   = runTo(20.0);   // ~20 s in
    const double offsetLate  = runTo(60.0);   // a full minute in (the set time)

    // Monotonic decay toward zero.
    REQUIRE(offsetEarly > offsetMid);
    REQUIRE(offsetMid   > offsetLate);
    REQUIRE(offsetLate  > 0.0);
    REQUIRE(offsetLate  < offsetEarly);

    // At the user-set warm time the offset has fallen to ~kWarmupSettleFrac of the
    // cold magnitude (the curve is designed to "land warm" at warmupTimeMin, §5.3).
    const double expectedLate = cd::kWarmupCents * cd::kWarmupSettleFrac;
    REQUIRE(offsetLate == Catch::Approx(expectedLate).margin(cd::kWarmupCents * 0.05));

    // Run far past the set time: the offset has decayed essentially to zero (it is
    // smaller than the late offset and within rounding of zero — never grows).
    const double offsetFar = runTo(600.0);    // 10 minutes
    REQUIRE(offsetFar < offsetLate);
    REQUIRE(offsetFar >= 0.0);
    REQUIRE(offsetFar < expectedLate);        // decayed below the at-time residual
}

// --- Determinism: same seed + same call sequence => bit-identical (§5.4, VV-17) -

TEST_CASE("vintage_thermal tick is deterministic for a fixed seed",
          "[vintage_thermal]") {
    auto run = [](std::uint64_t seed) {
        ThermalState th;
        th.reset(false);
        Xorshift128p rng(seed);
        std::vector<float> trace;
        trace.reserve(4096);
        const double dtBlock = 32.0 / 48000.0;
        for (int i = 0; i < 4096; ++i) {
            th.tick(rng, 0.3f, dtBlock, /*usePink=*/true, /*useWarmup=*/false, 0.0f);
            trace.push_back(th.value());
        }
        return trace;
    };

    const auto a = run(0x0123456789ABCDEFULL);
    const auto b = run(0x0123456789ABCDEFULL);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i] == b[i]);            // bit-identical, not merely approx
    }

    // Negative control: a different seed yields a different trajectory.
    const auto c = run(0xFEDCBA9876543210ULL);
    bool differs = false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != c[i]) { differs = true; break; }
    REQUIRE(differs);
}

// --- Block-rate cadence: one OU step per call, exactly (§5.4, §12.2, VV-14) -----

TEST_CASE("vintage_thermal tick consumes exactly one Gaussian draw per call - block rate",
          "[vintage_thermal]") {
    // The OU update draws one N(0,1) per tick. A reference PRNG advanced by exactly N
    // gaussian() calls must end in the SAME state as the PRNG the thermal state used
    // after N ticks (with pink/warm-up OFF so no extra draws occur).
    Xorshift128p used(0xA1B2C3D4E5F60718ULL);
    Xorshift128p ref (0xA1B2C3D4E5F60718ULL);

    ThermalState th;
    th.reset(false);
    const double dtBlock = 32.0 / 48000.0;
    constexpr int kBlocks = 1000;
    for (int i = 0; i < kBlocks; ++i) {
        th.tick(used, 0.4f, dtBlock, /*usePink=*/false, /*useWarmup=*/false, 0.0f);
        (void) ref.gaussian();            // exactly one draw mirrors one OU step
    }
    // Identical PRNG state proves tick consumed exactly one draw per call (no
    // per-sample noise generation, no double-stepping) [VV-14].
    REQUIRE(used.s0 == ref.s0);
    REQUIRE(used.s1 == ref.s1);
}

TEST_CASE("vintage_thermal each tick advances elapsed warm time by exactly dtBlock",
          "[vintage_thermal]") {
    ThermalState th;
    th.reset(/*cold=*/true);              // warmupSec == 0, armed
    Xorshift128p rng(0x99887766554433ULL);
    const double dtBlock = 0.01;
    for (int i = 1; i <= 50; ++i) {
        th.tick(rng, 0.2f, dtBlock, false, /*useWarmup=*/true, 5.0f);
        REQUIRE(th.warmupSec == Catch::Approx(static_cast<double>(i) * dtBlock));
    }
}

// --- One shared T drives both VCO and VCF (VV-13, §5.2): single scalar source ---

TEST_CASE("vintage_thermal value is a single shared scalar - read repeatedly returns the same T",
          "[vintage_thermal]") {
    ThermalState th;
    th.reset(false);
    Xorshift128p rng(0x0FEDCBA987654321ULL);
    const double dtBlock = 32.0 / 48000.0;
    for (int i = 0; i < 100; ++i) {
        th.tick(rng, 0.5f, dtBlock, false, false, 0.0f);
        // VCO and VCF both read value(); it is ONE state, never two independent walks.
        const float a = th.value();
        const float b = th.value();
        REQUIRE(a == b);
        REQUIRE(a == th.T);
    }
}
