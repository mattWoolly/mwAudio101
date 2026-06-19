// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for core/params/ParamSnapshot.h (task 102b). Test-case names
// begin with "paramsnapshot" so `-R paramsnapshot` selects exactly this suite (the
// silent-pass rule, AGENTS.md). No '[' in display text — only in the trailing tag.
//
// Covers the task's Acceptance criteria objectively:
//  * mw::ParamSnapshot is a JUCE-free, trivially-copyable, standard-layout POD with one
//    slot per LIVE kParamDefs entry [docs/design/00 §5.2/§5.4; ADR-001 C7/C14].
//  * It is the concrete type BlockContext::params (forward-declared in core/BlockContext.h)
//    points at — including both headers yields a complete type and a usable pointer.
//  * A test constructs/fills it and reads back per-param normalized values + indices.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

#include "BlockContext.h"             // forward-declares mw::ParamSnapshot + holds the pointer
#include "params/ParamDefs.h"         // mw::params::kParamDefs — the field universe
#include "params/ParamSnapshot.h"     // the type under test

// --- Acceptance: trivially-copyable, standard-layout POD (ADR-001 C7) -----------
TEST_CASE("paramsnapshot: is a trivially-copyable standard-layout POD", "[paramsnapshot]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::ParamSnapshot>);
    STATIC_REQUIRE(std::is_standard_layout_v<mw::ParamSnapshot>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<mw::ParamSnapshot>);
    // Aggregate, default-constructible (it is filled off the audio thread by the bridge).
    STATIC_REQUIRE(std::is_default_constructible_v<mw::ParamSnapshot>);
    STATIC_REQUIRE(std::is_aggregate_v<mw::ParamSnapshot>);
}

// --- Acceptance: a slot per LIVE kParamDefs entry, in registry-index order -------
TEST_CASE("paramsnapshot: holds exactly one slot per live kParamDefs entry", "[paramsnapshot]") {
    STATIC_REQUIRE(mw::ParamSnapshot::kCount
                   == static_cast<int>(mw::params::kParamDefs.size()));
    // The registry is the 91-entry live set (docs/design/06 §3.0).
    STATIC_REQUIRE(mw::ParamSnapshot::kCount == 91);

    mw::ParamSnapshot snap{};
    REQUIRE(snap.count() == static_cast<int>(mw::params::kParamDefs.size()));
}

// --- Acceptance: default-constructed snapshot is zero-initialized -----------------
TEST_CASE("paramsnapshot: default construction zero-fills every slot", "[paramsnapshot]") {
    const mw::ParamSnapshot snap{};
    for (int i = 0; i < mw::ParamSnapshot::kCount; ++i) {
        REQUIRE(snap.normalized(i) == 0.0f);
        REQUIRE(snap.index(i) == 0);
    }
}

// --- Acceptance: construct/fill, then read back per-param normalized values -------
TEST_CASE("paramsnapshot: fills and reads back per-param normalized values", "[paramsnapshot]") {
    mw::ParamSnapshot snap{};

    // Fill each slot with a distinct, in-range normalized value derived from its index.
    for (int i = 0; i < mw::ParamSnapshot::kCount; ++i) {
        snap.normalizedValues[static_cast<std::size_t>(i)] =
            static_cast<float>(i) / static_cast<float>(mw::ParamSnapshot::kCount);
    }

    // Read back exactly what was written.
    for (int i = 0; i < mw::ParamSnapshot::kCount; ++i) {
        const float expected =
            static_cast<float>(i) / static_cast<float>(mw::ParamSnapshot::kCount);
        REQUIRE(snap.normalized(i) == expected);
    }

    // Spot-check the accessor against the underlying array for a known registry slot.
    // kParamDefs[10] is mw101.vcf.cutoff (continuous); set it explicitly and read back.
    constexpr int kCutoffSlot = 10;
    STATIC_REQUIRE(kCutoffSlot < mw::ParamSnapshot::kCount);
    snap.normalizedValues[static_cast<std::size_t>(kCutoffSlot)] = 0.75f;
    REQUIRE(snap.normalized(kCutoffSlot) == 0.75f);
}

// --- Acceptance: choice/bool slots carry a typed-enum option index ---------------
TEST_CASE("paramsnapshot: choice and bool slots carry a typed-enum option index", "[paramsnapshot]") {
    mw::ParamSnapshot snap{};

    // Find the first Choice param and store an option index; read it back.
    int choiceSlot = -1;
    for (int i = 0; i < mw::ParamSnapshot::kCount; ++i) {
        if (mw::params::kParamDefs[static_cast<std::size_t>(i)].type
            == mw::params::ParamType::Choice) {
            choiceSlot = i;
            break;
        }
    }
    REQUIRE(choiceSlot >= 0);

    snap.indexValues[static_cast<std::size_t>(choiceSlot)] = 2;
    REQUIRE(snap.index(choiceSlot) == 2);

    // The index storage is wide enough for any choiceCount in the registry (uint8 max).
    std::uint8_t maxCount = 0;
    for (const auto& d : mw::params::kParamDefs) {
        if (d.choiceCount > maxCount) maxCount = d.choiceCount;
    }
    snap.indexValues[static_cast<std::size_t>(choiceSlot)] =
        static_cast<std::int16_t>(maxCount);
    REQUIRE(snap.index(choiceSlot) == static_cast<int>(maxCount));
}

// --- Acceptance: it IS the type BlockContext::params resolves to ------------------
TEST_CASE("paramsnapshot: resolves the BlockContext params pointer to the real type",
          "[paramsnapshot]") {
    // BlockContext forward-declares mw::ParamSnapshot and holds const ParamSnapshot*.
    // Including ParamSnapshot.h completes that type so the pointer is dereferenceable.
    STATIC_REQUIRE(std::is_same_v<decltype(mw::BlockContext::params),
                                  const mw::ParamSnapshot*>);

    const mw::ParamSnapshot snap{};
    mw::BlockContext ctx{};
    ctx.params = &snap;                       // a real, usable pointer to the concrete POD

    REQUIRE(ctx.params == &snap);
    REQUIRE(ctx.params->count() == mw::ParamSnapshot::kCount);   // dereference proves completeness
}

// --- Sanity: copying the snapshot is a plain value copy (no aliasing) -------------
TEST_CASE("paramsnapshot: value-copies independently of the source", "[paramsnapshot]") {
    mw::ParamSnapshot a{};
    a.normalizedValues[0] = 0.5f;
    a.indexValues[1]      = 3;

    mw::ParamSnapshot b = a;                  // trivial copy
    REQUIRE(b.normalized(0) == 0.5f);
    REQUIRE(b.index(1) == 3);

    // Mutating the copy does not touch the source.
    b.normalizedValues[0] = 0.9f;
    REQUIRE(a.normalized(0) == 0.5f);
    REQUIRE(b.normalized(0) == 0.9f);
}
