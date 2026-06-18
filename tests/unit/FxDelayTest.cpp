// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the FX Delay stage (task 093). Test-case NAMES begin with
// "fxdelay" so `ctest -R fxdelay` selects them (silent-pass rule, AGENTS.md). The
// display text avoids '[' so Catch2 does not parse a stray tag. Each case maps to a
// plan/backlog/093 acceptance criterion:
//   - tempo sync: realized delay == (60000/bpm)*beatsPerDivision within one sample,
//     and the conversion recomputes ONLY on tempo/division change (§5.2.3 / FX-7)
//   - feedback=1.0 requested => applied < kDelayMaxFeedback; long impulse-fed run
//     stays bounded (§5.2.4 / FX-8)
//   - width=0 => out[L]==out[R] centered mono (§5.2.4 / FX-8)
//   - stepping time/division at full feedback => no sample discontinuity above a
//     fixed threshold (pointer-glide) (§5.2.5 / FX-11)
//   - latencySamples()==0; process/setParams/prepare/reset perform no heap alloc and
//     no locks; the feedback flushes denormals (ADR-017 L3 / ADR-010 FX-10)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "dsp/fx/Delay.h"
#include "dsp/fx/FxParams.h"
#include "calibration/DelayConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::fx::Delay;
using mw::fx::FxParams;

namespace {

// Division enum order (owned by doc 06): {1/4=0, 1/8=1, 1/8.=2, 1/8T=3, 1/16=4, 1/16T=5}.
constexpr int kDiv_1_4   = 0;
constexpr int kDiv_1_8   = 1;
constexpr int kDiv_1_8d  = 2;
constexpr int kDiv_1_8t  = 3;
constexpr int kDiv_1_16  = 4;
constexpr int kDiv_1_16t = 5;

FxParams::DelayP makeDelayP() noexcept {
    FxParams::DelayP p{};
    p.on       = true;
    p.sync     = false;
    p.pingpong = false;
    p.division = kDiv_1_8;
    p.timeMs   = 350.0f;
    p.feedback = 0.35f;
    p.damp     = 0.5f;
    p.width    = 1.0f;
    p.mix      = 0.3f;
    return p;
}

// Drive an isolated impulse through the delay and capture the wet output position.
// out has dry already in L/R on entry (we pass zeros so the captured signal is wet
// only, except the impulse sample itself).
} // namespace

TEST_CASE("fxdelay: sync-on realized delay equals 60000/bpm times beatsPerDivision within one sample", "[fxdelay]") {
    // §5.2.3 / ADR-010 FX-7: with sync ON, the realized delay (in samples) must equal
    // (60000/bpm)*beatsPerDivision ms converted at the sample rate, within one sample.
    constexpr double kSR  = 48000.0;
    constexpr double kBpm = 120.0;
    Delay d;
    d.prepare(kSR, 64);

    struct Case { int division; double beats; };
    const Case cases[] = {
        { kDiv_1_4,   1.0      },
        { kDiv_1_8,   0.5      },
        { kDiv_1_8d,  0.75     },
        { kDiv_1_8t,  1.0/3.0  },
        { kDiv_1_16,  0.25     },
        { kDiv_1_16t, 1.0/6.0  },
    };

    for (const auto& c : cases) {
        auto p = makeDelayP();
        p.sync     = true;
        p.division = c.division;
        d.setParams(p, kBpm);

        const double expectedMs      = (60000.0 / kBpm) * c.beats;
        const double expectedSamples = expectedMs * 0.001 * kSR;

        // The cached ms-equivalent matches the oracle exactly.
        CHECK(static_cast<double>(d.cachedDelayMs()) == Catch::Approx(expectedMs).margin(1e-6));
        // The realized (target) read position matches within one sample.
        CHECK(std::fabs(static_cast<double>(d.targetDelaySamples()) - expectedSamples) <= 1.0);

        // The static oracle exposed for direct verification matches too.
        CHECK(Delay::beatsPerDivision(c.division) == Catch::Approx(c.beats).margin(1e-12));
    }
}

TEST_CASE("fxdelay: tempo-sync conversion recomputes only on tempo or division change not per sample", "[fxdelay]") {
    // §5.2.3 / FX-7: the conversion is cached; recomputed only when host tempo OR the
    // selected division changes. Re-publishing the SAME params and processing many
    // blocks must NOT bump the recompute counter.
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.sync     = true;
    p.division = kDiv_1_8;

    d.setParams(p, 120.0);
    const long afterFirst = d.conversionRecomputeCount();
    CHECK(afterFirst >= 1); // first publish computes

    // Re-publish identical params repeatedly: no recompute.
    for (int i = 0; i < 50; ++i) d.setParams(p, 120.0);
    CHECK(d.conversionRecomputeCount() == afterFirst);

    // Process audio blocks: process() must never touch the conversion counter.
    std::vector<float> L(64, 0.0f), R(64, 0.0f);
    for (int b = 0; b < 100; ++b) d.process(L.data(), R.data(), 64);
    CHECK(d.conversionRecomputeCount() == afterFirst);

    // Change the tempo => exactly one more recompute.
    d.setParams(p, 140.0);
    CHECK(d.conversionRecomputeCount() == afterFirst + 1);

    // Change the division => one more recompute.
    p.division = kDiv_1_16;
    d.setParams(p, 140.0);
    CHECK(d.conversionRecomputeCount() == afterFirst + 2);
}

TEST_CASE("fxdelay: requesting feedback 1.0 applies a clamped value below kDelayMaxFeedback", "[fxdelay]") {
    // §5.2.4 / FX-8: requested feedback 1.0 (and above) must be clamped to
    // kDelayMaxFeedback which is strictly < 1.0.
    STATIC_REQUIRE(mw::cal::delay::kDelayMaxFeedback < 1.0f);

    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.feedback = 1.0f; // request unity / runaway
    d.setParams(p, 120.0);

    CHECK(d.appliedFeedback() == Catch::Approx(mw::cal::delay::kDelayMaxFeedback));
    CHECK(d.appliedFeedback() < 1.0f);

    // Over-range request clamps too.
    p.feedback = 4.2f;
    d.setParams(p, 120.0);
    CHECK(d.appliedFeedback() == Catch::Approx(mw::cal::delay::kDelayMaxFeedback));
}

TEST_CASE("fxdelay: a long impulse-fed run at maxed feedback stays bounded", "[fxdelay]") {
    // §5.2.4 / FX-8: feed an impulse, request runaway feedback, run for a long time.
    // The clamp (<1.0) + damping LPF + gentle saturation must keep the loop bounded.
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.feedback = 1.0f;     // -> clamped to kDelayMaxFeedback
    p.damp     = 0.5f;
    p.timeMs   = 5.0f;     // short delay so many repeats happen in the run
    p.mix      = 1.0f;     // full wet so we observe the loop directly
    p.width    = 1.0f;
    d.setParams(p, 120.0);

    constexpr int kBlock  = 64;
    constexpr int kBlocks = 4000; // ~5.3 s at 48k — many feedback recirculations
    std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);

    float peak = 0.0f;
    for (int b = 0; b < kBlocks; ++b) {
        // Inject a full-scale impulse only on the very first sample.
        if (b == 0) { L[0] = 1.0f; R[0] = 1.0f; }
        else        { L[0] = 0.0f; R[0] = 0.0f; }
        for (int n = (b == 0 ? 1 : 0); n < kBlock; ++n) { L[n] = 0.0f; R[n] = 0.0f; }

        d.process(L.data(), R.data(), kBlock);

        for (int n = 0; n < kBlock; ++n) {
            const float aL = std::fabs(L[n]);
            const float aR = std::fabs(R[n]);
            if (aL > peak) peak = aL;
            if (aR > peak) peak = aR;
            REQUIRE(std::isfinite(L[n]));
            REQUIRE(std::isfinite(R[n]));
        }
    }

    // Bounded: a single unit impulse with clamped feedback < 1 + per-repeat LPF/sat
    // loss cannot blow up. Generous ceiling — the point is no divergence.
    CHECK(peak < 4.0f);
}

TEST_CASE("fxdelay: width 0 yields equal L and R centered mono", "[fxdelay]") {
    // §5.2.4 / FX-8: width=0 collapses the wet to centered mono => out[L]==out[R].
    // Test both ping-pong OFF and ON (width must override the pan spread either way).
    constexpr double kSR = 48000.0;

    for (bool pingpong : { false, true }) {
        Delay d;
        d.prepare(kSR, 64);

        auto p = makeDelayP();
        p.width    = 0.0f;
        p.feedback = 0.6f;
        p.mix      = 0.5f;
        p.timeMs   = 7.0f;
        p.pingpong = pingpong;
        d.setParams(p, 120.0);

        constexpr int kBlock = 64;
        std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);

        for (int b = 0; b < 200; ++b) {
            // Identical dry on both channels so any L/R difference is purely the
            // delay's wet spread.
            for (int n = 0; n < kBlock; ++n) {
                const float s = (b == 0 && n == 0) ? 1.0f
                                                   : 0.05f * std::sin(0.02f * static_cast<float>(b * kBlock + n));
                L[n] = s;
                R[n] = s;
            }
            d.process(L.data(), R.data(), kBlock);
            for (int n = 0; n < kBlock; ++n) {
                REQUIRE(L[n] == Catch::Approx(R[n]).margin(1e-6));
            }
        }
    }
}

TEST_CASE("fxdelay: stepping delay time at full feedback produces no sample discontinuity above threshold", "[fxdelay]") {
    // §5.2.5 / FX-11: a delay-time step (and a sync-division step) at full feedback
    // must pointer-glide, so there is no sample-to-sample discontinuity above a fixed
    // threshold. We feed a smooth sine, fill the line, then step the time mid-stream
    // and assert the output stays click-free across the step.
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.feedback = 1.0f;   // clamped to max — worst case for clicks
    p.damp     = 0.7f;
    p.mix      = 1.0f;   // full wet so we observe the read pointer directly
    p.width    = 1.0f;
    p.timeMs   = 200.0f;
    d.setParams(p, 120.0);

    constexpr int kBlock = 32;
    std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);

    auto fillSine = [&](int blockIdx) {
        for (int n = 0; n < kBlock; ++n) {
            const double t = static_cast<double>(blockIdx * kBlock + n) / kSR;
            const float s = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 220.0 * t));
            L[n] = s;
            R[n] = s;
        }
    };

    // Prime the delay line for a while at the original time.
    for (int b = 0; b < 400; ++b) { fillSine(b); d.process(L.data(), R.data(), kBlock); }

    // Now step the delay time DOWN hard and keep processing, tracking the largest
    // sample-to-sample jump across the change.
    p.timeMs = 40.0f;
    d.setParams(p, 120.0);

    float prevL = 0.0f, prevR = 0.0f;
    bool  have   = false;
    float maxJump = 0.0f;
    for (int b = 400; b < 1200; ++b) {
        fillSine(b);
        d.process(L.data(), R.data(), kBlock);
        for (int n = 0; n < kBlock; ++n) {
            if (have) {
                maxJump = std::max(maxJump, std::fabs(L[n] - prevL));
                maxJump = std::max(maxJump, std::fabs(R[n] - prevR));
            }
            prevL = L[n];
            prevR = R[n];
            have  = true;
        }
    }

    // A glided read pointer produces a continuous (pitch-bent) output. Without the
    // glide the read tap would jump ~160 ms instantly and slap the buffer — a huge
    // discontinuity. The threshold is comfortably below that but above the smooth
    // per-sample delta of a 220 Hz tone at full wet+feedback.
    CHECK(maxJump < 0.5f);
}

TEST_CASE("fxdelay: stepping the sync division at full feedback produces no discontinuity above threshold", "[fxdelay]") {
    // §5.2.5 / FX-11: the division step path also pointer-glides (it changes the
    // cached delay just like a free-time step). Same click-free assertion.
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.sync     = true;
    p.division = kDiv_1_4;
    p.feedback = 1.0f;
    p.damp     = 0.7f;
    p.mix      = 1.0f;
    p.width    = 1.0f;
    d.setParams(p, 120.0);

    constexpr int kBlock = 32;
    std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);
    auto fillSine = [&](int blockIdx) {
        for (int n = 0; n < kBlock; ++n) {
            const double t = static_cast<double>(blockIdx * kBlock + n) / kSR;
            const float s = 0.4f * static_cast<float>(std::sin(2.0 * M_PI * 180.0 * t));
            L[n] = s; R[n] = s;
        }
    };

    for (int b = 0; b < 600; ++b) { fillSine(b); d.process(L.data(), R.data(), kBlock); }

    // Step 1/4 -> 1/16 (a big delay-time decrease).
    p.division = kDiv_1_16;
    d.setParams(p, 120.0);

    float prevL = 0.0f, prevR = 0.0f; bool have = false; float maxJump = 0.0f;
    for (int b = 600; b < 1600; ++b) {
        fillSine(b);
        d.process(L.data(), R.data(), kBlock);
        for (int n = 0; n < kBlock; ++n) {
            if (have) {
                maxJump = std::max(maxJump, std::fabs(L[n] - prevL));
                maxJump = std::max(maxJump, std::fabs(R[n] - prevR));
            }
            prevL = L[n]; prevR = R[n]; have = true;
        }
    }
    CHECK(maxJump < 0.5f);
}

TEST_CASE("fxdelay: ping-pong routes the wet tap to opposite channels and is not equal L R", "[fxdelay]") {
    // §5.2.4: ping-pong ON alternates the wet tap routing across the stereo field, so
    // the wet content is NOT identical on both channels (unlike Width=0). This is the
    // positive control distinguishing ping-pong from the centered-mono collapse.
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.pingpong = true;
    p.width    = 1.0f;
    p.feedback = 0.7f;
    p.mix      = 1.0f;
    p.timeMs   = 5.0f; // short so the bounce toggles within the run
    d.setParams(p, 120.0);

    constexpr int kBlock = 64;
    std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);

    bool sawDifference = false;
    for (int b = 0; b < 400; ++b) {
        for (int n = 0; n < kBlock; ++n) {
            const float s = (b == 0 && n == 0) ? 1.0f : 0.0f; // single impulse, dry zero elsewhere
            L[n] = s; R[n] = s;
        }
        d.process(L.data(), R.data(), kBlock);
        for (int n = 0; n < kBlock; ++n) {
            if (std::fabs(L[n] - R[n]) > 1e-4f) sawDifference = true;
        }
    }
    CHECK(sawDifference);
}

TEST_CASE("fxdelay: latencySamples is zero per ADR-017 L3", "[fxdelay]") {
    // §5.2.4 / §6.1 / ADR-017 L3: the Delay musical time is intended musical delay and
    // does NOT contribute to reported PDC.
    Delay d;
    d.prepare(48000.0, 64);
    CHECK(d.latencySamples() == 0);
    // Constant regardless of params.
    auto p = makeDelayP();
    p.timeMs = 1500.0f;
    d.setParams(p, 90.0);
    CHECK(d.latencySamples() == 0);
}

TEST_CASE("fxdelay: prepare reset setParams process perform no heap allocation and no locks", "[fxdelay]") {
    // ADR-017 L3 / ADR-010 FX-10 / RT invariants: all storage is allocated in
    // prepare(); the hot paths only move indices. Arm the alloc sentinel AFTER
    // prepare() and assert a clean scope across reset/setParams/process.
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);            // allocation allowed here, before arming

    auto p = makeDelayP();
    p.feedback = 0.9f;
    p.sync     = true;
    p.division = kDiv_1_8;

    // Pre-touch scratch so its std::vector storage exists before arming.
    constexpr int kBlock = 64;
    std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);
    for (int n = 0; n < kBlock; ++n) { L[n] = 0.01f * static_cast<float>(n); R[n] = L[n]; }

    mw::test::AudioThreadGuard guard;
    guard.arm();
    d.reset();
    d.setParams(p, 120.0);
    d.setParams(p, 130.0); // tempo change path (recompute) — still no alloc
    for (int b = 0; b < 16; ++b) d.process(L.data(), R.data(), kBlock);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

TEST_CASE("fxdelay: feedback path flushes denormals and never produces subnormal output in the tail", "[fxdelay]") {
    // ADR-010 FX-10 / RT-5: the recirculating feedback path flushes denormals so a
    // long decay tail cannot stall in a subnormal CPU trap. After the impulse decays,
    // every loop-fed sample is either exactly 0 or a normal float (no subnormals).
    constexpr double kSR = 48000.0;
    Delay d;
    d.prepare(kSR, 64);

    auto p = makeDelayP();
    p.feedback = 0.85f;
    p.damp     = 0.3f;  // heavy LPF -> energy bleeds to tiny values quickly
    p.timeMs   = 3.0f;
    p.mix      = 1.0f;
    d.setParams(p, 120.0);

    constexpr int kBlock = 64;
    std::vector<float> L(kBlock, 0.0f), R(kBlock, 0.0f);

    // Excite once, then run long enough that the tail would reach denormal magnitudes.
    bool sawSubnormal = false;
    for (int b = 0; b < 6000; ++b) {
        for (int n = 0; n < kBlock; ++n) {
            const float s = (b == 0 && n == 0) ? 1.0f : 0.0f;
            L[n] = s; R[n] = s;
        }
        d.process(L.data(), R.data(), kBlock);
        if (b > 1000) { // well into the decay tail
            for (int n = 0; n < kBlock; ++n) {
                if (L[n] != 0.0f && std::fabs(L[n]) < std::numeric_limits<float>::min())
                    sawSubnormal = true;
                if (R[n] != 0.0f && std::fabs(R[n]) < std::numeric_limits<float>::min())
                    sawSubnormal = true;
                REQUIRE(std::isfinite(L[n]));
            }
        }
    }
    CHECK_FALSE(sawSubnormal);
}
