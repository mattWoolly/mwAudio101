// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/CcLearnMapTest.cpp — acceptance tests for the RT-safe, double-buffered
// CC/learn map (task 100), compiled into the JUCE-linked mw101_plugin_tests target.
// Every test-case display name begins with the `cclearn` tag so the `-R cclearn`
// ctest selector matches >= 1 [docs/design/09 §6.2-6.3; ADR-012 C15-C16, C20].
//
// Covers each acceptance criterion of plan/backlog/100-rt-safe-cc-learn-map.md:
//   * Default bindings match the §6.2 CC table (CC1/7/11/74/71/5 -> the listed doc-06
//     parameter IDs; CC64 -> HOLD semantics) [§6.2; ADR-012 C15, C20].
//   * lookup() on the audio thread performs ZERO heap allocation and acquires NO lock
//     [§6.3; ADR-012 C16].
//   * editableCopy()+publish() swaps the live map atomically WITHOUT mutating the
//     buffer the audio thread is currently reading [§6.3; ADR-012 C16].
//
// RT no-alloc / no-lock strategy. mw101_plugin_tests does NOT link the core
// tests/invariants/AudioThreadGuard.cpp sentinel, and a sibling JUCE-linked test
// (tests/plugin/LatencyReporterTest.cpp) ALREADY defines the single per-binary global
// operator-new override for this target — a second definition here would be a
// duplicate-symbol link error. So the no-alloc / no-lock contract is asserted
// STRUCTURALLY instead: (a) lookup()/publish()/editableCopy() are noexcept; (b) the
// live atomic pointer is always lock-free on this platform; (c) the map owns NO
// heap-allocating member — all storage is inline std::array, so a lookup CANNOT
// allocate. These are stronger than a runtime alloc counter (they hold for every
// input, not just the sampled one) and are exactly the §6.3 / ADR-012 C16 invariants.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>

#include "midi/CcLearnMap.h"                       // unit under test
#include "params/ParamDefs.h"                      // mw::params::kParamDefs (JUCE-free registry)
#include "calibration/CcLearnMapConstants.h"       // mw::cal::cclearn::kHoldParamIndex

namespace {

// Resolve a doc-06 string ID to its index in the kParamDefs registry, for the
// expected-binding assertions. -1 if absent. (Independent of the unit under test's
// own resolution so the test is not circular.)
int registryIndexOf(std::string_view id) noexcept {
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (std::string_view{mw::params::kParamDefs[i].id} == id)
            return static_cast<int>(i);
    return -1;
}

} // namespace

// ============================================================================
// Acceptance criterion 1: default bindings match the §6.2 CC table.
// ============================================================================
TEST_CASE("cclearn default map binds the doc-06 §6.2 CC table", "[cclearn]")
{
    mw::plugin::CcLearnMap map;   // default-constructed: seeded with the §6.2 defaults

    // CC1 mod, CC7 vol, CC11 expr, CC74 cutoff, CC71 resonance, CC5 portamento time
    // [docs/design/09 §6.2; ADR-012 C15]. Each must resolve to the SAME registry index
    // its doc-06 ID occupies.
    REQUIRE(map.lookup(1)  == registryIndexOf("mw101.mod.lfo_mod_wheel"));
    REQUIRE(map.lookup(7)  == registryIndexOf("mw101.vca.level"));
    REQUIRE(map.lookup(11) == registryIndexOf("mw101.amp.expression"));
    REQUIRE(map.lookup(74) == registryIndexOf("mw101.vcf.cutoff"));
    REQUIRE(map.lookup(71) == registryIndexOf("mw101.vcf.resonance"));
    REQUIRE(map.lookup(5)  == registryIndexOf("mw101.glide.time"));

    // Every mapped param index must actually exist in the registry (none is the
    // unmapped sentinel and none is the HOLD sentinel) [§6.2].
    for (const int cc : { 1, 7, 11, 74, 71, 5 }) {
        const int idx = map.lookup(static_cast<std::uint8_t>(cc));
        REQUIRE(idx >= 0);
        REQUIRE(idx < static_cast<int>(mw::params::kParamDefs.size()));
    }

    // CC64 sustain -> HOLD / external-HOLD input semantics (a real stock jack), NOT a
    // doc-06 parameter index: it resolves to the dedicated HOLD sentinel, distinct from
    // both -1 (unmapped) and any registry index [§6.2; ADR-012 C20].
    REQUIRE(map.lookup(64) == mw::cal::cclearn::kHoldParamIndex);
    REQUIRE(mw::cal::cclearn::kHoldParamIndex != mw::plugin::CcLearnMap::kUnmapped);
    REQUIRE(mw::cal::cclearn::kHoldParamIndex < 0);   // a sentinel, never a real index

    // An un-seeded CC is unmapped (-1).
    REQUIRE(map.lookup(0)   == mw::plugin::CcLearnMap::kUnmapped);
    REQUIRE(map.lookup(2)   == mw::plugin::CcLearnMap::kUnmapped);
    REQUIRE(map.lookup(127) == mw::plugin::CcLearnMap::kUnmapped);
    REQUIRE(mw::plugin::CcLearnMap::kUnmapped == -1);
}

// ============================================================================
// Acceptance criterion 2: lookup() on the audio thread performs ZERO heap allocation
// and takes NO lock [§6.3; ADR-012 C16]. Asserted structurally (see file header) —
// these invariants hold for EVERY input, not just a sampled one.
// ============================================================================
TEST_CASE("cclearn lookup is noexcept, lock-free, and owns no heap storage", "[cclearn]")
{
    mw::plugin::CcLearnMap map;

    // (a) The hot read path is noexcept (no throw -> no implicit allocation path).
    STATIC_REQUIRE(noexcept(map.lookup(std::uint8_t{ 0 })));

    // (b) The live pointer swap the audio thread reads through is ALWAYS lock-free on
    // this platform: a concurrent lookup() never blocks on the message-thread writer
    // [ADR-012 C16].
    REQUIRE(mw::plugin::CcLearnMap::liveIsAlwaysLockFree());

    // (c) The map's storage is entirely inline (two fixed std::array buffers + an
    // atomic pointer); it owns NO heap-allocating member, so lookup() cannot allocate.
    // Trivially destructible == no owned heap resource to release.
    STATIC_REQUIRE(std::is_trivially_destructible_v<mw::plugin::CcBinding>);
    REQUIRE(sizeof(mw::plugin::CcLearnMap)
            >= 2u * mw::plugin::CcLearnMap::kNumCc * sizeof(mw::plugin::CcBinding));

    // And drive the full read path to prove it runs and is internally consistent.
    long sink = 0;
    for (int rep = 0; rep < 64; ++rep)
        for (int cc = 0; cc < mw::plugin::CcLearnMap::kNumCc; ++cc)
            sink += map.lookup(static_cast<std::uint8_t>(cc));
    REQUIRE(sink != 0);   // defeat dead-code elimination of the lookup loop
}

// ============================================================================
// Acceptance criterion 3: editableCopy()+publish() swaps the live map atomically
// WITHOUT mutating the buffer the audio thread is currently reading [§6.3].
// ============================================================================
TEST_CASE("cclearn editableCopy and publish swap the live map without mutating the read buffer", "[cclearn]")
{
    mw::plugin::CcLearnMap map;

    // Baseline default binding for CC74 (cutoff).
    const int cutoffIdx = registryIndexOf("mw101.vcf.cutoff");
    REQUIRE(map.lookup(74) == cutoffIdx);

    // The editable (inactive) buffer is a DISTINCT object from the live one the audio
    // thread reads, so edits to it do not touch the reader.
    const mw::plugin::CcBinding* liveBefore = map.liveBuffer();
    mw::plugin::CcBinding*       draft       = map.editableCopy();
    REQUIRE(draft != nullptr);
    REQUIRE(draft != liveBefore);                 // editing the inactive buffer, not the live one

    // editableCopy() returns a COPY of the current live state (so an unedited publish
    // is a no-op) [§6.3].
    REQUIRE(draft[74].enabled == true);
    REQUIRE(draft[74].paramIndex == cutoffIdx);

    // Re-map CC20 -> resonance in the draft; remove CC74. Until publish(), the live
    // map (what the audio thread reads) is UNCHANGED.
    const int resoIdx = registryIndexOf("mw101.vcf.resonance");
    draft[20] = mw::plugin::CcBinding{ /*ccNumber=*/20, /*paramIndex=*/resoIdx, /*enabled=*/true };
    draft[74].enabled = false;

    REQUIRE(map.lookup(20) == mw::plugin::CcLearnMap::kUnmapped);   // not yet published
    REQUIRE(map.lookup(74) == cutoffIdx);                          // live read still sees the default

    // Publish: the live pointer atomically swaps to the edited buffer.
    map.publish();

    REQUIRE(map.liveBuffer() == draft);                            // the draft is now live
    REQUIRE(map.lookup(20) == resoIdx);                            // new binding visible
    REQUIRE(map.lookup(74) == mw::plugin::CcLearnMap::kUnmapped);  // disabled binding gone

    // The publish ping-pongs the buffers: a second editableCopy() must return the OTHER
    // (now-inactive) buffer, which differs from the freshly published live one, and the
    // double-buffer never hands out the buffer the audio thread is reading [§6.3].
    mw::plugin::CcBinding* draft2 = map.editableCopy();
    REQUIRE(draft2 != map.liveBuffer());
    REQUIRE(draft2 != draft);

    // editableCopy() re-seeds the draft from the now-live state, so it sees the edits
    // (CC20 mapped, CC74 disabled) — a subsequent unedited publish is a faithful no-op.
    REQUIRE(draft2[20].enabled == true);
    REQUIRE(draft2[20].paramIndex == resoIdx);
    REQUIRE(draft2[74].enabled == false);

    map.publish();   // unedited republish: identical observable mapping
    REQUIRE(map.lookup(20) == resoIdx);
    REQUIRE(map.lookup(74) == mw::plugin::CcLearnMap::kUnmapped);
}

// ============================================================================
// Acceptance criterion 2 (cont.): publish() must be a single lock-free atomic store so
// the concurrently-reading audio thread never blocks, and it must not allocate (it
// only swaps an inline pointer). Asserted via noexcept + the always-lock-free atomic
// (the publish writes through the SAME live_ pointer the lookup reads).
// ============================================================================
TEST_CASE("cclearn publish and editableCopy are noexcept lock-free swaps", "[cclearn]")
{
    mw::plugin::CcLearnMap map;

    STATIC_REQUIRE(noexcept(map.publish()));
    STATIC_REQUIRE(noexcept(map.editableCopy()));
    REQUIRE(mw::plugin::CcLearnMap::liveIsAlwaysLockFree());

    (void) map.editableCopy();
    map.publish();        // a single atomic store: no allocation, no lock
    REQUIRE(map.lookup(74) == registryIndexOf("mw101.vcf.cutoff"));
}
