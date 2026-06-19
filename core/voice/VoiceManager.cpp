// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/voice/VoiceManager.cpp — implementation of the fixed-pool owner, the MONO and
// UNISON drive paths, control-tick propagation, the active-voice list, and the
// fixed-index-order render/sum (task 074).
//
// Realizes docs/design/04-voice-and-control.md §6.1-§6.3 and §8 (RT1-RT7) under
// ADR-006 §Decision item 3 (MONO/UNISON), ADR-019 VT-01/VT-02, and ADR-001 (no-alloc/
// no-lock/noexcept hot path). No (PI) literal is inlined: the UNISON detune/spread
// laws and defaults live in core/calibration/VoiceManagerConstants.h [docs/design/00
// §1.2; ADR-020 S13].
//
// The POLY drive path is WIRED here (task 075b) by COMPOSING the task-075 PolyAllocator:
// POLY note events bypass the KeyAssigner and route straight into polyAlloc_ (allocate on
// note-on, release on note-off, deterministic steal at capacity), so VoiceMode::Poly
// allocates and SOUNDS voices through the shared render path. The allocator's policy
// (idle -> re-strike -> deterministic steal, §6.4 / ADR-006 C12-C16) is owned by task 075
// and is NOT re-implemented here. MONO/UNISON are unchanged.

#include "voice/VoiceManager.h"

#include <algorithm>
#include <cmath>

#include "calibration/VoiceConstants.h"        // kMaxPoly (the POLY group budget)
#include "calibration/VoiceManagerConstants.h"

namespace mw {

namespace {

// MIDI note -> Hz (A4 = note 69 = 440 Hz). VoiceManager drives the voice's glide
// target in Hz directly from the NoteDecision's resolved MIDI note; the VINTAGE 6-bit
// pitch quantization is ControlCore's (task 070/071, out of scope). Deterministic.
inline float midiToHz(int midiNote) noexcept {
    return 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
}

} // namespace

void VoiceManager::prepare(double sampleRate, int oversampleFactor,
                           std::uint32_t instanceSeed) noexcept {
    // Size and seed every voice once, off the audio thread (§8 RT6; ADR-006 §4, C17).
    // Each voice gets its fixed pool index so it carries a distinct deterministic
    // drift seed (§6.5; ADR-006 C18) — the basis for real unison beating (§6.3).
    for (int i = 0; i < kMaxVoices; ++i) {
        pool_[static_cast<std::size_t>(i)].prepare(
            sampleRate, oversampleFactor, i, instanceSeed);
    }

    keyAssigner_.prepare();

    mode_       = VoiceMode::Mono;     // default / bless target [ADR-016 R-3]
    unison_     = 1;
    nextSerial_ = 0;

    pendingReconfig_ = false;
    pendingMode_     = mode_;
    pendingUnison_   = unison_;

    active_.fill(0);
    activeCount_ = 0;

    // Bind the composed POLY allocator to THIS pool + the shared note-serial counter for
    // the current unison count (§6.4; ADR-006 C16). Off the audio thread; this is the
    // allocator's prepare-time configuration, never mid-block (§8 RT7).
    configurePolyAllocator();
}

void VoiceManager::configurePolyAllocator() noexcept {
    // (Re)bind polyAlloc_ to pool_ and the shared monotonic note-serial counter. The poly
    // group budget is the (PI) v1 poly cap kMaxPoly; the allocator forms floor(kMaxPoly/U)
    // groups of U contiguous slots, clamped so groups*U <= kMaxVoices [ADR-006 C16].
    // Pure integer configuration; noexcept, alloc-free, lock-free.
    polyAlloc_.configure(pool_, nextSerial_, /*maxPoly=*/cal::voice::kMaxPoly, unison_);
}

void VoiceManager::setMode(VoiceMode m) noexcept {
    // Latched: applied at the next block boundary (top of render), never mid-block
    // [§8 RT7; ADR-006 C17].
    pendingMode_     = m;
    pendingReconfig_ = true;
}

void VoiceManager::setUnisonCount(int u) noexcept {
    // Clamp to 1..kMaxUnison [ADR-006 C9]; latched to a block boundary [§8 RT7].
    pendingUnison_   = std::clamp(u, 1, kMaxUnison);
    pendingReconfig_ = true;
}

void VoiceManager::setGateTrigMode(GateTrigMode m) noexcept {
    // The S7 selector forwards straight to the KeyAssigner. It changes priority/
    // trigger behavior, not pool topology, so it does not need a block-boundary latch.
    keyAssigner_.setMode(m);
}

void VoiceManager::reset() noexcept {
    // Panic / transport reset: clear the sole KeyAssigner's held/scan state (doc 04
    // §5.2) and de-assert the gate on any sounding voice so its tail releases in place,
    // then refresh the active list. RT-safe: no allocation, no lock, bounded
    // O(kMaxVoices) (§8 RT3/RT6; ADR-001 C3/C4). The S7 selector itself is preserved
    // (a reset is not a parameter change).
    keyAssigner_.reset();
    for (int i = 0; i < kMaxVoices; ++i) {
        Voice& v = pool_[static_cast<std::size_t>(i)];
        if (v.isActive()) {
            v.noteOff();
        }
    }
    rebuildActiveList();
}

void VoiceManager::handleNoteEvent(const NoteEvent& e) noexcept {
    // Single ingress for all note sources (§9). MONO/UNISON flow through the
    // KeyAssigner (the sole note-priority authority); POLY bypasses it entirely and
    // routes straight into the composed PolyAllocator (§6.4; ADR-006 C12), so a POLY
    // note-on allocates/steals its own voice group and a note-off releases it.
    if (mode_ == VoiceMode::Poly) {
        switch (e.type) {
            case NoteEvent::Type::NoteOn:
                // Every poly note-on allocates its OWN voice group (idle -> re-strike ->
                // deterministic steal); the allocator stamps the monotonic note-serial and
                // fires a fresh GATE+TRIG trigger on the whole group [ADR-006 C12-C16].
                polyAlloc_.allocatePoly(static_cast<int>(e.note));
                break;
            case NoteEvent::Type::NoteOff:
                // Release EVERY group sounding this note (gate de-assert -> release tail in
                // place); other held groups are untouched [ADR-006 C12/C15].
                polyAlloc_.releasePoly(static_cast<int>(e.note));
                break;
            case NoteEvent::Type::AllNotesOff:
                // Panic / all-notes-off: de-assert the gate on every sounding poly voice so
                // each tail releases in place (the active list refreshes on the next tick/
                // render). Bounded O(kMaxVoices), alloc-free (§8 RT3/RT6).
                for (int i = 0; i < kMaxVoices; ++i) {
                    Voice& v = pool_[static_cast<std::size_t>(i)];
                    if (v.isActive()) {
                        v.noteOff();
                    }
                }
                break;
        }
        // Refresh the dense active list so a render that begins after these events (with no
        // intervening control tick) walks current voice state.
        rebuildActiveList();
        return;
    }

    switch (e.type) {
        case NoteEvent::Type::NoteOn:
            keyAssigner_.noteOn(static_cast<int>(e.note));
            break;
        case NoteEvent::Type::NoteOff:
            keyAssigner_.noteOff(static_cast<int>(e.note));
            break;
        case NoteEvent::Type::AllNotesOff:
            keyAssigner_.reset();
            break;
    }
}

void VoiceManager::controlTick(const NoteDecision& d) noexcept {
    // Propagate the resolved decision to the active voice(s) for MONO/UNISON (§6.1).
    // POLY does not use the KeyAssigner decision (task 075).
    switch (mode_) {
        case VoiceMode::Mono:
            driveMono(d);
            break;
        case VoiceMode::Unison:
            driveUnison(d);
            break;
        case VoiceMode::Poly:
            break;  // task 075
    }
    rebuildActiveList();
}

void VoiceManager::controlTick() noexcept {
    // The ControlCore-driven control tick (task 118b reconcile): resolve THIS manager's
    // own keyAssigner_ — the single MONO/UNISON note-priority authority (doc 04 §5.1/§9,
    // ADR-006 C12) — and apply the decision, so there is exactly ONE KeyAssigner in the
    // engine's path. POLY bypasses the KeyAssigner entirely [ADR-006 C12]; resolving it
    // for the bypassed mode would be wasted work and could surface a stale decision, so
    // POLY takes no decision here (its allocator is task 075). The resolve() snapshot
    // bookkeeping (prevScan_/lastActive_) advances only when the authority is actually
    // in the path. noexcept, alloc-free, lock-free [§8 RT3/RT6; ADR-001 C3-C5].
    if (mode_ == VoiceMode::Poly) {
        rebuildActiveList();
        return;
    }
    controlTick(keyAssigner_.resolve());
}

void VoiceManager::applyDecisionToVoice(int voiceIndex, const NoteDecision& d) noexcept {
    // The MONO pass-through (§6.2), reused per-voice by UNISON (§6.3): apply the
    // {activeNote, gate, retrigger} decision verbatim to one voice slot. Zero
    // behavioral logic on top — mono is bit-faithful by construction [ADR-006
    // §Decision item 3 MONO; ADR-016 R-3].
    Voice& v = pool_[static_cast<std::size_t>(voiceIndex)];

    if (d.gate && d.activeNote >= 0) {
        const float targetHz = midiToHz(d.activeNote);
        if (d.retrigger) {
            // Fire the ADSR from its trigger state on the resolved note (K3/K4).
            v.setNoteSerial(nextSerial_++);
            v.noteOn(d.activeNote, /*velocity=*/1.0f, /*retrigger=*/true);
            v.setGlideTarget(targetHz);
        } else if (v.isActive()) {
            // Gate stays asserted, no retrigger: glide to the (possibly new) note;
            // the single held gate is preserved (K1/K2 legato-no-retrigger).
            v.setGlideTarget(targetHz);
        } else {
            // Gate is newly asserted (silence -> held) with no retrigger flagged
            // (e.g. MONO+Gate gate edge resolves retrigger itself; this is the
            // first key). Sound the note without re-firing past trigger state.
            v.setNoteSerial(nextSerial_++);
            v.noteOn(d.activeNote, /*velocity=*/1.0f, /*retrigger=*/false);
            v.setGlideTarget(targetHz);
        }
    } else {
        // Gate de-asserted (all keys released, K7): release the tail in place.
        if (v.isActive()) {
            v.noteOff();
        }
    }
}

void VoiceManager::driveMono(const NoteDecision& d) noexcept {
    // Exactly ONE active Voice (slot 0), driven verbatim by the decision (§6.2).
    applyDecisionToVoice(0, d);
}

void VoiceManager::driveUnison(const NoteDecision& d) noexcept {
    // Broadcast the ONE decision to U voices (slots 0..unison_-1), then apply per-voice
    // symmetric centered cents detune and (PI) stereo-spread pan (§6.3; ADR-006
    // C9/C10). Each voice keeps its distinct deterministic drift seed (set in prepare),
    // so detune is real analog beating, not a static fan.
    const int u = std::clamp(unison_, 1, kMaxUnison);
    for (int i = 0; i < u; ++i) {
        // Selection/retrigger are identical to MONO for every voice (the same single
        // decision) — unison note-feel stays mono-faithful (K9).
        applyDecisionToVoice(i, d);

        // Per-voice detune/spread (K10). Symmetric and centered: voice i of u gets
        // detune cal::voice::unisonDetuneCents(i,u,spread) and pan unisonPan(...).
        Voice& v = pool_[static_cast<std::size_t>(i)];
        v.setDetuneCents(cal::voice::unisonDetuneCents(
            i, u, cal::voice::kDefaultUnisonDetuneCents));
        v.setStereoPan(cal::voice::unisonPan(
            i, u, cal::voice::kDefaultUnisonSpread));
    }
}

void VoiceManager::applyPendingReconfig() noexcept {
    // Apply a latched mode/unison-count change at a block boundary (§8 RT7; ADR-006
    // C17). To avoid a stuck note across a topology change, idle any voice that falls
    // outside the new active set (e.g. shrinking unison or switching mode). Bounded
    // O(kMaxVoices), alloc-free.
    if (!pendingReconfig_) {
        return;
    }

    const VoiceMode newMode   = pendingMode_;
    const int       newUnison = std::clamp(pendingUnison_, 1, kMaxUnison);

    // Determine how many low-index voice slots the new config may sound through.
    int activeSlots = 0;
    switch (newMode) {
        case VoiceMode::Mono:   activeSlots = 1;          break;
        case VoiceMode::Unison: activeSlots = newUnison;  break;
        case VoiceMode::Poly:   activeSlots = kMaxVoices; break;  // task 075 owns alloc
    }

    // Hard-stop voices outside the new active range so no slot is left stuck sounding
    // a note the new topology can no longer address.
    for (int i = activeSlots; i < kMaxVoices; ++i) {
        Voice& v = pool_[static_cast<std::size_t>(i)];
        if (v.isActive()) {
            v.noteOff();
        }
    }

    mode_   = newMode;
    unison_ = newUnison;

    // The POLY allocator's group layout (floor(kMaxPoly/U) groups of U contiguous slots)
    // depends on the unison count, so re-bind it whenever a reconfig lands while POLY is
    // the active mode (or has just become it). This happens only at the boundary, never
    // mid-block (§8 RT7; ADR-006 C16/C17). Integer-only, alloc-free.
    if (mode_ == VoiceMode::Poly) {
        configurePolyAllocator();
    }

    pendingReconfig_ = false;
    pendingMode_     = mode_;
    pendingUnison_   = unison_;

    rebuildActiveList();
}

void VoiceManager::rebuildActiveList() noexcept {
    // Rebuild the dense active-voice index list in ASCENDING voice-index order so the
    // render sum is in FIXED voice-index order (§8 RT2; ADR-019 VT-02). Bounded
    // O(kMaxVoices), alloc-free; run once per control tick / boundary, never per sample.
    activeCount_ = 0;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (pool_[static_cast<std::size_t>(i)].isActive()) {
            active_[static_cast<std::size_t>(activeCount_++)] = static_cast<std::uint8_t>(i);
        }
    }
}

void VoiceManager::render(float* outL, float* outR, int numSamples) noexcept {
    // §8 RT7: apply any latched mode/unison-count change ONLY here, at the block
    // boundary — never mid-block.
    applyPendingReconfig();

    // A render can begin after note events that mutated voice state without an
    // intervening controlTick; refresh the active list so render walks current state.
    rebuildActiveList();

    // §8 RT1/RT2 + ADR-019 VT-01/VT-02: walk the active-voice list and accumulate each
    // active voice into the block mix in FIXED voice-index order (active_ is built in
    // ascending index order). Single-threaded, no lock, no allocation.
    for (int k = 0; k < activeCount_; ++k) {
        const int idx = static_cast<int>(active_[static_cast<std::size_t>(k)]);
        pool_[static_cast<std::size_t>(idx)].render(outL, outR, numSamples);
    }
}

} // namespace mw
