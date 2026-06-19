// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/ControlCoreDriverTest.cpp — unit tests for the ControlCore advance()
// control-tick driver, VINTAGE/MODERN poles, loop-time jitter, per-feature MODERN
// auto-engage, and the macro VINTAGE<->MODERN CV crossfade (task 071).
//
// Test-case names begin with "controlcore" so `-R controlcore` selects exactly this
// suite (the silent-pass rule). Each TEST_CASE maps to a 071 acceptance criterion
// (CC1, CC2, CC3, CC4-CC6, CC7) and to the cited docs/design/04 §7.4-§7.7 and
// ADR-005 §Contract C1-C7. No '[' appears in any display name (Catch2 would parse it
// as a tag and break `-R` selection).
//
// advance() is templated on the VoiceManager type (duck-typed on controlTick()) so it
// stays decoupled from the concrete VoiceManager (task 074). A tiny FakeVoiceManager
// test double records each control-tick invocation and its sample-counter position.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "../invariants/AudioThreadGuard.h"
#include "calibration/ControlDriverConstants.h"
#include "calibration/PitchAssemblyConstants.h"
#include "control/ControlCore.h"
#include "voice/VoiceTypes.h"

using mw::ControlCore;
using mw::VintageControlPole;
using mw::VoiceMode;

namespace {

// A minimal VoiceManager stand-in: advance() only ever calls controlTick() (§7.8),
// so the double just counts ticks. It is NOT allocation-free on its own (it grows a
// vector), so the no-alloc test uses the counting-only variant below.
struct FakeVoiceManager {
    int ticks = 0;
    void controlTick() noexcept { ++ticks; }
};

// A pure counter with no heap state, for the AudioThreadGuard no-alloc test.
struct NoAllocVoiceManager {
    int ticks = 0;
    void controlTick() noexcept { ++ticks; }
};

constexpr double kSr = 48000.0;

// Expected VINTAGE fixed-tick period in samples at kSr, from the calibration second.
int expectedVintageTickSamples(double sr) {
    return static_cast<int>(std::lround(mw::cal::control::kVintageTickSeconds * sr));
}

} // namespace

// ---------------------------------------------------------------------------
// CC3 (default) — out-of-box runs MODERN-SMOOTH with jitter OFF (§7.5; ADR-016 R-1).
// ---------------------------------------------------------------------------

TEST_CASE("controlcore default pole is MODERN and jitter is OFF out of box", "[controlcore]") {
    ControlCore cc;  // default-constructed == INIT / out-of-box state.
    // ADR-016 R-1: shipped default control pole is MODERN-SMOOTH; jitter OFF (ADR-005 C1).
    REQUIRE(cc.macroPole() == VintageControlPole::Modern);
    REQUIRE(cc.jitterEnabled() == false);
}

TEST_CASE("controlcore MODERN fires the clean fixed sub-block tick", "[controlcore]") {
    ControlCore cc;
    cc.prepare(kSr);  // default pole is Modern.

    // §7.5 / CC3: the MODERN tick period is the (PI) clean sub-block tick (16-32 smp).
    const int period = mw::cal::control::kModernTickSamples;
    REQUIRE(period >= mw::cal::control::kModernTickMinSamples);
    REQUIRE(period <= mw::cal::control::kModernTickMaxSamples);
    REQUIRE(cc.samplesToNextTick() == period);

    // Advancing exactly N periods fires exactly N control ticks (clean sub-block tick).
    FakeVoiceManager vm;
    cc.advance(period * 7, vm);
    REQUIRE(vm.ticks == 7);
    REQUIRE(cc.tickCount() == 7);
    REQUIRE(cc.sampleCounter() == period * 7);
}

// ---------------------------------------------------------------------------
// CC1 — Mono+VINTAGE+jitter-OFF fires a fixed ~2 ms tick, deterministic/bit-exact.
// ---------------------------------------------------------------------------

TEST_CASE("controlcore VINTAGE jitter-off fires a fixed two ms tick", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);
    cc.setJitterEnabled(false);
    cc.prepare(kSr);

    const int period = expectedVintageTickSamples(kSr);
    REQUIRE(period == 96);  // round(0.002 * 48000) == 96 samples == ~2 ms
    REQUIRE(cc.samplesToNextTick() == period);

    FakeVoiceManager vm;
    cc.advance(period * 5, vm);
    REQUIRE(vm.ticks == 5);
    REQUIRE(cc.tickCount() == 5);
}

TEST_CASE("controlcore VINTAGE jitter-off tick stream is deterministic across runs", "[controlcore]") {
    // CC1: the fixed-tick jitter-OFF VINTAGE config is the bit-exact reference — two
    // identical setups must fire identically (same boundaries, same tick count).
    auto run = [](std::vector<long long>& tickPositions) {
        struct Recorder {
            ControlCore* core = nullptr;
            std::vector<long long>* out = nullptr;
            void controlTick() noexcept { out->push_back(core->sampleCounter()); }
        };
        ControlCore cc;
        cc.setPole(VintageControlPole::Vintage);
        cc.setJitterEnabled(false);
        cc.prepare(kSr);
        Recorder rec{&cc, &tickPositions};
        // Drive several uneven chunks; tick boundaries must still fall on the fixed grid.
        cc.advance(100, rec);
        cc.advance(53, rec);
        cc.advance(300, rec);
        cc.advance(443, rec);
    };
    std::vector<long long> a, b;
    run(a);
    run(b);
    REQUIRE(a == b);              // bit-identical tick positions
    REQUIRE_FALSE(a.empty());

    // Boundaries fall on the fixed ~2 ms grid (multiples of the period).
    const long long period = expectedVintageTickSamples(kSr);
    for (auto pos : a) REQUIRE(pos % period == 0);
}

TEST_CASE("controlcore tick boundaries are sample-accurate across split chunks", "[controlcore]") {
    // §7.4: the tick is driven by the sample counter inside processBlock and is
    // sample-accurate at block boundaries. Splitting one period across two chunks
    // fires the tick at the same absolute sample as one contiguous chunk.
    const int period = mw::cal::control::kModernTickSamples;

    ControlCore split;
    split.prepare(kSr);
    FakeVoiceManager vmSplit;
    split.advance(period - 3, vmSplit);  // not yet a tick
    REQUIRE(vmSplit.ticks == 0);
    split.advance(3, vmSplit);           // completes the period -> exactly one tick
    REQUIRE(vmSplit.ticks == 1);
    REQUIRE(split.sampleCounter() == period);

    ControlCore whole;
    whole.prepare(kSr);
    FakeVoiceManager vmWhole;
    whole.advance(period, vmWhole);
    REQUIRE(vmWhole.ticks == 1);
    REQUIRE(whole.sampleCounter() == period);
}

// ---------------------------------------------------------------------------
// CC4-CC6 — effectivePole auto-engages MODERN even when macro == Vintage (§7.6).
// ---------------------------------------------------------------------------

TEST_CASE("controlcore effectivePole honors VINTAGE only on the mono non-MPE path", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);

    // CC1/§7.6: mono, single-voice, no MPE, no automation => VINTAGE honored fully.
    REQUIRE(cc.effectivePole(VoiceMode::Mono, /*mpe=*/false, /*autom=*/false)
            == VintageControlPole::Vintage);
}

TEST_CASE("controlcore effectivePole auto-engages MODERN for poly or unison", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);

    // CC4: poly/unison (mode != Mono) forces MODERN even with macro == Vintage.
    REQUIRE(cc.effectivePole(VoiceMode::Unison, false, false) == VintageControlPole::Modern);
    REQUIRE(cc.effectivePole(VoiceMode::Poly, false, false) == VintageControlPole::Modern);
}

TEST_CASE("controlcore effectivePole auto-engages MODERN for MPE per-note pitch", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);

    // CC5: MPE-lite per-note pitch bend forces MODERN even on the mono path.
    REQUIRE(cc.effectivePole(VoiceMode::Mono, /*mpe=*/true, false) == VintageControlPole::Modern);
}

TEST_CASE("controlcore effectivePole auto-engages MODERN for sub-cent pitch automation", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);

    // CC6: sub-cent host automation of pitch forces MODERN even on the mono path.
    REQUIRE(cc.effectivePole(VoiceMode::Mono, false, /*autom=*/true) == VintageControlPole::Modern);
}

TEST_CASE("controlcore effectivePole stays MODERN when macro is MODERN regardless of features", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Modern);
    // With the macro already MODERN, every combination is MODERN.
    REQUIRE(cc.effectivePole(VoiceMode::Mono, false, false) == VintageControlPole::Modern);
    REQUIRE(cc.effectivePole(VoiceMode::Poly, true, true) == VintageControlPole::Modern);
}

// ---------------------------------------------------------------------------
// CC2 — loop-time jitter varies the tick within the 1.5-3.5 ms envelope,
//       deterministically from a seeded PRNG; jitter-off path is unchanged (§7.4).
// ---------------------------------------------------------------------------

TEST_CASE("controlcore jitter varies the VINTAGE tick within the envelope", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);
    cc.setJitterEnabled(true);
    cc.prepare(kSr);

    // Record the inter-tick spacing of many ticks.
    struct SpacingRec {
        ControlCore* core = nullptr;
        long long prev = 0;
        std::vector<long long>* spacings = nullptr;
        void controlTick() noexcept {
            const long long now = core->sampleCounter();
            spacings->push_back(now - prev);
            prev = now;
        }
    };
    std::vector<long long> spacings;
    SpacingRec rec{&cc, 0, &spacings};
    cc.advance(200000, rec);  // ~4 s of audio at 48 kHz -> many ticks

    REQUIRE(spacings.size() > 100);

    const long long lo = static_cast<long long>(std::lround(mw::cal::control::kVintageJitterMinSeconds * kSr));
    const long long hi = static_cast<long long>(std::lround(mw::cal::control::kVintageJitterMaxSeconds * kSr));
    REQUIRE(lo == 72);   // round(0.0015 * 48000)
    REQUIRE(hi == 168);  // round(0.0035 * 48000)

    bool sawVariation = false;
    for (auto s : spacings) {
        REQUIRE(s >= lo);          // never below the 1.5 ms envelope floor
        REQUIRE(s <= hi);          // never above the 3.5 ms envelope ceiling
        if (s != spacings.front()) sawVariation = true;
    }
    REQUIRE(sawVariation);          // the tick genuinely VARIES (not a constant period)
}

TEST_CASE("controlcore jitter is deterministic from the seed", "[controlcore]") {
    // CC2: jitter rides a seeded PRNG, so two identical jitter-ON setups produce the
    // identical tick stream (deterministic; never wall-clock).
    auto run = [](std::vector<long long>& spacings) {
        struct SpacingRec {
            ControlCore* core = nullptr;
            long long prev = 0;
            std::vector<long long>* out = nullptr;
            void controlTick() noexcept {
                const long long now = core->sampleCounter();
                out->push_back(now - prev);
                prev = now;
            }
        };
        ControlCore cc;
        cc.setPole(VintageControlPole::Vintage);
        cc.setJitterEnabled(true);
        cc.prepare(kSr);
        SpacingRec rec{&cc, 0, &spacings};
        cc.advance(50000, rec);
    };
    std::vector<long long> a, b;
    run(a);
    run(b);
    REQUIRE(a == b);
    REQUIRE_FALSE(a.empty());
}

TEST_CASE("controlcore jitter-off path is unchanged when jitter toggle never set", "[controlcore]") {
    // CC2 contract: the jitter-OFF path must be bit-identical to the fixed-tick
    // reference (the PRNG is only consumed in the jitter-ON branch).
    auto runFixed = [](std::vector<long long>& spacings) {
        struct SpacingRec {
            ControlCore* core = nullptr;
            long long prev = 0;
            std::vector<long long>* out = nullptr;
            void controlTick() noexcept {
                const long long now = core->sampleCounter();
                out->push_back(now - prev);
                prev = now;
            }
        };
        ControlCore cc;
        cc.setPole(VintageControlPole::Vintage);
        cc.setJitterEnabled(false);
        cc.prepare(kSr);
        SpacingRec rec{&cc, 0, &spacings};
        cc.advance(50000, rec);
    };
    std::vector<long long> a, b;
    runFixed(a);
    runFixed(b);
    REQUIRE(a == b);
    REQUIRE_FALSE(a.empty());

    // Every spacing is exactly the fixed period (no jitter when the toggle is off).
    const long long period = expectedVintageTickSamples(kSr);
    for (auto s : a) REQUIRE(s == period);
}

// ---------------------------------------------------------------------------
// CC7 — automating the macro crossfades both CV branches with no zipper and zero
//       allocation; advance() is noexcept / alloc-free / lock-free (§7.7).
// ---------------------------------------------------------------------------

TEST_CASE("controlcore blendedPitchVolts equals VINTAGE quantize at full blend", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);
    cc.setJitterEnabled(false);
    cc.prepare(kSr);
    // The blend starts AT the macro pole (Vintage == 1) after prepare (no transient).
    REQUIRE(cc.crossfadeBlend() == Catch::Approx(1.0f));

    // At full VINTAGE, the CV is the 6-bit-quantized counts->volts: an integer count
    // is exact, a fractional count snaps to the nearest count (stair-step).
    const float volts24 = cc.blendedPitchVolts(24.0f);          // 24 counts == 2 V
    REQUIRE(volts24 == Catch::Approx(2.0f).margin(1e-6));
    const float voltsFrac = cc.blendedPitchVolts(24.4f);        // snaps to 24 counts
    REQUIRE(voltsFrac == Catch::Approx(2.0f).margin(1e-6));      // quantized, not 24.4/12
}

TEST_CASE("controlcore blendedPitchVolts equals MODERN continuous float at zero blend", "[controlcore]") {
    ControlCore cc;
    cc.setPole(VintageControlPole::Modern);  // blend starts at 0 after prepare
    cc.prepare(kSr);
    REQUIRE(cc.crossfadeBlend() == Catch::Approx(0.0f));

    // At MODERN, a fractional count is carried continuously (no quantize): 24.4/12 V.
    const float voltsFrac = cc.blendedPitchVolts(24.4f);
    REQUIRE(voltsFrac == Catch::Approx(24.4f / 12.0f).margin(1e-6));
}

TEST_CASE("controlcore automating the macro crossfades without a zipper", "[controlcore]") {
    // CC7: automate VINTAGE -> MODERN; the blend must slew monotonically toward the
    // target (no instantaneous jump == no zipper), reach the target, and the resulting
    // CV for a fractional pitch must move smoothly from quantized to continuous.
    ControlCore cc;
    cc.setPole(VintageControlPole::Vintage);
    cc.setJitterEnabled(false);
    cc.prepare(kSr);
    REQUIRE(cc.crossfadeBlend() == Catch::Approx(1.0f));

    // Now automate the macro to MODERN. The first advance must NOT snap the blend.
    cc.setPole(VintageControlPole::Modern);

    struct BlendRec {
        ControlCore* core = nullptr;
        std::vector<float>* blends = nullptr;
        std::vector<float>* volts = nullptr;
        void controlTick() noexcept {
            blends->push_back(core->crossfadeBlend());
            volts->push_back(core->blendedPitchVolts(24.4f));  // a fractional pitch
        }
    };
    std::vector<float> blends, volts;
    BlendRec rec{&cc, &blends, &volts};

    // Drive enough ticks for the ~10 ms crossfade to complete (modern tick == 24 smp,
    // so ~10 ms is ~20 ticks at 48 kHz; advance well past that).
    cc.advance(mw::cal::control::kModernTickSamples * 400, rec);

    REQUIRE(blends.size() > 50);

    // Monotonically decreasing toward 0 (VINTAGE 1 -> MODERN 0), no jump > one step.
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < blends.size(); ++i) {
        REQUIRE(blends[i] <= blends[i - 1] + 1e-7f);  // non-increasing
        maxStep = std::max(maxStep, std::fabs(blends[i] - blends[i - 1]));
    }
    // The largest single-tick blend step is well under a full swing (no zipper).
    REQUIRE(maxStep < 0.3f);

    // It reaches the MODERN target by the end.
    REQUIRE(blends.back() == Catch::Approx(0.0f).margin(1e-5));

    // The CV ends at the continuous-float MODERN value for 24.4 counts.
    REQUIRE(volts.back() == Catch::Approx(24.4f / 12.0f).margin(1e-5));
    // It started near the quantized VINTAGE value (24 counts == 2 V) on the first tick.
    REQUIRE(volts.front() == Catch::Approx(2.0f).margin(0.02));
}

TEST_CASE("controlcore advance is allocation-free under the audio-thread guard", "[controlcore][rt]") {
    // CC7 / ADR-005 invariants / ADR-001 C3-C4: advance() does no heap allocation and
    // takes no lock on the audio thread. Exercise every pole + jitter + crossfade path
    // under the armed RT sentinel.
    ControlCore vintage;
    vintage.setPole(VintageControlPole::Vintage);
    vintage.setJitterEnabled(true);
    vintage.prepare(kSr);

    ControlCore modern;
    modern.prepare(kSr);

    NoAllocVoiceManager vm;

    mw::test::AudioThreadGuard guard;
    guard.arm();
    // VINTAGE jitter-ON path (consumes the PRNG, draws periods).
    vintage.advance(40000, vm);
    // Automate VINTAGE -> MODERN mid-stream (the crossfade hot path).
    vintage.setPole(VintageControlPole::Modern);
    vintage.advance(20000, vm);
    // The default MODERN path + a blended CV read.
    modern.advance(20000, vm);
    (void) modern.blendedPitchVolts(31.7f);
    (void) modern.effectivePole(VoiceMode::Poly, true, true);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
    REQUIRE(vm.ticks > 0);
}

// ---------------------------------------------------------------------------
// noexcept hot-path contract (RT4 / ADR-001 C5).
// ---------------------------------------------------------------------------

TEST_CASE("controlcore driver methods are noexcept", "[controlcore]") {
    ControlCore cc;
    FakeVoiceManager vm;
    STATIC_REQUIRE(noexcept(cc.prepare(48000.0)));
    STATIC_REQUIRE(noexcept(cc.setPole(VintageControlPole::Vintage)));
    STATIC_REQUIRE(noexcept(cc.setJitterEnabled(true)));
    STATIC_REQUIRE(noexcept(cc.advance(64, vm)));
    STATIC_REQUIRE(noexcept(cc.effectivePole(VoiceMode::Mono, false, false)));
    STATIC_REQUIRE(noexcept(cc.blendedPitchVolts(24.0f)));
    STATIC_REQUIRE(std::is_same_v<decltype(cc.effectivePole(VoiceMode::Mono, false, false)),
                                  VintageControlPole>);
}
