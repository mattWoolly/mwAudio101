// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/control/SequencerEngine.cpp — implementation of mw::seq::SequencerEngine, the
// arp/seq fixed-order control state machine (task 087). See SequencerEngine.h for the
// contract; design citations are docs/design/05 §2.1/§2.2/§2.3/§9.1/§9.2/§9.3 and
// ADR-007 C17, C25, C26, C27.
//
// The hot path (processBlock) is noexcept and allocation-free: it reads the live
// snapshot via an ACQUIRE load, walks the Clock-produced edges (placed at
// sample-accurate sub-block offsets, §2.2), and on EACH edge advances arp + seq +
// RANDOM reload together (§2.1 / C17), writing only into the caller's pre-sized span.
// State I/O runs off the audio thread and publishes via a RELEASE store [§9.2].

#include "control/SequencerEngine.h"

#include <algorithm>

#include "calibration/SequencerEngineConstants.h"   // mw::cal::seq::kControlTickSeconds

namespace mw::seq {

namespace ctl = mw::control;

void SequencerEngine::prepare(double sampleRate, int maxBlockSamples) noexcept {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    maxBlockSamples_ = (maxBlockSamples > 0) ? maxBlockSamples : 0;

    trigger_.prepare();
    arp_.prepare();
    seq_.prepare();
    clock_.prepare(sampleRate_);
    modRouter_.prepare(sampleRate_);

    randomReloadCount_ = 0;

    // Seed both double-buffer slots to the INIT defaults so the live pointer is never
    // null and a publish always has a clean inactive slot to fill [§9.2/§9.3].
    slots_[0] = ctl::ControlSnapshot{};
    slots_[1] = ctl::ControlSnapshot{};
    activeSlot_ = 0;
    applySnapshot(slots_[0]);
    live_.store(&slots_[0], std::memory_order_release);
}

void SequencerEngine::reset() noexcept {
    // Clear hosted-component play state to a known start without touching the
    // published snapshot. noexcept, no allocation [docs/design/00 §5.5].
    arp_.prepare();
    seq_.prepare();
    trigger_.prepare();
    clock_.prepare(sampleRate_);
    randomReloadCount_ = 0;
    // Re-apply the live snapshot so configured modes survive a transport reset.
    if (const ctl::ControlSnapshot* s = live_.load(std::memory_order_acquire)) {
        applySnapshot(*s);
    }
}

// --- Snapshot sanitation / application -------------------------------------------

ctl::ControlSnapshot SequencerEngine::sanitize(const ctl::ControlSnapshot& s) noexcept {
    // A malformed / forward-version snapshot collapses to INIT defaults [§9.3; ADR-021].
    // schemaVersion is versioned from v1 (C25); anything else is unreadable here.
    if (s.schemaVersion != 1u) {
        return ctl::ControlSnapshot{};
    }
    ctl::ControlSnapshot out = s;
    // Clamp the filled-count to the fixed buffer capacity (defensive).
    if (out.seqCount < 0) out.seqCount = 0;
    if (out.seqCount > ctl::kMaxSteps) out.seqCount = ctl::kMaxSteps;
    return out;
}

void SequencerEngine::applySnapshot(const ctl::ControlSnapshot& s) noexcept {
    // Push the snapshot fields onto the hosted components (off audio thread).
    seq_.loadBuffer(s.seq, s.seqCount);

    arp_.setMode(s.arpMode);
    arp_.setHold(s.arpHold);
    arp_.setUandDRepeatEndpoints(s.uAndDRepeatEndpoints);

    clock_.setSource(s.clockSource);
    clock_.setInternalRateHz(s.internalRateHz);
    clock_.setHostRate(s.hostRate);
    clock_.setSwing(s.swing);
    clock_.setClockResetOnKeypress(s.clockResetOnKeypress);

    trigger_.setMode(s.trigMode);

    modRouter_.setPwmSource(s.pwmSource);
    modRouter_.setVcaSource(s.vcaSource);
}

void SequencerEngine::publishSlot(int slot) noexcept {
    activeSlot_ = slot;
    live_.store(&slots_[slot], std::memory_order_release);
}

void SequencerEngine::publishSnapshot(const ctl::ControlSnapshot& s) noexcept {
    // Fill the INACTIVE double-buffer slot, apply it, then publish via RELEASE store.
    // The audio thread keeps reading the previously-live slot until the store lands.
    const int inactive = 1 - activeSlot_;
    slots_[inactive] = sanitize(s);
    applySnapshot(slots_[inactive]);
    publishSlot(inactive);
}

ctl::ControlSnapshot SequencerEngine::captureState() const noexcept {
    // Snapshot the current control-core state into a POD (schemaVersion == 1) [§9.1].
    // Start from the live (last-applied) configuration so every persisted field
    // round-trips, then overlay the sequencer buffer + filled count, which is the one
    // piece that can mutate at runtime (loadBuffer / record) [§9.1; ADR-007 C25].
    ctl::ControlSnapshot s{};
    if (const ctl::ControlSnapshot* live = live_.load(std::memory_order_acquire)) {
        s = *live;
    }
    s.seq = seq_.buffer();
    s.seqCount = seq_.count();
    s.schemaVersion = 1u;   // versioned from v1 (C25)
    return s;
}

void SequencerEngine::restoreState(const ctl::ControlSnapshot& s) noexcept {
    // Restore from a POD snapshot (off audio thread): apply + republish [§9.2/§9.3].
    publishSnapshot(s);
}

// --- Per-control-tick param + pattern application (audio thread; task 181) --------

void SequencerEngine::applyControlParams(const ControlParams& p) noexcept {
    // Apply the dynamic arp/clock/trigger config the Engine's control-tick dispatch
    // decoded from the seq.*/arp.* APVTS params, WITHOUT reloading the seq buffer or
    // rewinding the playhead (task 181; ADR-030 part 1). Before this seam nothing in
    // production drove these, so the hosted components ran on the INIT-default snapshot
    // forever (arpHold=false, arpMode=Up). All setters touch only fixed members — no
    // heap, no lock; noexcept [docs/design/05 §5.4/§7.7; ADR-007 C26; ADR-028].
    arp_.setMode(p.arpMode);
    arp_.setHold(p.arpHold);
    arp_.setUandDRepeatEndpoints(p.uAndDRepeatEndpoints);

    clock_.setSource(p.clockSource);
    clock_.setInternalRateHz(p.internalRateHz);
    clock_.setHostRate(p.hostRate);
    clock_.setClockResetOnKeypress(p.clockResetOnKeypress);

    trigger_.setMode(p.trigMode);

    // Mirror the dynamic scalar fields into the LIVE snapshot slot so liveSnapshot() — read
    // by the Engine's routing gate (renderChunk: arpEnabled == liveSnapshot()->arpHold) and
    // the clock-reset-on-keypress check in processBlock — tracks the live params.
    //
    // THREADING CONTRACT (single-audio-thread-writer; ADR-030) — this writes the ACTIVE
    // double-buffer slot IN PLACE from the audio thread, in contrast to publishSnapshot(),
    // which fills the INACTIVE slot then RELEASE-swaps the live pointer off the message
    // thread. That is SAFE only because the audio thread is the SOLE writer of these scalar
    // fields on the hot path AND publishSnapshot has NO production caller racing it (state
    // restore happens off-thread before transport; cf. the grep in task 181's QA). This
    // in-place active-slot update mirrors reset()'s audio-thread re-apply and never collides
    // with a publish, which only ever touches the inactive slot. INVARIANT FOR THE FUTURE:
    // if a message-thread ControlSnapshot publisher is ever added that runs CONCURRENTLY with
    // process() (e.g. a live editor pushing internalRateHz before task 182's rate param
    // lands), it MUST reconcile with this audio-thread writer — publish into the inactive
    // slot and let the audio thread own these scalar fields, or move this mirror behind the
    // same RELEASE swap — so the active slot keeps a single writer [forward ref: ADR-030].
    // The seq buffer / seqCount are left untouched (owned by loadPattern / capture).
    ctl::ControlSnapshot& live = slots_[static_cast<std::size_t>(activeSlot_)];
    live.arpMode              = p.arpMode;
    live.arpHold              = p.arpHold;
    live.uAndDRepeatEndpoints = p.uAndDRepeatEndpoints;
    live.clockSource          = p.clockSource;
    live.internalRateHz       = p.internalRateHz;
    live.hostRate             = p.hostRate;
    live.clockResetOnKeypress = p.clockResetOnKeypress;
    live.trigMode             = p.trigMode;
}

void SequencerEngine::loadPattern(const ctl::SeqBuffer& buffer, int count) noexcept {
    // Copy an edited / preset pattern into the hosted StepSequencer (re-phasing playback to
    // slot 0). RT-safe: a fixed-array POD copy + integer counters, no heap, no lock (task
    // 181). The caller gates on pattern change so playback is not rewound every block. Keep
    // the live snapshot's seq view coherent so captureState() round-trips the loaded pattern
    // even before the next message-thread publish [docs/design/05 §6.3/§6.5; ADR-030 part 1].
    seq_.loadBuffer(buffer, count);
    ctl::ControlSnapshot& live = slots_[static_cast<std::size_t>(activeSlot_)];
    live.seq      = seq_.buffer();
    live.seqCount = seq_.count();
}

// --- Transport toggles (message/key thread) --------------------------------------

void SequencerEngine::setSeqPlay(bool on) noexcept { seq_.setPlay(on); }
void SequencerEngine::setSeqRecord(bool on) noexcept { seq_.setRecord(on); }
void SequencerEngine::recordNote(int pitch6) noexcept { seq_.recordNote(pitch6); }
void SequencerEngine::recordRest() noexcept { seq_.recordRest(); }
void SequencerEngine::recordTie(int pitch6) noexcept { seq_.recordTie(pitch6); }

// --- The hot, fixed-order tick ---------------------------------------------------

void SequencerEngine::processBlock(const mw::TransportInfo& t,
                                   std::span<const ctl::KeyEvent> keyIn,
                                   std::span<const int> extPulseOffsets,
                                   std::span<ctl::ControlEvent> out,
                                   int numFrames,
                                   int& outCount) noexcept {
    outCount = 0;
    const int cap = static_cast<int>(out.size());
    if (cap <= 0 || numFrames <= 0) {
        return;
    }

    // §9.2 — read the live snapshot pointer with an ACQUIRE load (no lock). Never
    // null after prepare(); a defensive null check keeps the path total.
    const ctl::ControlSnapshot* snap = live_.load(std::memory_order_acquire);

    auto emit = [&](const ctl::ControlEvent& ev) noexcept -> bool {
        if (outCount >= cap) {
            return false;   // span full; stop (no overrun, no alloc)
        }
        out[static_cast<std::size_t>(outCount)] = ev;
        ++outCount;
        return true;
    };

    // -----------------------------------------------------------------------
    // §2.1 step 5 — Keyboard Read. Drive the held-key set (Arpeggiator) and build a
    // KeyState for the TriggerSource; honor clock-reset-on-keypress (§7.5). Each new
    // gate-on key is also emitted as a manual ControlEvent so single (non-arp/seq)
    // play passes through. keyIn is ordered by sampleOffset (doc 00 §5.3 contract).
    // -----------------------------------------------------------------------
    ctl::KeyState ks{};

    for (const ctl::KeyEvent& ke : keyIn) {
        const int key = ke.pitch;
        const bool keyInRange = (key >= 0 && key < ctl::TriggerSource::kNumKeys);

        if (ke.gate) {
            if (keyInRange) {
                const std::uint32_t bit = (std::uint32_t{1} << key);
                ks.justPressed |= bit & ~ks.held;
                ks.held |= bit;
            }
            arp_.noteOn(key);

            // Clock reset on keypress in LFO-trigger OR arpeggio mode (§7.5 / C22).
            const bool lfoMode = (snap != nullptr) && (snap->trigMode == ctl::TrigMode::Lfo);
            const bool arpMode = arp_.isEngaged();
            if (lfoMode || arpMode) {
                clock_.resetToKeypress(ke.sampleOffset);
                seq_.resetToStart();
            }
        } else {
            if (keyInRange) {
                const std::uint32_t bit = (std::uint32_t{1} << key);
                ks.justReleased |= bit & ks.held;
                ks.held &= ~bit;
            }
            arp_.noteOff(key);
        }
    }
    trigger_.observe(ks);

    // After the keyboard read, decide whether the sequencer/arp drive the voice (so a
    // single manual note is NOT double-emitted alongside a step event).
    const bool seqOrArpActive = (snap != nullptr) && (seq_.isPlaying() || arp_.isEngaged());

    // -----------------------------------------------------------------------
    // §2.1 step 6 — Clock Check. Produce all H->L edges for this block at
    // sample-accurate sub-block offsets, INDEPENDENT of the control-tick period
    // (§2.2 / C27). Edges are written into a fixed pre-sized scratch span.
    // -----------------------------------------------------------------------
    constexpr int kMaxEdges = 256;   // pre-sized worst-case edges per block (no alloc)
    ctl::ClockEdge edges[kMaxEdges];
    std::span<ctl::ClockEdge> edgeSpan{edges, kMaxEdges};
    int edgeCount = 0;
    clock_.renderEdges(t, extPulseOffsets, edgeSpan, numFrames, edgeCount);

    // -----------------------------------------------------------------------
    // Emit the keyboard-read ControlEvents first (manual play). A gate-on key resolves
    // through the S7 TriggerSource so priority + retrigger match the mode (§4).
    // These carry the per-event sampleOffset for sample-accurate placement.
    // -----------------------------------------------------------------------
    if (!seqOrArpActive) {
        for (const ctl::KeyEvent& ke : keyIn) {
            ctl::ControlEvent ev{};
            ev.pitch = ke.pitch;
            ev.gate = ke.gate;
            ev.trig = ke.trig;
            ev.porta = ke.porta;
            ev.mod = ke.mod;
            ev.sampleOffset = ke.sampleOffset;
            if (!emit(ev)) break;
        }
    }

    // -----------------------------------------------------------------------
    // §2.1 steps 7–13 — for EACH edge, on the SAME edge: fire RANDOM reload (step 7),
    // advance the sequencer slot (step 10) and the arp cursor (step 11), then emit the
    // CV/Gate output (steps 12–13). arp + seq + RANDOM stay phase-consistent because a
    // single edge drives all three [§2.1 / ADR-007 C17].
    // -----------------------------------------------------------------------
    for (int i = 0; i < edgeCount; ++i) {
        const int off = edges[i].sampleOffset;

        // step 7 — Random Data Output: fire the RANDOM reload signal on the edge.
        ++randomReloadCount_;

        // step 10 — sequencer advance (no-op when not playing / empty buffer).
        ctl::SeqPlayResult sr{};
        const bool seqPlaying = seq_.isPlaying() && (seq_.count() > 0);
        if (seqPlaying) {
            sr = seq_.advanceOnEdge();
        }

        // step 11 — arpeggiator advance (engaged only on chord/legato or HOLD).
        int arpKey = -1;
        const bool arpEngaged = arp_.isEngaged();
        if (arpEngaged) {
            arpKey = arp_.advanceOnEdge();
        }

        // steps 12–13 — CV / Gate output. The sequencer takes precedence when playing;
        // otherwise the arp drives the step; otherwise no step event is emitted.
        ctl::ControlEvent ev{};
        ev.sampleOffset = off;
        if (seqPlaying) {
            ev.pitch = sr.pitch6;
            ev.gate = sr.gateOn;
            ev.trig = sr.retrigger;
            ev.porta = sr.tie;
            if (!emit(ev)) break;
        } else if (arpEngaged && arpKey >= 0) {
            ev.pitch = arpKey;
            ev.gate = true;
            ev.trig = true;        // each arp step retriggers (per S7 GateTrig default)
            ev.porta = false;
            if (!emit(ev)) break;
        }
        // else: edge fired RANDOM reload only (no step note), still phase-consistent.
    }
}

} // namespace mw::seq
