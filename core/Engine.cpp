// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/Engine.cpp — the prepare/process/reset assembly that wires the voice loop, the
// voice manager, the control core, and the post-voice FX chain behind the seam
// (task 118).
//
// Realizes docs/design/00 §4.1 (graph), §4.4 (sub-blocking), §5.1/§5.5 (seam +
// lifecycle), §6.1 (single-threaded fixed voice-index-order accumulation, no
// synchronization primitive), §9 (RT invariants) [ADR-001 C2-C6, C11; ADR-019
// VT-01/VT-02/VT-03; ADR-017 §Decision]. See Engine.h for the scope/out-of-scope split.

#include "Engine.h"

#include <algorithm>
#include <cstdint>
#include <span>

#include "calibration/EngineConstants.h"
#include "calibration/OversampledZoneConstants.h"
#include "calibration/SequencerRoutingConstants.h"

namespace mw {

namespace {

// ---------------------------------------------------------------------------
// FTZ/DAZ scoped guard (§9.1 RT-5) — denormals flushed for the whole process call so
// self-oscillating / long-decay tails never enter a denormal CPU stall [ADR-001 C11].
// Set at process entry, restored on exit. Portable across arm64 (FPCR FZ bit) and
// x86 (MXCSR FTZ+DAZ); a no-op fallback on other targets keeps mwcore freestanding.
// ---------------------------------------------------------------------------
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
        std::uint64_t fpcr = 0;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        // FZ (bit 24): flush-to-zero. On AArch64 this also subsumes the input
        // denormal handling, so FZ alone gives FTZ+DAZ behavior.
        fpcr |= (std::uint64_t{1} << 24);
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        saved_ = getCsr();
        // FTZ (bit 15) + DAZ (bit 6).
        setCsr(saved_ | 0x8040u);
#else
        // Unknown target: no architectural denormal control — leave the guard inert.
        saved_ = 0;
#endif
    }

    ~ScopedFlushDenormals() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
        const std::uint64_t fpcr = saved_;
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        setCsr(static_cast<unsigned>(saved_));
#endif
    }

    ScopedFlushDenormals(const ScopedFlushDenormals&)            = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    static unsigned getCsr() noexcept {
        unsigned csr = 0;
        __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
        return csr;
    }
    static void setCsr(unsigned csr) noexcept {
        __asm__ __volatile__("ldmxcsr %0" : : "m"(csr));
    }
#endif
    std::uint64_t saved_ = 0;
};

// Translate one seam-side MidiEvent into the engine-internal NoteEvent the
// VoiceManager consumes (§4.1: host events are marshalled to PODs in plugin/; here we
// route the note-shaped ones to the voice path). Non-note events (CC/clock/param) are
// not voice-routed by this assembly and are skipped. The sampleOffset is rebased to
// the chunk so it stays sample-accurate within the segment.
[[nodiscard]] bool toNoteEvent(const MidiEvent& e, int chunkOffset, NoteEvent& out) noexcept {
    switch (e.type) {
        case NormalizedType::NoteOn:
            // A NoteOn with zero velocity is the conventional running-status note-off.
            if (e.value <= 0.0f) {
                out.type = NoteEvent::Type::NoteOff;
            } else {
                out.type = NoteEvent::Type::NoteOn;
            }
            break;
        case NormalizedType::NoteOff:
            out.type = NoteEvent::Type::NoteOff;
            break;
        default:
            return false; // not voice-routed by this assembly
    }
    out.note         = static_cast<std::uint8_t>(std::clamp<int>(e.noteId, 0, 127));
    out.velocity     = std::clamp(e.value, 0.0f, 1.0f);
    out.sampleOffset = e.sampleOffset - chunkOffset;
    return true;
}

// The fixed per-instance drift-PRNG seed (§9.2; ADR-001 Decision). A constant so
// prepare() and reset() seed the voice pool to the IDENTICAL byte-stable known start;
// the plugin overrides it per instance off the audio thread (out of scope here). (PI) —
// an internal assembly constant, not a public calibration surface.
constexpr std::uint32_t kInstanceSeed = 0x6d77656eu;

} // namespace

void Engine::prepare(double sampleRate, int maxBlockSize, int maxVoices) noexcept {
    sampleRate_   = sampleRate;
    maxBlockSize_ = std::max(0, maxBlockSize);
    maxVoices_    = std::max(0, maxVoices);

    // Select the per-voice 2x-oversampled-zone factor; clamp to 1x above OS_CEILING_HZ
    // so the filter-stability guard holds at unblessed high host rates [§8.5; ADR-023
    // V15]. The clamp is a (PI)-free policy from OversampledZoneConstants.
    oversampleFactor_ = cal::oszone::wouldExceedCeiling(sampleRate_, cal::oszone::kDefaultFactor)
        ? cal::oszone::kFactor1x
        : cal::oszone::kDefaultFactor;

    // The ONLY allocation site (§5.5 prepare; ADR-001 C2). Size the per-voice stereo
    // accumulation scratch and the mono voice-sum the FX chain consumes, all to the
    // worst-case host block.
    const auto n = static_cast<std::size_t>(maxBlockSize_);
    mixL_.assign(n, 0.0f);
    mixR_.assign(n, 0.0f);
    mono_.assign(n, 0.0f);

    // Pre-size the sequencer-handoff event scratch (task 118c; §9 RT6). A chunk's worst
    // case is one note event per frame (KeyEvent in) and the SequencerEngine can emit at
    // most one ControlEvent per frame (one edge or one passthrough key per sample), so
    // capping each at the worst-case host block is sufficient — the renderChunk that
    // fills them is <= maxBlockSize_ frames, and processBlock clamps to out.size().
    // A 1-frame floor keeps the spans non-empty so .data() is valid even at maxBlock 0.
    const auto evCap = static_cast<std::size_t>(std::max(maxBlockSize_, 1));
    keyScratch_.assign(evCap, control::KeyEvent{});
    ctrlScratch_.assign(evCap, control::ControlEvent{});

    // Prepare the consumed modules. A fixed instance seed keeps per-voice drift PRNG
    // state byte-stable across runs (§9.2; ADR-001 Decision); the plugin overrides it
    // per instance off the audio thread (out of scope here).
    // VoiceManager prepares its OWN sole KeyAssigner (the single MONO/UNISON authority,
    // doc 04 §5.1/§9) — the engine owns no second KeyAssigner (task 118b reconcile).
    voices_.prepare(sampleRate_, oversampleFactor_, kInstanceSeed);
    control_.prepare(sampleRate_);
    sequencer_.prepare(sampleRate_, std::max(maxBlockSize_, 1));   // arp/seq/clock (task 118c)
    fx_.prepare(sampleRate_, maxBlockSize_);

    prepared_ = true;

    reset();
}

void Engine::reset() noexcept {
    // Return EVERY consumed module to the SAME known start prepare() establishes, so
    // reset() — not only prepare() — is a deterministic fixed point: two divergent play
    // histories + reset() + the same input block produce BIT-IDENTICAL output to a
    // freshly-prepared engine (§5.5 known-start contract via reset(); task 134b). RT hot
    // path: no allocation, no lock [ADR-001 Decision / C2-C4].
    //
    // WHY a re-seed of the voice pool, not the soft VoiceManager::reset() panic: the
    // wave-12 QA finding on PR #75 (task 134) is that the as-built bare reset() left the
    // VoiceManager dirty — VoiceManager::reset() de-asserts the gate and RELEASES sounding
    // voices "in place", so a voice survives as Releasing with a non-zero envelope level
    // AND residual oscillator phase / filter state / drift-PRNG position; its decaying
    // tail then bleeds into the next block, so two histories + reset() diverged (~0.28
    // max-abs). Only re-deriving each voice's seed-keyed known start (the same Idle/
    // zero-phase state prepare() sets) makes reset() an actual fixed point. We reuse the
    // already-prepared sample-rate / oversample-factor / instance-seed, so this re-seeds
    // (re-establishes the known start) WITHOUT growing any buffer: every container the
    // voice pool / control core / FX chain own was sized at the first prepare() and is
    // unchanged here, so this path is allocation-free at steady state — asserted under an
    // armed AudioThreadGuard by the engine_reset RT test [ADR-001 C2; §9.1 RT-6]. The
    // hqTable build short-circuits (already built), so no DSP table is rebuilt.
    //
    //   - voices_ : re-derive the seed-keyed per-voice known start (Idle, zero phase,
    //     fresh drift PRNG) and the MONO/unison=1 default topology — the canonical fresh
    //     pool state. This subsumes the panic clear (the KeyAssigner is re-prepared too).
    //   - control_.reset() : re-derive the control-tick known start (sample/tick counters,
    //     jitter PRNG seed, crossfade blend, first tick boundary) WITHOUT re-sizing, so
    //     the residual control-tick phase that previously survived a bare reset() is wiped
    //     (task 134b; the macro pole + jitter toggle are preserved — a reset is not a
    //     parameter change).
    //   - fx_.reset() : clear the FX delay/modulation state.
    //   - sequencer_.reset() : clear the arp/seq/clock/RANDOM play state to the known
    //     start (arp cursor, seq playhead, clock phase, RANDOM counter) WITHOUT touching
    //     the published snapshot, so a loaded pattern + configured arp/clock mode survive
    //     a transport reset (task 118c; doc 05 §9.2 — only the play state is wiped). This
    //     re-derives the control-side fixed point so two divergent play histories +
    //     reset() converge on the seq/arp output too.
    voices_.prepare(sampleRate_, oversampleFactor_, kInstanceSeed);
    control_.reset();
    sequencer_.reset();
    seqHeldNote_    = -1;
    currentSeqStep_ = -1;
    seqPlayMirror_  = 0;
    seqWasPlaying_  = false;
    fx_.reset();
    std::fill(mixL_.begin(), mixL_.end(), 0.0f);
    std::fill(mixR_.begin(), mixR_.end(), 0.0f);
    std::fill(mono_.begin(), mono_.end(), 0.0f);
}

void Engine::renderChunk(const BlockContext& ctx, int n0, int len) noexcept {
    // 1. Apply the note events that fall within this chunk sample-accurately through the
    //    VoiceManager's SINGLE note ingress (§4.4, §9): handleNoteEvent feeds the
    //    VoiceManager's OWN KeyAssigner — the documented sole MONO/UNISON note-priority
    //    authority (doc 04 §5.1/§9; ADR-006 C12). The block is split at event offsets so
    //    events land at chunk heads; the control tick below resolves that same authority.
    //    Non-note events are not voice-routed by this assembly (task 118b reconcile: the
    //    engine no longer owns a duplicate KeyAssigner).
    //
    //    Task 118c routing seam: WHILE THE TRANSPORT IS RUNNING the note ingress is owned
    //    by the SequencerEngine (the arp/seq/clock/RANDOM state machine, doc 05 §2.1). The
    //    chunk's MIDI notes become KeyEvents fed to the SequencerEngine; its emitted
    //    ControlEvents (the step-sequencer / arpeggiator selected note + gate + trig, or a
    //    passthrough of the held key when neither is active) are translated back into
    //    NoteEvents and driven into the SAME single MONO/UNISON voice path as a keyboard
    //    key (doc 04 §9: "the arp/seq emit NoteEvents like a keyboard"; ADR-006 C12). When
    //    the transport is STOPPED the unchanged direct keyboard path runs (no regression to
    //    the task-118 voice path); POLY arp/seq routing is out of scope (SH-101 is mono).
    const MidiEventView& midi = ctx.midi;
    const bool transportRunning = ctx.transport.isPlaying;

    // The sequencer OWNS note ingress this chunk only when it is actually driving notes:
    // the step sequencer is playing, OR the arpeggiator is explicitly ENABLED. The
    // as-built ControlSnapshot (task 087) has no arp on/off field — arpMode is Up/UandD/
    // Down with no "off", and Arpeggiator::isEngaged() latches on any >=2-key hold — so a
    // bare held chord would otherwise auto-arpeggiate and silently break plain keyboard
    // play (the engine_s7 KeyAssigner-priority contract, ADR-006 C12). The one unambiguous
    // "arp is on" signal the as-built snapshot CAN express is the HOLD latch (arpHold);
    // doc 05 §5.1 names `mw101.arp.mode` as the on/off + direction, so the proper fix is a
    // doc-06/task-087 arp-enable in the snapshot (recorded as a routing-seam addendum in
    // the PR). Until then we gate arp routing on arpHold so a plain chord stays keyboard
    // play and only a latched arp (or a playing seq) hands ingress to the SequencerEngine.
    const mw::control::ControlSnapshot* snap = sequencer_.liveSnapshot();
    const bool seqPlaying = (snap != nullptr) && sequencer_.seq().isPlaying()
                            && (sequencer_.seq().count() > 0);
    const bool arpEnabled = (snap != nullptr) && snap->arpHold;   // the expressible arp-on
    const bool sequencerOwnsIngress = transportRunning && (seqPlaying || arpEnabled);

    if (!sequencerOwnsIngress) {
        // STOPPED, or RUNNING with no seq/arp active: the unchanged task-118 direct
        // keyboard ingress (plain keyboard play resolves via the sole KeyAssigner).
        for (int i = 0; i < midi.numEvents; ++i) {
            const MidiEvent& e = midi.events[i];
            if (e.sampleOffset < n0 || e.sampleOffset >= n0 + len) continue;
            NoteEvent ne{};
            if (toNoteEvent(e, n0, ne)) {
                voices_.handleNoteEvent(ne);
            }
        }
        // While not running the sequencer, keep the live step "no step" so telemetry does
        // not show a stale playhead (and the seam's held-note shadow stays clear).
        if (!transportRunning) {
            currentSeqStep_ = -1;
            seqHeldNote_    = -1;
            seqWasPlaying_  = false;
        }
    } else {
        // RUNNING: route note ingress through the SequencerEngine. Collect this chunk's
        // MIDI notes as KeyEvents, folding the MIDI note into the arp/trigger 0..31 key
        // space (subtract the seam base) so the SequencerEngine's 32-key arp bitmap and
        // the TriggerSource accept them; the same base maps an emitted pitch back to a
        // MIDI note in routeControlEvents (so an arp step recovers the exact note played).
        int keyCount = 0;
        const int keyCap = static_cast<int>(keyScratch_.size());
        for (int i = 0; i < midi.numEvents && keyCount < keyCap; ++i) {
            const MidiEvent& e = midi.events[i];
            if (e.sampleOffset < n0 || e.sampleOffset >= n0 + len) continue;
            NoteEvent ne{};
            if (!toNoteEvent(e, n0, ne)) continue;

            control::KeyEvent ke{};
            ke.pitch        = static_cast<int>(ne.note) - cal::seqroute::kSeqVoiceBaseMidi;
            ke.gate         = (ne.type == NoteEvent::Type::NoteOn);
            ke.trig         = ke.gate;
            ke.porta        = false;
            ke.mod          = ne.velocity;
            ke.sampleOffset = ne.sampleOffset;   // already rebased to the chunk
            keyScratch_[static_cast<std::size_t>(keyCount++)] = ke;
        }

        // Advance the arp/seq/clock state machine over this chunk: one H->L clock edge
        // advances arp + seq + RANDOM together (doc 05 §2.1 C17). The transport carries
        // the chunk-relative PPQ so host-sync edges land sample-accurately within [0,len).
        TransportInfo chunkT = ctx.transport;
        if (chunkT.sampleRate <= 0.0) chunkT.sampleRate = sampleRate_;
        // Advance the block-start PPQ to this chunk's head so a multi-chunk block keeps
        // host-sync edges continuous (PPQ advances by the chunk's frame count).
        if (chunkT.sampleRate > 0.0) {
            const double ppqPerSample = (chunkT.bpm / 60.0) / chunkT.sampleRate;
            chunkT.ppqPosition += ppqPerSample * static_cast<double>(n0);
        }

        int eventCount = 0;
        sequencer_.processBlock(
            chunkT,
            std::span<const control::KeyEvent>{keyScratch_.data(),
                                               static_cast<std::size_t>(keyCount)},
            std::span<const int>{},   // Ext-clock pulse offsets: none in this core seam
            std::span<control::ControlEvent>{ctrlScratch_.data(), ctrlScratch_.size()},
            len,
            eventCount);

        // Drive the emitted ControlEvents into the single voice path (sets seqHeldNote_
        // transitions + latches the live current step for telemetry).
        routeControlEvents(eventCount);
    }

    // 2. Advance the control core over this chunk; it fires the control tick(s) at the
    //    (PI) sub-block cadence. Each fired tick calls VoiceManager::controlTick(), which
    //    resolves the VoiceManager's OWN KeyAssigner (the single authority) and applies
    //    the decision to the active voice(s) — the SOLE call into the VoiceManager from
    //    the control side (§4.2; ControlCore owns no VM internals, ControlCore.h §7.8).
    //    There is exactly ONE KeyAssigner in the path, so the S7 selector set via
    //    Engine::setGateTrigMode (incl. LFO clock-reset) audibly takes effect.
    control_.advance(len, voices_);

    // 3. Render every active voice for this chunk, ACCUMULATING into the mono mix in
    //    FIXED voice-index order; single-threaded, no synchronization primitive
    //    (§6.1; ADR-019 VT-01/VT-02/VT-03). Clear the segment of the stereo scratch
    //    first so each chunk's voice sum is independent.
    float* L = mixL_.data();
    float* R = mixR_.data();
    for (int i = 0; i < len; ++i) {
        L[i] = 0.0f;
        R[i] = 0.0f;
    }
    voices_.render(L, R, len);

    // 4. Collapse the per-voice stereo accumulation to the MONO VOICE SUM the post-voice
    //    FX chain runs on (§4.1: FX run once on the mono sum). In MONO/UNISON-centered
    //    cases L == R so this is loss-free; for panned voices it is the balanced mono mix.
    float* m = mono_.data();
    for (int i = 0; i < len; ++i) {
        m[i] = 0.5f * (L[i] + R[i]);
    }

    // 5. Run the post-voice FX chain ONCE on the mono sum, writing the final stereo into
    //    the host's borrowed output channels for this segment (§4.1; ADR-017 §Decision).
    //    The output view is borrowed; the core writes, never grows/frees it (ADR-001 C3).
    float* outChans[2];
    AudioBlockView& audio = const_cast<AudioBlockView&>(ctx.audio);
    float* out0 = audio.channels[0];
    float* out1 = (audio.numChannels > 1) ? audio.channels[1] : audio.channels[0];
    outChans[0] = out0 + n0;
    outChans[1] = out1 + n0;
    fx_.process(m, outChans, len);
}

void Engine::routeControlEvents(int eventCount) noexcept {
    // Translate the SequencerEngine's emitted ControlEvents (this chunk) into the
    // VoiceManager's single note ingress, exactly as a keyboard would (doc 04 §9; ADR-006
    // C12). The ControlEvent.pitch is in the control-core pitch space (seq 6-bit / arp key
    // index, or — when neither seq nor arp drives — the engine-folded keyboard key); add
    // the seam base to recover the MIDI note the sole KeyAssigner is keyed on.
    //
    // TWO distinct routing modes (the SequencerEngine chose which by what it emitted):
    //
    //  - MANUAL PASSTHROUGH (transport running, NO seq playing, NO arp engaged): the
    //    SequencerEngine emits one ControlEvent per inbound keyboard KeyEvent, carrying its
    //    exact gate on/off. We route each straight to the KeyAssigner as NoteOn/NoteOff so
    //    genuine multi-key held state is preserved and the S7 lowest-/last-note priority is
    //    resolved by the single authority (no monophonic collapse) — this keeps running
    //    keyboard play identical to the stopped path's KeyAssigner semantics (ADR-006 C12).
    //
    //  - MONOPHONIC STEP SOURCE (seq playing OR arp engaged): the source emits exactly one
    //    note per clock edge. The KeyAssigner is a HELD-key model, so a momentary step is
    //    expressed as held-state transitions on a SINGLE engine-held note: a gate-on note
    //    step releases the previously-held step note and presses the new one (a fresh key ->
    //    retrigger under GateTrig); a TIE step (porta) sustains without re-pressing (legato,
    //    no re-gate, doc 05 §6.4); a gate-off step (REST) releases the held note.
    //
    // The subsequent control_.advance() in renderChunk resolves this single KeyAssigner
    // once per control tick and drives the voice(s).
    const bool seqPlaying  = sequencer_.seq().isPlaying() && (sequencer_.seq().count() > 0);
    const bool arpEngaged  = sequencer_.arp().isEngaged();
    const bool stepSource  = seqPlaying || arpEngaged;

    // Re-sync the playhead mirror on a not-playing -> playing transition: the StepSequencer
    // rewinds playPos to 0 there (loadBuffer / a fresh PLAY), so the next slot is 0.
    if (seqPlaying && !seqWasPlaying_) {
        seqPlayMirror_ = 0;
    }
    seqWasPlaying_ = seqPlaying;

    const int seqCount = sequencer_.seq().count();

    for (int i = 0; i < eventCount; ++i) {
        const control::ControlEvent& ev = ctrlScratch_[static_cast<std::size_t>(i)];
        const int midiNote =
            std::clamp(ev.pitch + cal::seqroute::kSeqVoiceBaseMidi, 0, 127);
        const float vel = std::clamp(ev.mod > 0.0f ? ev.mod : 1.0f, 0.0f, 1.0f);

        if (!stepSource) {
            // MANUAL PASSTHROUGH: route the keyboard event verbatim to the KeyAssigner so
            // multi-key held state + S7 priority match the stopped path exactly.
            NoteEvent ne{};
            ne.type         = ev.gate ? NoteEvent::Type::NoteOn : NoteEvent::Type::NoteOff;
            ne.note         = static_cast<std::uint8_t>(midiNote);
            ne.velocity     = ev.gate ? vel : 0.0f;
            ne.sampleOffset = ev.sampleOffset;
            voices_.handleNoteEvent(ne);
            continue;
        }

        // MONOPHONIC STEP SOURCE -------------------------------------------------------
        // Latch the REAL live playhead slot when a seq step plays this edge (a seq edge
        // emits exactly one ControlEvent — note OR rest — so each emitted event while seq
        // is playing corresponds to one playhead advance), then mirror the StepSequencer's
        // deterministic (playPos+1)%count advance (closes 111c). Arp-only edges leave the
        // seq playhead alone.
        if (seqPlaying) {
            currentSeqStep_ = seqPlayMirror_;
            if (seqCount > 0) {
                seqPlayMirror_ = (seqPlayMirror_ + 1) % seqCount;
            }
        }

        if (ev.gate) {
            if (ev.porta) {
                // TIE / legato: sustain without re-pressing. If the note changed, press
                // the new one WITHOUT releasing the prior (the KeyAssigner resolves the
                // legato, no re-gate); if it is the same note, nothing to do.
                if (midiNote != seqHeldNote_) {
                    NoteEvent on{};
                    on.type         = NoteEvent::Type::NoteOn;
                    on.note         = static_cast<std::uint8_t>(midiNote);
                    on.velocity     = vel;
                    on.sampleOffset = ev.sampleOffset;
                    voices_.handleNoteEvent(on);
                    seqHeldNote_ = midiNote;
                }
            } else {
                // Gate-on note step: release the previous step note, press the new one so
                // the KeyAssigner sees a fresh key (retrigger under GateTrig). Releasing
                // first keeps the held set to exactly the one current step note.
                if (seqHeldNote_ >= 0 && seqHeldNote_ != midiNote) {
                    NoteEvent off{};
                    off.type         = NoteEvent::Type::NoteOff;
                    off.note         = static_cast<std::uint8_t>(seqHeldNote_);
                    off.velocity     = 0.0f;
                    off.sampleOffset = ev.sampleOffset;
                    voices_.handleNoteEvent(off);
                }
                NoteEvent on{};
                on.type         = NoteEvent::Type::NoteOn;
                on.note         = static_cast<std::uint8_t>(midiNote);
                on.velocity     = vel;
                on.sampleOffset = ev.sampleOffset;
                voices_.handleNoteEvent(on);
                seqHeldNote_ = midiNote;
            }
        } else {
            // Gate-off step (REST or all keys up): release the held step note.
            if (seqHeldNote_ >= 0) {
                NoteEvent off{};
                off.type         = NoteEvent::Type::NoteOff;
                off.note         = static_cast<std::uint8_t>(seqHeldNote_);
                off.velocity     = 0.0f;
                off.sampleOffset = ev.sampleOffset;
                voices_.handleNoteEvent(off);
                seqHeldNote_ = -1;
            }
        }
    }
}

void Engine::process(const BlockContext& ctx) noexcept {
    // §9.1 RT-5: flush denormals for the whole call (set at entry, restored on exit).
    ScopedFlushDenormals flushGuard;

    const int numFrames = ctx.audio.numFrames;
    if (numFrames <= 0 || ctx.audio.channels == nullptr || ctx.audio.numChannels <= 0)
        return;

    // §4.4: split the host block at MIDI/event sample-offsets, then render each segment
    // in fixed-size internal chunks capped at kRenderBlock. We walk the block boundary
    // set { 0, every in-range event offset, numFrames } in ascending order; between two
    // consecutive boundaries we render kRenderBlock-capped chunks so no internal render
    // runs longer than the cap and every event applies at the exact sample.
    constexpr int kCap = cal::engine::kRenderBlock;

    int pos = 0;
    const MidiEventView& midi = ctx.midi;
    int evt = 0; // index into the (sampleOffset-ascending) event span

    while (pos < numFrames) {
        // Find the next event boundary strictly after `pos` (events are ordered by
        // sampleOffset; skip any at or before pos — they are applied at the chunk head).
        int nextBoundary = numFrames;
        while (evt < midi.numEvents
               && midi.events[evt].sampleOffset <= pos) {
            ++evt;
        }
        if (evt < midi.numEvents) {
            const int off = midi.events[evt].sampleOffset;
            if (off > pos && off < numFrames)
                nextBoundary = off;
        }

        // Render [pos, nextBoundary) in kRenderBlock-capped chunks.
        while (pos < nextBoundary) {
            const int len = std::min(kCap, nextBoundary - pos);
            renderChunk(ctx, pos, len);
            pos += len;
        }
    }
}

} // namespace mw
