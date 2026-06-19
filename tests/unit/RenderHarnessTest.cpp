// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/RenderHarnessTest.cpp — the TDD acceptance tests for the deterministic
// offline render harness (task 076, golden-4). Test-case names begin with
// "renderharness" so `ctest -R renderharness --no-tests=error` selects them under the
// silent-pass rule (AGENTS.md "Tests"); the display names contain NO literal '[' so
// Catch2 does not mis-parse a tag out of the name.
//
// Lives in tests/unit/ (NOT tests/golden/) because the build globs tests/unit/*.cpp
// into mw101_tests; a tests/golden/*.cpp would NOT be picked up and editing
// tests/CMakeLists.txt is forbidden by the parallel-fleet conflict-avoidance rule. The
// harness itself is the header-only tests/golden/RenderHarness.h (same pattern as the
// sibling tests/golden/Sha256.h / GoldenKey.h / Stimulus.h).
//
// Objective (plan/backlog/076): RenderHarness::render(patch, stim, key) produces a
// deterministic RenderResult — identical (patch, stimulus, key) yields byte-identical
// output on the same platform, the GoldenKey seed is a live determinism axis (changing
// it changes the bytes), and the renderVersion in the EngineTag selects the matching
// frozen constant-set at SETUP (prepare analogue), never at audio rate.
//
// Acceptance coverage (each criterion is an explicit assertion below):
//   1. The same (patch, stimulus, key) renders byte-identical output twice
//      [docs/design/11 §5.4] — the determinism contract.
//   2. Negative control: changing ONLY the seed in the GoldenKey (patch + stimulus
//      fixed) changes the rendered bytes [docs/design/11 §5.4].
//   3. renderVersion in the EngineTag selects the matching frozen constant-set at
//      setup, not at audio rate [ADR-023 V10; docs/design/11 §5.3] — a refused
//      (unshipped) renderVersion yields an empty render and an objective failure flag;
//      a shipped renderVersion (CURRENT) renders normally.
//   4. (selector) ctest -R renderharness --no-tests=error is green; names begin with
//      renderharness.
//
// Out of scope (other golden tasks; NOT asserted here): comparison logic (golden-6/7),
// blob persistence (golden-5), engine DSP internals (consumed opaque) [task 076
// Out-of-scope].

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "../golden/RenderHarness.h"
#include "../golden/GoldenKey.h"
#include "../golden/Stimulus.h"
#include "../../core/version/EngineVersion.h"

namespace {

using mw::golden::DeterminismClass;
using mw::golden::EngineTag;
using mw::golden::GoldenKey;
using mw::golden::LadderEngine;
using mw::golden::PatchSnapshot;
using mw::golden::RenderHarness;
using mw::golden::RenderResult;
using mw::golden::Stimulus;

constexpr int kCurrentRv = mw101::version::kCurrentRenderVersion;

// A small, valid blessed-set render context. The CURRENT renderVersion + ZDF ladder +
// 1x oversample factor is a SHIPPED constant set (ConstantSetSelector binds rv-1 ==
// CURRENT), so the harness renders the normal blessed path.
EngineTag tagCurrent() noexcept {
    return EngineTag{ LadderEngine::ZDF, /*oversampleFactor=*/1, /*renderVersion=*/kCurrentRv };
}

// Build a blessed-set GoldenKey at 48 kHz with a given seed. blockSize is small so the
// render exercises the engine's internal sub-blocking quickly.
GoldenKey keyAt(std::uint64_t seed, int renderVersion = kCurrentRv) {
    EngineTag tag{ LadderEngine::ZDF, 1, renderVersion };
    // renderGraphHash is folded from the stimulus by the caller in a real corpus; here a
    // fixed sentinel is fine — the harness does not key behavior on it (it keys the
    // RENDER on SR/blockSize/seed/tag).
    return mw::golden::makeGoldenKey(/*renderGraphHash=*/0xABCDu, tag,
                                     /*sampleRate=*/48000.0, /*blockSize=*/64, seed,
                                     DeterminismClass::Fp);
}

// A fixed, fully-deterministic stimulus: one sustained note held for a short duration.
// No PRNG inside the stimulus itself — so the ONLY determinism axis under test in the
// negative control is the GoldenKey seed, exactly per the acceptance wording.
Stimulus fixedStim() {
    return mw::golden::makeSustainedNote(/*note=*/60, /*velocity=*/0.8f,
                                         /*duration=*/4096);
}

// A trivial patch overlay (empty == init patch; the engine's event-driven voice path
// renders the init voice). Held fixed across the negative control.
PatchSnapshot fixedPatch() {
    PatchSnapshot p{};
    p.renderVersion = kCurrentRv;
    return p;
}

// Byte-equality over two f32 vectors (the CLASS-EXACT "identical bytes" contract). We
// compare raw bytes, not float == float, so a NaN-vs-NaN or -0.0-vs-0.0 difference is
// caught — the determinism contract is byte-identity, not numeric equality.
bool bytesIdentical(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

float maxAbs(const std::vector<float>& v) noexcept {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

bool allFinite(const std::vector<float>& v) noexcept {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Acceptance 1 — DETERMINISM: the same (patch, stimulus, key) renders byte-identical
// output twice [docs/design/11 §5.4]. The harness is a pure function of its inputs.
// ---------------------------------------------------------------------------
TEST_CASE("renderharness: same patch stimulus and key render byte-identical output twice",
          "[renderharness]") {
    const RenderHarness harness;
    const PatchSnapshot patch = fixedPatch();
    const Stimulus      stim  = fixedStim();
    const GoldenKey     key   = keyAt(/*seed=*/0x1234'5678'9abc'def0ull);

    const RenderResult a = harness.render(patch, stim, key);
    const RenderResult b = harness.render(patch, stim, key);

    // The render produced something: a non-empty buffer at the keyed duration, sane
    // (finite + non-silent). A constant-stub harness that returned an empty buffer would
    // fail the non-empty / non-silent asserts (paired positive/negative control).
    REQUIRE(a.samples.size() == static_cast<std::size_t>(stim.durationFrames));
    REQUIRE(allFinite(a.samples));
    REQUIRE(maxAbs(a.samples) > 0.0f);   // positive: the note actually sounded

    // The result carries the pinned context from the key (§5.4: RenderResult keeps the
    // engine tag and SR it was rendered under).
    REQUIRE(a.sampleRate == key.sampleRate);
    REQUIRE(a.engine.ladder == key.engine.ladder);
    REQUIRE(a.engine.oversampleFactor == key.engine.oversampleFactor);
    REQUIRE(a.engine.renderVersion == key.engine.renderVersion);

    // The determinism contract: two renders of the same (patch, stimulus, key) are
    // BYTE-identical (not merely numerically close).
    REQUIRE(a.samples.size() == b.samples.size());
    REQUIRE(bytesIdentical(a.samples, b.samples));
}

// ---------------------------------------------------------------------------
// Acceptance 2 — NEGATIVE CONTROL: changing ONLY the GoldenKey seed (patch + stimulus
// held fixed) changes the rendered bytes [docs/design/11 §5.4]. The seed is the live
// determinism axis the harness pins from the key; two distinct seeds must diverge, and
// each seed must still render reproducibly. This is the paired control that fails a
// harness that ignored the seed (returning identical bytes for every seed).
// ---------------------------------------------------------------------------
TEST_CASE("renderharness: changing the GoldenKey seed changes the rendered bytes",
          "[renderharness]") {
    const RenderHarness harness;
    const PatchSnapshot patch = fixedPatch();
    const Stimulus      stim  = fixedStim();

    const GoldenKey keyA = keyAt(/*seed=*/0x0000'0000'0000'0001ull);
    const GoldenKey keyB = keyAt(/*seed=*/0xFFFF'FFFF'FFFF'FFFEull);

    const RenderResult a = harness.render(patch, stim, keyA);
    const RenderResult b = harness.render(patch, stim, keyB);

    REQUIRE(allFinite(a.samples));
    REQUIRE(allFinite(b.samples));
    REQUIRE(maxAbs(a.samples) > 0.0f);
    REQUIRE(maxAbs(b.samples) > 0.0f);

    // Same shape (same SR / blockSize / stimulus duration), different bytes: the seed
    // perturbed the render. (Different seeds => different bytes.)
    REQUIRE(a.samples.size() == b.samples.size());
    REQUIRE_FALSE(bytesIdentical(a.samples, b.samples));   // negative control

    // ...and each seed is STILL individually reproducible (positive control: the
    // divergence is from the seed, not from nondeterminism).
    const RenderResult a2 = harness.render(patch, stim, keyA);
    REQUIRE(bytesIdentical(a.samples, a2.samples));
}

// ---------------------------------------------------------------------------
// Acceptance 3 — renderVersion selects the frozen constant-set AT SETUP, not at audio
// rate [ADR-023 V10; docs/design/11 §5.3]. A SHIPPED renderVersion (CURRENT) binds its
// frozen set and renders the normal path; an UNSHIPPED renderVersion is REFUSED (no
// silent fallback to CURRENT), surfaced as an objective ok==false + empty render. The
// selection happens once in the render setup (prepare analogue), so the per-sample loop
// never reselects.
// ---------------------------------------------------------------------------
TEST_CASE("renderharness: renderVersion selects the matching frozen constant-set at setup",
          "[renderharness]") {
    const RenderHarness harness;
    const PatchSnapshot patch = fixedPatch();
    const Stimulus      stim  = fixedStim();

    // Positive: CURRENT renderVersion is shipped -> the constant set binds and the
    // harness renders normally.
    {
        const GoldenKey key = keyAt(/*seed=*/0x55ull, /*renderVersion=*/kCurrentRv);
        const RenderResult r = harness.render(patch, stim, key);
        REQUIRE(r.constantSetSelected);                       // bound at setup
        REQUIRE(r.engine.renderVersion == kCurrentRv);
        REQUIRE(r.samples.size() == static_cast<std::size_t>(stim.durationFrames));
        REQUIRE(maxAbs(r.samples) > 0.0f);
    }

    // Negative control: an unshipped renderVersion is REFUSED. The harness reports the
    // refusal (constantSetSelected == false) and renders nothing — NEVER a silent
    // fallback to CURRENT that would mis-bless legacy audio under the wrong tag.
    {
        const int kUnshippedRv = 99999;   // not in the frozen-constant-set registry
        const GoldenKey key = keyAt(/*seed=*/0x55ull, /*renderVersion=*/kUnshippedRv);
        const RenderResult r = harness.render(patch, stim, key);
        REQUIRE_FALSE(r.constantSetSelected);                 // refused at setup
        REQUIRE(r.engine.renderVersion == kUnshippedRv);      // echoes the REQUEST, not CURRENT
        REQUIRE(r.samples.empty());                           // no fallback render
    }

    // The selection is keyed to the EngineTag's renderVersion: CURRENT binds, unshipped
    // refuses. Verifying the two arms cover the boundary objectively.
    REQUIRE(mw::cal::selectConstantSet(kCurrentRv).ok);
    REQUIRE_FALSE(mw::cal::selectConstantSet(99999).ok);
}

// ---------------------------------------------------------------------------
// The GoldenKey pins the render context (SR, block size, engine tag), and the harness
// honors it: a different blessed sample rate is a different render. This guards that the
// harness drives the engine at the KEYED sample rate (prepare(SR,...)) rather than a
// hard-coded one — a mis-pinned SR would silently bless the wrong rate's audio.
// ---------------------------------------------------------------------------
TEST_CASE("renderharness: the keyed sample rate is pinned into prepare and the result",
          "[renderharness]") {
    const RenderHarness harness;
    const PatchSnapshot patch = fixedPatch();
    const Stimulus      stim  = fixedStim();

    EngineTag tag = tagCurrent();
    const GoldenKey k48 =
        mw::golden::makeGoldenKey(0xABCDu, tag, 48000.0, 64, 0x9ull, DeterminismClass::Fp);
    const GoldenKey k44 =
        mw::golden::makeGoldenKey(0xABCDu, tag, 44100.0, 64, 0x9ull, DeterminismClass::Fp);

    const RenderResult r48 = harness.render(patch, stim, k48);
    const RenderResult r44 = harness.render(patch, stim, k44);

    REQUIRE(r48.sampleRate == 48000.0);    // the result reports the keyed SR
    REQUIRE(r44.sampleRate == 44100.0);
    REQUIRE(maxAbs(r48.samples) > 0.0f);
    REQUIRE(maxAbs(r44.samples) > 0.0f);

    // The same note rendered at two different sample rates produces different bytes (the
    // SR genuinely reached the engine's prepare, changing the per-sample oscillator
    // increment) — the positive proof the SR is pinned, plus the negative control that
    // it is not ignored.
    REQUIRE(r48.samples.size() == static_cast<std::size_t>(stim.durationFrames));
    REQUIRE(r44.samples.size() == static_cast<std::size_t>(stim.durationFrames));
    REQUIRE_FALSE(bytesIdentical(r48.samples, r44.samples));
}
