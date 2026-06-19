// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/LatencyReporterTest.cpp — acceptance tests for the constant-PDC
// LatencyReporter (task 105), compiled into the JUCE-linked mw101_plugin_tests
// target. Every test-case display name begins with the `latency` tag so the
// `-R latency` ctest selector matches >= 1 [docs/design/11 §8.2].
//
// Covers each ADR-017 / docs/design/09 §8.3 acceptance criterion:
//   * Reported latency is INVARIANT to FX bypass, Quality tier, and build-to-build
//     [§8.3; ADR-017 L4-L5, L7-L8, L11].
//   * Per-voice IIR zone [L1] + FX Drive 2x OS [L2] CONTRIBUTE; FX Delay/Chorus
//     musical time does NOT [L3] (the sum equals exactly the two contributors).
//   * Latency sized in prepare; padding lines preallocated; the worst-case compute
//     and the padding READ perform ZERO heap allocation on a representative process
//     (a self-contained alloc sentinel) [§8.3; ADR-017 L10].
//
// The per-voice IIR-zone group-delay constant (mw::cal::latency::
// kVoiceZoneGroupDelaySamples) is independently RE-DERIVED here from the frozen
// mw::cal::osiir coefficients via the same energy-weighted round-trip impulse-response
// centroid the FX oversampler uses, and asserted equal to the declared constant — so
// the number cannot silently drift from the coefficients it is derived from
// [ADR-017 L1, L11].

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "latency/LatencyReporter.h"                 // unit under test
#include "calibration/FxOversampler2xConstants.h"    // mw::cal::fxos::kReportedLatencySamples
#include "calibration/LatencyReporterConstants.h"    // mw::cal::latency::kVoiceZoneGroupDelaySamples
#include "calibration/OversamplerConstants.h"        // mw::cal::osiir frozen coefficients (re-derivation)

// ============================================================================
// A self-contained, header-local allocation sentinel.
//
// mw101_plugin_tests does NOT link tests/invariants/AudioThreadGuard.cpp (that sentinel
// lives only in mw101_tests). We provide a minimal global operator new/delete override
// here, gated by a thread-local armed counter, so the RT no-alloc criterion can be
// asserted in this JUCE-linked target without depending on the core sentinel. The
// override always services the allocation (so the program stays correct) and merely
// COUNTS allocations taken inside an armed scope; the test reads the count.
// ============================================================================
namespace {

std::atomic<bool>          gArmed{false};
std::atomic<std::size_t>   gAllocCount{0};

struct ScopedAllocSentinel {
    ScopedAllocSentinel() noexcept  { gAllocCount.store(0, std::memory_order_relaxed); gArmed.store(true,  std::memory_order_relaxed); }
    ~ScopedAllocSentinel() noexcept { gArmed.store(false, std::memory_order_relaxed); }
    [[nodiscard]] std::size_t count() const noexcept { return gAllocCount.load(std::memory_order_relaxed); }
};

void* sentinelAlloc(std::size_t n) {
    if (gArmed.load(std::memory_order_relaxed))
        gAllocCount.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n == 0 ? 1 : n);
}

// --- The per-voice IIR halfband round-trip group delay, RE-DERIVED from the frozen
// mw::cal::osiir coefficients exactly as FxOversampler2x::measureRoundTripLatency does
// (crossed branches, energy-weighted centroid, rounded). Used to prove the declared
// kVoiceZoneGroupDelaySamples is not a magic number [ADR-017 L1, L11].
struct AllpassSection {
    double a = 0.0, x1 = 0.0, y1 = 0.0;
    double process(double x) noexcept { const double y = a * (x - y1) + x1; x1 = x; y1 = y; return y; }
};
struct Branch {
    std::array<AllpassSection, mw::cal::osiir::kSectionsPerBranch> s{};
    void set(const std::array<double, mw::cal::osiir::kSectionsPerBranch>& c) noexcept {
        for (std::size_t i = 0; i < c.size(); ++i) s[i].a = c[i];
    }
    double process(double x) noexcept { for (auto& sec : s) x = sec.process(x); return x; }
};

int measureVoiceZoneRoundTripLatency() {
    Branch up0, up1, down0, down1;
    up0.set(mw::cal::osiir::kBranch0Coeffs);
    up1.set(mw::cal::osiir::kBranch1Coeffs);
    down0.set(mw::cal::osiir::kBranch0Coeffs);
    down1.set(mw::cal::osiir::kBranch1Coeffs);

    constexpr int kImpLen = 2048;
    double num = 0.0, den = 0.0;
    for (int n = 0; n < kImpLen; ++n) {
        const double x  = (n == 0) ? 1.0 : 0.0;
        const double h0 = up0.process(x);              // scratch[0]
        const double h1 = up1.process(x);              // scratch[1]
        const double y  = 0.5 * (down1.process(h0)     // CROSSED pairing (Oversampler.h)
                               + down0.process(h1));
        const double e  = y * y;
        num += e * static_cast<double>(n);
        den += e;
    }
    if (den <= 0.0) return 0;
    const long r = std::lround(num / den);
    return r < 0 ? 0 : static_cast<int>(r);
}

} // namespace

// The global override is shared with the whole TU; it is the ONLY definition of
// operator new in this target (mw101_plugin_tests neither links AudioThreadGuard.cpp
// nor any other override).
void* operator new(std::size_t n) { void* p = sentinelAlloc(n); if (!p) throw std::bad_alloc{}; return p; }
void* operator new[](std::size_t n) { void* p = sentinelAlloc(n); if (!p) throw std::bad_alloc{}; return p; }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return sentinelAlloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return sentinelAlloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

// ============================================================================
// Acceptance criterion 1: the per-voice IIR zone [L1] and the FX Drive 2x OS [L2]
// contribute; FX Delay/Chorus musical time does NOT [L3].
// ============================================================================
TEST_CASE("latency reporter sums per-voice IIR zone and FX Drive 2x OS only", "[latency]")
{
    // The declared per-voice IIR-zone group delay must match what the frozen osiir
    // coefficients actually produce (no magic number) [ADR-017 L1, L11].
    REQUIRE(measureVoiceZoneRoundTripLatency() == mw::cal::latency::kVoiceZoneGroupDelaySamples);

    mw::plugin::LatencyReporter reporter;
    const int reported = reporter.computeWorstCaseLatency(48000.0);

    // == exactly the two contributors, no more (Delay/Chorus excluded, L3).
    const int expected = mw::cal::latency::kVoiceZoneGroupDelaySamples   // L1
                       + mw::cal::fxos::kReportedLatencySamples;          // L2
    REQUIRE(reported == expected);

    // Each contributor is genuinely present and nonzero (so the host compensates both).
    REQUIRE(mw::cal::latency::kVoiceZoneGroupDelaySamples > 0);          // L1
    REQUIRE(mw::cal::fxos::kReportedLatencySamples > 0);                 // L2
    REQUIRE(reported > mw::cal::fxos::kReportedLatencySamples);          // zone term added
    REQUIRE(reported > mw::cal::latency::kVoiceZoneGroupDelaySamples);   // FX term added
}

// ============================================================================
// Acceptance criterion 2: reported latency is INVARIANT to FX bypass, Quality tier,
// and build-to-build [L4-L5, L7-L8, L11].
// ============================================================================
TEST_CASE("latency reporter value is invariant to FX bypass and quality tier", "[latency]")
{
    mw::plugin::LatencyReporter reporter;

    // The reporter holds NO FX-bypass / Quality state — the value is structurally
    // constant. Probe across the full blessed rate set AND across many calls to prove
    // it never moves (build-to-build stability is the fixed-constant property itself).
    const int baseline = reporter.computeWorstCaseLatency(48000.0);

    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        REQUIRE(reporter.computeWorstCaseLatency(sr) == baseline);

    // Repeated reads are identical (no recompute / no drift).
    for (int i = 0; i < 64; ++i)
        REQUIRE(reporter.computeWorstCaseLatency(48000.0) == baseline);

    // A freshly constructed reporter reports the same constant (no per-instance state
    // affects it — invariant build-to-build / instance-to-instance) [L5, L11].
    mw::plugin::LatencyReporter other;
    REQUIRE(other.computeWorstCaseLatency(48000.0) == baseline);

    // The constant is pinned to its known value (a change here is a deliberate,
    // reviewed golden re-bless, not an accident) [ADR-017 L11].
    REQUIRE(baseline == 20);   // 10 (per-voice IIR zone) + 10 (FX Drive 2x OS)
}

// ============================================================================
// Acceptance criterion 3a: latency sized in prepare; padding lines preallocated;
// a shorter config is delay-aligned UP TO the worst case [L5, L10].
// ============================================================================
TEST_CASE("latency reporter preallocates padding and aligns a short path up to worst case", "[latency]")
{
    constexpr int numChannels = 2;
    constexpr int blockSize   = 512;

    mw::plugin::LatencyReporter reporter;
    const int worst = reporter.computeWorstCaseLatency(48000.0);

    reporter.preparePadding(worst, numChannels);
    REQUIRE(reporter.isPrepared());
    REQUIRE(reporter.worstCaseSamples() == worst);
    REQUIRE(reporter.preparedChannels() == numChannels);

    // Drive an impulse through the FULL worst-case pad: the impulse must come out
    // delayed by exactly `worst` samples (the alignment a 0-latency config needs to be
    // padded up to the worst case) [ADR-017 L5].
    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    buffer.clear();
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.setSample(ch, 0, 1.0f);   // unit impulse at sample 0 on each channel

    reporter.padBlock(buffer.getArrayOfWritePointers(), numChannels, /*padSamples=*/worst, blockSize);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* d = buffer.getReadPointer(ch);
        REQUIRE(d[worst] == 1.0f);                    // impulse relocated to +worst
        for (int n = 0; n < blockSize; ++n)
            if (n != worst)
                REQUIRE(d[n] == 0.0f);                // nothing anywhere else
    }
}

TEST_CASE("latency reporter pad of zero is an identity pass-through", "[latency]")
{
    constexpr int numChannels = 1;
    constexpr int blockSize   = 64;

    mw::plugin::LatencyReporter reporter;
    reporter.preparePadding(reporter.computeWorstCaseLatency(48000.0), numChannels);

    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    buffer.clear();
    for (int n = 0; n < blockSize; ++n)
        buffer.setSample(0, n, static_cast<float>(n) * 0.01f);   // a ramp

    std::vector<float> expected(static_cast<std::size_t>(blockSize));
    for (int n = 0; n < blockSize; ++n)
        expected[static_cast<std::size_t>(n)] = buffer.getSample(0, n);

    // The fully-padded config (the FX/HQ path that needs no extra delay) uses pad 0:
    // a sample-identical pass-through [ADR-017 L5/L6 — FX-off bit-exact at the offset].
    reporter.padBlock(buffer.getArrayOfWritePointers(), numChannels, /*padSamples=*/0, blockSize);

    const float* d = buffer.getReadPointer(0);
    for (int n = 0; n < blockSize; ++n)
        REQUIRE(d[n] == expected[static_cast<std::size_t>(n)]);
}

// ============================================================================
// Acceptance criterion 3b: computeWorstCaseLatency and the padding READ perform
// ZERO heap allocation on a representative process (alloc sentinel) [L10].
// ============================================================================
TEST_CASE("latency reporter compute and padding read perform zero allocation", "[latency]")
{
    constexpr int numChannels = 2;
    constexpr int blockSize   = 512;

    mw::plugin::LatencyReporter reporter;
    const int worst = reporter.computeWorstCaseLatency(48000.0);
    reporter.preparePadding(worst, numChannels);   // the ONLY alloc site — OUTSIDE the armed scope

    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    buffer.clear();
    float* const* chans = buffer.getArrayOfWritePointers();

    // Representative process: read the constant + run the padding read several times,
    // at a couple of pad amounts (worst, an intermediate, and 0), exactly as the audio
    // thread would across FX/Quality changes. NOTHING here may allocate [ADR-017 L10].
    {
        ScopedAllocSentinel sentinel;
        for (int i = 0; i < 16; ++i)
        {
            volatile int rep = reporter.computeWorstCaseLatency(48000.0);
            (void) rep;
            reporter.padBlock(chans, numChannels, worst,     blockSize);
            reporter.padBlock(chans, numChannels, worst / 2, blockSize);
            reporter.padBlock(chans, numChannels, 0,         blockSize);
        }
        REQUIRE(sentinel.count() == 0u);
    }
}
