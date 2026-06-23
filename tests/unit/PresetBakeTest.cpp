// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/PresetBakeTest.cpp — the deterministic flat-POD preset bake loader
// (task 144b). Test-case names begin with "presets_bake"; tag is [presets_bake].
//
// Realizes the task acceptance criteria 1:1 [docs/design/11 §9.1; ADR-001 C3/C4;
// ADR-014 C9]:
//
//  - AUDIO-THREAD POD-ONLY: an AudioThreadGuard-fenced scope reads baked entries
//    (param values + the seq POD + meta) and indexes the contiguous POD table with
//    ZERO heap allocation and ZERO JSON/preset parse. The parse/projection (the
//    bake) runs entirely OUTSIDE the armed window. A positive control proves the
//    guard would catch an allocation, so the no-alloc assertion is non-vacuous.
//    (This case ARMS the guard, so it is RUN_SERIAL via cmake/SerialTests.cmake,
//    per task 184.)
//
//  - CHECKSUM + SCHEMA: every baked entry stores schemaVersion + a SHA-256 (via the
//    task-040 mw::golden::sha256, injected as the hasher) computed at bake time over
//    the entry's canonical byte image. A load verifies the checksum WITHOUT
//    re-parsing the JSON; corrupting a single byte of a baked entry makes
//    verification FAIL (non-vacuity for the checksum).
//
//  - DETERMINISM: re-baking the SAME patch set yields a BYTE-IDENTICAL POD table
//    (the canonical serialization of the whole table hashes equal across two
//    independent bakes). A mutation of one input value changes the table hash
//    (non-vacuity for determinism).
//
// The baker/loader + POD table are JUCE-FREE mwcore code; this test links mwcore
// only (the core test binary). The .mw101preset corpus is enumerated from the
// MW_PRESETS_DIR compile definition (the real presets/ tree).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "preset/PresetBake.h"          // mwcore (JUCE-free): the baker + POD table
#include "params/ParamDefs.h"           // kParamDefs (ordering / count)
#include "state/Extras.h"               // mw::state::Extras (the seq POD)
#include "version/EngineVersion.h"      // kCurrentSchemaVersion

#include "../invariants/AudioThreadGuard.h"  // the alloc/lock sentinel (task 010)
#include "../golden/Sha256.h"                // task-040 SHA-256 (the injected hasher)

namespace pb = mw::preset;

namespace {

// The real factory corpus directory, injected by tests/CMakeLists.txt.
#ifndef MW_PRESETS_DIR
#  error "MW_PRESETS_DIR must be defined by the build (the presets/ corpus root)."
#endif

// Read one file fully into a string (message-thread / bake-time work — never on the
// audio thread).
std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s;
}

// Enumerate every .mw101preset under the corpus root, returned in a DETERMINISTIC
// order (lexicographic by relative path) so the bake input order is reproducible.
std::vector<pb::PresetSource> gatherCorpus() {
    std::vector<std::filesystem::path> paths;
    for (const auto& e : std::filesystem::recursive_directory_iterator(MW_PRESETS_DIR)) {
        if (e.is_regular_file() && e.path().extension() == ".mw101preset")
            paths.push_back(e.path());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<pb::PresetSource> sources;
    sources.reserve(paths.size());
    for (const auto& p : paths) {
        const auto rel = std::filesystem::relative(p, MW_PRESETS_DIR).generic_string();
        sources.push_back(pb::PresetSource{ rel, slurp(p) });
    }
    return sources;
}

// A heap allocation the compiler cannot elide (size read from a volatile), so
// [expr.new] allocation-elision does not apply and operator new is genuinely called.
// Mirrors tests/invariants/AudioThreadGuardTest.cpp's positive-control helper.
[[nodiscard]] void* nonElidableAlloc() noexcept {
    volatile std::size_t n = 64;
    return ::operator new(static_cast<std::size_t>(n));
}

// The injected hasher: the task-040 SHA-256 over a byte span, returned as the 32-byte
// digest the baker stores per entry.
pb::Checksum sha256Hasher(std::span<const std::byte> bytes) {
    const mw::golden::Sha256 d = mw::golden::sha256(bytes);
    pb::Checksum out{};
    out.bytes = d.bytes;
    return out;
}

} // namespace

TEST_CASE("presets_bake: the corpus bakes to a non-empty flat POD table", "[presets_bake]") {
    const auto sources = gatherCorpus();
    REQUIRE(sources.size() >= 60);   // ~64 factory patches + INIT [ADR-014 C9]

    const pb::BakedTable table = pb::bake(sources, sha256Hasher);
    REQUIRE(table.size() == sources.size());

    // The table is contiguous POD: a trivially-copyable entry of a fixed sizeof, with
    // the param values in kParamDefs order [docs/design/11 §9.1; ADR-014 C9].
    STATIC_REQUIRE(std::is_trivially_copyable_v<pb::BakedPreset>);
    STATIC_REQUIRE(pb::BakedPreset::kParamCount == mw::params::kParamDefs.size());

    // INIT is in the corpus and projected correctly: its saw level is 0.8 in §11.
    bool foundInit = false;
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (std::string_view{ table.entry(i).name() } == "INIT") {
            foundInit = true;
            const int sawIdx = pb::paramIndex("mw101.saw.level");
            REQUIRE(sawIdx >= 0);
            REQUIRE(table.entry(i).params[static_cast<std::size_t>(sawIdx)] == 0.8f);
            REQUIRE(table.entry(i).schemaVersion == mw101::version::kCurrentSchemaVersion);
        }
    }
    REQUIRE(foundInit);
}

TEST_CASE("presets_bake: each entry carries schemaVersion plus a bake-time SHA-256 that "
          "verifies without re-parsing", "[presets_bake]") {
    const auto sources = gatherCorpus();
    pb::BakedTable table = pb::bake(sources, sha256Hasher);
    REQUIRE(table.size() >= 60);

    // Every entry verifies against its stored checksum WITHOUT touching the JSON: the
    // verifier recomputes the canonical-image hash with the SAME injected hasher.
    for (std::size_t i = 0; i < table.size(); ++i) {
        REQUIRE(table.entry(i).schemaVersion == mw101::version::kCurrentSchemaVersion);
        REQUIRE(pb::verify(table.entry(i), sha256Hasher));
    }

    // NON-VACUITY (checksum): corrupt a single payload byte of one entry -> its stored
    // checksum no longer matches its content, so verify() FAILS. (We flip a param bit,
    // not the checksum field itself, to prove the checksum guards the payload.)
    pb::BakedPreset& victim = table.entryMutable(0);
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(victim.params[0]);
    victim.params[0] = std::bit_cast<float>(bits ^ 0x1u);
    REQUIRE_FALSE(pb::verify(victim, sha256Hasher));
}

TEST_CASE("presets_bake: re-baking the same patch set yields a byte-identical POD table",
          "[presets_bake]") {
    const auto sources = gatherCorpus();

    const pb::BakedTable a = pb::bake(sources, sha256Hasher);
    const pb::BakedTable b = pb::bake(sources, sha256Hasher);

    // The canonical serialization of the whole table hashes equal across two
    // independent bakes [docs/design/11 §9.1]. This is the platform-stable byte image
    // (fixed little-endian / fixed-width), not raw struct memory with padding.
    const mw::golden::Sha256 ha = mw::golden::sha256(pb::canonicalImage(a));
    const mw::golden::Sha256 hb = mw::golden::sha256(pb::canonicalImage(b));
    REQUIRE(ha == hb);

    // NON-VACUITY (determinism): change ONE input param value and the table hash must
    // change. Mutate the first source's saw level token deterministically.
    auto mutated = sources;
    {
        auto& json = mutated.front().json;
        const auto pos = json.find("\"mw101.saw.level\":");
        REQUIRE(pos != std::string::npos);
        const auto colon = json.find(':', pos);
        const auto end = json.find_first_of(",}\n", colon + 1);
        json.replace(colon + 1, end - (colon + 1), " 0.123");
    }
    const pb::BakedTable c = pb::bake(mutated, sha256Hasher);
    const mw::golden::Sha256 hc = mw::golden::sha256(pb::canonicalImage(c));
    REQUIRE_FALSE(ha == hc);
}

TEST_CASE("presets_bake: the audio thread reads only the POD table — no parse, no alloc "
          "under the guard", "[presets_bake][rt]") {
    const auto sources = gatherCorpus();

    // The ENTIRE parse/projection (the bake) runs BEFORE the armed window. The audio
    // thread is handed only the finished POD table.
    const pb::BakedTable table = pb::bake(sources, sha256Hasher);
    REQUIRE(table.size() >= 60);

    // POSITIVE CONTROL (its own guard instance — violations accumulate per instance):
    // prove the guard catches an allocation, so the no-alloc assertion below is
    // non-vacuous.
    {
        mw::test::AudioThreadGuard positive;
        positive.arm();
        void* leak = nonElidableAlloc();   // a non-elidable allocation while armed
        positive.disarm();
        REQUIRE(positive.violated());
        ::operator delete(leak);           // freed outside the armed scope
    }

    // The hot-path read: index the contiguous POD table and consume each entry's
    // param values + the seq POD + meta length. No JSON parse, no projection, no heap.
    mw::test::AudioThreadGuard guard;
    float acc = 0.0f;
    std::int64_t seqAcc = 0;
    {
        guard.arm();
        for (std::size_t i = 0; i < table.size(); ++i) {
            const pb::BakedPreset& e = table.entry(i);   // pure POD index, no alloc
            for (const float v : e.params) acc += v;
            seqAcc += e.extras.stepCount;
            for (int s = 0; s < e.extras.stepCount; ++s)
                seqAcc += e.extras.steps[static_cast<std::size_t>(s)].noteSemitone;
            seqAcc += static_cast<std::int64_t>(e.schemaVersion);
        }
        guard.disarm();
    }

    REQUIRE_FALSE(guard.violated());   // ZERO heap allocations reading the POD table
    REQUIRE(acc != 0.0f);              // the read did real work (non-vacuous)
    REQUIRE(seqAcc >= 0);
}
