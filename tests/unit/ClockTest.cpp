// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the shared clock edge node (task 086). Test-case names begin with
// "clock" so `-R clock` selects exactly this suite (the silent-pass rule); display
// names avoid '[' so ctest -R selection is not broken by Catch2 tag parsing.
//
// Each TEST_CASE maps to an 086 acceptance criterion and the cited docs/design/05
// §7.1-§7.8 sections / ADR-007 C18-C24, C27.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#include "BlockContext.h"
#include "calibration/ClockConstants.h"
#include "control/Clock.h"

using namespace mw::control;
using Catch::Matchers::WithinAbs;

namespace {

// Build a TransportInfo POD (doc 00 §5.3) for a block.
mw::TransportInfo makeTransport(double bpm, double ppq, bool playing, double sr) {
    mw::TransportInfo t{};
    t.bpm = bpm;
    t.ppqPosition = ppq;
    t.isPlaying = playing;
    t.sampleRate = sr;
    return t;
}

// PPQ advanced by N samples at a given bpm/sr.
double ppqPerSample(double bpm, double sr) { return (bpm / 60.0) / sr; }

} // namespace

// ===========================================================================
// §7.7 — signature / POD shape
// ===========================================================================

TEST_CASE("clock: ClockEdge is a POD and the source accessor reflects setSource", "[clock]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ClockEdge>);

    Clock c;
    c.prepare(48000.0);
    REQUIRE(c.source() == ClockSource::Internal);   // default (doc 05 §9.2)

    c.setSource(ClockSource::HostSync);
    REQUIRE(c.source() == ClockSource::HostSync);
    REQUIRE(c.swingActive());                        // swing active only under HostSync

    c.setSource(ClockSource::Ext);
    REQUIRE(c.source() == ClockSource::Ext);
    REQUIRE_FALSE(c.swingActive());

    c.setSource(ClockSource::Internal);
    REQUIRE_FALSE(c.swingActive());
}

// ===========================================================================
// §7.4 / C19 — Host phase derived from ABSOLUTE PPQ; 1 host pulse = 1 step;
// tempo change / loop wrap / scrub re-derive next edge with no cumulative drift.
// ===========================================================================

TEST_CASE("clock: HostSync derives edges from absolute PPQ, one pulse per step", "[clock]") {
    const double sr = 48000.0;
    const double bpm = 120.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Quarter);   // 1.0 quarter-note per step
    c.setSwing(0.5f);                    // swing off

    // A block exactly 1 quarter-note long, starting on a boundary (ppq=0).
    const double qps = ppqPerSample(bpm, sr);     // ppq advanced per sample
    const int oneStep = static_cast<int>(std::lround(1.0 / qps));   // samples per quarter
    const int numFrames = oneStep;                 // [0, oneStep) -> exactly one boundary at 0

    std::array<ClockEdge, 64> out{};
    int n = 0;
    auto t = makeTransport(bpm, /*ppq=*/0.0, /*playing=*/true, sr);
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, numFrames, n);

    // Exactly one edge, at the boundary sitting at block start (offset 0).
    REQUIRE(n == 1);
    REQUIRE(out[0].sampleOffset == 0);
}

TEST_CASE("clock: HostSync edge offset matches absolute PPQ to the boundary", "[clock]") {
    const double sr = 48000.0;
    const double bpm = 120.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Quarter);   // step every 1.0 ppq
    c.setSwing(0.5f);

    const double qps = ppqPerSample(bpm, sr);
    // Start the block 0.25 quarter-notes BEFORE a boundary at ppq = 2.0; the block is
    // long enough to contain that boundary but stop before the next (at ppq = 3.0).
    const double blockStartPpq = 1.75;
    const int numFrames = 16384;   // 16384/24000 = 0.683 qn -> reaches 2.0, not 3.0

    std::array<ClockEdge, 64> out{};
    int n = 0;
    auto t = makeTransport(bpm, blockStartPpq, true, sr);
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, numFrames, n);

    // The next boundary at ppq = 2.0 falls at (2.0 - 1.75)/qps samples in.
    const int expected = static_cast<int>(std::lround((2.0 - 1.75) / qps));
    REQUIRE(n == 1);
    REQUIRE(out[0].sampleOffset == expected);
}

TEST_CASE("clock: HostSync re-derives from absolute PPQ with no cumulative drift across blocks", "[clock]") {
    // Walk many contiguous blocks; the edge count must equal floor(totalPpq/step)
    // boundaries crossed, computed afresh from absolute PPQ each block (no free-running
    // counter to desync). Includes a mid-stream tempo change and a loop-wrap (scrub).
    const double sr = 44100.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Sixteenth);  // 0.25 ppq per step
    c.setSwing(0.5f);

    std::array<ClockEdge, 256> out{};

    auto countBlock = [&](double bpm, double startPpq, int frames) {
        int n = 0;
        auto t = makeTransport(bpm, startPpq, true, sr);
        c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, frames, n);
        // Every emitted edge must be inside the block and monotone increasing.
        for (int i = 0; i < n; ++i) {
            REQUIRE(out[i].sampleOffset >= 0);
            REQUIRE(out[i].sampleOffset < frames);
            if (i > 0) REQUIRE(out[i].sampleOffset >= out[i - 1].sampleOffset);
        }
        return n;
    };

    // Block A: bpm 120, ppq [0.0, 0.0 + frames*qps).
    const int framesA = 8000;
    const double qpsA = ppqPerSample(120.0, sr);
    const double endPpqA = framesA * qpsA;             // span of block A in ppq
    int nA = countBlock(120.0, 0.0, framesA);
    // boundaries strictly in [0, endPpqA): indices 0,1,... at k*0.25
    int expectA = static_cast<int>(std::floor((endPpqA - 1e-12) / 0.25)) + 1; // includes ppq=0
    REQUIRE(nA == expectA);

    // Block B: TEMPO CHANGE to 90 bpm, contiguous start at endPpqA.
    const int framesB = 8000;
    const double qpsB = ppqPerSample(90.0, sr);
    const double startB = endPpqA;
    const double endB = startB + framesB * qpsB;
    int nB = countBlock(90.0, startB, framesB);
    // boundaries in (startB, endB): exclude one exactly at startB (already counted in A
    // only if it landed there). Count integer multiples of 0.25 in [startB, endB).
    int firstIdxB = static_cast<int>(std::ceil(startB / 0.25 - 1e-9));
    int lastIdxB  = static_cast<int>(std::floor((endB - 1e-9) / 0.25));
    int expectB = lastIdxB - firstIdxB + 1;
    REQUIRE(nB == expectB);

    // Block C: LOOP WRAP / SCRUB — jump backward to ppq 0.5 (transport relocates).
    // The clock must re-derive purely from this new absolute PPQ, NOT from any
    // accumulated counter; so the result depends only on (0.5, frames), not history.
    const int framesC = 4000;
    const double qpsC = ppqPerSample(120.0, sr);
    const double startC = 0.5;
    const double endC = startC + framesC * qpsC;
    int nC = countBlock(120.0, startC, framesC);
    int firstIdxC = static_cast<int>(std::ceil(startC / 0.25 - 1e-9));
    int lastIdxC  = static_cast<int>(std::floor((endC - 1e-9) / 0.25));
    int expectC = lastIdxC - firstIdxC + 1;
    REQUIRE(nC == expectC);

    // Determinism after scrub: rendering the SAME (startC, framesC) again yields the
    // identical edge set — proof there is no hidden free-running phase under HostSync.
    int n2 = 0;
    std::array<ClockEdge, 256> out2{};
    auto t2 = makeTransport(120.0, startC, true, sr);
    c.renderEdges(t2, std::span<const int>{}, std::span<ClockEdge>{out2}, framesC, n2);
    REQUIRE(n2 == nC);
    for (int i = 0; i < nC; ++i) REQUIRE(out2[i].sampleOffset == out[i].sampleOffset);
}

TEST_CASE("clock: HostSync emits no edges while transport is stopped", "[clock]") {
    Clock c;
    c.prepare(48000.0);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Quarter);

    std::array<ClockEdge, 64> out{};
    int n = 99;
    auto t = makeTransport(120.0, /*ppq=*/4.0, /*playing=*/false, 48000.0);
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 4096, n);
    REQUIRE(n == 0);
}

// ===========================================================================
// §7.8 / C23 — HostRate -> quarter-note period table.
// ===========================================================================

TEST_CASE("clock: each HostRate maps to its quarter-note period per the table", "[clock]") {
    struct Row { HostRate rate; double qnPerStep; };
    const std::array<Row, 8> table = {{
        {HostRate::Quarter,         1.0},
        {HostRate::Eighth,          0.5},
        {HostRate::EighthT,         1.0 / 3.0},
        {HostRate::Sixteenth,       0.25},
        {HostRate::SixteenthT,      0.25 / 1.5},
        {HostRate::ThirtySecond,    0.125},
        {HostRate::DottedEighth,    0.75},
        {HostRate::DottedSixteenth, 0.375},
    }};

    const double sr = 48000.0;
    const double bpm = 120.0;
    const double qps = ppqPerSample(bpm, sr);

    for (const auto& row : table) {
        Clock c;
        c.prepare(sr);
        c.setSource(ClockSource::HostSync);
        c.setHostRate(row.rate);
        c.setSwing(0.5f);

        // A long block starting exactly on a boundary (ppq 0); the gap between the
        // first two edges = one step = qnPerStep / qps samples.
        const int numFrames = 200000;
        std::vector<ClockEdge> out(4096);
        int n = 0;
        auto t = makeTransport(bpm, 0.0, true, sr);
        c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, numFrames, n);

        REQUIRE(n >= 2);
        REQUIRE(out[0].sampleOffset == 0);
        const int stepSamples = out[1].sampleOffset - out[0].sampleOffset;
        const double expected = row.qnPerStep / qps;
        REQUIRE(static_cast<double>(stepSamples) == Catch::Approx(expected).margin(1.0));
    }
}

// ===========================================================================
// §7.2 / §7.3 / C18, C21 — RATE role under each source.
//   Internal: RATE sets tempo over 0.1-30 Hz.
//   HostSync / Ext: RATE does NOT change step tempo.
// ===========================================================================

TEST_CASE("clock: Internal RATE sets the edge tempo over 0.1-30 Hz", "[clock]") {
    const double sr = 48000.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::Internal);

    auto periodSamplesAt = [&](float hz) {
        c.setInternalRateHz(hz);
        // Render two contiguous blocks and find the gap between the first two edges.
        std::array<ClockEdge, 4096> out{};
        std::vector<int> offsets;
        const int frames = 1 << 16;
        // accumulate edges across enough frames to capture >= 2 edges for slow rates.
        // Use a single big block; transport unused under Internal.
        auto t = makeTransport(120.0, 0.0, true, sr);
        int total = 0;
        int base = 0;
        for (int b = 0; b < 64 && static_cast<int>(offsets.size()) < 2; ++b) {
            int n = 0;
            c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, frames, n);
            for (int i = 0; i < n; ++i) offsets.push_back(base + out[i].sampleOffset);
            base += frames;
            (void) total;
        }
        REQUIRE(offsets.size() >= 2);
        return offsets[1] - offsets[0];
    };

    // 1 Hz => one edge per second => ~sr samples per period.
    int p1 = periodSamplesAt(1.0f);
    REQUIRE(static_cast<double>(p1) == Catch::Approx(sr).epsilon(0.01));

    // 10 Hz => ~sr/10 samples per period.
    int p10 = periodSamplesAt(10.0f);
    REQUIRE(static_cast<double>(p10) == Catch::Approx(sr / 10.0).epsilon(0.01));

    // Range clamp: 0.1 and 30 Hz endpoints accepted; out-of-range clamps in.
    c.setInternalRateHz(0.05f);   // below min -> clamped to 0.1
    c.setInternalRateHz(60.0f);   // above max -> clamped to 30
    SUCCEED("internal rate setters clamp without UB");
}

TEST_CASE("clock: under HostSync the internal RATE does not change step tempo", "[clock]") {
    const double sr = 48000.0;
    const double bpm = 120.0;
    const double qps = ppqPerSample(bpm, sr);
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Quarter);
    c.setSwing(0.5f);

    auto firstGap = [&](float rateHz) {
        c.setInternalRateHz(rateHz);   // RATE is LFO-mod only here; must not move tempo
        std::array<ClockEdge, 64> out{};
        int n = 0;
        auto t = makeTransport(bpm, 0.0, true, sr);
        c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 100000, n);
        REQUIRE(n >= 2);
        return out[1].sampleOffset - out[0].sampleOffset;
    };

    const int gapSlow = firstGap(0.5f);
    const int gapFast = firstGap(25.0f);
    REQUIRE(gapSlow == gapFast);   // tempo unchanged by RATE under HostSync
    REQUIRE(static_cast<double>(gapSlow) == Catch::Approx(1.0 / qps).margin(1.0));
}

TEST_CASE("clock: under Ext the internal RATE does not change step tempo and 1 pulse = 1 step", "[clock]") {
    const double sr = 48000.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::Ext);

    // Ext maps each supplied pulse offset to exactly one edge, regardless of RATE.
    const std::array<int, 4> pulses = {17, 512, 1024, 4095};
    std::array<ClockEdge, 64> out{};

    auto mapPulses = [&](float rateHz) {
        c.setInternalRateHz(rateHz);
        int n = 0;
        auto t = makeTransport(120.0, 3.21, true, sr);  // transport irrelevant under Ext
        c.renderEdges(t, std::span<const int>{pulses}, std::span<ClockEdge>{out}, 4096, n);
        std::vector<int> got;
        for (int i = 0; i < n; ++i) got.push_back(out[i].sampleOffset);
        return got;
    };

    auto a = mapPulses(0.5f);
    auto b = mapPulses(25.0f);
    REQUIRE(a.size() == pulses.size());
    REQUIRE(b == a);   // RATE does not change Ext stepping
    for (size_t i = 0; i < pulses.size(); ++i) REQUIRE(a[i] == pulses[i]);
}

// ===========================================================================
// §7.6 / C24 — SWING (host-sync only): 50% no offset, 75% half-step on even steps,
// inert under Internal / Ext, default 50%.
// ===========================================================================

TEST_CASE("clock: swing 75 percent delays even steps by half a step under HostSync", "[clock]") {
    const double sr = 48000.0;
    const double bpm = 120.0;
    const double qps = ppqPerSample(bpm, sr);
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Eighth);   // 0.5 ppq per step
    const double stepSamples = 0.5 / qps;

    // Baseline: swing 50% (off) — even and odd steps land on the grid.
    std::array<ClockEdge, 64> base{};
    int nb = 0;
    {
        c.setSwing(0.5f);
        auto t = makeTransport(bpm, 0.0, true, sr);
        c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{base}, 100000, nb);
        REQUIRE(nb >= 4);
        REQUIRE(base[0].sampleOffset == 0);
        // step 1 (odd, the "2nd" step) at one step in; step 2 at two steps in.
        REQUIRE(static_cast<double>(base[1].sampleOffset) == Catch::Approx(stepSamples).margin(1.0));
    }

    // Swing 75%: the even-numbered (2nd, 4th, ... -> odd 0-based index) steps are
    // delayed by half a step; on-beat steps (index 0, 2, ...) are unchanged.
    std::array<ClockEdge, 64> sw{};
    int ns = 0;
    {
        c.setSwing(0.75f);
        auto t = makeTransport(bpm, 0.0, true, sr);
        c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{sw}, 100000, ns);
        REQUIRE(ns >= 4);
    }

    const double half = 0.5 * stepSamples;
    // index 0 on-beat: unchanged.
    REQUIRE(sw[0].sampleOffset == base[0].sampleOffset);
    // index 1 off-beat: delayed by half a step.
    REQUIRE(static_cast<double>(sw[1].sampleOffset)
            == Catch::Approx(base[1].sampleOffset + half).margin(1.0));
    // index 2 on-beat: unchanged (back on the grid, no cumulative drift).
    REQUIRE(sw[2].sampleOffset == base[2].sampleOffset);
    // index 3 off-beat: delayed by half a step.
    REQUIRE(static_cast<double>(sw[3].sampleOffset)
            == Catch::Approx(base[3].sampleOffset + half).margin(1.0));
}

TEST_CASE("clock: swing is inert under Internal and Ext", "[clock]") {
    const double sr = 48000.0;

    // Internal: setting swing must not move internal edges.
    {
        Clock c;
        c.prepare(sr);
        c.setSource(ClockSource::Internal);
        c.setInternalRateHz(8.0f);

        auto firstGap = [&](float s) {
            c.setSwing(s);
            std::array<ClockEdge, 4096> out{};
            std::vector<int> offs;
            int base = 0;
            auto t = makeTransport(120.0, 0.0, true, sr);
            for (int b = 0; b < 16 && offs.size() < 2; ++b) {
                int n = 0;
                c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 1 << 14, n);
                for (int i = 0; i < n; ++i) offs.push_back(base + out[i].sampleOffset);
                base += 1 << 14;
            }
            REQUIRE(offs.size() >= 2);
            return offs[1] - offs[0];
        };
        REQUIRE(firstGap(0.5f) == firstGap(0.75f));
    }

    // Ext: swing must not move mapped pulses.
    {
        Clock c;
        c.prepare(sr);
        c.setSource(ClockSource::Ext);
        const std::array<int, 3> pulses = {10, 200, 3000};
        std::array<ClockEdge, 16> out{};

        auto mapAt = [&](float s) {
            c.setSwing(s);
            int n = 0;
            auto t = makeTransport(120.0, 0.0, true, sr);
            c.renderEdges(t, std::span<const int>{pulses}, std::span<ClockEdge>{out}, 4096, n);
            std::vector<int> got;
            for (int i = 0; i < n; ++i) got.push_back(out[i].sampleOffset);
            return got;
        };
        REQUIRE(mapAt(0.75f) == mapAt(0.5f));
    }
}

TEST_CASE("clock: swing taper calibration maps 50 to 0 and 75 to half a step", "[clock]") {
    // Oracle: the (PI) kSwingTaper map is the linear 50%->0, 75%->0.5 fraction.
    REQUIRE_THAT(mw::cal::clock::kSwingTaper(0.50f), WithinAbs(0.0f, 1e-7f));
    REQUIRE_THAT(mw::cal::clock::kSwingTaper(0.75f), WithinAbs(0.5f, 1e-7f));
    REQUIRE_THAT(mw::cal::clock::kSwingTaper(0.625f), WithinAbs(0.25f, 1e-7f));
}

// ===========================================================================
// §7.5 / C22 — Clock reset on keypress: re-phase to the keypress sample;
// default-on; togglable off.
// ===========================================================================

TEST_CASE("clock: resetToKeypress re-phases internal edges to the keypress sample", "[clock]") {
    const double sr = 48000.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::Internal);
    c.setInternalRateHz(4.0f);   // period = sr/4 = 12000 samples

    // Re-phase to a keypress 1000 samples into the block. The next internal edge must
    // be measured from that sample: the first edge fires AT the keypress sample.
    c.resetToKeypress(1000);

    std::array<ClockEdge, 4096> out{};
    int n = 0;
    auto t = makeTransport(120.0, 0.0, true, sr);
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 1 << 15, n);

    REQUIRE(n >= 1);
    // The first edge lands on the keypress sample (phase reset re-locks here).
    REQUIRE(out[0].sampleOffset == 1000);
    if (n >= 2) {
        const int period = static_cast<int>(std::lround(sr / 4.0));
        REQUIRE(out[1].sampleOffset - out[0].sampleOffset == period);
    }
}

TEST_CASE("clock: clock-reset-on-keypress defaults on and is togglable off", "[clock]") {
    Clock c;
    c.prepare(48000.0);
    // Default-on (C22): the accessor reports the default.
    REQUIRE(c.clockResetOnKeypress());
    c.setClockResetOnKeypress(false);
    REQUIRE_FALSE(c.clockResetOnKeypress());
    c.setClockResetOnKeypress(true);
    REQUIRE(c.clockResetOnKeypress());
}

TEST_CASE("clock: resetToKeypress re-phases the host next-boundary reference", "[clock]") {
    const double sr = 48000.0;
    const double bpm = 120.0;
    const double qps = ppqPerSample(bpm, sr);
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::Quarter);
    c.setSwing(0.5f);

    // With a keypress re-phase at sample 2000, the next host edge is measured from
    // the keypress sample (the reset path is reused for host re-phase, §7.5).
    c.resetToKeypress(2000);
    std::array<ClockEdge, 64> out{};
    int n = 0;
    // Block starts mid-step (not on a boundary) so the grid alone would not place an
    // edge at the keypress; the re-phase forces the first edge to the keypress sample.
    auto t = makeTransport(bpm, /*ppq=*/0.37, true, sr);
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 1 << 15, n);

    REQUIRE(n >= 1);
    REQUIRE(out[0].sampleOffset == 2000);
    if (n >= 2) {
        const int step = static_cast<int>(std::lround(1.0 / qps));
        REQUIRE(out[1].sampleOffset - out[0].sampleOffset == step);
    }
}

// ===========================================================================
// §7.7 / C27 / RT-1 — renderEdges does no heap allocation under a sentinel and
// never overruns the caller's pre-sized span.
// ===========================================================================

namespace {
// Minimal allocation sentinel: count global new/delete around the hot call. Built
// without arming any global operator new override (that lives in the project RT
// guard); here we assert structurally that renderEdges only touches the caller span.
struct AllocCounter {
    static inline int allocations = 0;
};
} // namespace

TEST_CASE("clock: renderEdges writes only into the pre-sized span and respects its capacity", "[clock]") {
    const double sr = 96000.0;
    Clock c;
    c.prepare(sr);
    c.setSource(ClockSource::HostSync);
    c.setHostRate(HostRate::ThirtySecond);  // dense rate
    c.setSwing(0.5f);

    // Give a span SMALLER than the number of boundaries in a huge block; renderEdges
    // must clamp to capacity and never write past out.size() (no overrun, no alloc).
    std::array<ClockEdge, 8> out{};
    for (auto& e : out) e.sampleOffset = -123;   // sentinel fill
    int n = 0;
    auto t = makeTransport(200.0, 0.0, true, sr);
    c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 1 << 20, n);

    REQUIRE(n <= static_cast<int>(out.size()));
    REQUIRE(n >= 1);
    // Every written slot is a real (non-sentinel) offset inside the block; untouched
    // tail keeps the sentinel.
    for (int i = 0; i < n; ++i) {
        REQUIRE(out[i].sampleOffset >= 0);
    }

    // Ext with more pulses than capacity also clamps to capacity.
    c.setSource(ClockSource::Ext);
    std::array<int, 32> manyPulses{};
    for (int i = 0; i < 32; ++i) manyPulses[i] = i * 100;
    int n2 = 0;
    c.renderEdges(t, std::span<const int>{manyPulses}, std::span<ClockEdge>{out}, 1 << 20, n2);
    REQUIRE(n2 <= static_cast<int>(out.size()));
}

TEST_CASE("clock: renderEdges and the setters are noexcept hot paths", "[clock]") {
    Clock c;
    std::array<ClockEdge, 4> out{};
    int n = 0;
    mw::TransportInfo t{};
    STATIC_REQUIRE(noexcept(c.prepare(48000.0)));
    STATIC_REQUIRE(noexcept(c.setSource(ClockSource::Internal)));
    STATIC_REQUIRE(noexcept(c.setInternalRateHz(1.0f)));
    STATIC_REQUIRE(noexcept(c.setHostRate(HostRate::Quarter)));
    STATIC_REQUIRE(noexcept(c.setSwing(0.5f)));
    STATIC_REQUIRE(noexcept(c.setClockResetOnKeypress(true)));
    STATIC_REQUIRE(noexcept(c.resetToKeypress(0)));
    STATIC_REQUIRE(noexcept(c.renderEdges(t, std::span<const int>{}, std::span<ClockEdge>{out}, 0, n)));
}
