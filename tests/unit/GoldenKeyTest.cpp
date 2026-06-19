// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-2 golden support: GoldenKey / EngineTag types, hash(GoldenKey), and
// sameEngineContext() — the engine-context-refusal primitive (task 041). Test-case
// names begin with "golden" so `ctest -R golden` selects them (silent-pass rule,
// AGENTS.md / docs/design/11 §8.3). This realizes docs/design/11 §5.3 / ADR-013 C22
// / ADR-023 V11.
//
// Acceptance coverage (plan/backlog/041):
//  - hash() is STABLE across runs and DIFFERS when any field differs
//    [docs/design/11 §5.3]
//  - sameEngineContext returns false when ladder, oversampleFactor, OR renderVersion
//    differs (paired with a true case for identical tags) [ADR-023 V11; ADR-013 C22]
//  - a GoldenKey built with a non-blessed sample rate is rejected/flagged by the
//    construction helper [docs/design/11 §5.2; ADR-023 V12]
//
// Tagged [core] only (an existing snapshot tag) so the checked-in label-snapshot
// (tests/golden/corpus/ctest-labels.snapshot, docs/design/11 §8.4) is unaffected by
// THIS file; selection is by the `golden` NAME prefix, not by tag. (A new [golden]
// tag is introduced for the engine-context cases per the task; the orchestrator
// regenerates the snapshot at wave integration — an expected, transient red on
// labels_snapshot only.)

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "../golden/GoldenKey.h"

namespace {

using mw::golden::DeterminismClass;
using mw::golden::EngineTag;
using mw::golden::GoldenKey;
using mw::golden::LadderEngine;

// A canonical, fully-populated valid key on a blessed sample rate. Each test
// perturbs exactly one field of a copy and asserts hash divergence.
GoldenKey canonicalKey() noexcept {
    GoldenKey k{};
    k.renderGraphHash = 0x0123456789abcdefULL;
    k.engine          = EngineTag{LadderEngine::Huovilainen, /*oversampleFactor=*/2, /*renderVersion=*/1};
    k.sampleRate      = 48000.0;
    k.blockSize       = 128;
    k.seed            = 0xdeadbeefcafef00dULL;
    k.cls             = DeterminismClass::Fp;
    return k;
}

} // namespace

// --- hash() stability + field sensitivity ----------------------------------------

TEST_CASE("golden: GoldenKey hash is stable across runs (deterministic) [docs/design/11 5.3]",
          "[golden]") {
    const auto k = canonicalKey();
    const std::uint64_t h1 = mw::golden::hash(k);
    const std::uint64_t h2 = mw::golden::hash(k);
    REQUIRE(h1 == h2);

    // A freshly constructed identical key hashes identically (no hidden state /
    // address-dependence). Paired positive control for the divergence cases below.
    const auto kCopy = canonicalKey();
    REQUIRE(mw::golden::hash(kCopy) == h1);
}

TEST_CASE("golden: GoldenKey hash differs when ANY field differs (negative controls) "
          "[docs/design/11 5.3]",
          "[golden]") {
    const auto base = canonicalKey();
    const std::uint64_t h0 = mw::golden::hash(base);

    SECTION("renderGraphHash") {
        auto k = base;
        k.renderGraphHash ^= 0x1ULL;   // flip one bit
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("engine.ladder") {
        auto k = base;
        k.engine.ladder = LadderEngine::ZDF;
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("engine.oversampleFactor") {
        auto k = base;
        k.engine.oversampleFactor = 1;
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("engine.renderVersion") {
        auto k = base;
        k.engine.renderVersion = 2;
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("sampleRate (another blessed rate)") {
        auto k = base;
        k.sampleRate = 44100.0;        // still blessed, but a different key
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("blockSize") {
        auto k = base;
        k.blockSize = 256;
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("seed") {
        auto k = base;
        k.seed ^= 0x1ULL;              // flip one bit
        REQUIRE(mw::golden::hash(k) != h0);
    }
    SECTION("cls") {
        auto k = base;
        k.cls = DeterminismClass::Exact;
        REQUIRE(mw::golden::hash(k) != h0);
    }
}

TEST_CASE("golden: GoldenKey hash distinguishes all blessed sample rates "
          "[docs/design/11 5.2/5.3]",
          "[golden]") {
    // Four blessed rates -> four distinct hashes (the rate axis is part of the key,
    // ADR-023 V12). A stub that ignores sampleRate would collide these.
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0};
    std::vector<std::uint64_t> hs;
    for (double sr : rates) {
        auto k = canonicalKey();
        k.sampleRate = sr;
        hs.push_back(mw::golden::hash(k));
    }
    for (std::size_t i = 0; i < hs.size(); ++i)
        for (std::size_t j = i + 1; j < hs.size(); ++j)
            REQUIRE(hs[i] != hs[j]);
}

// --- sameEngineContext: refuse on any engine-tag axis -----------------------------

TEST_CASE("golden: sameEngineContext is TRUE for identical engine tags (positive control) "
          "[ADR-013 C22; ADR-023 V11]",
          "[golden]") {
    const EngineTag a{LadderEngine::Huovilainen, 2, 1};
    const EngineTag b{LadderEngine::Huovilainen, 2, 1};
    REQUIRE(mw::golden::sameEngineContext(a, b));
    REQUIRE(mw::golden::sameEngineContext(a, a));   // reflexive
}

TEST_CASE("golden: sameEngineContext is FALSE when ladder differs (refuse) "
          "[ADR-013 C22]",
          "[golden]") {
    const EngineTag a{LadderEngine::Huovilainen, 2, 1};
    const EngineTag b{LadderEngine::ZDF, 2, 1};
    REQUIRE_FALSE(mw::golden::sameEngineContext(a, b));
}

TEST_CASE("golden: sameEngineContext is FALSE when oversampleFactor differs (refuse) "
          "[ADR-013 C22]",
          "[golden]") {
    const EngineTag a{LadderEngine::Huovilainen, 2, 1};
    const EngineTag b{LadderEngine::Huovilainen, 1, 1};
    REQUIRE_FALSE(mw::golden::sameEngineContext(a, b));
}

TEST_CASE("golden: sameEngineContext is FALSE when renderVersion differs (refuse) "
          "[ADR-023 V11]",
          "[golden]") {
    const EngineTag a{LadderEngine::Huovilainen, 2, 1};
    const EngineTag b{LadderEngine::Huovilainen, 2, 2};
    REQUIRE_FALSE(mw::golden::sameEngineContext(a, b));
}

// --- blessed-sample-rate construction helper --------------------------------------

TEST_CASE("golden: isBlessedSampleRate accepts the blessed set and rejects others "
          "[docs/design/11 5.2; ADR-023 V12]",
          "[golden]") {
    // Positive: each of the four blessed rates is accepted.
    REQUIRE(mw::golden::isBlessedSampleRate(44100.0));
    REQUIRE(mw::golden::isBlessedSampleRate(48000.0));
    REQUIRE(mw::golden::isBlessedSampleRate(88200.0));
    REQUIRE(mw::golden::isBlessedSampleRate(96000.0));

    // Negative controls: below-set, above-set, and a near-miss are all rejected.
    REQUIRE_FALSE(mw::golden::isBlessedSampleRate(22050.0));   // below set (ADR-023 V17)
    REQUIRE_FALSE(mw::golden::isBlessedSampleRate(192000.0));  // above set (ADR-023 V14)
    REQUIRE_FALSE(mw::golden::isBlessedSampleRate(48001.0));   // near-miss
    REQUIRE_FALSE(mw::golden::isBlessedSampleRate(0.0));
}

TEST_CASE("golden: makeGoldenKey builds a valid key on a blessed sample rate "
          "[docs/design/11 5.2]",
          "[golden]") {
    const EngineTag eng{LadderEngine::Huovilainen, 2, 1};
    const GoldenKey k = mw::golden::makeGoldenKey(
        /*renderGraphHash=*/0xabcdULL, eng, /*sampleRate=*/96000.0,
        /*blockSize=*/64, /*seed=*/7ULL, DeterminismClass::Exact);

    // Round-trips the inputs faithfully (paired positive control: a stub returning a
    // default-constructed key would fail these).
    REQUIRE(k.renderGraphHash == 0xabcdULL);
    REQUIRE(k.engine.ladder == LadderEngine::Huovilainen);
    REQUIRE(k.engine.oversampleFactor == 2);
    REQUIRE(k.engine.renderVersion == 1);
    REQUIRE(k.sampleRate == 96000.0);
    REQUIRE(k.blockSize == 64);
    REQUIRE(k.seed == 7ULL);
    REQUIRE(k.cls == DeterminismClass::Exact);

    // And it is hashable / consistent with hashing a hand-built equivalent.
    GoldenKey manual{0xabcdULL, eng, 96000.0, 64, 7ULL, DeterminismClass::Exact};
    REQUIRE(mw::golden::hash(k) == mw::golden::hash(manual));
}

TEST_CASE("golden: makeGoldenKey REJECTS a non-blessed sample rate "
          "[docs/design/11 5.2; ADR-023 V12]",
          "[golden]") {
    const EngineTag eng{LadderEngine::Huovilainen, 2, 1};

    // A non-blessed rate at construction is refused (offline harness code; the
    // throw is the rejection, ADR-023 V17/V14). Paired with the accepted case above.
    REQUIRE_THROWS_AS(
        mw::golden::makeGoldenKey(0x1ULL, eng, /*sampleRate=*/44101.0, 64, 1ULL,
                                  DeterminismClass::Fp),
        std::invalid_argument);

    // 192 kHz (above the set) is likewise refused at key construction.
    REQUIRE_THROWS_AS(
        mw::golden::makeGoldenKey(0x1ULL, eng, /*sampleRate=*/192000.0, 64, 1ULL,
                                  DeterminismClass::Fp),
        std::invalid_argument);

    // Sanity: the blessed rate still builds (so the throw above is the rate guard,
    // not an unconditional throw).
    REQUIRE_NOTHROW(
        mw::golden::makeGoldenKey(0x1ULL, eng, /*sampleRate=*/48000.0, 64, 1ULL,
                                  DeterminismClass::Fp));
}
