// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/CpuBudget.h — the CPU-budget regression measurement primitive
// (task 076b): mw::test::CpuBudgetSpec + double measureWorstCaseBlockMicros(const
// CpuBudgetSpec&), the median per-block wall-time of a worst-case render driven
// through the assembled mw::Engine.
//
// Realizes docs/design/11 §13.5 (the CPU-budget regression golden) under ADR-013 C21
// (a worst-case patch whose per-block wall-time exceeds the committed ceiling FAILS as
// an RT-budget regression) and ADR-019 VT-05 (the worst case is full poly + full unison
// on the one pool). The ceiling itself is read from MANIFEST.toml (the §13.5 "pinned in
// the MANIFEST alongside engine + oversample factor" provenance) by the sibling
// CpuBudgetManifest.h — this primitive only MEASURES; deriving/committing the ceiling
// value is out of scope [task 076b Out-of-scope].
//
// WHAT THIS OWNS (task 076b Scope):
//   * the CpuBudgetSpec POD (numVoices, unison, oversampleFactor, ladder engine, SR,
//     blockSize, ceilingMicrosPerBlock) — the §13.5 signature;
//   * measureWorstCaseBlockMicros(): build + prepare a fresh assembled Engine at full
//     poly + full unison + the keyed SR/blockSize, gate-on the worst-case voice load,
//     render N blocks, and return the MEDIAN of the per-block wall-times in microseconds.
//
// OUT OF SCOPE: deriving/committing the ceiling (a tuning/MANIFEST authoring action);
// CLASS-EXACT/CLASS-FP audio-output comparison (other golden tasks); per-format
// wall-time differences (host smoke matrix) [task 076b Out-of-scope].
//
// Header-only: the design tree lists tests/invariants/CpuBudget.cpp, but tests/invariants
// is NOT globbed by tests/CMakeLists.txt (only AudioThreadGuard*.cpp is), and editing
// tests/CMakeLists.txt is forbidden by the parallel-fleet conflict-avoidance rule. A
// header-only realization consumed by the globbed tests/unit/*.cpp is the established
// pattern of the sibling tests/golden/RenderHarness.h (076), GoldenKey.h (041),
// Stimulus.h (042), and Manifest.h (046).
//
// THIS IS OFFLINE MEASUREMENT CODE. The Engine it drives is the RT-constrained artifact
// (no alloc/lock on the audio thread — covered by the EngineRtSafe / AudioThreadGuard
// tests); this harness itself runs off any audio thread and is not RT-constrained
// [docs/design/11 §2.2]. It allocates its measurement buffers ONCE up front and then
// times only the steady-state process() calls, so the timing window excludes the
// one-time prepare() allocation [docs/design/11 §13.1 warm-up carve-out analogue].
//
// WHY 2x oversampling is the worst case here: the assembled Engine derives its per-voice
// oversample factor from the host sample rate at prepare (core/Engine.cpp; ADR-023 V15) —
// it runs 2x for every blessed rate <= 96 kHz (since 96000*2 == OS_CEILING_HZ is allowed)
// and clamps to 1x only ABOVE the ceiling. So a worst-case spec keys a blessed rate
// <= 96 kHz and the achieved factor is verified to equal the requested 2x via
// Engine::oversampleFactor(); the spec carries the requested factor so the test can
// assert "the engine actually ran 2x" rather than silently measuring a 1x render.
//
// WHY THE LOAD IS DRIVEN AS UNISON (not POLY) in the as-built engine: §13.5/ADR-019 VT-05
// describe the worst case as "full poly + full unison" — both modes are skins over the
// ONE Voice[kMaxVoices] pool (ADR-019 VT-01). In the engine assembled today (task 118 +
// VoiceManager task 074), POLY note allocation is NOT yet wired (it is task 075:
// VoiceManager::handleNoteEvent / controlTick early-return for VoiceMode::Poly), so a POLY
// drive would sound ZERO voices and SILENTLY measure a near-empty render — a false green
// for an RT-budget gate. The maximally-loaded path the assembled engine can ACTUALLY
// sound is UNISON at the full kMaxUnison count: every unison voice runs a distinct
// drift-seeded oscillator + the 2x-oversampled ladder/VCA, the genuine per-voice DSP
// stack the budget protects. So the worst-case load drives Unison at the spec's unison
// count, and the measurement reports soundingVoices so a test can REQUIRE the load really
// engaged the full stack (a future regression to a silent path is then caught, not
// measured-as-fast). When task 075 wires POLY into the engine note path, the worst case
// here broadens to full poly groups x full unison on the same pool with no signature
// change [ADR-019 VT-05].

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "GoldenKey.h"   // mw::golden::LadderEngine (the ADR-003 ladder A/B tag)

#include "../../core/Engine.h"
#include "../../core/BlockContext.h"
#include "../../core/voice/VoiceTypes.h"   // kMaxVoices, kMaxUnison, VoiceMode

namespace mw::test {

// ---------------------------------------------------------------------------
// CpuBudgetSpec — the §13.5 worst-case render descriptor. numVoices is the full poly
// load; unison the full unison count; oversampleFactor the REQUESTED per-voice factor
// (2 == worst case); engine the ladder A/B tag; sampleRate one of the blessed set;
// blockSize the host block we time; ceilingMicrosPerBlock the committed (PI) wall-time
// ceiling read from MANIFEST (NOT hard-coded here) [docs/design/11 §13.5].
// ---------------------------------------------------------------------------
struct CpuBudgetSpec {
    int                     numVoices;             // full poly (recorded; see file header —
                                                   //   the as-built engine sounds the full
                                                   //   UNISON stack, POLY alloc is task 075)
    int                     unison;                // full unison — the achievable max load
    int                     oversampleFactor;      // 2 (worst case); verified achieved
    mw::golden::LadderEngine engine;               // Newton-iterated ladder (ADR-003 A/B)
    double                  sampleRate;            // one of the blessed set (§5.2)
    int                     blockSize;             // host block size, in frames
    double                  ceilingMicrosPerBlock; // committed ceiling (PI) — from MANIFEST
};

// ---------------------------------------------------------------------------
// The result of one measurement run: the median per-block micros plus the oversample
// factor the engine ACTUALLY ran at, so the test can assert the worst-case render truly
// exercised 2x oversampling (and did not silently clamp to 1x). exceedsCeiling() is the
// hard-gate verdict the ctest asserts [ADR-013 C21].
// ---------------------------------------------------------------------------
struct CpuBudgetMeasurement {
    double medianMicrosPerBlock = 0.0;   // median of N per-block wall-times
    int    achievedOversample   = 0;     // Engine::oversampleFactor() after prepare
    int    blocksMeasured       = 0;     // N (the run count the median was taken over)
    int    soundingVoices       = 0;     // active voices the worst-case load drove sounding

    // The hard-gate verdict: did the worst-case median exceed the committed ceiling?
    // [docs/design/11 §13.5; ADR-013 C21]. A measured time over the ceiling => true =>
    // the ctest FAILS (RT-budget regression).
    [[nodiscard]] bool exceedsCeiling(double ceilingMicrosPerBlock) const noexcept {
        return medianMicrosPerBlock > ceilingMicrosPerBlock;
    }
};

namespace detail {

// The number of timed blocks the median is taken over. (PI) — a harness-local
// measurement constant (it does not feed the shipped engine, so it stays here, not in
// the calibration table [docs/design/11 §4.2]). Odd so the median is a single sample,
// and large enough that a one-off scheduler hiccup does not move the median.
inline constexpr int kRunCount = 31;

// Worst-case full-load note set: hold kMaxPoly distinct notes (full poly), each note
// driving a full unison stack, so the whole Voice[kMaxVoices] pool sounds. The notes
// span a musical range so no two voices share an identical phase increment (stressing
// the per-voice oscillator/filter/drift state independently).
inline void gateOnFullLoad(mw::Engine& engine, double sampleRate, int blockSize,
                           const std::vector<mw::MidiEvent>& noteOns) noexcept {
    // A single priming block applies all the note-ons (sample-accurate at offset 0) and
    // lets the voice pool reach its sounding steady state BEFORE the timed window, so the
    // measurement times the worst-case SUSTAINED load, not the note-on transient.
    std::vector<float> primeL(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> primeR(static_cast<std::size_t>(blockSize), 0.0f);
    float* chans[2] = { primeL.data(), primeR.data() };

    mw::BlockContext ctx{};
    ctx.audio.channels    = chans;
    ctx.audio.numChannels = 2;
    ctx.audio.numFrames   = blockSize;
    ctx.params            = nullptr;
    ctx.transport         = mw::TransportInfo{ /*bpm=*/120.0, /*ppq=*/0.0,
                                               /*isPlaying=*/true, sampleRate };
    ctx.midi.events       = noteOns.empty() ? nullptr : noteOns.data();
    ctx.midi.numEvents    = static_cast<int>(noteOns.size());
    engine.process(ctx);
}

} // namespace detail

// ---------------------------------------------------------------------------
// measureWorstCase — build the assembled Engine at the worst-case configuration, drive
// it under the worst-case (full-unison) voice load, and return the full measurement: the
// MEDIAN of N per-block wall-times in microseconds, the achieved oversample factor, the
// run count, and the sounding-voice count [docs/design/11 §13.5; ADR-019 VT-05].
//
// The median (not the mean) is used so a single scheduler preemption / cache-cold block
// does not inflate the reported time — exactly the §13.5 "median of N runs" contract.
// The buffers are allocated ONCE before the timed loop; the timer wraps ONLY the
// steady-state engine.process() call, so the one-time prepare() allocation is excluded.
// ---------------------------------------------------------------------------
[[nodiscard]] inline CpuBudgetMeasurement measureWorstCase(const CpuBudgetSpec& spec) {
    CpuBudgetMeasurement m{};

    const int blockSize = std::max(1, spec.blockSize);
    const int unison    = std::clamp(spec.unison, 1, mw::kMaxUnison);

    // --- Build + prepare a fresh Engine at the worst-case SR/blockSize. prepare() is the
    // ONLY allocation site (Engine.h §5.5); it derives the per-voice oversample factor
    // from the SR (2x for blessed rates <= 96 kHz). ------------------------------------
    mw::Engine engine;
    engine.prepare(spec.sampleRate, blockSize, mw::kMaxVoices);
    m.achievedOversample = engine.oversampleFactor();

    // --- Configure the worst case on the ONE pool [ADR-019 VT-05]. Drive UNISON at the
    // full unison count — the maximally-loaded path the assembled engine can actually
    // SOUND (POLY allocation is task 075 and not yet wired into the note path, so a POLY
    // drive would sound zero voices; see the file header). setMode/setUnisonCount latch
    // to the next block boundary (VoiceManager RT7); the priming block applies the latch.
    mw::VoiceManager& voices = const_cast<mw::VoiceManager&>(engine.voiceManager());
    voices.setMode(mw::VoiceMode::Unison);
    voices.setUnisonCount(unison);

    // --- The worst-case note: one held key at full velocity. UNISON broadcasts it to the
    // full unison stack (slots 0..unison-1), each voice carrying its own drift seed so the
    // detuned stack runs `unison` distinct oscillator increments + the 2x-oversampled
    // ladder/VCA — the genuine per-voice DSP cost the budget protects. -----------------
    std::vector<mw::MidiEvent> noteOns;
    {
        mw::MidiEvent e{};
        e.type         = mw::NormalizedType::NoteOn;
        e.channel      = 0;
        e.noteId       = 60;     // middle C
        e.value        = 1.0f;   // full velocity
        e.data0        = 0.0f;
        e.sampleOffset = 0;
        noteOns.push_back(e);
    }

    // Prime: apply the note-on and reach sounding steady state before timing (the latch
    // and the full unison stack are now sounding).
    detail::gateOnFullLoad(engine, spec.sampleRate, blockSize, noteOns);
    m.soundingVoices = engine.voiceManager().activeCount();

    // --- Allocate the timed-loop buffers ONCE (off the timed window). A held-note
    // (no further events) block is the sustained worst case. ---------------------------
    std::vector<float> outL(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> outR(static_cast<std::size_t>(blockSize), 0.0f);
    float* chans[2] = { outL.data(), outR.data() };

    mw::BlockContext ctx{};
    ctx.audio.channels    = chans;
    ctx.audio.numChannels = 2;
    ctx.audio.numFrames   = blockSize;
    ctx.params            = nullptr;
    ctx.transport         = mw::TransportInfo{ /*bpm=*/120.0, /*ppq=*/0.0,
                                               /*isPlaying=*/true, spec.sampleRate };
    ctx.midi.events       = nullptr;   // sustained: no further events in the timed window
    ctx.midi.numEvents    = 0;

    // --- Time N steady-state process() calls; collect the per-block micros. ----------
    std::vector<double> micros;
    micros.reserve(static_cast<std::size_t>(detail::kRunCount));
    for (int i = 0; i < detail::kRunCount; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        engine.process(ctx);
        const auto t1 = std::chrono::steady_clock::now();
        const double us =
            std::chrono::duration<double, std::micro>(t1 - t0).count();
        micros.push_back(us);
    }

    // --- Median of N (the §13.5 contract). nth_element puts the middle element in place
    // in O(N); kRunCount is odd so the median is the single middle sample. ------------
    const std::size_t mid = micros.size() / 2;
    std::nth_element(micros.begin(), micros.begin() + static_cast<std::ptrdiff_t>(mid),
                     micros.end());
    m.medianMicrosPerBlock = micros[mid];
    m.blocksMeasured       = detail::kRunCount;
    return m;
}

// ---------------------------------------------------------------------------
// measureWorstCaseBlockMicros — the §13.5-named entry point: the MEDIAN of N per-block
// wall-times in microseconds for the worst-case render [docs/design/11 §13.5]. A thin
// projection of measureWorstCase() (the richer measurement the gate uses to also assert
// the engine ran 2x and the full voice stack actually sounded).
// ---------------------------------------------------------------------------
[[nodiscard]] inline double measureWorstCaseBlockMicros(const CpuBudgetSpec& spec) {
    return measureWorstCase(spec).medianMicrosPerBlock;
}

} // namespace mw::test
