// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/MpeReconstructor.h — the MPE-over-MIDI per-channel rotation parser
// (task 103). Realizes docs/design/09 §7.3 / §7.1 and ADR-022 C2 / ADR-012 §4,
// C10-C13.
//
// For formats with no note-expression API (VST3 without MPE-mode handling, AU/Logic,
// LV2, Standalone), raw per-channel MIDI (per-channel pitch-bend + channel pressure /
// CC74) is reconstructed into the SAME per-voice pre-quantizer pitch offset + single
// assignable pressure value the CLAP Native rung produces [ADR-022 C2]. Lower zone
// ONLY: master = MIDI channel 1, members = MIDI channels 2..16, configurable member
// count (default OFF, opt-in 1..15) [ADR-012 C10; docs/design/09 §7.1].
//
// MPE-OFF (0 members) or master-channel messages COLLAPSE to channel pitch-bend +
// channel pressure applied globally to every voice — exactly ADR-012 C13's mono
// collapse, bit-identical to running without MPE.
//
// REAL-TIME INVARIANTS [docs/design/09 §1.3; ADR-022 C11]: all state is fixed-size
// std::array, sized once in prepare(); noteOn/noteOff/pitchBend/pressure are noexcept,
// allocation-free, and lock-free. There is no per-note timbre matrix — the hardware
// has one VCF and one VCA — so a voice carries ONLY a pre-Q pitch offset and ONE
// pressure value [ADR-012 C12; docs/design/09 §7.1].
//
// This class carries NO JUCE dependency; it is pure C++ over POD state. It compiles
// into the JUCE-linked plugin target and the plugin test binary identically.

#pragma once

#include <array>
#include <cstdint>

#include "../../core/calibration/MpeReconstructorConstants.h"

namespace mw::plugin {

// The single assignable pressure destination (ADR-012 C12). MPE-lite forbids a
// per-note timbre matrix, so this is ONE destination, default VCF cutoff CV. The
// selector itself is a doc-06 parameter (OUT of scope here, task param-schema); this
// enum names the routing target the reconstructor records against.
enum class PressureDestination : std::uint8_t {
    VcfCutoffCv = 0   // default = the documented, SDT-1000-scaled cutoff CV node
};

class MpeReconstructor {
public:
    // Sizes per-channel + per-voice state and resets. Off the audio thread; the SOLE
    // sizing point. maxVoices is clamped to the fixed voice-state capacity.
    void prepare(int maxVoices) noexcept;

    // Clears every channel<->voice assignment, the rotation cursor, and all per-voice
    // expression back to its (PI) rest state. RT-safe.
    void reset() noexcept;

    // Configure the enabled lower-zone member-channel count. Default OFF (0). Clamped
    // to [0, kMaxMembers] (== 15). count == 0 -> Collapsed rung [ADR-012 C10, C13].
    void setMemberCount(int count) noexcept;
    [[nodiscard]] int memberCount() const noexcept { return memberCount_; }

    // Select the single assignable pressure destination (default VCF cutoff CV).
    void setPressureDestination(PressureDestination d) noexcept { pressureDest_ = d; }
    [[nodiscard]] PressureDestination pressureDestination() const noexcept { return pressureDest_; }

    // --- The per-channel rotation parser (RT-safe, allocation-free) ---------------
    // channel is 1-based MIDI (lower-zone master = 1, members = 2..16). A member-channel
    // note-on claims the next free voice via round-robin rotation; note-off frees it.
    void noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t vel) noexcept;
    void noteOff(std::uint8_t channel, std::uint8_t note) noexcept;

    // Per-channel pitch-bend (semitones, signed, continuous Pre-Q offset). On a member
    // channel it sets that channel's voice offset; on the master channel it is the
    // global zone offset added to every voice [ADR-012 C11, C13].
    void pitchBend(std::uint8_t channel, float semis) noexcept;

    // Per-channel pressure (channel pressure / CC74, normalized 0..1) -> the single
    // assignable destination. Member channel -> that voice; master channel -> global
    // [ADR-012 C12, C13].
    void pressure(std::uint8_t channel, float norm) noexcept;

    // --- Query surface (used by the engine adapter + tests) -----------------------
    // The voice index currently assigned to a 1-based MIDI channel, or
    // kUnassignedVoice (-1) if none.
    [[nodiscard]] int voiceForChannel(std::uint8_t channel) const noexcept;

    // The effective per-voice pre-Q pitch offset (per-note member bend + global master
    // bend) and the effective per-voice pressure (per-note member pressure, or the
    // global master pressure when the voice carries none). v is 0..maxVoices-1.
    [[nodiscard]] float voicePitchOffsetSemis(int voice) const noexcept;
    [[nodiscard]] float voicePressure(int voice) const noexcept;

    [[nodiscard]] int maxVoices() const noexcept { return maxVoices_; }

private:
    // Fixed voice-state capacity. The engine's voice cap (task voice-stream) is well
    // under this; prepare() clamps maxVoices_ to it. Sized for the widest MPE config.
    static constexpr int kVoiceCapacity = 32;

    // True iff channel is an ENABLED lower-zone member (2 .. 1+memberCount_).
    [[nodiscard]] bool isEnabledMember(std::uint8_t channel) const noexcept;

    // channelToVoice_[ch-1] = assigned voice or kUnassignedVoice. Index 0 (master,
    // channel 1) is never a per-note voice; it stays unassigned by construction.
    std::array<int, mw::cal::mpe::kNumMidiChannels> channelToVoice_{};

    // Per-voice expression (NO timbre matrix: pitch offset + ONE pressure value only).
    std::array<float, kVoiceCapacity> voiceBendSemis_{};   // per-note member bend
    std::array<float, kVoiceCapacity> voicePressure_{};    // per-note member pressure
    std::array<bool,  kVoiceCapacity> voiceActive_{};      // voice currently held?

    // The lower-zone MASTER (channel 1) global offset/pressure (Collapsed floor +
    // master-channel contribution under MPE) [ADR-012 C13].
    float masterBendSemis_ = mw::cal::mpe::kInitialPitchOffsetSemis;
    float masterPressure_  = mw::cal::mpe::kInitialPressureNorm;

    int  memberCount_  = mw::cal::mpe::kDefaultMembers;   // 0 = OFF (Collapsed)
    int  maxVoices_    = 0;
    int  rotationCursor_ = 0;                             // round-robin voice cursor

    PressureDestination pressureDest_ = PressureDestination::VcfCutoffCv;
};

} // namespace mw::plugin
