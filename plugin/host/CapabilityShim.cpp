// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/host/CapabilityShim.cpp — implementation of the per-format capability
// resolver + branch-free per-block transport recheck + lock-free UI publish
// (task 112). Realizes docs/design/09 §7.2-7.4 / §8.1-8.2 and ADR-022 C5-C12.
//
// See CapabilityShim.h for the responsibility / concurrency contract.

#include "host/CapabilityShim.h"

#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>   // juce::AudioPlayHead

#include "calibration/CapabilityShimConstants.h"           // mw::cal::capshim::kPpqRelockEpsilon

namespace mw::plugin {

namespace {

// True iff `playhead` currently reports a usable transport position. Reading the
// (possibly absent) playhead is allocation-free [ADR-022 C7; ADR-011 C10]: a null
// pointer or an empty Optional<PositionInfo> simply means "no transport".
//
// `outPpq` receives the absolute PPQ position when a transport is present (the
// re-lock anchor for ADR-022 C8), clamped to >= 0 to absorb a host's sub-tick
// negative PPQ at bar 0. Untouched when no transport is present.
[[nodiscard]] bool queryTransport(const juce::AudioPlayHead* playhead, double& outPpq) noexcept
{
    if (playhead == nullptr)
        return false;

    const auto pos = playhead->getPosition();
    if (! pos.hasValue())
        return false;

    // A transport is "present" when the host is playing OR reports a recording head.
    // A stopped, playhead-less host falls to Free-run [§8.1 Free-run row; ADR-022 C7].
    const bool moving = pos->getIsPlaying() || pos->getIsRecording();
    if (! moving)
        return false;

    double ppq = 0.0;
    if (const auto p = pos->getPpqPosition(); p.hasValue())
        ppq = *p;
    if (ppq < mw::cal::capshim::kPpqRelockEpsilon)
        ppq = 0.0;
    outPpq = ppq;
    return true;
}

} // namespace

// --- Static capability matrix [docs/design/09 §8.1; ADR-022 Contract table] ---------

TransportRung CapabilityShim::staticTransportRung(PluginFormat fmt) noexcept
{
    switch (fmt)
    {
        case PluginFormat::CLAP:       return TransportRung::SampleAccurate; // sub-block edges [C5]
        case PluginFormat::VST3:                                            // one PositionInfo/block
        case PluginFormat::AU:                                              // [C6]
        case PluginFormat::LV2:        return TransportRung::BlockQuantized;
        case PluginFormat::Standalone: return TransportRung::FreeRun;        // no host transport [C7]
    }
    return TransportRung::FreeRun; // defensive; unreachable for the frozen enum
}

NoteExpressionRung CapabilityShim::noteExpressionRung(PluginFormat fmt, bool mpeLiteEnabled) noexcept
{
    // CLAP exposes typed native note-expressions -> Native rung [§7.2; ADR-022 C1].
    if (fmt == PluginFormat::CLAP)
        return NoteExpressionRung::Native;

    // Every other format has no note-expression API: MPE-over-MIDI when MPE-lite is ON,
    // else the universal Collapsed (global channel bend + pressure) floor [C2/C3].
    return mpeLiteEnabled ? NoteExpressionRung::MpeOverMidi
                          : NoteExpressionRung::Collapsed;
}

// --- Construction -------------------------------------------------------------------

CapabilityShim::CapabilityShim() noexcept
{
    // Seed both UI slots with the conservative floor (Collapsed / Free-run) and publish
    // slot 0 so a reader before the first resolve() sees a defined, non-null snapshot.
    const ResolvedCapabilities floor{ NoteExpressionRung::Collapsed, TransportRung::FreeRun };
    uiSlots_[0] = floor;
    uiSlots_[1] = floor;
    uiLive_.store(&uiSlots_[0], std::memory_order_release);
    uiWriteSlot_ = 1;
}

// --- resolve(): once at init / prepareToPlay ----------------------------------------

ResolvedCapabilities CapabilityShim::resolve(PluginFormat fmt,
                                             bool mpeLiteEnabled,
                                             const juce::AudioPlayHead* playhead) noexcept
{
    noteExpr_      = noteExpressionRung(fmt, mpeLiteEnabled);
    bestTransport_ = staticTransportRung(fmt);

    // Current host query: a format that CAN sync (Block/Sample) still resolves to
    // Free-run right now if the host reports no transport [§8.1 "Free-run if no
    // transport"; ADR-022 C7]. Standalone's best rung IS Free-run, so it stays there.
    double ppq = 0.0;
    const bool hasTransport = queryTransport(playhead, ppq);

    const ResolvedCapabilities caps{
        noteExpr_,
        hasTransport ? bestTransport_ : TransportRung::FreeRun
    };
    relockPpq_ = hasTransport ? ppq : 0.0;
    return caps;
}

// --- recheckPerBlock(): per block, on the audio thread ------------------------------

ResolvedCapabilities CapabilityShim::recheckPerBlock(const juce::AudioPlayHead* playhead) noexcept
{
    double ppq = 0.0;
    const bool hasTransport = queryTransport(playhead, ppq);

    // BRANCH-FREE transport select: pick bestTransport_ when a transport is present,
    // FreeRun (enum value 2) when it is absent — via arithmetic on the boolean, no
    // branch on the hot path [§8.2; ADR-022 C11]. Both candidate enum values are held
    // in registers; the select is a single conditional-move-equivalent multiply/add.
    const auto best = static_cast<std::uint8_t>(bestTransport_);
    const auto free = static_cast<std::uint8_t>(TransportRung::FreeRun);
    const auto present = static_cast<std::uint8_t>(hasTransport);  // 0 or 1
    const auto selected = static_cast<std::uint8_t>(best * present
                                                    + free * (std::uint8_t{ 1 } - present));

    // Re-lock anchor (ADR-022 C8): capture the absolute PPQ whenever a transport is
    // present so the clock re-phases from it when HOST-SYNC recovers; hold it at 0
    // while Free-run. Plain scalar store — no alloc, no lock.
    relockPpq_ = ppq * static_cast<double>(present);

    return ResolvedCapabilities{ noteExpr_, static_cast<TransportRung>(selected) };
}

// --- publishToUi(): the §6.3 single-writer atomic-pointer swap ----------------------

void CapabilityShim::publishToUi(const ResolvedCapabilities& caps) noexcept
{
    // Write into the inactive slot, then atomically swap the live pointer. The UI
    // reader only ever dereferences the live pointer, so it never observes a torn
    // write [docs/design/09 §6.3; ADR-022 C12]. Single writer -> no CAS needed.
    const int slot = uiWriteSlot_;
    uiSlots_[static_cast<std::size_t>(slot)] = caps;
    uiLive_.store(&uiSlots_[static_cast<std::size_t>(slot)], std::memory_order_release);
    uiWriteSlot_ = slot ^ 1;   // ping-pong to the other inline slot
}

ResolvedCapabilities CapabilityShim::uiRungs() const noexcept
{
    // Acquire the live snapshot pointer and copy the POD out. Never blocks.
    const ResolvedCapabilities* live = uiLive_.load(std::memory_order_acquire);
    return *live;
}

} // namespace mw::plugin
