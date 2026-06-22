// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/SequencerEngine.h — mw::seq::SequencerEngine, the arp/seq fixed-order
// state machine (task 087). Realizes docs/design/05 §2.1 (fixed-order super-loop tick),
// §2.2 (control tick vs sample-accurate edges), §2.3 (block-boundary data flow),
// §9.1/§9.2/§9.3 (persistence + RT-safe immutable snapshot swap + INIT fallback).
//
// This is DISTINCT from doc 04's `mw::ControlCore` (core/control/ControlCore.h): it
// lives in the `mw::seq` namespace and never collides with it [docs/design/05 §1.1/§9.2,
// D3]. It HOSTS the control-core components — TriggerSource (S7), Arpeggiator,
// StepSequencer, Clock, ModRouter — and advances arp + seq + RANDOM reload TOGETHER on
// the single clock H->L edge so they stay phase-consistent across Internal / HostSync /
// Ext sources [§2.1; ADR-007 C17].
//
// RT contract [docs/design/05 §10; ADR-007 C26; docs/design/00 §9.1]:
//   - processBlock() is a noexcept hot path: it reads the live snapshot via an atomic
//     ACQUIRE load, touches only pre-sized member storage, and writes only into the
//     caller's pre-sized `out` span — no heap allocation, no lock, no atomic-as-lock.
//   - State I/O (publishSnapshot / captureState / restoreState) runs OFF the audio
//     thread; publishSnapshot fills an inactive double-buffer slot and stores the new
//     pointer with RELEASE ordering [§9.2].
//   - A malformed / forward-version snapshot falls back to the INIT defaults [§9.3].
//
// No `juce::*` type appears here; mwcore is JUCE-free [ADR-001/D2]. juce::ValueTree
// (de)serialization of the POD ControlSnapshot and host transport from
// juce::AudioPlayHead are the plugin layer's job (owned format = doc 06). Parameter
// IDs/ranges are owned by docs/design/06 §2 [ADR-008], not minted here.

#pragma once

#include <atomic>
#include <cstdint>
#include <span>

#include "../BlockContext.h"      // mw::TransportInfo (doc 00 §5.3)
#include "ControlTypes.h"         // KeyEvent, ControlEvent, ControlSnapshot, enums
#include "Arpeggiator.h"
#include "Clock.h"
#include "ModRouter.h"
#include "StepSequencer.h"
#include "TriggerSource.h"

namespace mw::seq {

// The arp/seq fixed-order control state machine. One H->L clock edge advances the
// arp cursor, the sequencer slot, and the RANDOM reload signal together [§2.1].
class SequencerEngine {
public:
    // -----------------------------------------------------------------------
    // Off-audio-thread lifecycle.
    // -----------------------------------------------------------------------

    // Pre-size all hosted components + the edge scratch span; seed both snapshot
    // double-buffer slots to the INIT defaults and publish slot 0 as live. No
    // allocation here or on any hot path thereafter [§9.2; ADR-007 C26]. Idempotent.
    void prepare(double sampleRate, int maxBlockSamples) noexcept;

    // Clear hosted-component play state to a known start without touching the
    // published snapshot (transport reset / re-init). noexcept, no allocation.
    void reset() noexcept;

    // -----------------------------------------------------------------------
    // Message thread: publish / capture / restore the POD snapshot. May write the
    // inactive double-buffer slot; never touches the audio thread's live read.
    // -----------------------------------------------------------------------

    // Build a fresh live snapshot from `s` into the inactive double-buffer slot,
    // apply it to the hosted components, and publish it via an atomic RELEASE store.
    // A malformed / forward-version snapshot is replaced by INIT defaults [§9.3].
    void publishSnapshot(const mw::control::ControlSnapshot& s) noexcept;

    // Snapshot the current control-core state into a POD (schemaVersion == 1) [§9.1].
    [[nodiscard]] mw::control::ControlSnapshot captureState() const noexcept;

    // Restore from a POD snapshot (off audio thread). Equivalent to publishSnapshot:
    // applies the state and republishes the live pointer [§9.2/§9.3].
    void restoreState(const mw::control::ControlSnapshot& s) noexcept;

    // -----------------------------------------------------------------------
    // Transport toggles (message/key thread). PLAY (§2.1 step 9/10) and LOAD/record
    // (§6.3) are transport CONTROLS, not persisted snapshot fields (the seq buffer is
    // persisted; the seq MODE is the `mw101.seq.mode` param owned by doc 06). They are
    // surfaced here so the engine can drive the StepSequencer's PLAY/LOAD path on the
    // §2.1 fixed-order tick without the engine reaching into the host param schema.
    // -----------------------------------------------------------------------
    void setSeqPlay(bool on) noexcept;        // PLAY toggle (§6.3 / §2.1 step 9)
    void setSeqRecord(bool on) noexcept;      // LOAD toggle (§6.3)
    void recordNote(int pitch6) noexcept;     // keyboard-only LOAD write (§6.3)
    void recordRest() noexcept;
    void recordTie(int pitch6) noexcept;

    // -----------------------------------------------------------------------
    // Audio thread: the per-control-tick param + pattern application seam (task 181;
    // ADR-030 part 1). The shipped plugin had NO production path that drove these from
    // the seq.*/arp.* APVTS params or the edited pattern buffer, so the SequencerEngine
    // ran on the INIT-default snapshot with an empty buffer forever (the dead-subsystem
    // ship-blocker). These two seams close that, RT-safe (no heap, no lock, noexcept):
    //   * applyControlParams() updates the DYNAMIC arp/clock/trigger config the
    //     control-tick dispatch decodes from the params, applying it to the hosted Arp /
    //     Clock / TriggerSource AND mirroring it into the live snapshot's scalar fields
    //     (so liveSnapshot()->arpHold etc. — which the Engine's routing gate reads — track
    //     the live params). It does NOT touch the seq buffer / playback position, so it is
    //     safe to call every tick without re-loading the pattern or rewinding the playhead.
    //   * loadPattern() copies an edited / preset SeqBuffer into the StepSequencer (it
    //     wraps StepSequencer::loadBuffer, which re-phases playback to slot 0). The caller
    //     loads only when the pattern actually changed, so playback is not rewound every
    //     block. The seq PLAY / RECORD toggles stay the setSeqPlay/setSeqRecord controls.
    // Both are noexcept, allocation-free, lock-free — the StepSequencer/Arp/Clock setters
    // touch only fixed members [docs/design/05 §6.5/§9.2; ADR-007 C26; ADR-028; ADR-030].
    // -----------------------------------------------------------------------

    // The dynamic per-tick control config the dispatch decodes from the seq.*/arp.* params
    // (the parts of ControlSnapshot that can change between ticks WITHOUT a buffer reload).
    // A trivially-copyable POD; passed by value on the audio thread.
    struct ControlParams {
        mw::control::ArpMode     arpMode = mw::control::ArpMode::Up;
        bool                     arpHold = false;
        bool                     uAndDRepeatEndpoints = false;
        mw::control::ClockSource clockSource = mw::control::ClockSource::Internal;
        float                    internalRateHz = 1.0f;
        mw::control::HostRate    hostRate = mw::control::HostRate::Sixteenth;
        bool                     clockResetOnKeypress = true;
        mw::control::TrigMode    trigMode = mw::control::TrigMode::GateTrig;
    };

    // Apply the decoded dynamic control config to the hosted components + the live
    // snapshot's scalar fields, WITHOUT reloading the seq buffer or rewinding playback.
    // Audio-thread, noexcept, no alloc, no lock (task 181) [ADR-030 part 1].
    void applyControlParams(const ControlParams& p) noexcept;

    // Load an edited / preset pattern buffer into the hosted StepSequencer (re-phasing
    // playback to slot 0). Audio-thread, noexcept, no alloc, no lock; the caller gates on
    // pattern change so playback is not rewound every block (task 181) [ADR-030 part 1].
    void loadPattern(const mw::control::SeqBuffer& buffer, int count) noexcept;

    // -----------------------------------------------------------------------
    // Audio thread: the hot, noexcept fixed-order tick. Reads the live snapshot
    // pointer with an ACQUIRE load (no lock); consumes Clock edges produced for the
    // block (sample-accurate, §2.2) and, on EACH edge, advances arp + seq + RANDOM
    // reload together (§2.1 / C17); emits time-stamped ControlEvents into `out`.
    //
    // `extPulseOffsets` are the per-block Ext-clock pulse positions (one pulse = one
    // step); empty for Internal / HostSync. `out` is the caller's pre-sized event
    // span; `outCount` receives the number of events written (clamped to out.size()).
    // -----------------------------------------------------------------------
    void processBlock(const mw::TransportInfo& t,
                      std::span<const mw::control::KeyEvent> keyIn,
                      std::span<const int> extPulseOffsets,
                      std::span<mw::control::ControlEvent> out,
                      int numFrames,
                      int& outCount) noexcept;

    // -----------------------------------------------------------------------
    // Accessors (test / introspection). All const noexcept.
    // -----------------------------------------------------------------------

    // The RANDOM-on-edge reload signal counter: incremented once per clock edge,
    // on the SAME edge as arp/seq advance [§2.1 step 7; ADR-007 C17]. The engine
    // FIRES this signal; the LFO subsystem consumes it to reload the RANDOM value
    // (value generation is out of scope here).
    [[nodiscard]] std::uint64_t randomReloadCount() const noexcept { return randomReloadCount_; }

    // Live-snapshot pointer accessor (audio-thread ACQUIRE semantics). Never null
    // after prepare(): a bad publish falls back to INIT defaults [§9.3].
    [[nodiscard]] const mw::control::ControlSnapshot* liveSnapshot() const noexcept {
        return live_.load(std::memory_order_acquire);
    }

    // Hosted components (const access for verification).
    [[nodiscard]] const mw::control::TriggerSource& trigger() const noexcept { return trigger_; }
    [[nodiscard]] const mw::control::Arpeggiator&   arp()     const noexcept { return arp_; }
    [[nodiscard]] const mw::control::StepSequencer& seq()     const noexcept { return seq_; }
    [[nodiscard]] const mw::control::Clock&         clock()   const noexcept { return clock_; }
    [[nodiscard]] const mw::control::ModRouter&     modRouter() const noexcept { return modRouter_; }

private:
    // Reject a malformed / forward-version snapshot, returning a sanitized copy:
    // an unsupported schemaVersion or out-of-range count collapses to INIT defaults
    // [§9.3; ADR-021]. Pure, no allocation.
    [[nodiscard]] static mw::control::ControlSnapshot sanitize(
        const mw::control::ControlSnapshot& s) noexcept;

    // Apply a (sanitized) snapshot to the hosted components (off audio thread).
    void applySnapshot(const mw::control::ControlSnapshot& s) noexcept;

    // Publish `slot` (one of slots_) as the live pointer with RELEASE ordering, and
    // flip `activeSlot_` so the next publish writes the now-inactive slot [§9.2].
    void publishSlot(int slot) noexcept;

    // --- Hosted control-core components (§1.1) -------------------------------
    mw::control::TriggerSource trigger_{};
    mw::control::Arpeggiator   arp_{};
    mw::control::StepSequencer seq_{};
    mw::control::Clock         clock_{};
    mw::control::ModRouter     modRouter_{};

    // --- Sizing / tick bookkeeping ------------------------------------------
    double sampleRate_ = 48000.0;
    int    maxBlockSamples_ = 0;

    // RANDOM-on-edge reload signal counter (§2.1 step 7).
    std::uint64_t randomReloadCount_ = 0;

    // --- Double-buffered immutable snapshot (§9.2) --------------------------
    // Two storage slots owned by the message thread; the audio thread reads only
    // through the atomically-published `live_` pointer (ACQUIRE/RELEASE). The
    // pointer is never null after prepare().
    mw::control::ControlSnapshot slots_[2]{};
    int activeSlot_ = 0;   // index of the slot most recently published as live
    std::atomic<const mw::control::ControlSnapshot*> live_{nullptr};
};

} // namespace mw::seq
