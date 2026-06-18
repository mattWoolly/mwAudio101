// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the shared voice/control PODs, enums, and pool
// constants (task 067). Names begin with "voicetypes" so `-R voicetypes`
// selects them under the silent-pass rule.
//
// Asserts the §3.1-§3.4 contract of docs/design/04-voice-and-control.md:
//   - kMaxVoices == kMaxPoly * kMaxUnison (compile-time invariant)
//   - enum underlying types + member order (Mono=0, Gate=0, Idle=0)
//   - NoteEvent / NoteDecision field names/types/defaults verbatim
//   - all PODs trivially copyable; uint8_t-backed enums

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

#include "voice/VoiceTypes.h"

// --- §3.1 compile-time pool sizing -----------------------------------------

TEST_CASE("voicetypes: kMaxVoices == kMaxPoly * kMaxUnison (pool invariant)", "[core]") {
    STATIC_REQUIRE(mw::kMaxVoices == mw::kMaxPoly * mw::kMaxUnison);
    // §3.1: kMaxUnison = 8, kMaxPoly = 8, kMaxVoices = 64 [ADR-006 §3, C9].
    STATIC_REQUIRE(mw::kMaxUnison == 8);
    STATIC_REQUIRE(mw::kMaxPoly == 8);
    STATIC_REQUIRE(mw::kMaxVoices == 64);
    // Negative control: the product really is enforced, not just hard-coded 64.
    STATIC_REQUIRE(mw::kMaxVoices != mw::kMaxPoly + mw::kMaxUnison);
}

TEST_CASE("voicetypes: pool caps are int constexpr referencing the calibration table", "[core]") {
    STATIC_REQUIRE(std::is_same_v<decltype(mw::kMaxUnison), const int>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::kMaxPoly), const int>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::kMaxVoices), const int>);
    // The (PI) caps live in the calibration namespace; VoiceTypes references them.
    STATIC_REQUIRE(mw::kMaxUnison == mw::cal::voice::kMaxUnison);
    STATIC_REQUIRE(mw::kMaxPoly == mw::cal::voice::kMaxPoly);
}

// --- §3.2 modes and coupled selector ---------------------------------------

TEST_CASE("voicetypes: VoiceMode underlying type and member order match sec 3.2", "[core]") {
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<mw::VoiceMode>, std::uint8_t>);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceMode::Mono) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceMode::Unison) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceMode::Poly) == 2);
}

TEST_CASE("voicetypes: GateTrigMode underlying type and member order match sec 3.2", "[core]") {
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<mw::GateTrigMode>, std::uint8_t>);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::GateTrigMode::Gate) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::GateTrigMode::GateTrig) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::GateTrigMode::Lfo) == 2);
}

TEST_CASE("voicetypes: VoiceState underlying type and member order match sec 3.2", "[core]") {
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<mw::VoiceState>, std::uint8_t>);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceState::Idle) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceState::Active) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceState::Releasing) == 2);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::VoiceState::Stealing) == 3);
}

// --- §3.3 note events and decisions ----------------------------------------

TEST_CASE("voicetypes: NoteEvent field names/types match sec 3.3 verbatim", "[core]") {
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<mw::NoteEvent::Type>, std::uint8_t>);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::NoteEvent::Type::NoteOn) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::NoteEvent::Type::NoteOff) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(mw::NoteEvent::Type::AllNotesOff) == 2);

    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteEvent::type), mw::NoteEvent::Type>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteEvent::note), std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteEvent::velocity), float>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteEvent::sampleOffset), int>);
}

TEST_CASE("voicetypes: NoteDecision field names/types/defaults match sec 3.3 verbatim", "[core]") {
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteDecision::activeNote), int>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteDecision::gate), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteDecision::retrigger), bool>);
    STATIC_REQUIRE(std::is_same_v<decltype(mw::NoteDecision::clockReset), bool>);

    // Documented defaults: activeNote=-1, the rest false.
    constexpr mw::NoteDecision d{};
    STATIC_REQUIRE(d.activeNote == -1);
    STATIC_REQUIRE(d.gate == false);
    STATIC_REQUIRE(d.retrigger == false);
    STATIC_REQUIRE(d.clockReset == false);
}

// --- all PODs trivially copyable -------------------------------------------

TEST_CASE("voicetypes: all shared types are trivially copyable PODs", "[core]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::NoteEvent>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<mw::NoteDecision>);
    STATIC_REQUIRE(std::is_standard_layout_v<mw::NoteEvent>);
    STATIC_REQUIRE(std::is_standard_layout_v<mw::NoteDecision>);

    // Enums are trivially copyable by definition; assert their byte size is uint8_t.
    STATIC_REQUIRE(sizeof(mw::VoiceMode) == 1);
    STATIC_REQUIRE(sizeof(mw::GateTrigMode) == 1);
    STATIC_REQUIRE(sizeof(mw::VoiceState) == 1);
    STATIC_REQUIRE(sizeof(mw::NoteEvent::Type) == 1);
}
