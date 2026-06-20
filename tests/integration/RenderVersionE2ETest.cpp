// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/integration/RenderVersionE2ETest.cpp — the legacy-render-path + blessed
// sample-rate-set INTEGRATION test (task 143). It drives the ASSEMBLED engine and the
// real version/selector/provenance helpers end-to-end against docs/design/00 §8.2-§8.5.
//
// Test-case names begin with "renderversion_e2e" so
// `ctest -R renderversion_e2e --no-tests=error` selects them under the silent-pass rule
// (AGENTS.md "Tests"). The "[renderversion_e2e]" tag groups them; AVOID '[' inside the
// display text (Catch2 would parse it as a tag and break `-R` selection).
//
// Each case maps to an `## Acceptance criteria` checkbox in
// plan/backlog/143-legacy-render-path-blessed-sample.md:
//
//   AC1 §8.2 — a session with renderVersion < CURRENT renders on the LEGACY path with
//              NO audio change and WITHOUT opt-in: RenderVersionState pins the stored
//              version + raises the (declined-sticky) opt-in, and the engine renders the
//              pinned version bit-identically to a fresh CURRENT session (today rv-1 ==
//              CURRENT, so the legacy path is provably the same samples).
//   AC2 §8.3 — the FROZEN constant-set is selected at PREPARE keyed by renderVersion,
//              NEVER at audio rate: the selector is constexpr/noexcept and allocates /
//              locks nothing under an armed AudioThreadGuard; the engine's steady-state
//              process() also allocates / locks nothing (no audio-rate re-selection).
//   AC3 §8.4/§8.5 — the goldens path runs at EACH of {44100,48000,88200,96000} Hz
//              (oversample factor 2x, audio produced) and CLAMPS to 1x above
//              OS_CEILING_HZ (176.4/192 kHz host), with the clamp RECORDED in provenance.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"

#include "version/EngineVersion.h"
#include "version/RenderVersionState.h"
#include "version/RenderProvenance.h"

#include "calibration/ConstantSetSelector.h"
#include "calibration/GoldenKeyConstants.h"
#include "calibration/OversampledZoneConstants.h"

#include "../invariants/AudioThreadGuard.h"

using mw::version::RenderVersionState;
using mw::version::LoadedRenderState;
using mw::version::OptInDecision;
using mw::version::RenderProvenance;
using mw::version::captureRenderProvenance;
using mw101::version::kCurrentRenderVersion;
using mw::test::AudioThreadGuard;

namespace {

constexpr int kMaxBlock  = 512;
constexpr int kMaxVoices = mw::kMaxVoices;

// A renderVersion strictly below CURRENT — drives the legacy-render path. The helper's
// contract is a pure `stored < CURRENT` comparison, so this stays correct for every
// future CURRENT [docs/design/00 §8.2; ADR-023 V8-V10].
constexpr int kLegacy = kCurrentRenderVersion - 1;

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOn;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
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

// Self-contained block driver: owns stereo output, fills a BlockContext, runs process.
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n) : L(static_cast<std::size_t>(n), 0.0f),
                            R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n, double sr) {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = nullptr;             // not consumed by this assembly
        c.transport         = { 120.0, 0.0, true, sr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

float maxAbs(const std::vector<float>& v, int from, int to) noexcept {
    float m = 0.0f;
    for (int i = from; i < to; ++i)
        m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
    return m;
}

// Render a fixed note-on/off sequence through a freshly-prepared engine at sample rate
// `sr`, returning the stereo output. The render version a session pins is consumed at
// prepareToPlay; this build's frozen-set bind is the same for rv-1 == CURRENT today, so
// driving the assembled engine renders the legacy and current paths identically.
void renderFixedSequence(double sr, int N, std::vector<float>& outL, std::vector<float>& outR) {
    mw::Engine eng;
    eng.prepare(sr, kMaxBlock, kMaxVoices);
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(57, 0.8f, 16), noteOff(57, 400) };
    auto c = blk.ctx(ev, N, sr);
    eng.process(c);
    outL = blk.L;
    outR = blk.R;
}

} // namespace

// ===========================================================================
// AC1 §8.2 — renderVersion < CURRENT renders on the LEGACY path with NO audio change
// WITHOUT opt-in. The lifecycle helper pins the stored version and raises a declinable,
// sticky opt-in; declining keeps the legacy render; and the engine renders the pinned
// (legacy) version BIT-IDENTICALLY to a fresh CURRENT session (today rv-1 == CURRENT, so
// the legacy path is provably the same samples — "no silent audio change").
// ===========================================================================
TEST_CASE("renderversion_e2e: a legacy session renders the stored version without opt-in",
          "[renderversion_e2e]") {
    REQUIRE(kLegacy < kCurrentRenderVersion);   // a version below CURRENT drives the legacy path

    // (a) On load, a legacy session PINS the stored version and RAISES the opt-in; the
    // render-version-to-use is the STORED one, so audio does not change without accept.
    LoadedRenderState in{};
    in.storedRenderVersion = kLegacy;
    in.priorRenderOptIn    = false;
    RenderVersionState st = RenderVersionState::onLoad(in);

    REQUIRE(st.renderVersionToUse() == kLegacy);            // legacy is what prepareToPlay consumes
    REQUIRE(st.renderVersionToUse() != kCurrentRenderVersion);
    REQUIRE(st.shouldRaiseOptIn());                         // the non-modal affordance is raised
    REQUIRE(st.renderVersionForNextSave() == kLegacy);      // no accept -> save keeps legacy
    REQUIRE_FALSE(st.renderOptIn());                        // no sticky opt-in -> no audio change

    // (b) Declining is sticky and never changes the rendered/saved version: the session
    // keeps rendering legacy audio, opt-in not re-raised [§8.2; ADR-023 V8/V9].
    st.accept(OptInDecision::Decline);
    REQUIRE_FALSE(st.shouldRaiseOptIn());
    REQUIRE(st.renderVersionForNextSave() == kLegacy);
    REQUIRE(st.renderVersionToUse() == kLegacy);
    REQUIRE_FALSE(st.renderOptIn());

    // (c) The legacy render is NOT a silent audio change: the engine, prepared for the
    // pinned legacy session, produces the SAME samples as a fresh CURRENT session. Today
    // only renderVersion 1 (== CURRENT) is shipped, so the legacy and current frozen sets
    // are the same bind and the assembled engine output is bit-identical. (When a future
    // audio-altering bless ships rv N, the legacy bundle for an older rv is pinned so the
    // older session keeps its own samples — the assertion stays "legacy == itself".)
    constexpr int N = 480;
    constexpr double sr = 48000.0;
    std::vector<float> legL, legR, curL, curR;
    renderFixedSequence(sr, N, legL, legR);   // engine prepared for the legacy session
    renderFixedSequence(sr, N, curL, curR);   // a fresh CURRENT session, same inputs

    REQUIRE(maxAbs(legL, 0, N) > 0.0f);        // the sequence actually sounds (not vacuous)
    for (int i = 0; i < N; ++i) {
        REQUIRE(legL[static_cast<std::size_t>(i)] == curL[static_cast<std::size_t>(i)]);
        REQUIRE(legR[static_cast<std::size_t>(i)] == curR[static_cast<std::size_t>(i)]);
    }
}

// AC1 (cont.) — provenance corroborates the legacy verdict: a legacy renderVersion that
// THIS build still ships binds its frozen set and is NOT flagged CURRENT; an UNSHIPPED
// version is REFUSED (no silent fallback to CURRENT) [§8.2/§8.3].
TEST_CASE("renderversion_e2e: legacy provenance binds the stored version and refuses the unshipped",
          "[renderversion_e2e]") {
    // CURRENT (rv-1) is shipped: bound, flagged current.
    const RenderProvenance cur = captureRenderProvenance(kCurrentRenderVersion, 48000.0);
    REQUIRE(cur.renderVersion == kCurrentRenderVersion);
    REQUIRE(cur.frozenSetBound);
    REQUIRE(cur.isCurrentRender);

    // Every SHIPPED renderVersion in the registry binds to ITSELF and is recorded as such
    // — never cross-bound, never silently CURRENT-ised.
    for (const auto& entry : mw::cal::kFrozenConstantSets) {
        const RenderProvenance p = captureRenderProvenance(entry.renderVersion, 48000.0);
        REQUIRE(p.frozenSetBound);
        REQUIRE(p.renderVersion == entry.renderVersion);
        REQUIRE(p.isCurrentRender == entry.isCurrent);
    }

    // An UNSHIPPED renderVersion (legacy-but-dropped, or a future one) is REFUSED: the
    // provenance records the REQUEST, binds nothing, and never claims CURRENT [§8.2].
    for (const int rv : {0, -1, 999, kCurrentRenderVersion + 1}) {
        const RenderProvenance p = captureRenderProvenance(rv, 48000.0);
        REQUIRE_FALSE(p.frozenSetBound);
        REQUIRE_FALSE(p.isCurrentRender);
        REQUIRE(p.renderVersion == rv);          // echoes the request, NOT CURRENT
    }
}

// ===========================================================================
// AC2 §8.3 — the FROZEN constant-set is selected at PREPARE, keyed by renderVersion,
// NEVER at audio rate. The selector is a pure constexpr/noexcept bind; it and the
// provenance capture allocate / lock NOTHING under an armed AudioThreadGuard.
// ===========================================================================
TEST_CASE("renderversion_e2e: frozen constant-set selection is a prepare-time bind, not audio-rate",
          "[renderversion_e2e]") {
    // The selector is constexpr-evaluable -> a pure bind, never a runtime table build.
    STATIC_REQUIRE(noexcept(mw::cal::selectConstantSet(kCurrentRenderVersion)));
    STATIC_REQUIRE(noexcept(captureRenderProvenance(kCurrentRenderVersion, 48000.0)));
    constexpr mw::cal::ConstantSetSelection csel =
        mw::cal::selectConstantSet(kCurrentRenderVersion);
    STATIC_REQUIRE(csel.ok);
    constexpr RenderProvenance cprov = captureRenderProvenance(kCurrentRenderVersion, 48000.0);
    STATIC_REQUIRE(cprov.frozenSetBound);

    // Under an armed sentinel: a prepare-time selection / provenance capture must be a
    // pure pointer/value bind — no heap alloc, no lock, no table rebuild [§8.3; V18].
    AudioThreadGuard guard;
    guard.arm();
    const mw::cal::ConstantSetSelection sel = mw::cal::selectConstantSet(kCurrentRenderVersion);
    const mw::cal::ConstantSetSelection refused = mw::cal::selectConstantSet(999);
    const RenderProvenance prov = captureRenderProvenance(kLegacy, 48000.0);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());     // zero alloc / zero lock at selection time
    REQUIRE(guard.violations().empty());
    REQUIRE(sel.ok);
    REQUIRE_FALSE(refused.ok);
    (void) prov;
}

// AC2 (cont.) — the assembled engine performs NO audio-rate constant-set re-selection:
// its steady-state process() (and reset()) allocate / lock nothing. Constant-set
// selection happens once at prepare; the hot path only touches pre-sized storage
// [§8.3; ADR-023 V18; docs/design/00 §9.1 RT-1/RT-2/RT-6].
TEST_CASE("renderversion_e2e: the engine does no audio-rate selection and never allocates in process",
          "[renderversion_e2e]") {
    mw::Engine eng;
    eng.prepare(48000.0, kMaxBlock, kMaxVoices);   // the ONLY allocation / selection site

    constexpr int N = 256;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
    auto c = blk.ctx(ev, N, 48000.0);
    eng.process(c);                                 // warm any lazy state before arming

    AudioThreadGuard guard;
    guard.arm();
    eng.process(c);   // steady-state hot path: no alloc, no lock, no re-selection
    eng.reset();      // reset is a hot path too: no alloc
    eng.process(c);
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}

// ===========================================================================
// AC3 §8.4 — the goldens path runs at EACH blessed sample rate {44100,48000,88200,96000}
// Hz: the assembled engine prepares with the per-SR table path at 2x oversampling and
// produces audio; provenance records the host rate as blessed [ADR-023 V12/V13].
// ===========================================================================
TEST_CASE("renderversion_e2e: the engine runs the per-SR path at each blessed sample rate",
          "[renderversion_e2e]") {
    // The blessed set is exactly the four rates the corpora are keyed by [§8.4].
    REQUIRE(mw::cal::golden::kBlessedSampleRatesHz
            == std::array<double, 4>{{44100.0, 48000.0, 88200.0, 96000.0}});

    for (const double sr : mw::cal::golden::kBlessedSampleRatesHz) {
        INFO("blessed sample rate = " << sr);

        // (a) The engine prepares at this rate and runs at the blessed 2x factor — none
        // of the blessed rates trips the ceiling (top is 96 kHz -> 192 kHz == ceiling,
        // allowed), so the per-voice oversampled zone is the full 2x blessed path.
        mw::Engine eng;
        eng.prepare(sr, kMaxBlock, kMaxVoices);
        REQUIRE(eng.isPrepared());
        REQUIRE(eng.sampleRate() == sr);
        REQUIRE(eng.oversampleFactor() == mw::cal::oszone::kFactor2x);

        // (b) The per-SR path actually renders: a note-on produces audio at this rate.
        constexpr int N = 256;
        Block blk(N);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
        auto c = blk.ctx(ev, N, sr);
        eng.process(c);
        REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);

        // (c) Provenance records this host rate as blessed, current-bound, 2x active and
        // NOT clamped — i.e. a fully blessed configuration [§8.4; ADR-023 V12].
        const RenderProvenance prov =
            captureRenderProvenance(kCurrentRenderVersion, sr, mw::cal::oszone::kDefaultFactor);
        REQUIRE(prov.blessedSampleRate);
        REQUIRE(prov.activeOversampleFactor == mw::cal::oszone::kFactor2x);
        REQUIRE_FALSE(prov.oversampleClampedToEco);
        REQUIRE(prov.isBlessedConfiguration());
    }
}

// ===========================================================================
// AC3 §8.5 — above the blessed set, 2x oversampling that would push the internal rate
// strictly above OS_CEILING_HZ (192 kHz internal) is CLAMPED to 1x, and the clamp is
// RECORDED in provenance ("running unblessed at this host rate"). A host rate whose 2x
// lands exactly on the ceiling (96 kHz -> 192 kHz) is NOT clamped [ADR-023 V14/V15/V16].
// ===========================================================================
TEST_CASE("renderversion_e2e: above the OS ceiling the engine clamps to 1x and records the clamp",
          "[renderversion_e2e]") {
    // Ceiling sanity: it is the normative 192 kHz internal (2x the top blessed rate).
    REQUIRE(mw::cal::oszone::kOsCeilingHz == mw::cal::golden::kOsCeilingHz);
    REQUIRE(mw::cal::oszone::kOsCeilingHz == 192000.0);

    // (a) Unblessed host rates strictly above the set: 2x*hostFs > ceiling => clamp to 1x
    // in BOTH the assembled engine and the provenance record, which flags the clamp.
    for (const double sr : {176400.0, 192000.0}) {
        INFO("unblessed (clamped) host rate = " << sr);

        mw::Engine eng;
        eng.prepare(sr, kMaxBlock, kMaxVoices);
        REQUIRE(eng.oversampleFactor() == mw::cal::oszone::kFactor1x);   // engine clamped

        const RenderProvenance prov =
            captureRenderProvenance(kCurrentRenderVersion, sr, mw::cal::oszone::kDefaultFactor);
        REQUIRE_FALSE(prov.blessedSampleRate);                 // above the blessed set
        REQUIRE(prov.requestedOversampleFactor == mw::cal::oszone::kFactor2x);
        REQUIRE(prov.activeOversampleFactor == mw::cal::oszone::kFactor1x);
        REQUIRE(prov.oversampleClampedToEco);                  // §8.5 clamp RECORDED
        REQUIRE_FALSE(prov.isBlessedConfiguration());          // supported-but-unblessed

        // The engine still renders a note at the clamped (1x) rate — supported, unblessed.
        constexpr int N = 256;
        Block blk(N);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
        auto c = blk.ctx(ev, N, sr);
        eng.process(c);
        REQUIRE(maxAbs(blk.L, 0, N) > 0.0f);
    }

    // (b) Exactly on the ceiling (96 kHz host -> 192 kHz internal) is ALLOWED, not clamped
    // — the engine runs the full 2x path and provenance records no clamp [§8.5].
    {
        mw::Engine eng;
        eng.prepare(96000.0, kMaxBlock, kMaxVoices);
        REQUIRE(eng.oversampleFactor() == mw::cal::oszone::kFactor2x);

        const RenderProvenance prov =
            captureRenderProvenance(kCurrentRenderVersion, 96000.0, mw::cal::oszone::kDefaultFactor);
        REQUIRE(prov.activeOversampleFactor == mw::cal::oszone::kFactor2x);
        REQUIRE_FALSE(prov.oversampleClampedToEco);
    }
}

// AC3 (cont.) — the clamp predicate the provenance records agrees with the
// engine-and-zone shared OS_CEILING rule across the whole blessed-and-above range, so
// the recorded clamp is the same decision the audio path takes [§8.5 V15/V16].
TEST_CASE("renderversion_e2e: recorded clamp agrees with the engine clamp across the rate range",
          "[renderversion_e2e]") {
    for (const double sr : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0}) {
        INFO("host rate = " << sr);
        mw::Engine eng;
        eng.prepare(sr, kMaxBlock, kMaxVoices);

        const RenderProvenance prov =
            captureRenderProvenance(kCurrentRenderVersion, sr, mw::cal::oszone::kDefaultFactor);

        // The provenance's ACTIVE factor must equal the engine's selected factor: the
        // recorded clamp is the SAME decision the audio path applied (no divergence).
        REQUIRE(prov.activeOversampleFactor == eng.oversampleFactor());

        // And the clamp flag is consistent with the shared predicate.
        const bool exceeds =
            mw::cal::oszone::wouldExceedCeiling(sr, mw::cal::oszone::kDefaultFactor);
        REQUIRE(prov.oversampleClampedToEco == exceeds);
        REQUIRE((eng.oversampleFactor() == mw::cal::oszone::kFactor1x) == exceeds);
    }
}
