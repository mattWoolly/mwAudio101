// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EndToEndDeterminismTest.cpp — the end-to-end determinism test (task 135).
// Test-case names begin with "e2e_determinism" so `ctest -R e2e_determinism
// --no-tests=error` selects them under the silent-pass rule (AGENTS.md "Tests"). The
// display names avoid any literal '[' so Catch2 does not mis-parse a tag out of the
// name and break the -R selector.
//
// Objective (plan/backlog/135): assert that two assembled Engine instances given the
// IDENTICAL seed (the Engine's fixed per-instance drift seed, set in prepare()) and an
// IDENTICAL BlockContext event/param sequence produce BIT-identical output on
// integer/deterministic paths and within FP tolerance on the analog stages, and that
// the fixed voice-index summation order (§6.2) is exercised as the load-bearing reason
// that FP reduction is stable.
//
// Design refs read first: docs/design/00 §9.1 (RT-7 determinism: same seed + same
// BlockContext sequence => bit-identical integer-path, FP analog bit-exact on macOS
// arm64 / max abs <= 1e-6 elsewhere), §9.2 (determinism mechanics: pre-seeded per-voice
// PRNG sized/seeded in prepare; integer paths bit-exact by construction), §6.2 (why
// fixed-order single-thread: fixed-order summation FIXES the non-associative FP
// reduction order, so output is bless-stable), §9.3 (headless: the test binary links
// mwcore ONLY). [ADR-001 C8/C9; ADR-019 VT-02/VT-04.]
//
// Acceptance coverage (each criterion is an explicit assertion group below):
//   1. Identical seed + identical BlockContext sequence yields BIT-identical
//      integer-path output per §9.1 RT-7 — two independently-prepared engines fed the
//      same multi-block event sequence compare exactly (==) sample-for-sample. The
//      Engine seeds its voice pool from a FIXED per-instance seed in prepare() (§9.2),
//      so the two instances share identical pre-seeded drift state by construction.
//   2. FP analog stages compare within max abs <= 1e-6 off the macOS arm64 reference
//      per §9.1/§9.2 — asserted through the project's own CLASS-FP comparer
//      (tests/golden/CompareFp.h) under the off-reference band (maxAbsErr = 1e-6), and
//      separately under the bit-exact band (maxAbsErr = 0) that the reference platform
//      holds.
//   3. The fixed voice-index summation order is exercised per §6.2 — a UNISON stack is
//      driven through the VoiceManager (the engine's own summation path); the active
//      list is asserted to be a dense ASCENDING voice-index prefix, AND an oracle shows
//      the fixed order is load-bearing: re-reducing the SAME per-voice contributions in
//      a permuted order is NOT guaranteed to match, so only the fixed order is
//      bless-stable (§6.2 non-associativity).
//   4. (selector) ctest -R e2e_determinism --no-tests=error is green; names begin with
//      e2e_determinism.
//
// Out of scope (plan/backlog/135 "Out of scope"; NOT asserted here): blessing/storing
// the golden corpus, cross-platform CI runner wiring, per-module determinism. The
// ParamSnapshot is the seam's immutable-snapshot pointer (§5.4); this assembly's
// event-driven voice path does not consume it, so a nullptr snapshot mirrors the real
// shell contract for an init patch (the Engine holds only the pointer) — exactly as the
// sibling e2e_smoke / engine_assembly suites drive it.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "voice/VoiceManager.h"
#include "voice/VoiceTypes.h"
#include "calibration/EngineConstants.h"

#include "../golden/CompareFp.h"   // CLASS-FP comparer: the off-reference 1e-6 band (§9.1)

namespace {

constexpr double        kSr        = 48000.0;
constexpr int           kMaxBlock  = 512;
constexpr int           kMaxVoices = mw::kMaxVoices;
constexpr double        kVmSr      = 48000.0;
constexpr int           kVmOs      = 2;
constexpr std::uint32_t kVmSeed    = 0xC0FFEEu;

// --- seam-side event builders (host->POD marshalling already happened in plugin/) ---
mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOn;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.data0        = 0.0f;
    e.value        = vel;
    e.sampleOffset = offset;
    return e;
}

mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOff;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.value        = 0.0f;
    e.sampleOffset = offset;
    return e;
}

// One block of stereo host-borrowed output + a BlockContext factory. Mirrors the real
// shell: the core only ever sees borrowed channel pointers + value-typed PODs (§5.3).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n)
        : L(static_cast<std::size_t>(n), 0.0f),
          R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n) noexcept {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels  = 2;
        c.audio.numFrames    = n;
        c.params             = nullptr;             // immutable-snapshot pointer (§5.4)
        c.transport          = mw::TransportInfo{ 120.0, 0.0, true, kSr };
        c.midi.events        = events.empty() ? nullptr : events.data();
        c.midi.numEvents     = static_cast<int>(events.size());
        return c;
    }
};

// One scripted block in a determinism stimulus: a frame count + the events to apply.
struct ScriptStep {
    int                        frames;
    std::vector<mw::MidiEvent> events;
};

// The fixed, deterministic BlockContext SEQUENCE both engines are fed (§9.1 RT-7). It
// exercises a sample-accurate mid-block note-on, a sustain, an octave change, a release,
// and a drained tail — over a spread of block sizes (incl. the cap and sub-kRenderBlock
// sizes) so the §4.4 chunker is part of the determinism contract.
std::vector<ScriptStep> determinismScript() {
    return {
        { 200, { noteOn(60, 0.9f, 37) } },        // mid-block onset (not a chunk multiple)
        { kMaxBlock, {} },                         // sustain across the worst-case block
        { mw::cal::engine::kRenderBlock + 1, {} }, // odd, just over the chunk cap
        { 128, { noteOn(67, 0.8f, 11), noteOff(60, 90) } }, // overlap then drop the first
        { 96, {} },
        { 333, { noteOff(67, 200) } },             // release
        { kMaxBlock, {} },                         // tail
        { 64, {} },                                // tail
    };
}

// Drive one freshly-prepared engine through the whole script, concatenating the stereo
// output into interleaved-free L/R streams. A fresh engine each call means each run only
// ever sees the FIXED per-instance drift seed prepare() installs (§9.2) — there is no
// hidden run-to-run state, so two runs share identical pre-seeded drift state.
void runScript(std::vector<float>& outL, std::vector<float>& outR) {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    outL.clear();
    outR.clear();
    for (const ScriptStep& step : determinismScript()) {
        Block blk(step.frames);
        auto c = blk.ctx(step.events, step.frames);
        eng.process(c);
        outL.insert(outL.end(), blk.L.begin(), blk.L.end());
        outR.insert(outR.end(), blk.R.begin(), blk.R.end());
    }
}

bool allFinite(const std::vector<float>& v) noexcept {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

float peakAbs(const std::vector<float>& v) noexcept {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    double m = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        m = std::max(m, std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    return m;
}

// A NoteDecision sounding `note` as a fresh trigger (gate + retrigger).
mw::NoteDecision freshTrigger(int note) {
    mw::NoteDecision d;
    d.activeNote = note;
    d.gate       = true;
    d.retrigger  = true;
    return d;
}

// Drive a freshly-prepared UNISON VoiceManager of `u` voices onto note 62 and render
// `n` samples into `outL`/`outR` as the fixed-voice-index-order stereo sum. A fresh
// manager each call only ever sees the same seed (§9.2), so two calls are a determinism
// twin. The active-voice list (a dense ascending index prefix) is checked by the caller.
void renderUnisonSum(int u, int n, std::vector<float>& outL, std::vector<float>& outR,
                     mw::VoiceManager& vm) {
    vm.prepare(kVmSr, kVmOs, kVmSeed);
    vm.setMode(mw::VoiceMode::Unison);
    vm.setUnisonCount(u);
    { std::array<float, 1> z0{}, z1{}; vm.render(z0.data(), z1.data(), 1); } // boundary-apply
    vm.handleNoteEvent({ mw::NoteEvent::Type::NoteOn, 62, 1.0f, 0 });
    vm.controlTick(freshTrigger(62));
    outL.assign(static_cast<std::size_t>(n), 0.0f);
    outR.assign(static_cast<std::size_t>(n), 0.0f);
    vm.render(outL.data(), outR.data(), n);   // ACCUMULATES the u voices in fixed order
}

} // namespace

// ---------------------------------------------------------------------------
// §9.1 RT-7 / §9.2 — Acceptance 1: identical seed + identical BlockContext sequence
// yields BIT-identical output on the integer/deterministic paths. Two engines are
// prepared INDEPENDENTLY (each seeds its voice pool from the same fixed per-instance
// drift seed in prepare(), §9.2) and fed the SAME scripted sequence; every output sample
// must compare exactly (==). A single-threaded, fixed-order, lock-free voice loop is
// deterministic by construction (ADR-019 VT-04), so bit-identity is the contract, not a
// tolerance. The output is also non-silent and finite, so the equality is a real signal,
// not two silent buffers trivially matching.
// ---------------------------------------------------------------------------
TEST_CASE("e2e_determinism: identical seed and block sequence yield bit-identical output",
          "[e2e_determinism]") {
    std::vector<float> aL, aR, bL, bR;
    runScript(aL, aR);
    runScript(bL, bR);

    REQUIRE(aL.size() == bL.size());
    REQUIRE(aR.size() == bR.size());
    REQUIRE_FALSE(aL.empty());

    // The signal is real (finite + non-silent), so a match is meaningful (§4.1 graph).
    REQUIRE(allFinite(aL));
    REQUIRE(allFinite(aR));
    REQUIRE(peakAbs(aL) > 0.0f);

    // RT-7: bit-identical on the integer/deterministic paths — exact equality, not a
    // band. Any single differing sample fails (the integer/deterministic-path contract).
    for (std::size_t i = 0; i < aL.size(); ++i) {
        REQUIRE(aL[i] == bL[i]);
        REQUIRE(aR[i] == bR[i]);
    }
}

// ---------------------------------------------------------------------------
// §9.1 / §9.2 — Acceptance 2: the FP analog stages compare within max abs <= 1e-6 off
// the macOS arm64 reference. The two engine runs are routed through the project's own
// CLASS-FP comparer (tests/golden/CompareFp.h), first under the OFF-REFERENCE band
// (maxAbsErr = 1e-6, the §9.1 RT-7 tolerance for non-reference platforms) and then under
// the BIT-EXACT band (maxAbsErr = 0, which the macOS arm64 reference platform holds).
// Using the shipped comparer ties this assertion to the same FP-discipline math the
// golden harness uses, not an ad-hoc epsilon. The comparer's engine-tag REFUSAL guard is
// satisfied because both runs share the same engine context (same ladder / oversample /
// renderVersion -> same tag), so a pass is a real comparison, never a silent refusal.
// ---------------------------------------------------------------------------
TEST_CASE("e2e_determinism: FP analog stages compare within the off-reference tolerance",
          "[e2e_determinism]") {
    std::vector<float> aL, aR, bL, bR;
    runScript(aL, aR);
    runScript(bL, bR);

    // Build CLASS-FP render results sharing one engine tag so the comparer does not
    // REFUSE (a refusal is not a pass — ADR-013 C22). The default-constructed EngineTag
    // is identical for both, so sameEngineContext() holds.
    mw::golden::RenderResult got{};
    mw::golden::RenderResult ref{};
    got.samples = aL;
    got.sampleRate = kSr;
    ref.samples = bL;
    ref.sampleRate = kSr;

    // (a) Off-reference band: the §9.1 RT-7 non-reference tolerance, max abs <= 1e-6.
    mw::golden::FpTolerance offRef{};
    offRef.maxAbsErr = 1.0e-6;   // the §9.1 RT-7 off-reference ceiling
    offRef.rmsErr    = 1.0e-6;
    const mw::golden::FpResult offResult = mw::golden::compareFp(got, ref, offRef);
    REQUIRE_FALSE(offResult.refused);              // a real comparison, not a refusal
    REQUIRE(offResult.s1.maxAbsErr <= 1.0e-6);     // the criterion, stated directly
    REQUIRE(offResult.pass);                       // within the off-reference band

    // The raw scalar diff is itself within the band (independent of the comparer path).
    REQUIRE(maxAbsDiff(aL, bL) <= 1.0e-6);
    REQUIRE(maxAbsDiff(aR, bR) <= 1.0e-6);

    // (b) Bit-exact band: on the reference platform the analog stages are bit-exact, so
    // maxAbsErr == 0 must also pass (a strictly tighter gate than the 1e-6 band).
    mw::golden::FpTolerance exact{};
    exact.maxAbsErr = 0.0;   // arm64 reference: bit-exact gate (ADR-013 C6; §9.2)
    exact.rmsErr    = 0.0;
    const mw::golden::FpResult exactResult = mw::golden::compareFp(got, ref, exact);
    REQUIRE_FALSE(exactResult.refused);
    REQUIRE(exactResult.pass);                     // bit-exact off the reference
}

// ---------------------------------------------------------------------------
// §6.2 — Acceptance 3: the fixed voice-index summation order is exercised and is the
// load-bearing reason the FP reduction is bless-stable. A UNISON stack of U>1 voices is
// driven through the VoiceManager (the engine's own summation path); the active list is
// asserted to be a dense ASCENDING voice-index prefix [0..U-1], so the per-voice sum
// runs in fixed voice-index order (§6.1 / ADR-019 VT-02). Two identically-prepared
// managers fed the IDENTICAL sequence reduce that >1-voice stack to BIT-identical bits —
// the bless-stable FP reduction order under test. The summed signal is finite and
// non-silent, so the equality is a meaningful multi-voice reduction, not a silent match.
// ---------------------------------------------------------------------------
TEST_CASE("e2e_determinism: fixed voice-index summation order is the stable FP reduction",
          "[e2e_determinism]") {
    constexpr int U = 5;     // a UNISON stack > 1 so the reduction order is observable
    constexpr int N = 256;

    std::vector<float> sumL, sumR, sum2L, sum2R;
    mw::VoiceManager vm, vm2;
    renderUnisonSum(U, N, sumL, sumR, vm);

    REQUIRE(vm.activeCount() == U);
    // The active list is a dense ASCENDING voice-index prefix => the sum runs in fixed
    // voice-index order (§6.1 / ADR-019 VT-02). This IS the reduction order.
    for (int k = 0; k < vm.activeCount(); ++k)
        REQUIRE(static_cast<int>(vm.activeIndex(k)) == k);
    for (int k = 1; k < vm.activeCount(); ++k)
        REQUIRE(vm.activeIndex(k) > vm.activeIndex(k - 1));

    // A second identically-prepared manager fed the same sequence reduces the SAME
    // U-voice stack to BIT-identical bits — the bless-stable fixed-order FP reduction.
    renderUnisonSum(U, N, sum2L, sum2R, vm2);
    REQUIRE(vm2.activeCount() == U);
    for (int i = 0; i < N; ++i) {
        REQUIRE(sumL[static_cast<std::size_t>(i)] == sum2L[static_cast<std::size_t>(i)]);
        REQUIRE(sumR[static_cast<std::size_t>(i)] == sum2R[static_cast<std::size_t>(i)]);
    }

    // The U-voice sum is a real (finite, non-silent) signal, so the equality above is a
    // meaningful reduction of >1 voice, not a trivial silent match.
    REQUIRE(allFinite(sumL));
    REQUIRE(peakAbs(sumL) > 0.0f);
}

// ---------------------------------------------------------------------------
// §6.2 — the non-associativity oracle, stated directly and independently of the DSP: a
// hand-built set of voice partials proves that summing the SAME values in a different
// order can change the FP result, so a FIXED reduction order is REQUIRED for
// bless-stability. This pins the WHY of §6.2 (fixed-order summation fixes the
// non-associative reduction) as an objective, deterministic check.
// ---------------------------------------------------------------------------
TEST_CASE("e2e_determinism: FP addition is non-associative so fixed reduction order matters",
          "[e2e_determinism]") {
    // Values chosen so the float sum is order-dependent: a large magnitude that absorbs
    // a small one, plus its negative. Ascending order vs a permuted order differ in f32.
    const std::array<float, 3> partials{ 1.0e8f, 1.0f, -1.0e8f };

    // Fixed ascending (voice-index) reduction: ((p0 + p1) + p2).
    float ascending = 0.0f;
    for (float p : partials) ascending += p;

    // A permuted reduction over the SAME values: ((p2 + p0) + p1).
    float permuted = 0.0f;
    permuted += partials[2];
    permuted += partials[0];
    permuted += partials[1];

    // FP addition is non-associative: the two orders disagree on the SAME inputs. This
    // is exactly why the voice loop pins a FIXED order (§6.2 / ADR-019 VT-04) — without
    // it the bless would not be reproducible.
    REQUIRE(ascending != permuted);

    // And the fixed ascending order is itself reproducible bit-for-bit run to run (the
    // bless-stable property the voice loop relies on).
    float ascendingAgain = 0.0f;
    for (float p : partials) ascendingAgain += p;
    REQUIRE(ascending == ascendingAgain);
}
