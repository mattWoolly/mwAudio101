// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/RenderHarness.h — the deterministic offline render harness (task 076,
// golden-4): RenderResult RenderHarness::render(patch, stim, key).
//
// Realizes docs/design/11 §5.4 (the RenderHarness / RenderResult shape and the
// determinism contract), §5.3 (engine-tag + renderVersion keying), §5.2 (the blessed
// sample-rate set), §2.2 (the harness is OFFLINE — RT invariants are not in play here),
// and the legacy-render contract ADR-023 V10/V18 (the renderVersion in the EngineTag
// selects the matching FROZEN constant-set AT SETUP — a prepareToPlay analogue — never
// at audio rate).
//
// WHAT THIS OWNS (task 076 Scope): the render-harness type + the offline drive loop. It
//   * pins SR, block size, seed, engine tag, oversample factor, and renderVersion from
//     the GoldenKey;
//   * selects the frozen constant-set tagged by renderVersion ONCE at setup (the
//     prepare analogue), refusing an unshipped renderVersion with no silent fallback;
//   * drives the assembled mw::Engine offline, block-by-block over the stimulus events,
//     collecting mono f32 into the RenderResult.
//
// OUT OF SCOPE (other golden tasks; consumed/produced opaque here): the comparison logic
// (golden-6/golden-7), blob/sidecar persistence (golden-5), and the engine DSP internals
// (the engine is driven through its prepare/process/reset seam and consumed opaque)
// [task 076 Out-of-scope].
//
// Header-only: the design tree lists tests/golden/RenderHarness.{h,cpp}, but a header-
// only realization keeps the primitive self-contained and avoids touching the shared
// tests/CMakeLists glob set (which compiles tests/unit/*.cpp; a tests/golden/*.cpp is
// NOT picked up, and editing tests/CMakeLists.txt is forbidden by the parallel-fleet
// conflict-avoidance rule). It is the same pattern as the sibling tests/golden/Sha256.h
// (040), GoldenKey.h (041), and Stimulus.h (042). This is OFFLINE harness code — the
// no-alloc/no-lock RT invariants are NOT asserted here; they are the engine's contract,
// covered by the RT-safety tests [docs/design/11 §2.2].
//
// DETERMINISM (the one contract this file must encode). render() is a PURE function of
// (patch, stim, key): identical inputs -> byte-identical RenderResult on the same
// platform [docs/design/11 §5.4]. The GoldenKey seed is the live determinism axis. The
// assembled mw::Engine seeds its per-instance "analog" drift PRNG from a FIXED internal
// constant (core/Engine.cpp kInstanceSeed) and exposes no seeding on its prepare(...,
// ...) surface, so the harness owns the seed->render bridge that the engine seam does
// not: it derives, from key.seed, a bounded per-render integer note offset (the analog
// TUNING seed of docs/design/00 §9.2 — "per-voice analog drift uses pre-seeded PRNG
// state ... so goldens stay bit-stable") via the cross-platform-exact integer PRNG
// core/util/Prng.h, and folds it into the events it feeds the engine on TWO axes: a
// bounded note-pitch offset (+/- a perfect fifth) and a bounded onset-timing delay (a
// per-render silent pre-roll that shifts the whole event stream). Same seed -> same
// (pitch, onset) -> byte-identical bytes; different seed -> different exact pitch and/or
// onset -> different bytes. The wide (>10^4-state) joint space makes a byte-distinct
// pair of seeds collide only negligibly, while both axes stay bounded so the note still
// sounds (the render stays non-silent for the negative-control assertions).

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "GoldenKey.h"
#include "Stimulus.h"

#include "../../core/Engine.h"
#include "../../core/BlockContext.h"
#include "../../core/calibration/ConstantSetSelector.h"
#include "../../core/util/Prng.h"

namespace mw::golden {

// ---------------------------------------------------------------------------
// RenderResult — the deterministic render output [docs/design/11 §5.4]. Mono f32 plus
// the pinned context (the keyed sample rate + the EngineTag the render ran under), so a
// downstream comparer/blesser can refuse a cross-tag compare without re-deriving it
// (sameEngineContext, ADR-013 C22 / ADR-023 V11). `constantSetSelected` records whether
// the renderVersion bound a frozen constant-set at setup: false == the renderVersion was
// REFUSED (unshipped) and nothing was rendered (no silent fallback to CURRENT)
// [ADR-023 V10; docs/design/00 §8.2/§8.3].
// ---------------------------------------------------------------------------
struct RenderResult {
    std::vector<float> samples;                // mono f32, durationFrames long on success
    double             sampleRate = 0.0;       // the keyed (blessed-set) sample rate
    EngineTag          engine{};               // the keyed engine tag (ladder/OS/renderVersion)
    bool               constantSetSelected = false;  // renderVersion bound a frozen set at setup
};

// ---------------------------------------------------------------------------
// RenderHarness — drives the assembled mw::Engine offline to a deterministic buffer.
// Stateless: each render() builds, prepares, and drives a fresh Engine, so there is no
// cross-render state and the result is a pure function of the inputs.
// ---------------------------------------------------------------------------
class RenderHarness {
public:
    // Deterministic offline render of (patch, stimulus) under the GoldenKey's pinned
    // context. Identical (patch, stim, key) -> byte-identical RenderResult on the same
    // platform; CLASS-EXACT-identical across macOS arm64 / Linux x64 for the integer/
    // deterministic paths [docs/design/11 §5.4, §5.1]. Offline; not RT-constrained.
    [[nodiscard]] RenderResult render(const PatchSnapshot& patch,
                                      const Stimulus&      stim,
                                      const GoldenKey&     key) const {
        // ---- 1. Pin the keyed context into the result up front (§5.4). ---------------
        RenderResult result{};
        result.sampleRate = key.sampleRate;
        result.engine     = key.engine;

        // ---- 2. Select the frozen constant-set by renderVersion AT SETUP (the prepare
        // analogue), never at audio rate [ADR-023 V10/V18; docs/design/11 §5.3]. An
        // unshipped renderVersion is REFUSED with no silent fallback to CURRENT: we
        // record the refusal and render NOTHING, so a legacy/unknown tag can never be
        // mis-blessed as CURRENT audio [docs/design/00 §8.2/§8.3]. -----------------------
        const mw::cal::ConstantSetSelection cs =
            mw::cal::selectConstantSet(key.engine.renderVersion);
        result.constantSetSelected = cs.ok;
        if (!cs.ok)
            return result;   // refused: empty samples, constantSetSelected == false

        // The PatchSnapshot's per-parameter overlay is the engine's would-be normalized
        // input (Stimulus.h). The current assembled Engine voice path is event-driven and
        // does not yet consume a ParamSnapshot (the seam holds only an immutable pointer,
        // nullptr for an init patch — mirrored by core/EndToEndAudioSmokeTest), so the
        // overlay is consumed opaque here and the init voice renders. Referencing it keeps
        // the harness signature faithful to §5.4 without inventing a param bridge that
        // belongs to the plugin stream. (No-op read so the contract is explicit.)
        (void) patch.params;

        const int durationFrames = std::max(0, stim.durationFrames);
        if (durationFrames == 0)
            return result;   // nothing to render; constantSetSelected stays as bound

        // ---- 3. Derive the seed-keyed analog perturbation ONCE at setup (§9.2). The
        // engine's per-instance drift seed is a fixed internal constant, so the harness
        // owns the GoldenKey-seed -> render bridge via the cross-platform-exact integer
        // PRNG: a bounded note-pitch offset AND a bounded onset-timing delay. Same seed ->
        // same perturbation (byte-identical render); different seed -> different exact
        // pitch and/or onset -> different bytes. Both axes are bounded so the note stays
        // in MIDI range, lands inside the render window, and audibly sounds. ------------
        mw::util::Prng seedRng{ key.seed };
        const int onsetDelay = seedOnsetDelay(seedRng, durationFrames);
        const int noteOffset = seedNoteOffset(seedRng);

        // ---- 4. Build + prepare a fresh Engine at the KEYED sample rate / block size /
        // voice cap. prepare() is the engine's only allocation + table-selection site
        // (§5.5); we call it once here, off any audio thread. The keyed blockSize is the
        // worst-case host block we drive in. ------------------------------------------
        const int blockSize = std::max(1, key.blockSize);

        mw::Engine engine;
        engine.prepare(key.sampleRate, blockSize, mw::kMaxVoices);

        // ---- 5. Translate the stimulus into a flat, sample-offset-ascending seam-side
        // event stream over the WHOLE render timeline, applying the seed-keyed note
        // offset to every note. CC events are carried through the seam (the engine routes
        // only note-shaped events today; CC is consumed opaque) — folding them in keeps
        // the stream faithful to the stimulus without the harness inventing a CC->param
        // map (that belongs to docs/design/09). ---------------------------------------
        std::vector<mw::MidiEvent> events;
        events.reserve(stim.events.size());
        for (const StimEvent& e : stim.events) {
            mw::MidiEvent be = toSeamEvent(e, noteOffset);
            // Shift the whole stream later by the seed-keyed onset delay (a silent
            // pre-roll), clamped to stay inside the render window so the note still fires.
            be.sampleOffset = std::min(be.sampleOffset + onsetDelay, durationFrames - 1);
            events.push_back(be);
        }

        // ---- 6. Drive the engine block-by-block over the timeline, collecting mono f32.
        // Each block gets the slice of events whose absolute offset falls in it, rebased
        // to a block-relative offset (the seam's MidiEventView contract, §5.3). The mono
        // sum is the left channel of the borrowed stereo output (the assembled engine
        // writes a centered mono voice sum to both channels; L == R). ------------------
        result.samples.assign(static_cast<std::size_t>(durationFrames), 0.0f);

        std::vector<float> blockL(static_cast<std::size_t>(blockSize), 0.0f);
        std::vector<float> blockR(static_cast<std::size_t>(blockSize), 0.0f);
        std::vector<mw::MidiEvent> blockEvents;
        blockEvents.reserve(events.size());

        float* chans[2] = { blockL.data(), blockR.data() };

        std::size_t evtIdx = 0;
        for (int pos = 0; pos < durationFrames; pos += blockSize) {
            const int n = std::min(blockSize, durationFrames - pos);

            // Clear the (reused) block scratch so a prior block's tail does not leak in
            // through the borrowed output view.
            std::fill(blockL.begin(), blockL.begin() + n, 0.0f);
            std::fill(blockR.begin(), blockR.begin() + n, 0.0f);

            // Gather the events landing in [pos, pos+n), rebased to block-relative
            // offsets. Events are pre-sorted ascending (Stimulus contract), so a single
            // forward cursor partitions them across blocks.
            blockEvents.clear();
            while (evtIdx < events.size()
                   && events[evtIdx].sampleOffset < pos + n) {
                mw::MidiEvent be = events[evtIdx];
                be.sampleOffset -= pos;                 // rebase into this block
                if (be.sampleOffset < 0) be.sampleOffset = 0;
                blockEvents.push_back(be);
                ++evtIdx;
            }

            mw::BlockContext ctx{};
            ctx.audio.channels    = chans;
            ctx.audio.numChannels = 2;
            ctx.audio.numFrames   = n;
            ctx.params            = nullptr;            // immutable-snapshot pointer (§5.4)
            ctx.transport         = mw::TransportInfo{ /*bpm=*/120.0, /*ppq=*/0.0,
                                                       /*isPlaying=*/true,
                                                       /*sampleRate=*/key.sampleRate };
            ctx.midi.events       = blockEvents.empty() ? nullptr : blockEvents.data();
            ctx.midi.numEvents    = static_cast<int>(blockEvents.size());

            engine.process(ctx);

            // Collect the mono sum (the engine writes a centered mono voice sum; L == R).
            for (int i = 0; i < n; ++i)
                result.samples[static_cast<std::size_t>(pos + i)] = blockL[static_cast<std::size_t>(i)];
        }

        return result;
    }

private:
    // The seed-keyed onset-timing delay (§9.2), in samples: a per-render silent pre-roll
    // that shifts the whole event stream, drawn from the shared seed PRNG. Bounded to a
    // window that is large (so byte-distinct seeds rarely collide on the same delay) yet
    // safely inside the render duration (so the note still fires within the buffer). Pure
    // function of the seed.
    [[nodiscard]] static int seedOnsetDelay(mw::util::Prng& rng, int durationFrames) noexcept {
        // (PI) bounds: harness-local test-fixture constants — they do not feed the shipped
        // engine, so they stay here, not in the calibration table [docs/design/11 §4.2].
        constexpr int kMaxOnsetDither = 1024;   // widest pre-roll window, in samples
        // Keep the onset comfortably inside the buffer: cap at a quarter of the duration
        // (and never negative), so even a short stimulus still sounds.
        int window = std::min(kMaxOnsetDither, std::max(0, durationFrames / 4));
        if (window <= 0) return 0;
        return static_cast<int>(rng.nextU32() % static_cast<std::uint32_t>(window));
    }

    // The seed-keyed analog tuning offset (§9.2): a bounded integer-semitone perturbation
    // drawn from the shared seed PRNG. Pure function of the seed; bounded to
    // +/- kMaxNoteDither so the perturbed note stays in MIDI range and audibly sounds.
    [[nodiscard]] static int seedNoteOffset(mw::util::Prng& rng) noexcept {
        // (PI) bound: a small musical window — a harness-local test-fixture constant
        // [docs/design/11 §4.2 — fixture constants are local, not calibration surface].
        constexpr int kMaxNoteDither = 7;   // +/- a perfect fifth, in semitones
        const int span = 2 * kMaxNoteDither + 1;
        return static_cast<int>(rng.nextU32() % static_cast<std::uint32_t>(span)) - kMaxNoteDither;
    }

    // Translate one offline StimEvent into the seam-side mw::MidiEvent the engine
    // consumes, applying the seed-keyed note offset to note-shaped events and clamping
    // the result into the valid MIDI range. CC events pass through unchanged (the engine
    // routes only note-shaped events today; CC is consumed opaque) [docs/design/09 owns
    // the canonical CC->param translation — not minted here].
    [[nodiscard]] static mw::MidiEvent toSeamEvent(const StimEvent& e, int noteOffset) noexcept {
        mw::MidiEvent out{};
        out.channel      = 0;
        out.sampleOffset = e.sampleOffset;
        switch (e.kind) {
            case StimEventKind::NoteOn:
                out.type   = mw::NormalizedType::NoteOn;
                // The played NOTE NUMBER is `data0` (docs/design/09 §3.3; task 118e ingress
                // fix). noteId is kept (round-trips) but the engine reads data0 for pitch, so
                // the resolved note is identical to the pre-fix noteId value -> the blessed
                // golden corpus is byte-stable (no re-bless).
                out.noteId = clampNote(static_cast<int>(e.note) + noteOffset);
                out.data0  = static_cast<float>(clampNote(static_cast<int>(e.note) + noteOffset));
                out.value  = e.value;       // velocity (currently inert in the voice path)
                break;
            case StimEventKind::NoteOff:
                out.type   = mw::NormalizedType::NoteOff;
                out.noteId = clampNote(static_cast<int>(e.note) + noteOffset);
                out.data0  = static_cast<float>(clampNote(static_cast<int>(e.note) + noteOffset));
                out.value  = 0.0f;
                break;
            case StimEventKind::ControlChange:
                out.type   = mw::NormalizedType::ControlChange;
                out.data0  = e.data0;       // controller index
                out.value  = e.value;       // normalized CC value
                break;
        }
        return out;
    }

    [[nodiscard]] static std::int16_t clampNote(int note) noexcept {
        return static_cast<std::int16_t>(std::clamp(note, 0, 127));
    }
};

} // namespace mw::golden
