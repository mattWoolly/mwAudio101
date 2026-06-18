// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// Layer-1 unit tests for the control-core POD header (task 081). Test-case names
// begin with "modseqtypes" so `-R modseqtypes` selects exactly this suite (the
// silent-pass rule). Each TEST_CASE maps to a 081 acceptance criterion and to the
// cited docs/design/05 sections (§3.2/§4.2/§5.4/§6.2/§7.7/§9.2).

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

#include "control/ControlTypes.h"

using namespace mw::control;

// --- §6.2 SeqStep: 1 byte + pitch/REST/TIE bit decode ------------------------

TEST_CASE("modseqtypes: SeqStep is exactly 1 byte (docs/design/05 sec 6.2)", "[modseqtypes]") {
    STATIC_REQUIRE(sizeof(SeqStep) == 1u);
    STATIC_REQUIRE(std::is_trivially_copyable_v<SeqStep>);
    STATIC_REQUIRE(std::is_standard_layout_v<SeqStep>);
    STATIC_REQUIRE(std::is_same_v<decltype(SeqStep::bits), std::uint8_t>);
}

TEST_CASE("modseqtypes: SeqStep masks match the sec 6.2 byte layout", "[modseqtypes]") {
    STATIC_REQUIRE(SeqStep::kPitchMask == 0x3F);  // [b5:b0] pitch (anl #3fh)
    STATIC_REQUIRE(SeqStep::kRestFlag  == 0x40);  // b6 REST
    STATIC_REQUIRE(SeqStep::kTieFlag   == 0x80);  // b7 TIE/legato
    // The three regions are disjoint and cover the whole byte.
    STATIC_REQUIRE((SeqStep::kPitchMask & SeqStep::kRestFlag) == 0);
    STATIC_REQUIRE((SeqStep::kPitchMask & SeqStep::kTieFlag)  == 0);
    STATIC_REQUIRE((SeqStep::kRestFlag  & SeqStep::kTieFlag)  == 0);
    STATIC_REQUIRE((SeqStep::kPitchMask | SeqStep::kRestFlag | SeqStep::kTieFlag) == 0xFF);
}

TEST_CASE("modseqtypes: SeqStep pitch/REST/TIE accessors decode the byte", "[modseqtypes]") {
    SeqStep s{};
    REQUIRE(s.pitch() == 0);
    REQUIRE_FALSE(s.isRest());
    REQUIRE_FALSE(s.isTie());

    // Pitch occupies the low 6 bits (0..63); 0x3F is the max pitch.
    SeqStep maxPitch{0x3F};
    REQUIRE(maxPitch.pitch() == 63);
    REQUIRE_FALSE(maxPitch.isRest());
    REQUIRE_FALSE(maxPitch.isTie());

    // Pitch is masked: high bits must not leak into pitch().
    SeqStep restWithPitch{static_cast<std::uint8_t>(SeqStep::kRestFlag | 0x05)};
    REQUIRE(restWithPitch.isRest());
    REQUIRE_FALSE(restWithPitch.isTie());
    REQUIRE(restWithPitch.pitch() == 5);

    SeqStep tieWithPitch{static_cast<std::uint8_t>(SeqStep::kTieFlag | 0x21)};
    REQUIRE(tieWithPitch.isTie());
    REQUIRE_FALSE(tieWithPitch.isRest());
    REQUIRE(tieWithPitch.pitch() == 0x21);

    // Both flags + pitch decode independently.
    SeqStep both{0xFF};
    REQUIRE(both.isRest());
    REQUIRE(both.isTie());
    REQUIRE(both.pitch() == 63);
}

// --- §6.2 SeqBuffer / kMaxSteps ----------------------------------------------

TEST_CASE("modseqtypes: kMaxSteps==100 and SeqBuffer is array<SeqStep,100>", "[modseqtypes]") {
    STATIC_REQUIRE(kMaxSteps == 100);
    STATIC_REQUIRE(std::is_same_v<SeqBuffer, std::array<SeqStep, 100>>);
    STATIC_REQUIRE(std::tuple_size_v<SeqBuffer> == 100u);
    STATIC_REQUIRE(sizeof(SeqBuffer) == 100u);  // 1 byte per slot
    STATIC_REQUIRE(std::is_trivially_copyable_v<SeqBuffer>);
}

// --- §3.2/§4.2/§5.4/§7.7 enums: uint8_t-backed + enumerator order ------------

TEST_CASE("modseqtypes: every stream enum is uint8_t-backed", "[modseqtypes]") {
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<PwmSource>,    std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<VcaSource>,    std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<TrigMode>,     std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<NotePriority>, std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<ArpMode>,      std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<ClockSource>,  std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<std::underlying_type_t<HostRate>,     std::uint8_t>);
}

TEST_CASE("modseqtypes: enum enumerator order matches docs/design/05", "[modseqtypes]") {
    // §3.2
    STATIC_REQUIRE(static_cast<std::uint8_t>(PwmSource::Env)    == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(PwmSource::Manual) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(PwmSource::Lfo)    == 2);

    STATIC_REQUIRE(static_cast<std::uint8_t>(VcaSource::Env)  == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(VcaSource::Gate) == 1);

    // §4.2
    STATIC_REQUIRE(static_cast<std::uint8_t>(TrigMode::GateTrig) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TrigMode::Gate)     == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(TrigMode::Lfo)      == 2);

    STATIC_REQUIRE(static_cast<std::uint8_t>(NotePriority::LastNote)   == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(NotePriority::LowestNote) == 1);

    // §5.4
    STATIC_REQUIRE(static_cast<std::uint8_t>(ArpMode::Up)    == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(ArpMode::UandD) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(ArpMode::Down)  == 2);

    // §7.7
    STATIC_REQUIRE(static_cast<std::uint8_t>(ClockSource::Internal) == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(ClockSource::HostSync) == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(ClockSource::Ext)      == 2);

    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::Quarter)         == 0);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::Eighth)          == 1);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::EighthT)         == 2);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::Sixteenth)       == 3);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::SixteenthT)      == 4);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::ThirtySecond)    == 5);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::DottedEighth)    == 6);
    STATIC_REQUIRE(static_cast<std::uint8_t>(HostRate::DottedSixteenth) == 7);
}

// --- §2.3/§9.2 KeyEvent / ControlEvent block-boundary PODs -------------------

TEST_CASE("modseqtypes: KeyEvent and ControlEvent are trivially-copyable PODs", "[modseqtypes]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<KeyEvent>);
    STATIC_REQUIRE(std::is_standard_layout_v<KeyEvent>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ControlEvent>);
    STATIC_REQUIRE(std::is_standard_layout_v<ControlEvent>);

    // Both carry a sub-block sample offset (§2.3 time-stamped events).
    STATIC_REQUIRE(std::is_same_v<decltype(KeyEvent::sampleOffset), int>);
    STATIC_REQUIRE(std::is_same_v<decltype(ControlEvent::sampleOffset), int>);

    // ControlEvent carries the pitch/gate/trig/porta/mod payload (§2.3 data flow).
    ControlEvent ce{};
    (void) ce.pitch;
    (void) ce.gate;
    (void) ce.trig;
    (void) ce.porta;
    (void) ce.mod;
    (void) ce.sampleOffset;

    KeyEvent ke{};
    (void) ke.pitch;
    (void) ke.gate;
    (void) ke.trig;
    (void) ke.porta;
    (void) ke.mod;
    (void) ke.sampleOffset;
}

// --- §3.2/§4.2/§6.5/§7.7 mod + decision structs are PODs ---------------------

TEST_CASE("modseqtypes: ModInputs/ModOutputs/ModDepths are trivially-copyable PODs", "[modseqtypes]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModInputs>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModOutputs>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ModDepths>);
    STATIC_REQUIRE(std::is_standard_layout_v<ModInputs>);
    STATIC_REQUIRE(std::is_standard_layout_v<ModOutputs>);
    STATIC_REQUIRE(std::is_standard_layout_v<ModDepths>);
}

TEST_CASE("modseqtypes: TriggerDecision/KeyState/SeqPlayResult/ClockEdge are PODs", "[modseqtypes]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<TriggerDecision>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<KeyState>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<SeqPlayResult>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<ClockEdge>);

    // §4.2 KeyState: 32-key bitmap fields are uint32_t.
    STATIC_REQUIRE(std::is_same_v<decltype(KeyState::held),         std::uint32_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(KeyState::justPressed),  std::uint32_t>);
    STATIC_REQUIRE(std::is_same_v<decltype(KeyState::justReleased), std::uint32_t>);

    // §4.2 TriggerDecision defaults: no note selected, gate off, no retrigger.
    TriggerDecision td{};
    REQUIRE(td.selectedKey == -1);
    REQUIRE_FALSE(td.retrigger);
    REQUIRE_FALSE(td.gateOn);
    REQUIRE_FALSE(td.legato);

    // §6.5 SeqPlayResult defaults.
    SeqPlayResult sr{};
    REQUIRE(sr.pitch6 == 0);
    REQUIRE(sr.gateOn);
    REQUIRE_FALSE(sr.tie);
    REQUIRE(sr.retrigger);
    REQUIRE(sr.slotIndex == 0);

    // §7.7 ClockEdge default sample offset.
    ClockEdge ed{};
    REQUIRE(ed.sampleOffset == 0);
    STATIC_REQUIRE(std::is_same_v<decltype(ClockEdge::sampleOffset), int>);
}

// --- §9.2 ControlSnapshot immutable persistence POD --------------------------

TEST_CASE("modseqtypes: ControlSnapshot.schemaVersion defaults to 1 and is a POD", "[modseqtypes]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<ControlSnapshot>);
    STATIC_REQUIRE(std::is_standard_layout_v<ControlSnapshot>);
    STATIC_REQUIRE(std::is_same_v<decltype(ControlSnapshot::schemaVersion), std::uint32_t>);

    ControlSnapshot s{};
    REQUIRE(s.schemaVersion == 1u);                 // versioned from v1 (C25)

    // §9.2 documented default poles (INIT/out-of-box state).
    REQUIRE(s.seqCount == 0);
    REQUIRE(s.arpMode == ArpMode::Up);
    REQUIRE_FALSE(s.arpHold);
    REQUIRE_FALSE(s.uAndDRepeatEndpoints);
    REQUIRE(s.clockSource == ClockSource::Internal);
    REQUIRE(s.hostRate == HostRate::Sixteenth);
    REQUIRE(s.swing == 0.5f);
    REQUIRE(s.clockResetOnKeypress);
    REQUIRE(s.trigMode == TrigMode::GateTrig);
    REQUIRE(s.pwmSource == PwmSource::Lfo);
    REQUIRE(s.vcaSource == VcaSource::Env);
}

TEST_CASE("modseqtypes: ControlSnapshot embeds the 100-slot SeqBuffer", "[modseqtypes]") {
    STATIC_REQUIRE(std::is_same_v<decltype(ControlSnapshot::seq), SeqBuffer>);
    ControlSnapshot s{};
    REQUIRE(s.seq.size() == 100u);
}
