// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Unit tests for the minBLEP residual table + per-voice applicator (task 027).
// Names begin with "minblep" so `ctest -R minblep` selects them (silent-pass rule,
// AGENTS.md). Covers every Acceptance criterion of
// plan/backlog/027-minblep-residual-table-voice-applicator.md against
// docs/design/01-dsp-oscillators.md §3.2/§3.3/§2.4 and ADR-002 C8/C11.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "calibration/MinBlepConstants.h"
#include "dsp/MinBlepTable.h"

#include "../invariants/AudioThreadGuard.h"

using mw101::dsp::MinBlepTable;
using mw101::dsp::MinBlepApplicator;
using mw::test::AudioThreadGuard;
using Catch::Approx;

// --- §3.2 table build / length / read-only -----------------------------------

TEST_CASE("minblep: table builds once with the design base-resolution length", "[minblep]") {
    MinBlepTable t;
    REQUIRE_FALSE(t.isBuilt());          // nothing allocated before build()

    t.build();
    REQUIRE(t.isBuilt());

    // Total residual length in TABLE samples = 2 * kZeroCrossings * kOversampling [§3.2].
    const int expectedTableLen =
        2 * MinBlepTable::kZeroCrossings * MinBlepTable::kOversampling;
    REQUIRE(t.tableLength() == expectedTableLen);
    // Length in BASE samples = 2 * kZeroCrossings.
    REQUIRE(t.length() == 2 * MinBlepTable::kZeroCrossings);
    REQUIRE(t.length() * MinBlepTable::kOversampling == t.tableLength());
}

TEST_CASE("minblep: table is read-only and stable after build (repeated reads identical)",
          "[minblep]") {
    MinBlepTable t;
    t.build();

    // Capture a full snapshot, read again: a pure read must not mutate the table.
    std::vector<float> first(static_cast<std::size_t>(t.tableLength()));
    for (int i = 0; i < t.tableLength(); ++i)
        first[static_cast<std::size_t>(i)] = t.residualAt(i);
    for (int i = 0; i < t.tableLength(); ++i)
        REQUIRE(t.residualAt(i) == first[static_cast<std::size_t>(i)]);

    // "Built once": a second build() reproduces the same table (deterministic).
    MinBlepTable u;
    u.build();
    REQUIRE(u.tableLength() == t.tableLength());
    for (int i = 0; i < t.tableLength(); ++i)
        REQUIRE(u.residualAt(i) == Approx(first[static_cast<std::size_t>(i)]).margin(1e-6));
}

TEST_CASE("minblep: residual is a step correction — rises from near -1 to near 0", "[minblep]") {
    MinBlepTable t;
    t.build();
    // The stored residual r = blep - 1 (blep rising 0->1), so it starts low and ends ~0.
    REQUIRE(t.residualAt(0) < -0.5f);
    REQUIRE(std::abs(t.residualAt(t.tableLength() - 1)) < 1e-3f);
}

// --- §3.2/§10 (PI) centralization --------------------------------------------

TEST_CASE("minblep: kZeroCrossings is sourced from calibration, not duplicated", "[minblep]") {
    // The table's compile-time constants must equal the centralized (PI) calibration
    // values [§3.2, §10 "(PI) centralization"].
    REQUIRE(MinBlepTable::kZeroCrossings == mw::cal::minblep::kZeroCrossings);
    REQUIRE(MinBlepTable::kOversampling  == mw::cal::minblep::kOversampling);
}

// --- §3.3 applicator: step shape + settling ----------------------------------

TEST_CASE("minblep: a scheduled step settles exactly to its amplitude", "[minblep]") {
    MinBlepTable t;
    t.build();

    MinBlepApplicator app;
    app.prepare(t, 48000.0);

    const float amp = 0.75f;
    app.scheduleStep(amp, 0.0f);

    // Pop the whole residual window. After length() samples the overlap-add window is
    // fully consumed, so the output equals the held DC level == amp exactly [§3.3].
    const int len = t.length();
    float last = 0.0f;
    for (int n = 0; n < len + 8; ++n)
        last = app.next();
    REQUIRE(last == Approx(amp).margin(1e-6));

    // And it keeps holding that level for further samples (no leftover ringing).
    for (int n = 0; n < len; ++n)
        REQUIRE(app.next() == Approx(amp).margin(1e-6));
}

TEST_CASE("minblep: repeated next() reproduces a band-limited step shape", "[minblep]") {
    MinBlepTable t;
    t.build();

    MinBlepApplicator app;
    app.prepare(t, 48000.0);

    const float amp = 1.0f;
    app.scheduleStep(amp, 0.0f);

    const int len = t.length();
    std::vector<float> y(static_cast<std::size_t>(len + 8));
    for (int n = 0; n < len + 8; ++n)
        y[static_cast<std::size_t>(n)] = app.next();

    // Early in the window the band-limited step has not yet reached its amplitude;
    // by the end it has settled. A naive (zero-order-hold) step would be a single
    // jump — the band-limited version transitions gradually around the edge.
    REQUIRE(y.front() < 0.5f * amp);                 // not yet stepped at the start
    REQUIRE(y.back()  == Approx(amp).margin(1e-6));   // settled to amp at the end

    // The transition is gradual: the residual length spans more than one sample.
    int risingSamples = 0;
    for (std::size_t n = 1; n < y.size(); ++n)
        if (y[n] > y[n - 1] + 1e-4f) ++risingSamples;
    REQUIRE(risingSamples > 1);                       // multi-sample band-limited edge
}

TEST_CASE("minblep: negative and superposed steps are linear", "[minblep]") {
    MinBlepTable t;
    t.build();

    MinBlepApplicator app;
    app.prepare(t, 48000.0);

    // A step up then, after it settles, a step down of equal size returns to ~0.
    app.scheduleStep(+0.5f, 0.0f);
    const int len = t.length();
    for (int n = 0; n < len + 4; ++n) (void) app.next();
    app.scheduleStep(-0.5f, 0.0f);
    float last = 0.0f;
    for (int n = 0; n < len + 4; ++n) last = app.next();
    REQUIRE(last == Approx(0.0f).margin(1e-6));
}

TEST_CASE("minblep: reset clears the applicator to silence", "[minblep]") {
    MinBlepTable t;
    t.build();

    MinBlepApplicator app;
    app.prepare(t, 48000.0);

    app.scheduleStep(1.0f, 0.3f);
    (void) app.next();
    (void) app.next();
    app.reset();
    // After reset, with nothing scheduled, the output is silence.
    for (int n = 0; n < t.length() + 4; ++n)
        REQUIRE(app.next() == Approx(0.0f).margin(1e-12));
}

// --- §2.4 / ADR-002 C11 real-time safety -------------------------------------

TEST_CASE("minblep: scheduleStep and next() perform no heap allocation", "[minblep][rt]") {
    MinBlepTable t;
    t.build();                            // build allocates OFF the audio thread

    MinBlepApplicator app;
    app.prepare(t, 48000.0);              // prepare allocates the ring (init-time)

    // Now exercise the hot path under the alloc sentinel: zero allocations allowed.
    AudioThreadGuard g;
    g.arm();
    for (int rep = 0; rep < 64; ++rep) {
        app.scheduleStep(0.5f, static_cast<float>(rep) / 64.0f);
        for (int n = 0; n < 8; ++n)
            (void) app.next();
    }
    g.disarm();

    REQUIRE_FALSE(g.violated());
    REQUIRE(g.violations().empty());
}
