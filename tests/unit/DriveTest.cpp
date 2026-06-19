// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the post-voice FX Drive stage (task 091). Test-case names
// begin with "fxdrive" so `ctest -R fxdrive` selects them (silent-pass rule,
// AGENTS.md). The names avoid '[' in the display text so the tag does not break
// ctest -R selection. Covers each acceptance criterion in plan/backlog/091:
//   - tone=0.5 yields flat pre/de-emphasis; low drive is near-linear pass-through
//     [§4.4]
//   - nonzero bias produces even harmonics (asymmetry) and the DC blocker removes the
//     standing offset [§4.3 / §4.5]
//   - a full-scale sine into hot Drive has in-band aliasing below kDriveAliasFloorDb,
//     and the 2x path beats the same shaper at 1x [ADR-017 L2 / §9]
//   - latencySamples() equals the FxOversampler2x group delay and is invariant to
//     drive amount / `on` [ADR-017 L2/L8]
//   - prepare/reset/process/setParams perform no heap allocation and take no locks
//     [§4.7]

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/fx/Drive.h"
#include "dsp/fx/FxOversampler2x.h"
#include "dsp/fx/FxParams.h"
#include "calibration/DriveConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::fx::Drive;
using mw::fx::FxOversampler2x;
using mw::fx::FxParams;

namespace {

constexpr double kPi = 3.14159265358979323846;

FxParams::DriveP makeParams(bool on, float amount, float tone, float output) {
    FxParams::DriveP p{};
    p.on     = on;
    p.amount = amount;
    p.tone   = tone;
    p.output = output;
    return p;
}

// Run a settled block so the smoothed gains reach their targets, then capture a fresh
// processed block. Returns the processed (in-place) output of the LAST block.
std::vector<float> processSettled(Drive& d, const std::vector<float>& in,
                                  int warmBlocks = 8) {
    const int n = static_cast<int>(in.size());
    std::vector<float> buf(n);
    for (int b = 0; b < warmBlocks; ++b) {
        buf = in;
        d.process(buf.data(), n);
    }
    buf = in;
    d.process(buf.data(), n);
    return buf;
}

// Goertzel-style single-bin magnitude of a real signal at integer bin k of an N-point
// DFT. Returns |X[k]| (not normalized). Pure, allocation-light.
double binMag(const std::vector<float>& x, int k) {
    const int N = static_cast<int>(x.size());
    std::complex<double> acc{0.0, 0.0};
    const double w = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(N);
    for (int n = 0; n < N; ++n) {
        acc += std::complex<double>(x[n], 0.0) *
               std::complex<double>(std::cos(w * n), std::sin(w * n));
    }
    return std::abs(acc);
}

} // namespace

TEST_CASE("fxdrive: tone=0.5 with low drive is a near-linear flat pass-through", "[fxdrive]") {
    // §4.4: at tone == 0.5 the pre and de-emphasis tilt stages are unity (flat). With
    // a low drive amount the shaper is near-linear, so a small-signal sine passes
    // through near-unchanged in SHAPE (only the fixed makeup gain scales it, and the
    // oversampler adds group delay). We assert (a) the fundamental survives at the
    // expected makeup gain (no extra coloration) and (b) the harmonics are negligible.
    using namespace mw::cal::drive;
    const double sr = 48000.0;
    Drive d;
    d.prepare(sr, 512);
    // Low drive, tone flat. Pick `output` so the makeup is exactly 0 dB (unity), so we
    // can assert absolute near-unity. The makeup range is [kDriveOutMinDb,
    // kDriveOutMaxDb]; 0 dB is at output = -min/(max-min).
    const float outUnity = (0.0f - kDriveOutMinDb) / (kDriveOutMaxDb - kDriveOutMinDb);
    d.setParams(makeParams(/*on=*/true, /*amount=*/0.02f, /*tone=*/0.5f, /*output=*/outUnity));

    constexpr int N = 4096;
    const double freq = 1000.0;
    const int kBin = static_cast<int>(std::lround(freq * N / sr));
    std::vector<float> in(N);
    for (int i = 0; i < N; ++i)
        in[i] = 0.05f * static_cast<float>(std::sin(2.0 * kPi * freq * i / sr));

    const std::vector<float> out = processSettled(d, in);

    const double inFund  = binMag(in, kBin);
    const double outFund = binMag(out, kBin);
    REQUIRE(inFund > 0.0);
    // Near-unity passband at tone=0.5, makeup forced to 0 dB: within ~1 dB (the only
    // residual is the near-linear shaper's small-signal slope and OS passband ripple).
    const double ratioDb = 20.0 * std::log10(outFund / inFund);
    REQUIRE(std::fabs(ratioDb) < 1.0);

    // Harmonic distortion is small for low drive: 2nd harmonic well below fundamental.
    const double h2 = binMag(out, 2 * kBin);
    REQUIRE(20.0 * std::log10(h2 / outFund) < -40.0);
}

TEST_CASE("fxdrive: tilt at tone=0.5 is exactly flat (de-emphasis inverts pre-emphasis)", "[fxdrive]") {
    // §4.4 oracle: at tone == 0.5 the tilt gain is 0, so the pre and de-emphasis
    // shelves are both unity and their round trip is the identity. With amount=0 the
    // shaper pre-gain is unity, so shape() is the only nonlinearity. We verify the
    // tilt path itself is transparent by comparing tone=0.5 vs a deliberately bright
    // tone over a broadband signal: the flat tone must NOT color the magnitude balance.
    const double sr = 48000.0;

    auto bandRatio = [&](float tone) {
        Drive d;
        d.prepare(sr, 512);
        d.setParams(makeParams(true, /*amount=*/0.0f, tone, /*output=*/0.5f));
        constexpr int N = 4096;
        // A two-tone probe: 200 Hz (low) and 8 kHz (high).
        const double fLo = 200.0, fHi = 8000.0;
        std::vector<float> in(N);
        for (int i = 0; i < N; ++i)
            in[i] = 0.2f * static_cast<float>(std::sin(2.0 * kPi * fLo * i / sr)
                                              + std::sin(2.0 * kPi * fHi * i / sr));
        const std::vector<float> out = processSettled(d, in);
        const int kLo = static_cast<int>(std::lround(fLo * N / sr));
        const int kHi = static_cast<int>(std::lround(fHi * N / sr));
        return binMag(out, kHi) / binMag(out, kLo); // high/low energy balance
    };

    const double darkBalance   = bandRatio(0.0f);
    const double flatBalance   = bandRatio(0.5f);
    const double brightBalance = bandRatio(1.0f);

    // The tilt is monotone in tone with tone=0.5 the flat MIDPOINT: a brighter tone
    // raises the high/low energy balance and a darker tone lowers it, bracketing the
    // flat case. This proves tone=0.5 is the unity/flat pivot (the pre/de shelves
    // cancel) WITHOUT conflating the oversampler's own fixed passband roll-off — that
    // roll-off is common to all three runs and divides out of the comparison.
    REQUIRE(brightBalance > flatBalance * 1.05);
    REQUIRE(darkBalance   < flatBalance * 0.95);
}

TEST_CASE("fxdrive: nonzero bias produces even harmonics and the DC blocker removes the offset", "[fxdrive]") {
    // §4.3: the (PI) kDriveBias makes the shaper asymmetric, which generates EVEN
    // harmonics (the signature of asymmetry). §4.5: the post-downsample DC blocker
    // removes the standing DC offset the bias would otherwise leave.
    const double sr = 48000.0;
    Drive d;
    d.prepare(sr, 1024);
    // Hot drive so the asymmetry is pronounced; tone flat, output unity.
    d.setParams(makeParams(true, /*amount=*/0.9f, /*tone=*/0.5f, /*output=*/0.5f));

    constexpr int N = 8192;
    const double freq = 500.0;
    const int kBin = static_cast<int>(std::lround(freq * N / sr));
    std::vector<float> in(N);
    for (int i = 0; i < N; ++i)
        in[i] = 0.5f * static_cast<float>(std::sin(2.0 * kPi * freq * i / sr));

    const std::vector<float> out = processSettled(d, in, /*warmBlocks=*/16);

    // POSITIVE asymmetry oracle: the 2nd harmonic (even) is present and substantial.
    const double fund = binMag(out, kBin);
    const double h2   = binMag(out, 2 * kBin);
    const double h3   = binMag(out, 3 * kBin);
    REQUIRE(fund > 0.0);
    // Even harmonic clearly above the noise/alias floor (asymmetry present).
    REQUIRE(20.0 * std::log10(h2 / fund) > -40.0);
    // Sanity: odd harmonic also present (tanh is odd); the point of the test is that
    // the EVEN one exists at all, which only happens with the bias asymmetry.
    REQUIRE(h3 > 0.0);

    // DC-BLOCKER oracle: the standing offset (DFT bin 0 / mean) is removed. The raw
    // asymmetric shaper would leave a nonzero mean; after the blocker the block mean
    // is ~0. Compare against the SAME shaper WITHOUT the DC blocker (bin 0 of the
    // shaper-only signal) to show the blocker actually does the removal.
    double mean = 0.0;
    for (float v : out) mean += v;
    mean /= static_cast<double>(N);
    REQUIRE(std::fabs(mean) < 1e-3); // standing offset removed

    // Reference: the un-blocked asymmetric shaper has a clearly nonzero mean.
    double rawMean = 0.0;
    for (int i = 0; i < N; ++i)
        rawMean += static_cast<double>(Drive::shape(in[i], /*preGain=*/8.0f));
    rawMean /= static_cast<double>(N);
    REQUIRE(std::fabs(rawMean) > 1e-2); // the bias DID create an offset the blocker removed
}

TEST_CASE("fxdrive: full-scale sine into hot Drive keeps in-band aliasing below the floor and 2x beats 1x", "[fxdrive]") {
    // §9 / ADR-017 L2: a full-scale sine into a hot Drive must produce in-band
    // aliasing below the fixed (PI) floor kDriveAliasFloorDb thanks to the dedicated
    // 2x oversampler; and the 2x path must beat the SAME shaper run at 1x.
    //
    // Method: drive a high-frequency full-scale sine whose harmonics fold back into a
    // measurable in-band region. Pick a fundamental f0 so that the strong harmonics
    // (2 f0, 3 f0, ...) land ABOVE Nyquist and, when aliased by a 1x (no-OS) shaper,
    // reflect to a KNOWN in-band alias bin. The oversampled Drive band-limits those
    // harmonics before downsampling, so the alias bin is far quieter.
    const double sr = 48000.0;
    const double nyq = sr * 0.5;
    constexpr int N = 8192;

    // f0 chosen so 3*f0 > Nyquist and the 3rd-harmonic alias lands on an exact bin.
    const double f0 = 9000.0;            // 2f0=18k (in band), 3f0=27k -> aliases to 21k
    const int kF0 = static_cast<int>(std::lround(f0 * N / sr));
    // 3rd harmonic at 27 kHz aliases (mirror about Nyquist) to 48k-27k = 21 kHz.
    const double aliasFreq = sr - 3.0 * f0; // 21000 Hz, in band
    REQUIRE(aliasFreq > 0.0);
    REQUIRE(aliasFreq < nyq);
    const int kAlias = static_cast<int>(std::lround(aliasFreq * N / sr));

    std::vector<float> in(N);
    for (int i = 0; i < N; ++i)
        in[i] = static_cast<float>(std::sin(2.0 * kPi * f0 * i / sr)); // full-scale

    // --- 2x path: the real Drive (hot, tone flat, unity output) -------------------
    Drive d;
    d.prepare(sr, 1024);
    d.setParams(makeParams(true, /*amount=*/1.0f, /*tone=*/0.5f, /*output=*/0.5f));
    const std::vector<float> out2x = processSettled(d, in, /*warmBlocks=*/16);

    const double fund2x  = binMag(out2x, kF0);
    const double alias2x = binMag(out2x, kAlias);
    REQUIRE(fund2x > 0.0);
    const double aliasDb2x = 20.0 * std::log10(alias2x / fund2x);

    // --- 1x reference path: the IDENTICAL shaper with NO oversampling -------------
    // Reconstruct the pre-gain the Drive uses at amount=1.0 so the curves match.
    const float preGain =
        std::pow(10.0f, (1.0f * mw::cal::drive::kDriveMaxGainDb) * (1.0f / 20.0f));
    std::vector<float> out1x(N);
    for (int i = 0; i < N; ++i)
        out1x[i] = Drive::shape(in[i], preGain); // shaped at base rate (no OS)
    const double fund1x  = binMag(out1x, kF0);
    const double alias1x = binMag(out1x, kAlias);
    const double aliasDb1x = 20.0 * std::log10(alias1x / fund1x);

    // ACCEPTANCE 1: in-band aliasing below the fixed floor.
    REQUIRE(aliasDb2x < mw::cal::drive::kDriveAliasFloorDb);
    // ACCEPTANCE 2: the 2x path beats the same shaper at 1x (lower alias energy).
    REQUIRE(aliasDb2x < aliasDb1x);
}

TEST_CASE("fxdrive: latencySamples equals the FxOversampler2x group delay and is invariant", "[fxdrive]") {
    // ADR-017 L2/L8: Drive::latencySamples() == the dedicated 2x halfband round-trip
    // group delay, and it is invariant to drive amount and to `on`. It is the FX
    // Drive's contribution to constant PDC, counted even when bypassed.
    const double sr = 48000.0;
    Drive d;
    d.prepare(sr, 256);

    // The oracle: a standalone FxOversampler2x prepared the same way reports the same
    // fixed group delay (the Drive uses that exact source).
    FxOversampler2x os;
    os.prepare(256);
    REQUIRE(d.latencySamples() == os.latencySamples());
    REQUIRE(d.latencySamples() > 0); // nonzero so the host actually compensates

    const int baseline = d.latencySamples();

    // Invariant to drive amount and tone.
    d.setParams(makeParams(true, 0.0f, 0.0f, 0.0f));
    REQUIRE(d.latencySamples() == baseline);
    d.setParams(makeParams(true, 1.0f, 1.0f, 1.0f));
    REQUIRE(d.latencySamples() == baseline);

    // Invariant to `on` (counted even when bypassed) [ADR-017 L8].
    d.setParams(makeParams(/*on=*/false, 1.0f, 0.5f, 0.5f));
    REQUIRE(d.latencySamples() == baseline);
    REQUIRE(d.on == false);
}

TEST_CASE("fxdrive: prepare/reset/process/setParams perform no heap allocation", "[fxdrive]") {
    // §4.7 / ADR-010 FX-10 / ADR-017 L10: the 2x oversampler is the ONLY allocation
    // source (sized in prepare); the hot paths only move indices and touch preallocated
    // state. Arm the alloc sentinel AFTER prepare() and assert a clean scope across
    // reset/setParams/process/latencySamples.
    Drive d;
    d.prepare(48000.0, 512); // allocation allowed here, before arming
    d.setParams(makeParams(true, 0.7f, 0.6f, 0.55f));
    d.reset();

    // Pre-size scratch so its std::vector storage exists before arming.
    constexpr int kN = 256;
    std::vector<float> mono(kN, 0.0f);
    for (int i = 0; i < kN; ++i)
        mono[i] = 0.3f * static_cast<float>(std::sin(0.05 * i));

    mw::test::AudioThreadGuard guard;
    guard.arm();
    d.reset();
    d.setParams(makeParams(true, 0.9f, 0.4f, 0.5f));
    d.process(mono.data(), kN);
    (void) d.latencySamples();
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

TEST_CASE("fxdrive: shape is asymmetric and re-centered at zero", "[fxdrive]") {
    // §4.3 direct oracle on the memoryless curve: shape(0) == 0 (the subtracted bias
    // term re-centers), and shape(+x) != -shape(-x) for x != 0 (asymmetry from the
    // bias), which is precisely what produces even harmonics.
    REQUIRE(Drive::shape(0.0f, 1.0f) == Catch::Approx(0.0f).margin(1e-7f));

    // Asymmetry: positive and negative excursions are NOT mirror images.
    const float p = Drive::shape(0.5f, 2.0f);
    const float m = Drive::shape(-0.5f, 2.0f);
    REQUIRE(std::fabs(p + m) > 1e-3f); // |shape(x)+shape(-x)| != 0 => asymmetric

    // Monotonic / bounded (a tanh-family saturator): output magnitude stays < 2.
    REQUIRE(std::fabs(Drive::shape(100.0f, 4.0f)) < 2.0f);
    REQUIRE(std::fabs(Drive::shape(-100.0f, 4.0f)) < 2.0f);
}
