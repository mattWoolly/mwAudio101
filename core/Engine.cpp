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
#include <cmath>       // std::pow — control-rate skew inverse only, never the audio hot path
#include <cstdint>
#include <limits>
#include <span>

#include "calibration/EngineConstants.h"
#include "calibration/OversampledZoneConstants.h"
#include "calibration/SequencerRoutingConstants.h"
#include "calibration/ControlDispatchConstants.h"   // pitch-CV anchor + vco.range mapping (ADR-028)
#include "calibration/ControlDispatchVcfConstants.h" // cutoff-CV / env-mod / kbd-track law (task 161)
#include "calibration/ControlDispatchLfoConstants.h" // LFO/vel/bend routing depths (task 162)
#include "calibration/ControlDispatchPwmVcfModConstants.h" // manual-PWM + VCF LFO-mod depths (task 162e)
#include "calibration/ControlDispatchCcIngressConstants.h" // CC1/bend ingress + wheel-boost law (task 162c)
#include "calibration/FxDispatchConstants.h"          // FX free-ms / chorus-Hz decode (task 163)
#include "calibration/PitchAssemblyConstants.h"      // kVoltsPerCount (count->volt anchor)
#include "calibration/ControlDispatchCharacterConstants.h" // a4 ref / cents->volts / expression (task 164)

#include "dsp/drift/VintageMacro.h"   // the Age-macro -> drift/var target mapping (task 164)

#include "params/ParamDefs.h"   // kParamDefs registry order (slot lookup, off-thread only)
#include "params/ParamIDs.h"    // canonical parameter string-IDs (no hand-typed strings)

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
    // The played NOTE NUMBER is `data0` — NOT `noteId` (task 118e; the ADR-028 ingress fix).
    // Per docs/design/09 §3.3 + BlockContext.h: `data0` is the note number (widened to float;
    // the §3.3 HostEvent->MidiEvent translation puts the note number here), while `noteId` is
    // the CLAP note-id, which is -1 for a MIDI-derived event and exists ONLY for note-expression
    // VOICE matching — it is NOT the pitch. The pre-fix `out.note = clamp(e.noteId,...)` made a
    // real DAW MIDI note (noteId == -1) resolve to note 0, so the synth played the WRONG note for
    // its primary input (and the seq/arp path that reads ne.note inherited the same wrong note).
    // Truncate the float note number toward zero and clamp into the standard MIDI 0..127 range;
    // the velocity / NoteOn-Off type / noteId (note-expression) handling above is unchanged.
    out.note         = static_cast<std::uint8_t>(
        std::clamp<int>(static_cast<int>(e.data0), 0, 127));
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

    // The analog-character drift engine (task 164; ADR-028 / docs/design/08). Seed it with the
    // SAME fixed kInstanceSeed the voice pool uses so the per-voice drift personality is
    // deterministic + byte-stable across runs and re-prepare (the plugin overrides the seed per
    // instance off the audio thread, out of scope here). De-zipper / OU step at the per-chunk
    // (kRenderBlock) cadence — the dispatch advances it once per renderChunk, matching the
    // control-tick site the rest of the seam runs on. Sizes nothing dynamically (the
    // DriftState[kMaxVoices] array is by-value) [docs/design/08 §8.2, §12.1].
    drift_.setInstanceSeed(static_cast<std::uint64_t>(kInstanceSeed));
    drift_.prepare(sampleRate_, cal::engine::kRenderBlock, mw::kMaxVoices);

    // Resolve the dispatch-consumed parameter slot indices ONCE, off the audio thread, so
    // the per-control-tick dispatch reads the ParamSnapshot by index with no string scan
    // (ADR-028; task 160). RT-safe-by-construction: the hot path never resolves IDs.
    cacheParamSlots();

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
    // Telemetry display latches back to the known start (task 118d): no cutoff dispatched
    // yet, LFO phase at 0. A plain member write on the single-threaded audio path.
    cutoffDisplay_  = 0.0f;
    lfoPhase_       = 0;
    fx_.reset();
    // Re-derive the drift engine's known start (re-draw Tier-1 from the seed, clear the
    // thermal/smoother state) so reset() is a deterministic fixed point for the character
    // model too (task 164). Clear the per-voice note-serial shadow to a sentinel so the FIRST
    // note after reset registers as a fresh trigger and fires the note-on draw [docs/design/08
    // §5.5 lifecycle parity].
    drift_.reset();
    std::fill(lastDriftSerial_.begin(), lastDriftSerial_.end(),
              std::numeric_limits<std::uint64_t>::max());
    // Continuous-controller state back to the neutral no-controller identity (task 162c) so
    // reset() is a deterministic fixed point for the bend/wheel too — a divergent controller
    // history + reset() + the same block renders bit-identically to a fresh engine. process()
    // re-seeds these from ctx.controllers at the next block entry.
    pitchBend_ = 0.0f;
    modWheel_  = 0.0f;
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

    // 0. CONTINUOUS-CONTROLLER INGRESS (task 162c; ADR-028 control-dispatch repair; bend
    //    authority reconciled in task 162d). The 162 dispatch wired pitch-bend->{VCO,VCF} and
    //    mod-wheel->LFO-depth, but the live controller POSITION never reached the engine.
    //
    //    BEND. The pitch-bend WHEEL position is the centered signed unit [-1,+1] that
    //    process() seeded into pitchBend_ from BlockContext::controllers.pitchBend (the host
    //    half, task 162d). It is NOT re-read from the PitchBend MidiEvent here: that event's
    //    `value` carries the bend-range-scaled SEMITONE offset for the §4.4 Pre-Q tuning path
    //    (plugin/midi/MidiFrontEnd forwards `value = semis`), NOT a [-1,+1] unit — re-reading
    //    it as a unit (the old 162c code) mis-scaled the wheel (a half-wheel that is +1 semis
    //    at a ±2-semitone channel range was clamped to a unit of 1.0 and rendered a FULL bend).
    //    So the bend authority is the process() controllers seed; the forwarded SEMITONE
    //    PitchBend MidiEvent stays untouched for the §4.4 path (no regression to midi_tuning).
    //
    //    MOD WHEEL. CC1 (mod wheel) DOES update per-chunk here: its `value` is the raw 7-bit
    //    0..127, normalized to [0,1] — sample-accurate within the block, last-in-window wins
    //    (events are sampleOffset-ascending). Other CC numbers are NOT consumed here — they
    //    reach the engine as ParamValue events via the plugin's CcLearnMap (a separate path;
    //    docs/design/09 §6.2). Independent of the note path. Pure POD read + arithmetic;
    //    noexcept, alloc-free, lock-free [ADR-001 §9; docs/design/09 §4.4].
    for (int i = 0; i < midi.numEvents; ++i) {
        const MidiEvent& e = midi.events[i];
        if (e.sampleOffset < n0 || e.sampleOffset >= n0 + len) continue;
        if (e.type == NormalizedType::ControlChange
            && static_cast<int>(e.data0) == cal::ccingress::kModWheelCcNumber) {
            modWheel_ = std::clamp(e.value / cal::ccingress::kSevenBitMax,
                                   cal::ccingress::kModWheelMin, cal::ccingress::kModWheelMax);
        }
    }

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

    // 2pre. STAGE the dispatched ADSR onto the voice pool BEFORE the control tick fires any
    //       note-on (task 161). The envelope latches its active-stage one-pole coefficient at
    //       the trigger EDGE (startAttack copies the attack coefficient into the live coeff),
    //       so a note triggered by the tick below must already have its A/D/S/R times in hand
    //       — otherwise the Attack runs at the prepared default and the attack-time knob has
    //       no effect. The A/D/S/R are NOTE-INDEPENDENT (one shared ADSR per voice), so this
    //       stages the same decoded EnvParams to every pool voice; the per-voice pitch/cutoff
    //       (note-dependent) still apply AFTER the tick in applyParamSnapshot. Bounded
    //       O(kMaxVoices), pure POD copy; noexcept, alloc-free [ADR-028; docs/design/03 §2.5].
    if (ctx.params != nullptr)
        stageEnvParams(*ctx.params);

    // 2. Advance the control core over this chunk; it fires the control tick(s) at the
    //    (PI) sub-block cadence. Each fired tick calls VoiceManager::controlTick(), which
    //    resolves the VoiceManager's OWN KeyAssigner (the single authority) and applies
    //    the decision to the active voice(s) — the SOLE call into the VoiceManager from
    //    the control side (§4.2; ControlCore owns no VM internals, ControlCore.h §7.8).
    //    There is exactly ONE KeyAssigner in the path, so the S7 selector set via
    //    Engine::setGateTrigMode (incl. LFO clock-reset) audibly takes effect.
    control_.advance(len, voices_);

    // 2b. THE CONTROL-DISPATCH SEAM (ADR-028; task 160, the keystone). The control tick(s)
    //     above resolved the sole KeyAssigner and drove each active voice's note lifecycle;
    //     NOW read BlockContext::params and apply the per-voice control values to the DSP
    //     setters (VCO pitch from the ADR-005 count-domain authority, footage, PWM, sub
    //     shape, the source-mixer levels, and the glide mode/time). Before this task the
    //     snapshot was NEVER read, so the synth ignored every knob and played one fixed
    //     pitch through a fixed saw. The snapshot pointer is borrowed + immutable (§5.4);
    //     an init patch with no bridge supplies nullptr (each voice then keeps its INIT
    //     saw-only mix and its default pitch — unchanged from the pre-dispatch behavior).
    //     Applied at the chunk cadence (<= kRenderBlock, finer than the ~2 ms vintage tick)
    //     so the glide slew is smooth. RT-safe: POD read + arithmetic + setters, no alloc,
    //     no lock [ADR-028 items 1-4; ADR-001 §9].
    if (ctx.params != nullptr)
        applyParamSnapshot(*ctx.params, len);

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

    // 4b. THE FX-PARAM DISPATCH (task 163; ADR-028 item 5). Before running the FX, decode
    //     the snapshot's FX range (fx.bypass + drive/chorus/delay enables+params + out.mono)
    //     into FxParams and publish it via fx_.setParams() — ONCE per chunk, at this §4.1 FX
    //     site, a SEPARATE site from the per-voice applyParamSnapshot (the FX run once on the
    //     mono voice sum, not per voice). Before this task the FxParams were never fed, so
    //     the FX section was permanently bypassed/at-defaults regardless of the knobs/preset.
    //     hostBpm comes from the block transport so the Delay tempo-sync conversion tracks.
    //     When ctx.params is null (an init path with no bridge) the FX keep their prepared
    //     defaults (master bypass ON -> FX-off, bit-exact per task 141 — unchanged). RT-safe:
    //     a POD read + arithmetic + a lock-free double-buffer publish; no alloc, no lock.
    if (ctx.params != nullptr)
        fx_.setParams(decodeFxParams(*ctx.params, ctx.transport.bpm));

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
        // Publish the REAL live playhead slot read straight from the StepSequencer: the slot
        // its most-recent advanceOnEdge() actually played (task 118d). This REPLACES the 118c
        // reconstructed (playPos+1)%count mirror, which DIVERGED from the real playhead when a
        // clock-reset-on-keypress (LFO-trig / arp mode + clockResetOnKeypress) rewound the
        // sequencer to slot 0 inside SequencerEngine::processBlock — a rewind the engine-side
        // mirror never observed. currentSlot() is the authoritative playhead, so it tracks
        // that rewind exactly (closes the 111c/118c QA MEDIUM) [docs/design/05 §6.3]. Arp-only
        // edges leave the seq playhead alone (currentSlot stays at the last seq step).
        if (seqPlaying) {
            currentSeqStep_ = sequencer_.seq().currentSlot();
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

// ===========================================================================
// The control-dispatch seam (ADR-028; task 160). Off-thread slot resolution + the
// RT-safe per-control-tick decode/apply.
// ===========================================================================

namespace {

// Denormalize a CONTINUOUS registry slot's snapshot value to its engineering value. The
// bridge stores convertTo0to1(value); every dispatch-consumed continuous param here is
// LINEAR-skew (vco.tune/fine/pw, the four mixer levels, glide.time — see ParamDefs.h), so
// the inverse is exactly min + norm*(max-min). (Non-linear-skew params are owned by 161/
// 162 and will denormalize against their own skew there.) Returns the registry default if
// the slot is unresolved. noexcept, pure arithmetic.
[[nodiscard]] float contValue(const mw::ParamSnapshot& s, int slot) noexcept {
    if (slot < 0) return 0.0f;
    const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(slot)];
    const float span = d.maxValue - d.minValue;
    return d.minValue + s.normalized(slot) * span;
}

// Denormalize a CONTINUOUS slot that carries a NON-LINEAR (log-ish) skew to its
// engineering value. The bridge stores the JUCE NormalisableRange's
// convertTo0to1(value) == ((value-min)/span)^skew (non-symmetric skew; ParameterLayout
// builds the range straight from kParamDefs.skew). The exact inverse the dispatch needs
// is therefore value = min + span * norm^(1/skew). For skew == 1 (linear) this collapses
// to contValue's min + norm*span, so the seam's task-161 cutoff (kCutoff) and env A/D/R
// (kEnvTime) slots denormalize against their OWN skew here — closing the "non-linear-skew
// params are owned by 161/162" note left on contValue. std::pow runs at control rate only
// (decode, off the per-sample path); a slot with skew==1 returns the linear value without
// the pow. Returns 0 for an unresolved slot. noexcept, pure arithmetic.
[[nodiscard]] float contValueSkewed(const mw::ParamSnapshot& s, int slot) noexcept {
    if (slot < 0) return 0.0f;
    const auto& d = mw::params::kParamDefs[static_cast<std::size_t>(slot)];
    const float span = d.maxValue - d.minValue;
    const float norm = s.normalized(slot);
    const float skew = d.skew;
    const float deskewed = (skew == 1.0f) ? norm
                                          : std::pow(norm, 1.0f / skew);
    return d.minValue + deskewed * span;
}

// The typed option index for a CHOICE/BOOL slot (the bridge stored it in indexValues).
[[nodiscard]] int choiceIndex(const mw::ParamSnapshot& s, int slot) noexcept {
    return slot < 0 ? 0 : s.index(slot);
}

// mw101.glide.mode choice {Off=0, Auto=1, On=2} (ParamDefs detail::kGlideMode) -> GlideMode.
[[nodiscard]] mw::GlideMode glideModeFor(int idx) noexcept {
    switch (idx) {
        case 1:  return mw::GlideMode::Auto;
        case 2:  return mw::GlideMode::On;
        default: return mw::GlideMode::Off;
    }
}

// mw101.sub.mode choice {-1 Oct Sq=0, -2 Oct Sq=1, -2 Oct Pulse=2} -> SubShape (ordered to
// match, SubOscillator.h §5.2). Defensive default = OctDownSquare.
[[nodiscard]] mw101::dsp::SubShape subShapeFor(int idx) noexcept {
    switch (idx) {
        case 1:  return mw101::dsp::SubShape::TwoOctDownSquare;
        case 2:  return mw101::dsp::SubShape::TwoOctDown25Pulse;
        default: return mw101::dsp::SubShape::OctDownSquare;
    }
}

// mw101.vca.mode choice {ENV=0, GATE=1} (ParamDefs detail::kVcaMode) -> VcaMode. The VCA
// gate fade follows the ADSR contour (ENV) or holds a flat full level (GATE). Default ENV.
[[nodiscard]] mw101::dsp::VcaMode vcaModeFor(int idx) noexcept {
    return idx == 1 ? mw101::dsp::VcaMode::Gate : mw101::dsp::VcaMode::Env;
}

// mw101.lfo.shape choice {Tri=0, Sq=1, Random=2, Noise=3, Sine=4} -> dsp::LfoShape (task
// 162). The DSP LfoShape has only the FOUR hardware positions; the 5th "Sine" choice (a
// software-reissue artifact) maps to SmoothTri (the rounded-toward-sine triangle is the
// closest hardware-true smooth shape — there is no separate sine core, docs/design/03 §3.2/
// §3.3). Defensive default SmoothTri [ControlDispatchLfoConstants.h shape-map note].
[[nodiscard]] mw101::dsp::LfoShape lfoShapeFor(int idx) noexcept {
    switch (idx) {
        case 1:  return mw101::dsp::LfoShape::Square;
        case 2:  return mw101::dsp::LfoShape::Random;
        case 3:  return mw101::dsp::LfoShape::Noise;
        case 4:  return mw101::dsp::LfoShape::SmoothTri;   // "Sine" -> rounded-tri (no sine core)
        default: return mw101::dsp::LfoShape::SmoothTri;   // Tri
    }
}

} // namespace

void Engine::cacheParamSlots() noexcept {
    // Resolve each consumed ID to its kParamDefs index ONCE (off the audio thread). A tiny
    // local linear scan over the 91-row registry; never run on the hot path. The string-ID
    // constants are the canonical ones from ParamIDs.h (no hand-typed strings) [ADR-008 C1].
    auto find = [](const char* id) noexcept -> int {
        for (int i = 0; i < static_cast<int>(mw::params::kParamDefs.size()); ++i) {
            const char* a = mw::params::kParamDefs[static_cast<std::size_t>(i)].id;
            const char* b = id;
            int k = 0;
            while (a[k] != '\0' && b[k] != '\0' && a[k] == b[k]) ++k;
            if (a[k] == '\0' && b[k] == '\0') return i;
        }
        return -1;
    };
    using namespace mw::params::ids;
    slots_.vcoTune    = find(kVcoTune);
    slots_.vcoFine    = find(kVcoFine);
    slots_.vcoPw      = find(kVcoPw);
    slots_.vcoPwmDepth = find(kVcoPwmDepth);   // manual PWM depth (task 162e)
    slots_.vcoRange   = find(kVcoRange);
    slots_.subMode    = find(kSubMode);
    slots_.sawLevel   = find(kSawLevel);
    slots_.pulseLevel = find(kPulseLevel);
    slots_.subLevel   = find(kSubLevel);
    slots_.noiseLevel = find(kNoiseLevel);
    slots_.glideTime  = find(kGlideTime);
    slots_.glideMode  = find(kGlideMode);

    // --- VCF / Env / VCA (task 161) ---
    slots_.vcfCutoff    = find(kVcfCutoff);
    slots_.vcfResonance = find(kVcfResonance);
    slots_.vcfEnvMod    = find(kVcfEnvMod);
    slots_.vcfLfoMod    = find(kVcfLfoMod);   // VCF-panel LFO->cutoff amount (task 162e)
    slots_.vcfKbdTrack  = find(kVcfKbdTrack);
    slots_.envAttack    = find(kEnvAttack);
    slots_.envDecay     = find(kEnvDecay);
    slots_.envSustain   = find(kEnvSustain);
    slots_.envRelease   = find(kEnvRelease);
    slots_.vcaLevel     = find(kVcaLevel);
    slots_.vcaMode      = find(kVcaMode);

    // --- LFO + modulation routing (task 162) ---
    slots_.lfoRate        = find(kLfoRate);
    slots_.lfoShape       = find(kLfoShape);
    slots_.lfoDest        = find(kLfoDest);
    slots_.lfoDelay       = find(kLfoDelay);
    slots_.lfoDepthPitch  = find(kLfoDepthPitch);
    slots_.lfoDepthPwm    = find(kLfoDepthPwm);
    slots_.lfoDepthCutoff = find(kLfoDepthCutoff);
    slots_.modLfoModWheel = find(kModLfoModWheel);
    slots_.modBendDest    = find(kModBendDest);
    slots_.modBendRangeVco = find(kModBendRangeVco);
    slots_.modBendRangeVcf = find(kModBendRangeVcf);
    slots_.velEnable      = find(kVelEnable);
    slots_.velDepth       = find(kVelDepth);

    // --- FX param dispatch (task 163; ADR-028 item 5) ---
    slots_.fxBypass        = find(kFxBypass);
    slots_.fxDriveEnable   = find(kFxDriveEnable);
    slots_.fxDriveAmount   = find(kFxDriveAmount);
    slots_.fxDriveTone     = find(kFxDriveTone);
    slots_.fxDriveOutput   = find(kFxDriveOutput);
    slots_.fxChorusEnable  = find(kFxChorusEnable);
    slots_.fxChorusMode    = find(kFxChorusMode);
    slots_.fxChorusRate    = find(kFxChorusRate);
    slots_.fxChorusDepth   = find(kFxChorusDepth);
    slots_.fxChorusWidth   = find(kFxChorusWidth);
    slots_.fxChorusMix     = find(kFxChorusMix);
    slots_.fxDelayEnable   = find(kFxDelayEnable);
    slots_.fxDelaySync     = find(kFxDelaySync);
    slots_.fxDelayDivision = find(kFxDelayDivision);
    slots_.fxDelayTime     = find(kFxDelayTime);
    slots_.fxDelayFeedback = find(kFxDelayFeedback);
    slots_.fxDelayDamp     = find(kFxDelayDamp);
    slots_.fxDelayWidth    = find(kFxDelayWidth);
    slots_.fxDelayMix      = find(kFxDelayMix);
    slots_.fxDelayPingpong = find(kFxDelayPingpong);
    slots_.outMono         = find(kOutMono);

    // --- analog character / tuning / expression / MPE (task 164) ---
    slots_.vintageEnable    = find(kVintageEnable);
    slots_.vintageAge       = find(kVintageAge);
    slots_.vintageCalSpread = find(kVintageCalSpread);
    slots_.vintageDetuneAmt = find(kVintageDetuneAmt);
    slots_.driftDepth       = find(kDriftDepth);
    slots_.driftRate        = find(kDriftRate);
    slots_.tuneA4           = find(kTuneA4);
    slots_.tuneSlop         = find(kTuneSlop);
    slots_.warmupTime       = find(kWarmupTime);
    slots_.varCutoff        = find(kVarCutoff);
    slots_.varEnvTime       = find(kVarEnvTime);
    slots_.varPw            = find(kVarPw);
    slots_.varGlide         = find(kVarGlide);
    slots_.ampExpression    = find(kAmpExpression);
    slots_.mpeEnable        = find(kMpeEnable);
    slots_.mpeBendRange     = find(kMpeBendRange);
    slots_.mpePressureDest  = find(kMpePressureDest);
}

VoiceControls Engine::decodeShared(const ParamSnapshot& snap) const noexcept {
    // Decode the parts of VoiceControls that are identical for every active voice (the VCO
    // shape controls, the source-mixer levels, the glide config). Per-voice PITCH depends on
    // each voice's resolved note and is filled by the caller. Pure arithmetic; noexcept.
    VoiceControls vc{};

    // VCO range choice -> footage (+ software-ext 32'/64' octave offset applied to pitch).
    const cal::dispatch::RangeMapping rng =
        cal::dispatch::rangeMappingFor(choiceIndex(snap, slots_.vcoRange));
    vc.footage   = rng.footage;
    vc.pwmCvNorm = contValue(snap, slots_.vcoPw);          // 0..1 (0 => square)
    // mw101.vco.pwm_depth (task 162e): the MANUAL static PWM depth (linear 0..1), DISTINCT from
    // the LFO->PWM amount (lfoPwmDepthNorm below). Scaled by the (PI) full-depth span so it biases
    // the duty LFO-independently; summed into the pwmCvNorm CV (clamped) in applyControls per the
    // CEM3340 manual-PW model [docs/design/01 §4.6; docs/design/05 §3.1; ADR-028].
    vc.manualPwmDepthNorm = contValue(snap, slots_.vcoPwmDepth)
                          * cal::dispatch::kManualPwmDepthNorm;
    vc.subShape  = subShapeFor(choiceIndex(snap, slots_.subMode));

    // Source mixer (§4.1).
    vc.sawLevel   = contValue(snap, slots_.sawLevel);
    vc.pulseLevel = contValue(snap, slots_.pulseLevel);
    vc.subLevel   = contValue(snap, slots_.subLevel);
    vc.noiseLevel = contValue(snap, slots_.noiseLevel);

    // Glide config (§5.5) — the single per-voice Glide owns the count-domain pitch slew.
    vc.glideMode    = glideModeFor(choiceIndex(snap, slots_.glideMode));
    vc.glideSeconds = contValue(snap, slots_.glideTime);

    // --- VCF (task 161): cutoff base CV + resonance + the env-mod depth (note-INDEPENDENT;
    // the kbd-track CV is per-voice and filled by applyParamSnapshot). The cutoff pot is a
    // log-ish-skewed 0..1 (kCutoff), so denormalize against its OWN skew, then map LINEARLY
    // in the CV (octave) domain across the musical span [ControlDispatchVcfConstants.h]. ---
    const float cutoff01 = contValueSkewed(snap, slots_.vcfCutoff);   // skew-aware 0..1
    vc.cutoffBaseCvVolts = cal::dispatch::kCutoffCvAtZero
                         + cutoff01 * (cal::dispatch::kCutoffCvAtOne
                                       - cal::dispatch::kCutoffCvAtZero);
    vc.resonance01   = contValue(snap, slots_.vcfResonance);                 // linear 0..1
    vc.envModOctaves = contValue(snap, slots_.vcfEnvMod)                     // env_mod (linear)
                     * cal::dispatch::kEnvModOctaves;                        // -> octaves of CV
    // mw101.vcf.lfo_mod (task 162e): the VCF module's OWN LFO->cutoff amount (linear 0..1),
    // DISTINCT from the LFO panel's lfo.depth_cutoff (already wired as lfoCutoffDepthOct). Both are
    // always-active per-destination depths now (ADR-007 / task 180; lfo.dest only emphasizes, never
    // gates) — this is a SEPARATE VCF-panel depth control. Scaled to octaves of CV; applyControls
    // sums lfoEff * this ALONGSIDE the lfo.depth_cutoff term into the cutoff CV [docs/design/02 §1.2;
    // docs/design/05 §3.1].
    vc.vcfLfoModDepthOct = contValue(snap, slots_.vcfLfoMod)
                         * cal::dispatch::kVcfLfoModDepthOctaves;

    // --- Envelope (task 161): A/D/R are kEnvTime-skewed seconds (denormalize against their
    // own skew); sustain is a linear level. setParams runs per control tick in the voice. ---
    vc.envAttackSec  = contValueSkewed(snap, slots_.envAttack);
    vc.envDecaySec   = contValueSkewed(snap, slots_.envDecay);
    vc.envSustain    = contValue(snap, slots_.envSustain);
    vc.envReleaseSec = contValueSkewed(snap, slots_.envRelease);

    // --- VCA (task 161): level (linear) + the ENV/GATE source select. ---
    vc.vcaLevel = contValue(snap, slots_.vcaLevel);
    vc.vcaMode  = vcaModeFor(choiceIndex(snap, slots_.vcaMode));

    // --- LFO (task 162 leg, mod-wheel boost ACTIVATED by 162c): rate (skewed Hz), shape, dest,
    // delay (skewed seconds), and the per-dest depths (linear 0..1) scaled to their CV units. The
    // mod-wheel->LFO routing param (mod.lfo_mod_wheel) scales the LFO depths by the LIVE wheel:
    //   effectiveBoost = kModWheelBoostBase + modWheelRouting x liveWheel x kModWheelBoostSpan
    // With the wheel DOWN (modWheel_ == 0) the boost is exactly the base (== 1, identity) for ANY
    // routing — a routed patch with the wheel down is identical to no routing (the wheel, not the
    // routing knob alone, opens the modulation). As the wheel rises the boost deepens the vibrato/
    // wobble/PWM-sweep up to (base + routing x span). This closes the 162 ingress gap: the live
    // wheel position (modWheel_) is consumed from CC1 MidiEvents / BlockContext::controllers by
    // task 162c [ControlDispatchCcIngressConstants.h; ADR-028 item 3]. ---
    vc.lfoRateHz  = contValueSkewed(snap, slots_.lfoRate);
    vc.lfoShape   = lfoShapeFor(choiceIndex(snap, slots_.lfoShape));
    vc.lfoDest    = static_cast<int>(cal::dispatch::lfoDestFor(choiceIndex(snap, slots_.lfoDest)));
    vc.lfoDelaySec = contValueSkewed(snap, slots_.lfoDelay) * cal::dispatch::kLfoDelayMaxSec;

    const float modWheelRouting = contValue(snap, slots_.modLfoModWheel);   // 0..1 routing depth
    const float depthBoost = cal::ccingress::kModWheelBoostBase
                           + modWheelRouting * modWheel_
                             * cal::ccingress::kModWheelBoostSpan;           // live wheel boost
    vc.lfoPitchDepthVolts = contValue(snap, slots_.lfoDepthPitch) * depthBoost
                          * cal::dispatch::kLfoPitchDepthVolts;
    vc.lfoCutoffDepthOct  = contValue(snap, slots_.lfoDepthCutoff) * depthBoost
                          * cal::dispatch::kLfoCutoffDepthOctaves;
    vc.lfoPwmDepthNorm    = contValue(snap, slots_.lfoDepthPwm) * depthBoost
                          * cal::dispatch::kLfoPwmDepthNorm;

    return vc;
}

mw::dsp::drift::DriftParams Engine::decodeDriftParams(const ParamSnapshot& snap) const noexcept {
    // Decode the analog-character group into the DriftModel's DriftParams (task 164). The whole
    // group is GATED by mw101.vintage.enable: when OFF, return the default DriftParams (all
    // bands zero / scales at identity) so the DriftModel contributes nothing and the default
    // render is bit-identical to pre-164 [docs/design/08 §4.2/§5.1 — spread=0 => no offset].
    mw::dsp::drift::DriftParams p{};   // identity baseline (depth/slop/var = 0, rate min)

    const bool vintageOn = choiceIndex(snap, slots_.vintageEnable) != 0;
    if (!vintageOn)
        return p;

    // The manual analog-character knobs (engineering units). drift.rate is kDriftRate-skewed in
    // the registry, so deskew it against its own skew; the others are linear.
    const float manualDepthCents = contValue(snap, slots_.driftDepth);        // 0..50 cents
    const float manualRate01     = contValueSkewed(snap, slots_.driftRate);   // 0.01..1 Hz, here 0..1-ish
    const float slopCents        = contValue(snap, slots_.tuneSlop);          // 0..20 cents
    const float calSpread01      = contValue(snap, slots_.vintageCalSpread);  // 0..1
    const float detuneAmt01      = contValue(snap, slots_.vintageDetuneAmt);  // 0..1
    const float varCutoff01      = contValue(snap, slots_.varCutoff);         // 0..1
    const float varEnv01         = contValue(snap, slots_.varEnvTime);        // 0..1
    const float varPw01          = contValue(snap, slots_.varPw);             // 0..1
    const float varGlide01       = contValue(snap, slots_.varGlide);          // 0..1
    const float warmupMin        = contValue(snap, slots_.warmupTime);        // 0..30 min

    // mw101.vintage.age — the host Age MACRO. The real shell maps it off-thread onto the
    // drift/variance group targets (VintageMacro, docs/design/08 §3.2/§10.1); replicate that
    // mapping at the seam so age alone opens the model. The resolved value the DriftModel sees
    // is the MAX of the manual knob and the age-derived target — a manual knob and a high Age
    // both open the model, neither cancels the other (the macro is additive in spirit).
    const float age01 = contValue(snap, slots_.vintageAge);   // 0..1
    const mw::dsp::drift::VintageTargets ageT =
        mw::dsp::drift::VintageMacro::computeTargets(age01);

    // drift.rate target is in Hz; DriftParams.driftRate01 is the 0..1 control the OU rate map
    // consumes (ThermalState log-maps it), so normalize the age target Hz across the schema
    // 0.01..1 Hz span before folding. The manual deskewed value is already the 0..1 control.
    const float ageRate01 = (ageT.driftRateHz - mw::cal::drift::kAgeDriftRateMinHz)
                          / std::max(1.0e-6f, (mw::cal::drift::kAgeDriftRateMaxHz
                                               - mw::cal::drift::kAgeDriftRateMinHz));

    p.driftDepthCents = std::max(manualDepthCents, ageT.driftDepthCents);
    p.driftRate01     = std::clamp(std::max(manualRate01, ageRate01), 0.0f, 1.0f);
    p.slopCents       = std::max(slopCents, ageT.tuneSlopCents);
    p.calSpread01     = calSpread01;          // cal spread is its own knob (not an Age target)
    p.detuneAmt01     = detuneAmt01;
    p.varCutoff01     = std::max(varCutoff01, ageT.varCutoff);
    p.varEnv01        = std::max(varEnv01,    ageT.varEnvTime);
    p.varPw01         = std::max(varPw01,     ageT.varPw);
    p.varGlide01      = std::max(varGlide01,  ageT.varGlide);
    p.warmupTimeMin   = warmupMin;
    p.useWarmup       = warmupMin > 0.0f;     // warm-up engaged only when a time is dialed in
    p.usePink         = false;                // 1/f component stays off by default (§5.1)
    return p;
}

fx::FxParams Engine::decodeFxParams(const ParamSnapshot& snap, double hostBpm) const noexcept {
    // Decode the FX range of the snapshot into a single flat FxParams (task 163; ADR-028
    // item 5). This is the SEPARATE FX site: the FX run once on the mono voice sum, so the
    // FxParams are NOT per-voice — one decode per block, published via fx_.setParams().
    //
    // Field conventions (see FxDispatchConstants.h + docs/design/07 §7):
    //  - bypass / enables / sync / pingpong / out.mono : bool option index (0/1).
    //  - drive amount/tone/output, chorus depth/width/mix, delay damp/width/mix : the
    //    registry stores these as LINEAR 0..1 knobs the stages interpret internally, so
    //    contValue passes the engineering value (== the 0..1 knob) straight through.
    //  - delay.feedback : registry range is already 0..kDelayFeedbackMax (engineering), so
    //    contValue yields the clamped feedback the stage wants directly.
    //  - delay.timeMs : free-delay MILLISECONDS — deskew delay_time to a linear 0..1, then
    //    log-map across the 1..2000 ms musical span.
    //  - chorus.rate : LFO Hz OVERRIDE — log-map the linear 0..1 across 0.05..10 Hz (0 keeps
    //    the mode preset, per the Chorus stage's p.rate>0 test).
    //  - chorus.mode / delay.division : the typed choice option index.
    //  - hostBpm : from the block transport so the Delay tempo-sync conversion tracks.
    fx::FxParams p{};

    p.masterBypass = (choiceIndex(snap, slots_.fxBypass) != 0);
    p.monoOutput   = (choiceIndex(snap, slots_.outMono) != 0);
    p.hostBpm      = (hostBpm > 0.0) ? hostBpm : 120.0;

    // --- Drive ---
    p.drive.on     = (choiceIndex(snap, slots_.fxDriveEnable) != 0);
    p.drive.amount = contValue(snap, slots_.fxDriveAmount);
    p.drive.tone   = contValue(snap, slots_.fxDriveTone);
    p.drive.output = contValue(snap, slots_.fxDriveOutput);

    // --- Chorus. The chain treats mode==Off (0) as the per-block bypass; the chorus_enable
    // bool gates it so a patch can disable the chorus without losing its selected mode
    // (enable off -> mode Off regardless of the mode choice). ---
    const bool chorusOn = (choiceIndex(snap, slots_.fxChorusEnable) != 0);
    const int  chorusModeChoice = choiceIndex(snap, slots_.fxChorusMode);
    p.chorus.mode  = chorusOn ? chorusModeChoice
                              : static_cast<int>(fx::Chorus::Mode::Off);
    p.chorus.rate  = cal::fxdispatch::chorusRateHz(contValue(snap, slots_.fxChorusRate));
    p.chorus.depth = contValue(snap, slots_.fxChorusDepth);
    p.chorus.width = contValue(snap, slots_.fxChorusWidth);
    p.chorus.mix   = contValue(snap, slots_.fxChorusMix);

    // --- Delay ---
    p.delay.on       = (choiceIndex(snap, slots_.fxDelayEnable) != 0);
    p.delay.sync     = (choiceIndex(snap, slots_.fxDelaySync) != 0);
    p.delay.pingpong = (choiceIndex(snap, slots_.fxDelayPingpong) != 0);
    p.delay.division = choiceIndex(snap, slots_.fxDelayDivision);
    // delay_time is kDelayTime-skewed in the registry; deskew to linear 0..1 then log-map
    // to free ms. contValueSkewed inverts the registry skew (returning the engineering
    // value across the registry's 0..1 range == a linear 0..1 here), which we re-map.
    {
        const float deskewedNorm = (slots_.fxDelayTime < 0)
            ? 0.0f
            : contValueSkewed(snap, slots_.fxDelayTime);   // linear 0..1 (registry range is 0..1)
        p.delay.timeMs = cal::fxdispatch::delayFreeMs(deskewedNorm);
    }
    p.delay.feedback = contValue(snap, slots_.fxDelayFeedback);   // already 0..kDelayFeedbackMax
    p.delay.damp     = contValue(snap, slots_.fxDelayDamp);
    p.delay.width    = contValue(snap, slots_.fxDelayWidth);
    p.delay.mix      = contValue(snap, slots_.fxDelayMix);

    return p;
}

void Engine::stageEnvParams(const ParamSnapshot& snap) noexcept {
    // Decode the note-independent ADSR (skew-aware times + linear sustain) and stage it onto
    // EVERY pool voice so a voice the control tick triggers THIS chunk latches the correct
    // attack coefficient at its edge (task 161). One shared ADSR per voice, so the same params
    // go to all; staging is idle-voice-safe (it only updates the env's coefficients + the
    // pending cache, never fires). The pool is the fixed kMaxVoices array (VoiceTypes.h).
    mw101::dsp::EnvParams ep{};
    ep.attackSec  = contValueSkewed(snap, slots_.envAttack);
    ep.decaySec   = contValueSkewed(snap, slots_.envDecay);
    ep.sustain    = contValue(snap, slots_.envSustain);
    ep.releaseSec = contValueSkewed(snap, slots_.envRelease);
    for (int vi = 0; vi < mw::kMaxVoices; ++vi)
        voices_.voiceMutable(vi).stageEnvParams(ep);
}

void Engine::applyParamSnapshot(const ParamSnapshot& snap, int chunkSamples) noexcept {
    // Shared (note-independent) controls decoded once.
    VoiceControls shared = decodeShared(snap);

    // --- audio->GUI telemetry latches (task 118d) ----------------------------------------
    // Latch the dispatched/modulated cutoff + advance the LFO display phase from the SAME
    // decoded values the per-voice dispatch uses, so the processor can publish a LIVE scope
    // cutoff indicator + a moving LFO phase (127's ScopeMeterOverlay) instead of the zero
    // fields it left before. Pure arithmetic on the single-threaded audio path; no alloc,
    // no lock [docs/design/10-ui.md §8.4; ADR-015 C5].
    //
    // cutoffDisplay_ : the mw101.vcf.cutoff pot position decoded skew-aware to 0..1 (the same
    //   cutoff01 decodeShared maps to the cutoff CV) — the modulated base cutoff the §1.2 filter
    //   tracks. Clamped to [0,1] (the display domain Snapshot.vcfCutoffDisplay declares).
    cutoffDisplay_ = std::clamp(contValueSkewed(snap, slots_.vcfCutoff), 0.0f, 1.0f);
    // lfoPhase_ : a deterministic fixed-point [0,2^32) phase accumulator advanced once per
    //   chunk by the dispatched LFO rate (lfoRateHz over chunkSamples at the engine sample
    //   rate). It ADVANCES whenever the LFO has a non-zero rate, giving the UI a continuously
    //   moving mod-source indicator; it wraps naturally on uint32 overflow (a free phase wrap).
    //   Display-only — the per-voice audio LFO is unchanged (it owns its own phase).
    if (sampleRate_ > 0.0 && chunkSamples > 0) {
        const double cyclesPerSample =
            static_cast<double>(shared.lfoRateHz) / sampleRate_;
        const double advanceTurns = cyclesPerSample * static_cast<double>(chunkSamples);
        // Fraction of a full uint32 turn this chunk advances (wrap-around is intentional).
        const double inc = advanceTurns * 4294967296.0;   // 2^32 counts per LFO cycle
        lfoPhase_ += static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(inc) & 0xFFFFFFFFull);
    }

    // The tune (coarse semitones) + fine (sub-semitone) sum into the pitch, in DAC-count
    // units (1 count == 1 semitone). vco.range's software-ext positions (32'/64') add whole
    // octaves on top of the footage offset (12 counts/octave).
    const float tuneSemis  = contValue(snap, slots_.vcoTune);   // -24..+24
    const float fineSemis  = contValue(snap, slots_.vcoFine);   // -1..+1
    const double extraOct   =
        cal::dispatch::rangeMappingFor(choiceIndex(snap, slots_.vcoRange)).extraOctaves;
    const float pitchShiftCounts = tuneSemis + fineSemis;
    const double extraOctCounts  =
        extraOct * static_cast<double>(cal::pitch::kCountsPerOctave);

    // Anchor: the reference key (A4) at the reference footage maps to the VCO converter's
    // reference CV (kPitchRefVolts), so the count-domain CV lands in the VCO's CV frame and
    // 1 V/octave is exact between notes [ControlDispatchConstants.h; ADR-005; ADR-028].
    const float anchorVolts =
        ControlCore::countsToVolts(cal::dispatch::kReferenceMidiNote);

    // The VCF keyboard-track depth (task 161): kbd_track in [0,1] (linear). At 1 the cutoff
    // tracks the keyboard at exactly 1 V/oct; the per-voice CV is depth x (note - refNote)
    // counts x kVoltsPerCount, centered on the SAME A4 anchor the pitch dispatch uses, so a
    // note below the reference lowers the cutoff and above it raises it (§1.2 keyboard track).
    const float kbdTrackDepth = contValue(snap, slots_.vcfKbdTrack);

    // --- velocity routing (task 162): mw101.vel.{enable,depth}. When sensing is ON the
    // per-note velocity scales the VCA (velVcaDepth folds it into a post-VCA gain) and opens
    // the filter (velCutoffVolts = depth x velocity x kVelCutoffOctaves volts). Both ZERO when
    // vel.enable is off so velocity has no effect with sensing disabled. The velocity itself is
    // per-voice (recorded at noteOn), so velCutoffVolts is finished in the per-voice loop.
    const bool  velOn    = choiceIndex(snap, slots_.velEnable) != 0;   // bool index 0/1
    const float velDepth = velOn ? contValue(snap, slots_.velDepth) : 0.0f;

    // --- pitch-bend routing (task 162 leg, ACTIVATED by 162c; bend authority fixed in 162d):
    // mw101.mod.{bend_dest,bend_range_*}. The bend RANGE (cents) per destination is decoded here;
    // the live bend WHEEL position is the engine's running pitchBend_ — the [-1,+1] centered unit
    // seeded by process() from BlockContext::controllers.pitchBend (NOT re-read from the forwarded
    // PitchBend MidiEvent, whose value is the §4.4 SEMITONE offset, not a unit — see the ingress
    // note at the top of renderChunk; task 162d). The resolved bend offset is
    // wheel x (rangeCents/100 semitones) x kVoltsPerCount, summed into the pitch CV (dest VCO)
    // and/or the cutoff CV (dest VCF); Both routes to both. A centered wheel (pitchBend_ == 0)
    // yields a zero offset — the neutral identity. A HALF wheel (+0.5) at a 1200-cent range is
    // +half an octave (x sqrt2 ~ 1.414); a full wheel (+-1) at 1200 cents is +-1 octave at
    // exactly 1 V/oct [ControlDispatchConstants.h; ADR-005; ADR-028 item 3].
    const int   bendDest      = choiceIndex(snap, slots_.modBendDest);   // 0 VCO / 1 VCF / 2 Both
    const float bendRangeVcoCents = contValue(snap, slots_.modBendRangeVco);
    const float bendRangeVcfCents = contValue(snap, slots_.modBendRangeVcf);
    const float bendWheel = pitchBend_;   // live centered [-1,+1] bend position (162c seed; 162d authority)
    const float bendVcoVolts = (bendDest == 0 || bendDest == 2)
        ? bendWheel * (bendRangeVcoCents / 100.0f)
          * static_cast<float>(cal::pitch::kVoltsPerCount)
        : 0.0f;
    const float bendVcfVolts = (bendDest == 1 || bendDest == 2)
        ? bendWheel * (bendRangeVcfCents / 100.0f)
          * static_cast<float>(cal::pitch::kVoltsPerCount)
        : 0.0f;

    // --- analog character: tuning reference + expression + MPE decode (task 164) ----------
    // tune.a4: the A4 tuning reference (400..460 Hz, default 440). The CEM3340 VCO homes on
    // kHardwareRefHz (442) at the 8' reference, so to land the rendered A4 on the configured
    // a4Hz the dispatch adds log2(a4Hz / 442) OCTAVES (== volts at 1 V/oct) as a GLOBAL pitch
    // bias on every voice. a4 == 442 => 0 (the hardware home, an exact identity); 440 => a
    // small flat shift; 460 => sharp. A global bias — it never re-anchors note spacing.
    const float a4Hz = contValue(snap, slots_.tuneA4);
    const float pitchRefVolts =
        (a4Hz > 0.0f)
            ? static_cast<float>(std::log2(static_cast<double>(a4Hz)
                                           / cal::character::kHardwareRefHz)
                                 * cal::pitch::kVoltsPerOctave)
            : 0.0f;

    // amp.expression (CC11): a clean linear VCA output scaler. The PARAM reaches the seam
    // (schema default 1.0 == unity), so this is directly audible — 1 unity, 0 silent. Clamped
    // to the [min,max] floor/ceiling so a stray normalized value never inverts the gain.
    const float ampExpression = std::clamp(contValue(snap, slots_.ampExpression),
                                           cal::character::kExpressionMin,
                                           cal::character::kExpressionMax);

    // MPE routing decode (mpe.{enable,bend_range,pressure_dest}). The routing is DECODED here,
    // but the live per-note MPE pitch-bend / pressure POSITION ingress is a SEPARATE controller
    // seam — BlockContext carries no per-note MPE state (the same gap the task-162 bend leg has).
    // So the resolved MPE bend offset (position x range) is ZERO today (no position source), and
    // the pressure destination is decoded but routes nothing. The decode + routing math are in
    // place so a future MPE-ingress seam only supplies the live per-note position/pressure.
    const bool  mpeEnable      = choiceIndex(snap, slots_.mpeEnable) != 0;
    const float mpeBendSemis   = contValue(snap, slots_.mpeBendRange);   // 0..96 semitones
    const int   mpePressureDest = choiceIndex(snap, slots_.mpePressureDest);
    constexpr float kMpePosition = 0.0f;   // no live per-note MPE position in the core seam
    const float mpeBendVolts = mpeEnable
        ? kMpePosition * mpeBendSemis * static_cast<float>(cal::pitch::kVoltsPerCount)
        : 0.0f;

    // --- the analog-character DRIFT engine (task 164; ADR-028 / docs/design/08) ------------
    // Publish the decoded DriftParams (gated by vintage.enable — identity when off), fire a
    // per-voice note-on DRAW for any voice that was freshly triggered (detected by its monotonic
    // noteSerial changing), then ADVANCE the drift engine ONCE for this chunk so its per-voice
    // smoothed outputs (pitch/cutoff drift cents, var.pw, the env/glide time scales) are current.
    // The DriftModel is the existing analog-character orchestration engine — the dispatch WIRES
    // it (it was un-consumed before this task), it does not re-implement the drift DSP.
    const mw::dsp::drift::DriftParams driftParams = decodeDriftParams(snap);
    const bool vintageOn = choiceIndex(snap, slots_.vintageEnable) != 0;
    drift_.setParams(driftParams);

    const int activeCountForDrift = voices_.activeCount();
    if (vintageOn) {
        // Fire the note-on draw for freshly-triggered voices (the monotonic noteSerial the
        // VoiceManager stamps on allocation changed since we last saw this slot), so each note
        // freezes its Tier-3 slop + the four variance spreads ONCE. midiHz for the octave-slop
        // hook is the voice's resolved note at the a4 reference (the draw is octave-independent
        // in v1, so the exact Hz is advisory) [docs/design/08 §6/§7].
        for (int k = 0; k < activeCountForDrift; ++k) {
            const int vi = static_cast<int>(voices_.activeIndex(k));
            const Voice& v = voices_.voiceMutable(vi);   // const read of serial/note only here
            const std::uint64_t serial = v.noteSerial();
            if (serial != lastDriftSerial_[static_cast<std::size_t>(vi)]) {
                lastDriftSerial_[static_cast<std::size_t>(vi)] = serial;
                const int note = v.currentNote();
                const double noteHz = (note >= 0)
                    ? static_cast<double>(a4Hz) * std::pow(2.0, (note - cal::dispatch::kReferenceMidiNote) / 12.0)
                    : static_cast<double>(a4Hz);
                drift_.noteOn(vi, noteHz);
            }
        }
        // Advance the OU thermal integrator + the de-zipper smoothers ONCE for this chunk
        // (control-rate, the §12.2 once-per-block contract; the per-voice accessors below then
        // read the settled-toward value). Bounded O(activeCount); no alloc, no lock.
        drift_.processBlock(activeCountForDrift);
    }

    // Apply to every ACTIVE voice (MONO drives exactly one; UNISON/POLY share the same VCO/
    // VCF/Env/VCA controls + a per-voice pitch and keyboard-track CV). The non-const voice is
    // taken through VoiceManager::voiceMutable (task 161 cleanup of the 160 const_cast smell);
    // voices_ is a genuinely non-const Engine member, so this is the clean typed write surface
    // for the dispatch. The active scan is bounded O(activeCount) [§6.1; ADR-019 VT-02].
    const int activeCount = voices_.activeCount();
    for (int k = 0; k < activeCount; ++k) {
        const int vi = static_cast<int>(voices_.activeIndex(k));
        Voice& v = voices_.voiceMutable(vi);

        const int note = v.currentNote();
        VoiceControls vc = shared;
        if (note >= 0) {
            // Count-domain pitch CV via the ControlCore authority (ADR-005). MODERN pole
            // (default) keeps continuous float so fine tune resolves; VINTAGE quantizes to
            // 6-bit counts at the S/H boundary. Anchored into the VCO converter's CV frame.
            const float counts = static_cast<float>(note)
                               + pitchShiftCounts
                               + static_cast<float>(extraOctCounts);
            const float countVolts = control_.blendedPitchVolts(counts);
            vc.targetPitchCvVolts = countVolts - anchorVolts
                                  + static_cast<float>(mw::cal::vco::kPitchRefVolts);

            // Per-voice keyboard-track CV summed into the filter cutoff (task 161): the note
            // delta from the A4 anchor in counts, scaled by 1 V/oct (kVoltsPerCount) and the
            // tracking depth. Note 60 above/below the A4 ref simply yields a non-zero CV that
            // raises/lowers cutoff with pitch; depth 0 leaves it zero (fixed cutoff).
            const float noteDeltaCounts =
                static_cast<float>(note - cal::dispatch::kReferenceMidiNote);
            vc.kbdTrackCvVolts = kbdTrackDepth * noteDeltaCounts
                               * static_cast<float>(cal::pitch::kVoltsPerCount);
        }

        // --- velocity (task 162): the per-voice velocity term. velVcaDepth folds velocity
        // into a post-VCA gain inside the voice; velCutoffVolts opens the filter for a hard
        // key (depth x velocity x kVelCutoffOctaves volts). Both are ZERO with sensing off
        // (velDepth==0). Read THIS voice's recorded velocity. ---
        const float vel = v.currentVelocity();
        vc.velVcaDepth    = velDepth * cal::dispatch::kVelVcaDepthMax;
        vc.velCutoffVolts = velDepth * vel * cal::dispatch::kVelCutoffOctaves;

        // --- pitch-bend (task 162): the resolved per-dest bend CV (zero with no live wheel). ---
        vc.bendVcoVolts = bendVcoVolts;
        vc.bendVcfVolts = bendVcfVolts;

        // --- analog character (task 164): the tune.a4 reference bias + expression + MPE decode
        // (note-independent, shared across voices), then the PER-VOICE drift/variance outputs
        // read from the DriftModel (identity when vintage is off). The DriftModel speaks CENTS
        // for pitch + cutoff and a duty FRACTION for PW; convert cents->volts for the CV frame.
        vc.pitchRefVolts = pitchRefVolts;
        vc.ampExpression = ampExpression;
        vc.mpeEnable     = mpeEnable;
        vc.mpeBendVolts  = mpeBendVolts;
        vc.mpePressureDest = mpePressureDest;
        if (vintageOn) {
            vc.pitchDriftVolts  = static_cast<float>(
                cal::character::centsToVolts(drift_.pitchOffsetCents(vi)));
            vc.cutoffDriftVolts = static_cast<float>(
                cal::character::centsToVolts(drift_.cutoffOffset(vi)));
            vc.pwDriftNorm      = drift_.pwOffset(vi);
            vc.envTimeScale     = drift_.envTimeScale(vi);
            vc.glideTimeScale   = drift_.glideTimeScale(vi);
        }
        // (else: VoiceControls' defaults are the identity — 0 offsets, unit scales.)

        v.applyControls(vc, chunkSamples);
    }
}

void Engine::process(const BlockContext& ctx) noexcept {
    // §9.1 RT-5: flush denormals for the whole call (set at entry, restored on exit).
    ScopedFlushDenormals flushGuard;

    const int numFrames = ctx.audio.numFrames;
    if (numFrames <= 0 || ctx.audio.channels == nullptr || ctx.audio.numChannels <= 0)
        return;

    // Seed the running continuous-controller state from the block's controller snapshot
    // (task 162c). A host that decodes bend/wheel into BlockContext::controllers once per block
    // sets the position here; a host that streams PitchBend/CC1 MidiEvents leaves the neutral
    // default and renderChunk's ingress loop fills the running state from the events. The two
    // paths compose: the snapshot is the block-entry value, per-chunk events then update it
    // sample-accurately. Clamped to the valid controller domain so a stray value never inverts
    // the bend sign or runs the LFO boost negative.
    pitchBend_ = std::clamp(ctx.controllers.pitchBend, cal::ccingress::kPitchBendMin,
                            cal::ccingress::kPitchBendMax);
    modWheel_  = std::clamp(ctx.controllers.modWheel, cal::ccingress::kModWheelMin,
                            cal::ccingress::kModWheelMax);

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
