// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/MpeReconstructor.cpp — implementation of the MPE-over-MIDI per-channel
// rotation parser (task 103). See MpeReconstructor.h for scope, the lower-zone
// topology, and the real-time invariants. Realizes docs/design/09 §7.3 / §7.1 and
// ADR-022 C2 / ADR-012 §4, C10-C13.
//
// Every method below is allocation-free and lock-free: it only reads/writes the
// fixed-size std::arrays sized in prepare() [ADR-022 C11; docs/design/09 §1.3].

#include "MpeReconstructor.h"

#include <algorithm>

namespace mw::plugin {

namespace cal = mw::cal::mpe;

void MpeReconstructor::prepare(int maxVoices) noexcept
{
    maxVoices_ = std::clamp(maxVoices, 0, kVoiceCapacity);
    reset();
}

void MpeReconstructor::reset() noexcept
{
    channelToVoice_.fill(cal::kUnassignedVoice);
    voiceBendSemis_.fill(cal::kInitialPitchOffsetSemis);
    voicePressure_.fill(cal::kInitialPressureNorm);
    voiceActive_.fill(false);
    masterBendSemis_ = cal::kInitialPitchOffsetSemis;
    masterPressure_  = cal::kInitialPressureNorm;
    rotationCursor_  = 0;
}

void MpeReconstructor::setMemberCount(int count) noexcept
{
    memberCount_ = std::clamp(count, 0, cal::kMaxMembers);
}

bool MpeReconstructor::isEnabledMember(std::uint8_t channel) const noexcept
{
    // Members occupy MIDI channels 2 .. 1+memberCount_ (lower zone, master = 1).
    if (memberCount_ <= 0) return false;
    return channel >= cal::kFirstMemberChannel
        && channel <= static_cast<std::uint8_t>(cal::kMasterChannel + memberCount_);
}

void MpeReconstructor::noteOn(std::uint8_t channel, std::uint8_t /*note*/, std::uint8_t /*vel*/) noexcept
{
    // Only ENABLED lower-zone member channels claim a per-note voice. Master-channel
    // and out-of-zone note-ons claim nothing (Collapsed / global path) [ADR-012 C10].
    if (! isEnabledMember(channel)) return;
    if (maxVoices_ <= 0) return;

    const int chIdx = channel - 1;   // 0-based index into channelToVoice_

    // Already assigned (retrigger on the same channel): reuse the held voice so the
    // channel<->voice rotation stays stable; refresh its active flag.
    int voice = channelToVoice_[static_cast<std::size_t>(chIdx)];
    if (voice == cal::kUnassignedVoice) {
        // Round-robin: scan from the rotation cursor for the next FREE voice. If the
        // pool is exhausted, steal the voice at the cursor (never allocate) [ADR-012 C17].
        voice = rotationCursor_;
        for (int i = 0; i < maxVoices_; ++i) {
            const int candidate = (rotationCursor_ + i) % maxVoices_;
            if (! voiceActive_[static_cast<std::size_t>(candidate)]) {
                voice = candidate;
                break;
            }
        }
        // Detach any channel previously pointing at this stolen/selected voice.
        for (auto& mapped : channelToVoice_) {
            if (mapped == voice) mapped = cal::kUnassignedVoice;
        }
        channelToVoice_[static_cast<std::size_t>(chIdx)] = voice;
        rotationCursor_ = (voice + 1) % maxVoices_;
    }

    // Fresh note on this voice: clear its per-note expression to the rest state so a
    // stale member bend/pressure from a prior note does not leak in.
    voiceBendSemis_[static_cast<std::size_t>(voice)] = cal::kInitialPitchOffsetSemis;
    voicePressure_[static_cast<std::size_t>(voice)]  = cal::kInitialPressureNorm;
    voiceActive_[static_cast<std::size_t>(voice)]    = true;
}

void MpeReconstructor::noteOff(std::uint8_t channel, std::uint8_t /*note*/) noexcept
{
    if (channel < 1 || channel > cal::kLastMemberChannel) return;
    const int chIdx = channel - 1;
    const int voice = channelToVoice_[static_cast<std::size_t>(chIdx)];
    if (voice == cal::kUnassignedVoice) return;

    voiceActive_[static_cast<std::size_t>(voice)] = false;
    channelToVoice_[static_cast<std::size_t>(chIdx)] = cal::kUnassignedVoice;
}

void MpeReconstructor::pitchBend(std::uint8_t channel, float semis) noexcept
{
    // Master channel (1): the lower-zone-wide / Collapsed global bend offset, added to
    // every voice [ADR-012 C13]. This is the universal floor when members are OFF.
    if (channel == cal::kMasterChannel) {
        masterBendSemis_ = semis;
        return;
    }

    // Member channel: per-note Pre-Q pitch offset on that channel's voice [ADR-012 C11].
    if (! isEnabledMember(channel)) return;
    const int voice = channelToVoice_[static_cast<std::size_t>(channel - 1)];
    if (voice == cal::kUnassignedVoice) return;
    voiceBendSemis_[static_cast<std::size_t>(voice)] = semis;
}

void MpeReconstructor::pressure(std::uint8_t channel, float norm) noexcept
{
    // Master channel (1): global channel pressure -> the single assignable destination
    // for every voice [ADR-012 C12, C13].
    if (channel == cal::kMasterChannel) {
        masterPressure_ = norm;
        return;
    }

    if (! isEnabledMember(channel)) return;
    const int voice = channelToVoice_[static_cast<std::size_t>(channel - 1)];
    if (voice == cal::kUnassignedVoice) return;
    voicePressure_[static_cast<std::size_t>(voice)] = norm;
}

int MpeReconstructor::voiceForChannel(std::uint8_t channel) const noexcept
{
    if (channel < 1 || channel > cal::kLastMemberChannel) return cal::kUnassignedVoice;
    return channelToVoice_[static_cast<std::size_t>(channel - 1)];
}

float MpeReconstructor::voicePitchOffsetSemis(int voice) const noexcept
{
    if (voice < 0 || voice >= maxVoices_) return cal::kInitialPitchOffsetSemis;
    // Effective offset = per-note member bend + the lower-zone master (global) bend.
    return voiceBendSemis_[static_cast<std::size_t>(voice)] + masterBendSemis_;
}

float MpeReconstructor::voicePressure(int voice) const noexcept
{
    if (voice < 0 || voice >= maxVoices_) return cal::kInitialPressureNorm;
    // A voice carrying its own per-note member pressure uses it; otherwise the global
    // master-channel pressure applies (Collapsed floor) [ADR-012 C12, C13].
    const float perNote = voicePressure_[static_cast<std::size_t>(voice)];
    return (perNote != cal::kInitialPressureNorm) ? perNote : masterPressure_;
}

} // namespace mw::plugin
