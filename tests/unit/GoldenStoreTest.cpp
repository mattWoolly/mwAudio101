// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/GoldenStoreTest.cpp — the TDD acceptance tests for the GoldenStore
// blob/sidecar keying, lookup and load (task 077, golden-5). Test-case names begin
// with "golden" so `ctest -R golden --no-tests=error` selects them under the
// silent-pass rule (AGENTS.md "Tests"); the display names contain NO literal '['
// so Catch2 does not mis-parse a tag out of the name.
//
// Lives in tests/unit/ (NOT tests/golden/) because the build globs tests/unit/*.cpp
// into mw101_tests; a tests/golden/*.cpp would NOT be picked up and editing
// tests/CMakeLists.txt is forbidden by the parallel-fleet conflict-avoidance rule.
// The store itself is the header-only tests/golden/GoldenStore.h (same pattern as the
// sibling tests/golden/Sha256.h / GoldenKey.h / RenderHarness.h / Stimulus.h).
//
// Objective (plan/backlog/077): GoldenStore maps a GoldenKey to an on-disk golden blob
// (raw f32) + sidecar JSON, with has() / load() (throws if absent) / blobPath() /
// sidecarPath(), loading a blessed RenderResult.
//
// Acceptance coverage (each criterion is an explicit assertion below):
//   1. A stored blob+sidecar round-trips to an IDENTICAL RenderResult — samples,
//      sampleRate, and EngineTag — [docs/design/11 §5.4].
//   2. load() on an ABSENT key THROWS; has() returns false for it (PAIRED present/
//      absent) [docs/design/11 §5.4].
//   3. blobPath/sidecarPath are STABLE functions of the GoldenKey and DISTINCT across
//      determinism class and sample rate [docs/design/11 §7.1].
//   4. (selector) ctest -R golden --no-tests=error is green; names begin with golden.
//
// Out of scope (other golden tasks; NOT asserted here): comparison (golden-6/7),
// MANIFEST validation (golden-8), writing blessed artifacts (golden-10 bless tool)
// [task 077 Out-of-scope]. This test writes a temp corpus directly (NOT via the bless
// tool) purely to exercise the round-trip — it is harness self-test scaffolding, not a
// bless.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../golden/GoldenStore.h"
#include "../golden/GoldenKey.h"
#include "../golden/RenderHarness.h"

namespace {

namespace fs = std::filesystem;

using mw::golden::DeterminismClass;
using mw::golden::EngineTag;
using mw::golden::GoldenKey;
using mw::golden::GoldenStore;
using mw::golden::LadderEngine;
using mw::golden::RenderResult;

// A unique, self-cleaning temp corpus root for one test. Uses the process-unique temp
// dir so parallel ctest shards never collide. The store derives all paths UNDER this
// root from the key; the test owns nothing but the root.
struct TempCorpus {
    fs::path root;
    explicit TempCorpus(const char* tag) {
        root = fs::temp_directory_path()
             / ("mw101-goldenstore-" + std::string(tag) + "-"
                + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempCorpus() {
        std::error_code ec;
        fs::remove_all(root, ec);   // best-effort cleanup; never throws from a dtor
    }
    TempCorpus(const TempCorpus&) = delete;
    TempCorpus& operator=(const TempCorpus&) = delete;
};

EngineTag tag(int oversample = 1, int renderVersion = 1) noexcept {
    return EngineTag{ LadderEngine::ZDF, oversample, renderVersion };
}

GoldenKey keyAt(double sampleRate,
                DeterminismClass cls = DeterminismClass::Fp,
                std::uint64_t renderGraphHash = 0xABCDu,
                std::uint64_t seed = 0x42u,
                int blockSize = 64) {
    return mw::golden::makeGoldenKey(renderGraphHash, tag(), sampleRate, blockSize,
                                     seed, cls);
}

// A small synthesized RenderResult: a few distinctive f32 samples plus a pinned
// context. Not from a real render — the store does not care; it serializes whatever
// RenderResult it is given. (The harness->store wiring is golden-4/golden-10's
// concern.)
RenderResult sampleResult(double sampleRate, const EngineTag& t) {
    RenderResult r{};
    r.sampleRate          = sampleRate;
    r.engine              = t;
    r.constantSetSelected = true;
    r.samples = { 0.0f, 0.25f, -0.5f, 0.75f, -1.0f, 1.0f, 0.123456789f, -0.987654321f };
    return r;
}

bool bytesIdentical(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Acceptance 1 — ROUND-TRIP: a stored blob+sidecar loads back to an identical
// RenderResult (samples + sampleRate + EngineTag) [docs/design/11 §5.4].
// ---------------------------------------------------------------------------
TEST_CASE("golden: goldenstore a stored blob and sidecar round-trip to an identical RenderResult",
          "[golden]") {
    TempCorpus corpus{"roundtrip"};
    const GoldenStore store{ corpus.root };

    const EngineTag    t   = tag(/*oversample=*/2, /*renderVersion=*/1);
    const GoldenKey    key = mw::golden::makeGoldenKey(
        0xFEEDu, t, /*sampleRate=*/48000.0, /*blockSize=*/512,
        /*seed=*/0x1234u, DeterminismClass::Fp);
    const RenderResult original = sampleResult(48000.0, t);

    // Absent before storing (paired with the store -> present below).
    REQUIRE_FALSE(store.has(key));

    store.store(key, original);

    REQUIRE(store.has(key));

    // The blob + sidecar are actually on disk under the keyed paths.
    REQUIRE(fs::exists(store.blobPath(key)));
    REQUIRE(fs::exists(store.sidecarPath(key)));

    const RenderResult loaded = store.load(key);

    // Samples round-trip BYTE-identically (raw f32, not numeric-close): the store
    // serializes/deserializes the exact float bits. A stubbed loader that returned an
    // empty/constant buffer fails here.
    REQUIRE(loaded.samples.size() == original.samples.size());
    REQUIRE(bytesIdentical(loaded.samples, original.samples));

    // The pinned context round-trips: sampleRate + the full EngineTag (ladder /
    // oversampleFactor / renderVersion). A downstream comparer refuses a cross-tag
    // compare on these, so they MUST survive the round-trip exactly [§5.3 / §5.4].
    REQUIRE(loaded.sampleRate == original.sampleRate);
    REQUIRE(loaded.engine.ladder == original.engine.ladder);
    REQUIRE(loaded.engine.oversampleFactor == original.engine.oversampleFactor);
    REQUIRE(loaded.engine.renderVersion == original.engine.renderVersion);
}

// ---------------------------------------------------------------------------
// Acceptance 1b — the round-trip is faithful for an EMPTY sample buffer too (boundary):
// has() flips to true, load() returns the empty buffer with the pinned context intact.
// Guards a blob format that mis-encodes a zero-length payload.
// ---------------------------------------------------------------------------
TEST_CASE("golden: goldenstore an empty-sample RenderResult round-trips faithfully",
          "[golden]") {
    TempCorpus corpus{"empty"};
    const GoldenStore store{ corpus.root };

    const EngineTag t   = tag(1, 1);
    const GoldenKey key = keyAt(96000.0, DeterminismClass::Exact);
    RenderResult    original = sampleResult(96000.0, t);
    original.samples.clear();   // zero-length payload

    store.store(key, original);

    REQUIRE(store.has(key));
    const RenderResult loaded = store.load(key);
    REQUIRE(loaded.samples.empty());
    REQUIRE(loaded.sampleRate == 96000.0);
    REQUIRE(loaded.engine.oversampleFactor == 1);
}

// ---------------------------------------------------------------------------
// Acceptance 2 — ABSENT key: load() THROWS, has() returns false (PAIRED present/
// absent) [docs/design/11 §5.4]. The present arm is the paired positive control: a
// key that WAS stored has()==true and load() succeeds; an UNSTORED sibling key in the
// same corpus has()==false and load() throws.
// ---------------------------------------------------------------------------
TEST_CASE("golden: goldenstore load on an absent key throws and has returns false",
          "[golden]") {
    TempCorpus corpus{"absent"};
    const GoldenStore store{ corpus.root };

    // Two distinct keys in the SAME corpus root: present (stored) and absent (never
    // stored). They differ only by seed, so this also proves the store keys per-key,
    // not per-corpus.
    const GoldenKey present = keyAt(44100.0, DeterminismClass::Fp, 0x1u, /*seed=*/100u);
    const GoldenKey absent  = keyAt(44100.0, DeterminismClass::Fp, 0x1u, /*seed=*/200u);

    store.store(present, sampleResult(44100.0, tag()));

    // PRESENT arm (positive control).
    REQUIRE(store.has(present));
    REQUIRE_NOTHROW(store.load(present));

    // ABSENT arm (the acceptance assertion).
    REQUIRE_FALSE(store.has(absent));
    REQUIRE_THROWS_AS(store.load(absent), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Acceptance 3a — STABILITY: blobPath/sidecarPath are pure, stable functions of the
// GoldenKey — equal keys yield equal paths, recomputed identically [docs/design/11
// §7.1]. (No disk I/O is needed for the path derivation; it is a function of the key.)
// ---------------------------------------------------------------------------
TEST_CASE("golden: goldenstore blobPath and sidecarPath are stable functions of the key",
          "[golden]") {
    TempCorpus corpus{"stable"};
    const GoldenStore store{ corpus.root };

    const GoldenKey k1 = keyAt(48000.0, DeterminismClass::Fp, 0xAAu, /*seed=*/7u);
    const GoldenKey k2 = keyAt(48000.0, DeterminismClass::Fp, 0xAAu, /*seed=*/7u);  // equal

    // Same key -> same paths (stable / deterministic).
    REQUIRE(store.blobPath(k1) == store.blobPath(k2));
    REQUIRE(store.sidecarPath(k1) == store.sidecarPath(k2));

    // The two artifact paths for a single key are DISTINCT (a blob is not its sidecar).
    REQUIRE(store.blobPath(k1) != store.sidecarPath(k1));

    // Both live UNDER the corpus root the store was constructed with.
    const auto rel = [&](const fs::path& p) {
        return p.lexically_relative(corpus.root).native().rfind("..", 0) != 0;
    };
    REQUIRE(rel(store.blobPath(k1)));
    REQUIRE(rel(store.sidecarPath(k1)));
}

// ---------------------------------------------------------------------------
// Acceptance 3b — DISTINCTNESS across the determinism class and the sample rate
// [docs/design/11 §7.1]. Two keys that differ ONLY in determinism class, or ONLY in
// sample rate, must derive DISTINCT blob+sidecar paths (the path is partitioned by
// both). This is the negative control against a path derivation that collapses the
// class/SR axes (which would let a 48 kHz EXACT golden overwrite a 48 kHz FP golden).
// ---------------------------------------------------------------------------
TEST_CASE("golden: goldenstore paths are distinct across determinism class and sample rate",
          "[golden]") {
    TempCorpus corpus{"distinct"};
    const GoldenStore store{ corpus.root };

    // Hold everything fixed EXCEPT the one axis under test.
    const std::uint64_t graph = 0xCAFEu;
    const std::uint64_t seed  = 0x9u;

    // --- Axis 1: determinism class (same SR, same hash/seed) ---
    const GoldenKey exact48 =
        mw::golden::makeGoldenKey(graph, tag(), 48000.0, 64, seed, DeterminismClass::Exact);
    const GoldenKey fp48 =
        mw::golden::makeGoldenKey(graph, tag(), 48000.0, 64, seed, DeterminismClass::Fp);

    REQUIRE(store.blobPath(exact48)    != store.blobPath(fp48));
    REQUIRE(store.sidecarPath(exact48) != store.sidecarPath(fp48));

    // The class partition is reflected in the directory layout (a class-named partition
    // segment), so the two classes never share a directory — guards an accidental
    // overwrite across classes.
    REQUIRE(store.blobPath(exact48).parent_path() != store.blobPath(fp48).parent_path());

    // --- Axis 2: sample rate (same class, same hash/seed) ---
    const GoldenKey fp44 =
        mw::golden::makeGoldenKey(graph, tag(), 44100.0, 64, seed, DeterminismClass::Fp);
    const GoldenKey fp96 =
        mw::golden::makeGoldenKey(graph, tag(), 96000.0, 64, seed, DeterminismClass::Fp);

    REQUIRE(store.blobPath(fp44)    != store.blobPath(fp96));
    REQUIRE(store.sidecarPath(fp44) != store.sidecarPath(fp96));
    REQUIRE(store.blobPath(fp44).parent_path() != store.blobPath(fp96).parent_path());

    // ...and the three blessed SRs all map to mutually-distinct partitions.
    REQUIRE(store.blobPath(fp44) != store.blobPath(fp48));
    REQUIRE(store.blobPath(fp48) != store.blobPath(fp96));

    // Negative control on the hash axis too: a different render-graph hash (same class
    // + SR) is a different blob, so two distinct goldens in one partition never clash.
    const GoldenKey fp48b =
        mw::golden::makeGoldenKey(0xBEEFu, tag(), 48000.0, 64, seed, DeterminismClass::Fp);
    REQUIRE(store.blobPath(fp48) != store.blobPath(fp48b));
}

// ---------------------------------------------------------------------------
// The sidecar JSON records the GoldenKey fields + a human-readable render-graph
// description [task 077 Scope; docs/design/11 §7.1]. We assert the sidecar contains the
// key's identifying fields verbatim so a human (or a downstream MANIFEST audit) can read
// what was rendered. This is a content check, NOT a full JSON-parser contract.
// ---------------------------------------------------------------------------
TEST_CASE("golden: goldenstore sidecar JSON records the GoldenKey fields and a graph description",
          "[golden]") {
    TempCorpus corpus{"sidecar"};
    const GoldenStore store{ corpus.root };

    const EngineTag t   = tag(/*oversample=*/2, /*renderVersion=*/1);
    const GoldenKey key = mw::golden::makeGoldenKey(
        0x0102030405060708ull, t, /*sampleRate=*/88200.0, /*blockSize=*/256,
        /*seed=*/0xDEADBEEFull, DeterminismClass::Fp);
    store.store(key, sampleResult(88200.0, t));

    std::ifstream in{ store.sidecarPath(key) };
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();

    // The identifying GoldenKey fields appear in the sidecar (numeric values as text).
    REQUIRE(json.find("88200") != std::string::npos);     // sampleRate
    REQUIRE(json.find("256")   != std::string::npos);      // blockSize
    const bool hasFpClass = json.find("\"Fp\"") != std::string::npos
                         || json.find("\"FP\"") != std::string::npos;
    REQUIRE(hasFpClass);                                    // determinism class
    REQUIRE(json.find("renderVersion") != std::string::npos);
    REQUIRE(json.find("oversampleFactor") != std::string::npos);
    // A human-readable render-graph description is present.
    REQUIRE(json.find("renderGraph") != std::string::npos);
}
