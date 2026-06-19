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

#include "calibration/EngineConstants.h"
#include "calibration/OversampledZoneConstants.h"

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

    // Prepare the consumed modules. A fixed instance seed keeps per-voice drift PRNG
    // state byte-stable across runs (§9.2; ADR-001 Decision); the plugin overrides it
    // per instance off the audio thread (out of scope here).
    // VoiceManager prepares its OWN sole KeyAssigner (the single MONO/UNISON authority,
    // doc 04 §5.1/§9) — the engine owns no second KeyAssigner (task 118b reconcile).
    voices_.prepare(sampleRate_, oversampleFactor_, kInstanceSeed);
    control_.prepare(sampleRate_);
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
    voices_.prepare(sampleRate_, oversampleFactor_, kInstanceSeed);
    control_.reset();
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
    const MidiEventView& midi = ctx.midi;
    for (int i = 0; i < midi.numEvents; ++i) {
        const MidiEvent& e = midi.events[i];
        if (e.sampleOffset < n0 || e.sampleOffset >= n0 + len) continue;
        NoteEvent ne{};
        if (toNoteEvent(e, n0, ne)) {
            voices_.handleNoteEvent(ne);
        }
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
