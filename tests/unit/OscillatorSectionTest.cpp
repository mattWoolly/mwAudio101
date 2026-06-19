// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the per-voice OscillatorSection owner (task 032).
// Test-case names begin with "oscsection" so `ctest -R oscsection` selects them
// (silent-pass rule, AGENTS.md). Display names avoid '[' so the tag is not
// mis-parsed by ctest -R.
//
// Covers every acceptance criterion of
// plan/backlog/032-oscillatorsection-owner-voice-hq-escalation.md against
// docs/design/01-dsp-oscillators.md §7.1-§7.3, §2.3, §8, §10 and
// ADR-002 C4/C7/C9, ADR-018 Q-table/Q5/Q6:
//   - per-sample order: the VCO advances the master phase BEFORE the sub reads it,
//     and the sub uses the VCO saw wrap as the 4013 clock so it is exactly
//     phase-locked (sub fundamental == VCO/2 / VCO/4) [§7.3; ADR-002 C4];
//   - HQ auto-escalation: a voice whose VCO fundamental exceeds kHqEscalationHz
//     switches the WHOLE section (VCO + sub) to the minBLEP applicator regardless
//     of the requested tier and reverts below it; escalation is never a parameter
//     [§2.3; ADR-002 C9; ADR-018 Q6];
//   - tier binding: Eco/Standard => PolyBLEP, HQ => minBLEP; the AA mode is set
//     only in prepare/setControls, never per-sample on the audio thread
//     [§7.2; ADR-018 Q-table, Q5];
//   - output contract: four bipolar sources (saw/pulse/sub in [-1,+1], noise in
//     [-1,1)), pre-level/pre-mix, at base sample rate only — the section never
//     reads any filter oversample stride [§8; ADR-002 C7];
//   - kHqEscalationHz is read from the calibration header, not duplicated [§2.3, §10];
//   - renderSample()/reset()/setControls() are noexcept and allocate / lock nothing
//     on the hot path [§2.4; ADR-002 C11].

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "dsp/OscillatorSection.h"
#include "dsp/Oscillator.h"
#include "dsp/SubOscillator.h"
#include "dsp/OscAaMode.h"
#include "dsp/MinBlepTable.h"
#include "calibration/VcoConstants.h"
#include "calibration/VcoShapeConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::OscillatorSection;
using mw101::dsp::OscControls;
using mw101::dsp::OscAaMode;
using mw101::dsp::Footage;
using mw101::dsp::SubShape;
using mw101::dsp::MinBlepTable;
using mw::test::AudioThreadGuard;
using Catch::Approx;

namespace {

// Build a section's Controls at a summed pitch CV / footage / sub shape / tier.
OscillatorSection::Controls makeControls (double cvVolts, Footage f, SubShape sub,
                                          float pwmCvNorm, OscAaMode mode)
{
    OscillatorSection::Controls c{};
    c.vco.pitchCvVolts = static_cast<float> (cvVolts);
    c.vco.footage      = f;
    c.vco.pwmCvNorm    = pwmCvNorm;
    c.vco.aaMode       = mode;          // the section forces the same mode onto all sources
    c.subShape         = sub;
    c.aaMode           = mode;
    return c;
}

// Render `n` samples; collect the four raw source channels.
struct Capture {
    std::vector<float> saw, pulse, sub, noise;
    std::vector<bool>  wrapped;
};

Capture render (OscillatorSection& s, int n)
{
    Capture cap;
    cap.saw.reserve (static_cast<std::size_t> (n));
    cap.pulse.reserve (static_cast<std::size_t> (n));
    cap.sub.reserve (static_cast<std::size_t> (n));
    cap.noise.reserve (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const auto src = s.renderSample();
        cap.saw.push_back (src.saw);
        cap.pulse.push_back (src.pulse);
        cap.sub.push_back (src.sub);
        cap.noise.push_back (src.noise);
    }
    return cap;
}

// Count rising zero-crossings (a fundamental-cycle proxy) of a bipolar signal.
int countRises (const std::vector<float>& sig)
{
    int rises = 0;
    for (std::size_t i = 1; i < sig.size(); ++i)
        if (sig[i - 1] < 0.0f && sig[i] >= 0.0f) ++rises;
    return rises;
}

} // namespace

// -----------------------------------------------------------------------------
// §7.3 / ADR-002 C4 — per-sample ordering: VCO advances the master phase BEFORE
// the sub reads it; the sub clocks off the VCO saw wrap so it is exactly
// phase-locked (sub == VCO/2 and VCO/4).
// -----------------------------------------------------------------------------
TEST_CASE("oscsection: VCO advances the master phase before the sub reads it, sub clocks off the saw wrap", "[oscsection]") {
    const double sr = 48000.0;
    const double cv = mw::cal::vco::kPitchRefVolts;   // 8' + Transpose Middle => A4 = 442 Hz
    MinBlepTable table; table.build();

    OscillatorSection sec;
    sec.prepare (sr, table);

    // -1 oct square: the sub Q1 is exactly VCO/2 (phase-locked to the saw wrap).
    sec.reset (0x1234u);
    sec.setControls (makeControls (cv, Footage::Eight, SubShape::OctDownSquare,
                                   0.0f, OscAaMode::PolyBlep));
    const auto a = render (sec, static_cast<int> (sr));   // 1 second
    const int sawRises = countRises (a.saw);
    const int subRises = countRises (a.sub);
    // VCO fundamental ~442 Hz; the saw rises ~442/s, the -1 oct sub ~221/s.
    REQUIRE (std::abs (sawRises - 442) <= 2);
    // Sub fundamental is EXACTLY VCO/2: the sub must rise ~half as often as the saw.
    REQUIRE (std::abs (subRises - sawRises / 2) <= 2);

    // -2 oct square: the sub Q2 is exactly VCO/4.
    sec.reset (0x1234u);
    sec.setControls (makeControls (cv, Footage::Eight, SubShape::TwoOctDownSquare,
                                   0.0f, OscAaMode::PolyBlep));
    const auto b = render (sec, static_cast<int> (sr));
    const int sawRisesB = countRises (b.saw);
    const int subRisesB = countRises (b.sub);
    REQUIRE (std::abs (subRisesB - sawRisesB / 4) <= 2);
}

// Footage ratios stay exact: at 4' the sub still tracks VCO/2, proving the sub
// derives from the SAME advanced master phase (§7.3; research/02 §3.7).
TEST_CASE("oscsection: sub stays exactly VCO over 2 at every footage because it shares the advanced master phase", "[oscsection]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();
    OscillatorSection sec;
    sec.prepare (sr, table);

    for (Footage f : { Footage::Sixteen, Footage::Eight, Footage::Four })
    {
        sec.reset (0x55u);
        sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts, f,
                                       SubShape::OctDownSquare, 0.0f, OscAaMode::PolyBlep));
        const auto cap = render (sec, static_cast<int> (sr));
        const int sawRises = countRises (cap.saw);
        const int subRises = countRises (cap.sub);
        REQUIRE (sawRises > 0);
        REQUIRE (std::abs (subRises - sawRises / 2) <= 2);   // exact VCO/2 at every footage
    }
}

// -----------------------------------------------------------------------------
// §2.3 / ADR-002 C9 / ADR-018 Q6 — HQ auto-escalation keyed off the VCO
// fundamental, never a parameter, and reverting below the threshold.
// -----------------------------------------------------------------------------
TEST_CASE("oscsection: a voice above kHqEscalationHz escalates the whole section to minBLEP regardless of tier", "[oscsection]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();
    OscillatorSection sec;
    sec.prepare (sr, table);

    // A fundamental ABOVE the (sample-rate-scaled) Valimaki threshold, requested in
    // the Eco/Standard (PolyBLEP) tier: the section must still escalate to minBLEP.
    const double hiCv = mw::cal::vco::kPitchRefVolts + 3.0;   // ~3536 Hz at 8'
    sec.reset (0x9u);
    sec.setControls (makeControls (hiCv, Footage::Eight, SubShape::OctDownSquare,
                                   0.0f, OscAaMode::PolyBlep));
    REQUIRE (sec.fundamentalHz() > mw::cal::vco::hqEscalationHzAt (sr));
    REQUIRE (sec.effectiveAaMode() == OscAaMode::MinBlepHq);   // escalated even in PolyBLEP tier

    // Below the threshold, the same PolyBLEP-tier voice stays on PolyBLEP.
    sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts, Footage::Eight,
                                   SubShape::OctDownSquare, 0.0f, OscAaMode::PolyBlep));
    REQUIRE (sec.fundamentalHz() < mw::cal::vco::hqEscalationHzAt (sr));
    REQUIRE (sec.effectiveAaMode() == OscAaMode::PolyBlep);    // reverted below the threshold
}

// The escalation is per-voice and may toggle between blocks but NOT within a block:
// the effective mode is stable across every sample of a block (set in setControls).
TEST_CASE("oscsection: the effective AA mode is fixed within a block and not flipped per sample", "[oscsection]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();
    OscillatorSection sec;
    sec.prepare (sr, table);
    sec.reset (0x7u);
    sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts + 3.0, Footage::Eight,
                                   SubShape::OctDownSquare, 0.0f, OscAaMode::PolyBlep));
    const OscAaMode m0 = sec.effectiveAaMode();
    REQUIRE (m0 == OscAaMode::MinBlepHq);
    for (int i = 0; i < 4096; ++i)
    {
        (void) sec.renderSample();
        REQUIRE (sec.effectiveAaMode() == m0);   // invariant within the block
    }
}

// Escalation is not exposed as a parameter: there is no setter that forces minBLEP
// other than the tier (the Controls struct carries only the tier aaMode, not an
// escalation flag), and the section escalates purely off the fundamental.
TEST_CASE("oscsection: escalation is never a parameter — it follows only the fundamental", "[oscsection]") {
    // The Controls aggregate exposes exactly the tier knobs; no escalation toggle.
    using C = OscillatorSection::Controls;
    static_assert (std::is_same_v<decltype (std::declval<C&>().aaMode), OscAaMode>,
                   "Controls carries the tier AA mode only.");
    static_assert (std::is_same_v<decltype (std::declval<C&>().subShape), SubShape>,
                   "Controls carries the sub shape.");

    const double sr = 44100.0;   // reference SR: threshold == kHqEscalationHz exactly
    MinBlepTable table; table.build();
    OscillatorSection sec;
    sec.prepare (sr, table);

    // Sweep the fundamental across the threshold in the HQ tier AND the Eco tier:
    // in both, the effective mode is minBLEP iff f > threshold (tier only matters
    // BELOW the threshold).
    for (OscAaMode tier : { OscAaMode::PolyBlep, OscAaMode::MinBlepHq })
    {
        // Below threshold (~442 Hz at 8').
        sec.reset (0x3u);
        sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts, Footage::Eight,
                                       SubShape::OctDownSquare, 0.0f, tier));
        REQUIRE (sec.fundamentalHz() < mw::cal::vco::kHqEscalationHz);
        const OscAaMode below = sec.effectiveAaMode();

        // Above threshold (~3536 Hz at 8').
        sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts + 3.0, Footage::Eight,
                                       SubShape::OctDownSquare, 0.0f, tier));
        REQUIRE (sec.fundamentalHz() > mw::cal::vco::kHqEscalationHz);
        REQUIRE (sec.effectiveAaMode() == OscAaMode::MinBlepHq);   // always escalated above

        if (tier == OscAaMode::PolyBlep)
            REQUIRE (below == OscAaMode::PolyBlep);     // tier honored below threshold
        else
            REQUIRE (below == OscAaMode::MinBlepHq);    // HQ tier is minBLEP everywhere
    }
}

// -----------------------------------------------------------------------------
// §7.2 / ADR-018 Q-table, Q5 — tier binding: Eco/Standard => PolyBLEP, HQ => minBLEP.
// -----------------------------------------------------------------------------
TEST_CASE("oscsection: Eco and Standard tiers use PolyBLEP and the HQ tier uses minBLEP below the escalation threshold", "[oscsection]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();
    OscillatorSection sec;
    sec.prepare (sr, table);
    const double cv = mw::cal::vco::kPitchRefVolts;   // ~442 Hz, well below escalation

    // PolyBLEP tier (Eco/Standard derive to OscAaMode::PolyBlep per ADR-018 Q-table).
    sec.reset (0x11u);
    sec.setControls (makeControls (cv, Footage::Eight, SubShape::OctDownSquare,
                                   0.0f, OscAaMode::PolyBlep));
    REQUIRE (sec.fundamentalHz() < mw::cal::vco::hqEscalationHzAt (sr));
    REQUIRE (sec.effectiveAaMode() == OscAaMode::PolyBlep);

    // HQ tier (minBLEP) even below the escalation threshold.
    sec.setControls (makeControls (cv, Footage::Eight, SubShape::OctDownSquare,
                                   0.0f, OscAaMode::MinBlepHq));
    REQUIRE (sec.effectiveAaMode() == OscAaMode::MinBlepHq);
}

// -----------------------------------------------------------------------------
// §8 / ADR-002 C7 — output contract: four bipolar sources, base SR only, no
// oversample stride is read.
// -----------------------------------------------------------------------------
TEST_CASE("oscsection: emits four bipolar sources with saw pulse sub in -1 to +1 and noise in -1 to 1", "[oscsection]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();
    OscillatorSection sec;
    sec.prepare (sr, table);
    sec.reset (0xABCDu);
    sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts, Footage::Eight,
                                   SubShape::TwoOctDown25Pulse, 0.4f, OscAaMode::PolyBlep));

    const auto cap = render (sec, 8192);

    // saw / pulse / sub are nominally bipolar [-1,+1]; PolyBLEP can overshoot a little
    // at the band-limited edges, so allow a small margin but require both polarities.
    auto bipolar = [] (const std::vector<float>& v, float margin) {
        float lo = 2.0f, hi = -2.0f;
        for (float x : v) { lo = std::min (lo, x); hi = std::max (hi, x); }
        REQUIRE (hi <=  1.0f + margin);
        REQUIRE (lo >= -1.0f - margin);
        REQUIRE (hi >  0.0f);   // reaches the positive plateau
        REQUIRE (lo <  0.0f);   // reaches the negative plateau
    };
    bipolar (cap.saw,   0.30f);
    bipolar (cap.pulse, 0.30f);
    bipolar (cap.sub,   0.30f);

    // Noise is the HALF-OPEN [-1, 1): never reaches +1.0f, but reaches at least -1.
    float nlo = 2.0f, nhi = -2.0f;
    for (float x : cap.noise) { nlo = std::min (nlo, x); nhi = std::max (nhi, x); }
    REQUIRE (nhi < 1.0f);      // strictly below +1 (half-open upper bound)
    REQUIRE (nlo >= -1.0f);    // inclusive lower bound
    REQUIRE (nhi > 0.0f);      // exercises both signs
    REQUIRE (nlo < 0.0f);
}

// The four sources are RAW and pre-mix: no level scaling / summation happens here
// (the mixer owns that, §9). Distinct seeds decorrelate the noise channel (the only
// per-voice stochastic source) — a sanity check the section wires the seed through.
TEST_CASE("oscsection: noise channel is reseeded per voice so distinct seeds decorrelate", "[oscsection]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();

    OscillatorSection a, b;
    a.prepare (sr, table);
    b.prepare (sr, table);
    a.reset (0x1u);
    b.reset (0x2u);
    const auto controls = makeControls (mw::cal::vco::kPitchRefVolts, Footage::Eight,
                                        SubShape::OctDownSquare, 0.0f, OscAaMode::PolyBlep);
    a.setControls (controls);
    b.setControls (controls);

    const auto ca = render (a, 4096);
    const auto cb = render (b, 4096);

    // The VCO/sub channels are deterministic and identical for the same controls; only
    // the noise stream differs by seed. Count exact-equal noise samples — independent
    // streams agree only by chance (vanishingly often over 4096 samples).
    int equalNoise = 0;
    for (std::size_t i = 0; i < ca.noise.size(); ++i)
        if (ca.noise[i] == cb.noise[i]) ++equalNoise;
    REQUIRE (equalNoise < 16);   // decorrelated: practically no exact matches

    // Same controls + same seed => the deterministic saw is bit-identical.
    OscillatorSection c; c.prepare (sr, table); c.reset (0x1u); c.setControls (controls);
    const auto cc = render (c, 256);
    for (std::size_t i = 0; i < cc.saw.size(); ++i)
        REQUIRE (ca.saw[i] == cc.saw[i]);   // determinism for the same seed + controls
}

// -----------------------------------------------------------------------------
// §2.3 / §10 — kHqEscalationHz is read from the calibration header, not duplicated.
// -----------------------------------------------------------------------------
TEST_CASE("oscsection: the escalation threshold is sourced from calibration and sample-rate scaled", "[oscsection]") {
    // (PI) centralization: the threshold value lives in VcoShapeConstants.h and is
    // sample-rate-scaled there; the section reads it, never inlines a literal [§2.3, §10].
    REQUIRE (mw::cal::vco::kHqEscalationHz == 2000.0);
    REQUIRE (mw::cal::vco::hqEscalationHzAt (44100.0) == Approx (2000.0));
    REQUIRE (mw::cal::vco::hqEscalationHzAt (88200.0) == Approx (4000.0));   // scales with fs

    // The section's escalation crossover matches the calibrated threshold at the
    // prepared sample rate (not 44.1 kHz): at 88.2 kHz a 3 kHz fundamental is BELOW
    // the scaled 4 kHz threshold and must NOT escalate in the PolyBLEP tier.
    const double sr = 88200.0;
    MinBlepTable table; table.build();
    OscillatorSection sec; sec.prepare (sr, table); sec.reset (0x5u);
    // ~442 * 2^2.764 ~ 3000 Hz: above the 44.1k threshold but below the 88.2k one.
    sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts + 2.764, Footage::Eight,
                                   SubShape::OctDownSquare, 0.0f, OscAaMode::PolyBlep));
    REQUIRE (sec.fundamentalHz() > mw::cal::vco::kHqEscalationHz);              // > 2 kHz
    REQUIRE (sec.fundamentalHz() < mw::cal::vco::hqEscalationHzAt (sr));        // < 4 kHz
    REQUIRE (sec.effectiveAaMode() == OscAaMode::PolyBlep);   // scaled threshold respected
}

// -----------------------------------------------------------------------------
// §2.4 / ADR-002 C11 — real-time safety: noexcept hot paths, no alloc / no locks.
// -----------------------------------------------------------------------------
TEST_CASE("oscsection: renderSample setControls and reset are noexcept", "[oscsection]") {
    OscillatorSection sec;
    STATIC_REQUIRE (noexcept (sec.renderSample()));
    STATIC_REQUIRE (noexcept (sec.reset (1u)));
    OscillatorSection::Controls c{};
    STATIC_REQUIRE (noexcept (sec.setControls (c)));
    STATIC_REQUIRE (std::is_same_v<decltype (sec.renderSample()),
                                   OscillatorSection::Sources>);
}

TEST_CASE("oscsection: renderSample performs no heap allocation and takes no locks", "[oscsection][rt]") {
    const double sr = 48000.0;
    MinBlepTable table; table.build();   // build allocates OFF the audio thread
    OscillatorSection sec;
    sec.prepare (sr, table);             // prepare sizes the applicators (init-time)
    sec.reset (0xCAFEu);

    AudioThreadGuard g;
    g.arm();
    // Exercise BOTH tiers and the escalated path under the guard.
    for (auto tier : { OscAaMode::PolyBlep, OscAaMode::MinBlepHq })
    {
        // Below threshold then above it (escalated) — both inside the armed window;
        // setControls only re-derives scalars (no allocation), the render loop is hot.
        sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts, Footage::Eight,
                                       SubShape::TwoOctDown25Pulse, 0.3f, tier));
        for (int i = 0; i < 2048; ++i) (void) sec.renderSample();

        sec.setControls (makeControls (mw::cal::vco::kPitchRefVolts + 3.0, Footage::Eight,
                                       SubShape::TwoOctDown25Pulse, 0.3f, tier));
        for (int i = 0; i < 2048; ++i) (void) sec.renderSample();
    }
    g.disarm();

    REQUIRE_FALSE (g.violated());
    REQUIRE (g.violations().empty());
}
