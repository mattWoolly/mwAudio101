// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the Envelope.h public API + POD-state declaration
// (task 050). Names begin with "env_header" so `-R env_header` selects them.
//
// This task declares ONLY the header surface from docs/design/03 §2.2 (the
// coefficient/curve math is the separate .cpp task). These tests therefore assert
// the *contract* of the declared layout — enum members, POD-ness, no heap members,
// the exact public signatures, and the noexcept hot paths (§2.1) — rather than any
// runtime envelope behavior.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

#include "dsp/Envelope.h"

using mw101::dsp::Envelope;
using mw101::dsp::EnvParams;
using mw101::dsp::EnvStage;
using mw101::dsp::EnvTrigMode;

// --- Enum contract (§2.2) ---------------------------------------------------

TEST_CASE("env_header: EnvStage has the five documented stages in order", "[env_header]") {
    // enum class EnvStage : uint8_t { Idle, Attack, Decay, Sustain, Release };
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<EnvStage>, std::uint8_t>);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvStage::Idle)    == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvStage::Attack)  == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvStage::Decay)   == 2);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvStage::Sustain) == 3);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvStage::Release) == 4);
}

TEST_CASE("env_header: EnvTrigMode has GateTrig/Gate/Lfo in order", "[env_header]") {
    // enum class EnvTrigMode : uint8_t { GateTrig, Gate, Lfo };
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<EnvTrigMode>, std::uint8_t>);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvTrigMode::GateTrig) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvTrigMode::Gate)     == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(EnvTrigMode::Lfo)      == 2);
}

// --- EnvParams POD + documented defaults (§2.2) -----------------------------

TEST_CASE("env_header: EnvParams is a standard-layout, trivially-copyable POD", "[env_header]") {
    // POD-state: standard-layout + trivially copyable + trivially destructible
    // (the in-class default-member-initializers make the default ctor non-trivial,
    // which is fine; "POD" here means no owning/heap members and bit-copyable).
    STATIC_REQUIRE(std::is_standard_layout_v<EnvParams>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<EnvParams>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<EnvParams>);
    STATIC_REQUIRE(std::is_aggregate_v<EnvParams>);   // brace-initializable POD struct
}

TEST_CASE("env_header: EnvParams carries the documented defaults", "[env_header]") {
    // Verbatim §2.2 defaults. These are documented out-of-box patch values, not
    // measured spec — asserted here only to pin the declared header surface.
    constexpr EnvParams p{};
    REQUIRE(p.attackSec  == 0.003f);
    REQUIRE(p.decaySec   == 0.060f);
    REQUIRE(p.sustain    == 0.7f);
    REQUIRE(p.releaseSec == 0.100f);
    REQUIRE(p.trig       == EnvTrigMode::GateTrig);
    REQUIRE(p.curve      == 1.0f);
}

// --- Envelope POD-state, no heap members (§2.1, ADR-020 S14) ----------------

TEST_CASE("env_header: Envelope is standard-layout with no heap members", "[env_header]") {
    // POD-state: trivially copyable, standard-layout, no owning/dynamic members.
    // A heap member (e.g. a vector/unique_ptr) would break trivial-copyability.
    STATIC_REQUIRE(std::is_standard_layout_v<Envelope>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Envelope>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<Envelope>);
}

TEST_CASE("env_header: a default-constructed Envelope is Idle, inactive, level 0", "[env_header]") {
    constexpr Envelope e{};
    STATIC_REQUIRE(e.stage() == EnvStage::Idle);
    STATIC_REQUIRE(e.active() == false);
    STATIC_REQUIRE(e.level() == 0.0f);
}

// --- Hot-path / lifecycle signatures are noexcept (§2.1) --------------------

TEST_CASE("env_header: prepare/reset/setParams/triggers/tick are all noexcept", "[env_header]") {
    Envelope e;
    const EnvParams p{};

    STATIC_REQUIRE(noexcept(e.prepare(48000.0, 1)));
    STATIC_REQUIRE(noexcept(e.reset()));
    STATIC_REQUIRE(noexcept(e.setParams(p)));
    STATIC_REQUIRE(noexcept(e.noteOn(false)));
    STATIC_REQUIRE(noexcept(e.noteOff()));
    STATIC_REQUIRE(noexcept(e.clockTrigger()));
    STATIC_REQUIRE(noexcept(e.tick()));
}

TEST_CASE("env_header: const accessors are noexcept and correctly typed", "[env_header]") {
    const Envelope e{};
    STATIC_REQUIRE(noexcept(e.stage()));
    STATIC_REQUIRE(noexcept(e.active()));
    STATIC_REQUIRE(noexcept(e.level()));

    STATIC_REQUIRE(std::is_same_v<decltype(e.stage()), EnvStage>);
    STATIC_REQUIRE(std::is_same_v<decltype(e.active()), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(e.level()), float>);
}

// --- Exact declared signatures (the env_header_api compile check, acceptance) -

TEST_CASE("env_header: env_header_api — public member signatures match §2.2", "[env_header]") {
    // Taking the address of each member with its fully-qualified type is a
    // compile-time assertion that the declared signature is exactly as specified.
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::prepare),
                                  void (Envelope::*)(double, int) noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::reset),
                                  void (Envelope::*)() noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::setParams),
                                  void (Envelope::*)(const EnvParams&) noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::noteOn),
                                  void (Envelope::*)(bool) noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::noteOff),
                                  void (Envelope::*)() noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::clockTrigger),
                                  void (Envelope::*)() noexcept>);
    STATIC_REQUIRE(std::is_same_v<decltype(&Envelope::tick),
                                  float (Envelope::*)() noexcept>);

    // tick() returns the normalized level type [0,1]; pin the return type.
    Envelope e;
    STATIC_REQUIRE(std::is_same_v<decltype(e.tick()), float>);
}
