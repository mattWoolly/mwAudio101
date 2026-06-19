// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/host/CapabilityShim.h — the per-format capability resolver + per-block
// transport recheck + lock-free UI publish (task 112). Realizes docs/design/09
// §7.2-7.4 / §8.1-8.2 and ADR-022 C5-C12.
//
// RESPONSIBILITY (the wrapper is capability-aware; the engine is capability-agnostic):
//   - resolve(fmt, mpeLiteEnabled, playhead): ONCE at init / prepareToPlay, pick the
//     NoteExpressionRung + TransportRung from the §8.1 / ADR-022 per-format matrix and
//     the current host query. Caches the format's "best" transport rung so the
//     per-block recheck is a branch-free fall-to/recover-from Free-run [§8.2; C11].
//   - recheckPerBlock(playhead): per-block, BRANCH-FREE recheck of transport presence
//     via the cached pointer. Falls to Free-run when the host stops reporting a
//     transport and re-locks to the format's best rung from absolute PPQ when it
//     reappears — with ZERO allocation / ZERO lock across the transition
//     [§8.2; ADR-022 C7-C8, C11; ADR-011 C9-C10].
//   - publishToUi(rungs): a single lock-free atomic-pointer swap (the §6.3 double-
//     buffer pattern) so a Collapsed note-expr / Free-run transport state is
//     user-visible, not a silent surprise [§7.4; ADR-022 C12].
//
// HOST-SYNC WITHOUT TRANSPORT (ADR-022 C8): the shim does not own the clock-source
// selector (that is ADR-007's HOST-SYNC/INTERNAL/EXT param). It owns only the
// transport *capability*: with no transport present the resolved TransportRung is
// FreeRun, so a HOST-SYNC selection silently behaves as INTERNAL; when a transport
// appears the recheck re-locks the rung to the format's best (Block/Sample) and the
// clock re-phases from absolute PPQ. The re-lock anchor PPQ is exposed for the clock.
//
// CONCURRENCY: resolve() runs off the audio thread (prepareToPlay). recheckPerBlock()
// runs ON the audio thread and is noexcept + allocation-free + lock-free (it reads a
// cached enum and a single playhead pointer). publishToUi() is the single-writer side
// of the §6.3 atomic swap; the UI reader calls uiRungs() (acquire) — never blocks.

#pragma once

#include <array>
#include <atomic>
#include <type_traits>

#include "host/Capabilities.h"   // NoteExpressionRung / TransportRung / PluginFormat / ResolvedCapabilities

namespace juce { class AudioPlayHead; }

namespace mw::plugin {

class CapabilityShim {
public:
    CapabilityShim() noexcept;

    CapabilityShim(const CapabilityShim&)            = delete;
    CapabilityShim& operator=(const CapabilityShim&) = delete;

    // --- INIT / PREPARE (off the audio thread) --------------------------------
    // Resolve the per-format rungs once from the static capability matrix (§8.1) plus
    // the current host query (is a transport reported right now?). Caches the format's
    // best transport rung for the branch-free per-block recheck. No alloc / no lock.
    ResolvedCapabilities resolve(PluginFormat fmt,
                                 bool mpeLiteEnabled,
                                 const juce::AudioPlayHead* playhead) noexcept;

    // --- AUDIO THREAD (per block) ---------------------------------------------
    // Branch-free recheck of transport presence via the cached pointer: returns the
    // (possibly updated) rungs. Note-expression rung is fixed at resolve(); only the
    // transport rung tracks presence — best-rung when a transport is present, FreeRun
    // when it is absent — selected without a branch on the hot path. ZERO alloc / lock
    // across a FreeRun<->transport transition [§8.2; ADR-022 C11].
    ResolvedCapabilities recheckPerBlock(const juce::AudioPlayHead* playhead) noexcept;

    // --- UI PUBLISH (single writer; the §6.3 atomic-ptr pattern) --------------
    // Atomically store `caps` as the live UI snapshot (a single release store of a
    // pointer into an inline double buffer). Lock-free, allocation-free [ADR-022 C12].
    void publishToUi(const ResolvedCapabilities& caps) noexcept;

    // UI READER: the rungs currently visible to the UI (acquire load). Never blocks.
    [[nodiscard]] ResolvedCapabilities uiRungs() const noexcept;

    // --- Introspection / re-lock anchor ---------------------------------------
    // The format's best transport rung (the rung the recheck re-locks TO when a
    // transport reappears) — Sample-accurate for CLAP, Block-quantized for VST3/AU/LV2,
    // Free-run for Standalone [§8.1].
    [[nodiscard]] TransportRung bestTransportRung() const noexcept { return bestTransport_; }

    // The absolute-PPQ position captured at the last recheck where a transport was
    // present (the re-lock anchor for ADR-022 C8). 0.0 while Free-run / before any
    // transport is seen. The clock re-phases from this on transport reappearance.
    [[nodiscard]] double relockPpq() const noexcept { return relockPpq_; }

    // True iff the UI publish pointer is always lock-free on this platform (it is on
    // every supported target). Asserts the C12 lock-free publish contract.
    [[nodiscard]] static bool uiPublishIsAlwaysLockFree() noexcept {
        return std::atomic<const ResolvedCapabilities*>::is_always_lock_free;
    }

    // The static (host-independent) transport rung a format is capable of [§8.1].
    [[nodiscard]] static TransportRung staticTransportRung(PluginFormat fmt) noexcept;

    // The note-expression rung for a format given the MPE-lite flag [§7.2].
    [[nodiscard]] static NoteExpressionRung
        noteExpressionRung(PluginFormat fmt, bool mpeLiteEnabled) noexcept;

private:
    // Resolved-once note-expression rung (does not change per block).
    NoteExpressionRung noteExpr_{ NoteExpressionRung::Collapsed };

    // Format's best transport rung (what recheck re-locks to when transport is present).
    TransportRung bestTransport_{ TransportRung::FreeRun };

    // The absolute-PPQ re-lock anchor [ADR-022 C8].
    double relockPpq_{ 0.0 };

    // --- §6.3 UI double buffer: live_ always names one of the two inline slots ----
    std::array<ResolvedCapabilities, 2>          uiSlots_{};
    std::atomic<const ResolvedCapabilities*>     uiLive_{ nullptr };
    int                                          uiWriteSlot_{ 0 };  // single-writer ping-pong index
};

static_assert(std::is_trivially_copyable_v<ResolvedCapabilities>,
              "UI publish swaps a trivially-copyable POD [docs/design/09 §7.2].");

} // namespace mw::plugin
