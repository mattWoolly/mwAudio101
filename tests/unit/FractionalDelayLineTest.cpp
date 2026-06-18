// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for FractionalDelayLine (task 089). Test-case names begin
// with "fracdelay" so `ctest -R fracdelay` selects them (silent-pass rule,
// AGENTS.md). Covers each acceptance criterion in plan/backlog/089:
//   - integer-delay read returns input delayed by exactly D (07-fx-section §5.3)
//   - fractional read interpolates monotonically between bracketing taps (§5.3)
//   - after prepare(), write/read/processBlock perform no heap allocation
//     (alloc-tracking via AudioThreadGuard) [ADR-010 FX-10; §3.2]

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/fx/FractionalDelayLine.h"
#include "calibration/FractionalDelayLineConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::fx::FractionalDelayLine;

TEST_CASE("fracdelay: integer-delay read returns the input delayed by exactly D", "[fracdelay]") {
    // §5.3: an impulse written in comes back out at exactly index D for an integer
    // delay; samples before D read 0.
    constexpr int kMax = 64;
    constexpr int kD   = 7;

    FractionalDelayLine line;
    line.prepare(kMax);
    line.reset();

    // Feed an impulse at sample 0, then zeros, reading at integer delay D each step.
    // read(D) immediately after write(x) returns the sample that was written D
    // samples earlier (read(0) == the just-written sample).
    std::vector<float> out;
    out.reserve(32);
    for (int n = 0; n < 20; ++n) {
        const float x = (n == 0) ? 1.0f : 0.0f;
        line.write(x);
        out.push_back(line.read(static_cast<float>(kD)));
    }

    // The impulse (written at n=0) reappears at the read taken D samples later.
    REQUIRE(out[kD] == 1.0f);
    // Everything else is exactly zero (no smearing for integer delay).
    for (int n = 0; n < static_cast<int>(out.size()); ++n) {
        if (n == kD) continue;
        REQUIRE(out[n] == 0.0f);
    }
}

TEST_CASE("fracdelay: read(0) returns the most recently written sample", "[fracdelay]") {
    // Negative/sanity control: a zero delay is a pure pass-through of the freshest tap.
    FractionalDelayLine line;
    line.prepare(16);
    line.reset();

    line.write(0.25f);
    REQUIRE(line.read(0.0f) == 0.25f);
    line.write(-0.5f);
    REQUIRE(line.read(0.0f) == -0.5f);
}

TEST_CASE("fracdelay: fractional read interpolates monotonically between bracketing taps", "[fracdelay]") {
    // §5.3: with a strictly increasing ramp in the buffer, a fractional delay
    // between integers k and k+1 must read a value strictly between the two
    // bracketing integer taps (monotone interpolation, at-least-linear).
    constexpr int kMax = 128;
    FractionalDelayLine line;
    line.prepare(kMax);
    line.reset();

    // Write a ramp 0,1,2,... so tap value == (sample index back from now).
    // After writing N samples, read(d) ~ value written d samples ago. Because the
    // ramp increases over time, read(integer d) == (lastWrittenValue - d).
    constexpr int kN = 64;
    for (int n = 0; n < kN; ++n)
        line.write(static_cast<float>(n));

    const float vk0 = line.read(10.0f);  // bracketing integer tap k
    const float vk1 = line.read(11.0f);  // bracketing integer tap k+1
    const float vmid = line.read(10.5f); // fractional, between the two

    // The two integer taps differ (the ramp is strictly monotone).
    REQUIRE(vk0 != vk1);

    // Order the brackets so the test is direction-agnostic w.r.t. ramp slope.
    const float lo = std::min(vk0, vk1);
    const float hi = std::max(vk0, vk1);

    // POSITIVE: the fractional read lands strictly inside the bracketing pair —
    // proof of interpolation, not nearest-neighbour.
    REQUIRE(vmid > lo);
    REQUIRE(vmid < hi);

    // Monotone sweep of the fractional position must produce a monotone output
    // across the [10,11] interval (at-least-linear interpolation).
    const bool increasing = (vk1 > vk0);
    float prev = line.read(10.0f);
    for (int i = 1; i <= 10; ++i) {
        const float frac = 10.0f + 0.1f * static_cast<float>(i);
        const float v = line.read(frac);
        if (increasing) REQUIRE(v >= prev);
        else            REQUIRE(v <= prev);
        prev = v;
    }
}

TEST_CASE("fracdelay: linear-interpolation midpoint equals the average of bracketing taps", "[fracdelay]") {
    // For a linear ramp, the half-sample read is exactly the mean of the two
    // bracketing integer taps regardless of interpolation order >= linear (a ramp
    // is reproduced exactly by linear AND by cubic/Lagrange). This pins the
    // numeric behaviour, not just monotonicity.
    FractionalDelayLine line;
    line.prepare(64);
    line.reset();

    for (int n = 0; n < 32; ++n)
        line.write(static_cast<float>(n) * 2.0f); // ramp with slope 2

    const float vk0 = line.read(5.0f);
    const float vk1 = line.read(6.0f);
    const float vmid = line.read(5.5f);
    REQUIRE(vmid == Catch::Approx(0.5f * (vk0 + vk1)).margin(1e-5));
}

TEST_CASE("fracdelay: processBlock applies a fixed integer delay (dry-pad alignment)", "[fracdelay]") {
    // §5.3 / §6.3: processBlock is the integer-delay helper used by the dry pad.
    // It delays an input block by the line's current configured tap. Here we drive
    // an impulse block through and confirm it emerges shifted by the integer delay.
    constexpr int kMax = 64;
    constexpr int kDelay = 5;
    FractionalDelayLine line;
    line.prepare(kMax, kDelay);   // fixed integer delay = kDelay
    line.reset();

    constexpr int kBlock = 24;
    std::vector<float> buf(kBlock, 0.0f);
    buf[0] = 1.0f; // impulse at start of block
    line.processBlock(buf.data(), kBlock);

    // The impulse must be delayed by exactly kDelay samples, everything else zero.
    for (int n = 0; n < kBlock; ++n) {
        if (n == kDelay) REQUIRE(buf[n] == 1.0f);
        else             REQUIRE(buf[n] == 0.0f);
    }
}

TEST_CASE("fracdelay: a zero-length pad passes a block through unchanged", "[fracdelay]") {
    // Negative control: dry pad of length 0 is an identity (FX-off with no Drive
    // latency to compensate still routes through processBlock per §3.4 step 2).
    FractionalDelayLine line;
    line.prepare(16, 0); // zero integer delay
    line.reset();

    std::vector<float> buf{0.1f, 0.2f, 0.3f, 0.4f};
    const std::vector<float> expected = buf;
    line.processBlock(buf.data(), static_cast<int>(buf.size()));
    for (std::size_t n = 0; n < buf.size(); ++n)
        REQUIRE(buf[n] == expected[n]);
}

TEST_CASE("fracdelay: after prepare(), write/read/processBlock perform no heap allocation", "[fracdelay]") {
    // ADR-010 FX-10 / §3.2: all storage is allocated once in prepare(); the hot
    // paths only move indices. We arm the alloc sentinel AFTER prepare() and assert
    // a clean scope across every hot-path call.
    constexpr int kMax = 256;
    FractionalDelayLine line;
    line.prepare(kMax, 4);   // allocation allowed here, before arming
    line.reset();            // reset must not allocate either

    // Pre-touch a scratch block so the std::vector storage exists before arming.
    std::vector<float> block(64, 0.0f);
    for (int n = 0; n < 64; ++n) block[n] = 0.01f * static_cast<float>(n);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    for (int n = 0; n < 64; ++n) {
        line.write(block[n]);
        volatile float sink = line.read(3.5f); // fractional read on the hot path
        (void) sink;
    }
    line.processBlock(block.data(), static_cast<int>(block.size()));
    line.reset();
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

TEST_CASE("fracdelay: interpolation-order calibration constant is at least linear", "[fracdelay]") {
    // The (PI) interpolation-order constant lives in a calibration header (centralized
    // per AGENTS.md). At-least-linear is the §5.3 contract floor.
    STATIC_REQUIRE(mw::cal::fracdelay::kInterpolationOrder >= 1);
}
