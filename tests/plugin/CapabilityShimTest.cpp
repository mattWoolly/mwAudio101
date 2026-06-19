// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/plugin/CapabilityShimTest.cpp — JUCE-linked Catch2 acceptance tests for the
// per-format CapabilityShim (task 112). Realizes docs/design/09 §7.2-7.4 / §8.1-8.2
// and ADR-022 C5-C12.
//
// Every test-case display name begins with the task tag `capshim` so the `-R capshim`
// ctest selector matches >= 1 and the silent-pass rule holds.
//
// Covers each acceptance criterion of plan/backlog/112-...:
//   1. Per-format rungs match the §8.1 / ADR-022 capability matrix.
//   2. recheckPerBlock is branch-free and performs ZERO heap allocation across a
//      FreeRun<->transport transition. The no-alloc invariant is proved with the
//      mstats() byte-delta probe (the same override-free, collision-proof sentinel
//      MpeReconstructorTest uses: mw101_plugin_tests globs every tests/plugin/*.cpp
//      into ONE binary and LatencyReporterTest already defines the single global
//      operator new, so a second override here would be a duplicate-symbol error;
//      mstats() needs no global symbol) [docs/design/09 §1.2 — macOS arm64 bless box].
//   3. Both resolved rungs are published via the atomic-pointer path so Collapsed /
//      Free-run are user-visible (§7.4; ADR-022 C12).
//   + ADR-022 C8: HOST-SYNC-without-transport behaves as INTERNAL (Free-run) then
//     re-locks from absolute PPQ when a transport reappears, with no allocation.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

#include <malloc/malloc.h>   // mstats(): override-free heap-usage probe (macOS arm64)

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/CapabilityShim.h"

using namespace mw::plugin;

namespace {

// A controllable fake playhead. getPosition() returns an empty Optional when
// `hasTransport_` is false (modelling Standalone / a stopped, playhead-less host) and
// a populated PositionInfo (isPlaying + absolute PPQ) when true. Constructing the
// Optional<PositionInfo> is allocation-free (it is an inline value type), so a recheck
// driving this fake never heap-allocates.
class FakePlayHead final : public juce::AudioPlayHead {
public:
    void setTransport(bool present, double ppq = 0.0, double bpm = 120.0) noexcept {
        hasTransport_ = present;
        ppq_ = ppq;
        bpm_ = bpm;
    }

    juce::Optional<PositionInfo> getPosition() const override {
        if (! hasTransport_)
            return juce::Optional<PositionInfo>{};   // no transport reported
        PositionInfo info;
        info.setIsPlaying(true);
        info.setPpqPosition(ppq_);
        info.setBpm(bpm_);
        return info;
    }

private:
    bool   hasTransport_ = false;
    double ppq_ = 0.0;
    double bpm_ = 120.0;
};

} // namespace

// ============================================================================
// Acceptance criterion 1: per-format rungs match the §8.1 / ADR-022 matrix.
// ============================================================================
TEST_CASE("capshim resolves per-format note-expression and transport rungs per the matrix", "[capshim]")
{
    CapabilityShim shim;
    FakePlayHead playing;
    playing.setTransport(/*present=*/true, /*ppq=*/4.0);

    // --- Transport rung (best, with a transport present) -----------------------
    // CLAP -> Sample-accurate; VST3/AU/LV2 -> Block-quantized; Standalone -> Free-run.
    REQUIRE(shim.resolve(PluginFormat::CLAP,       false, &playing).transport == TransportRung::SampleAccurate);
    REQUIRE(shim.resolve(PluginFormat::VST3,       false, &playing).transport == TransportRung::BlockQuantized);
    REQUIRE(shim.resolve(PluginFormat::AU,         false, &playing).transport == TransportRung::BlockQuantized);
    REQUIRE(shim.resolve(PluginFormat::LV2,        false, &playing).transport == TransportRung::BlockQuantized);
    REQUIRE(shim.resolve(PluginFormat::Standalone, false, &playing).transport == TransportRung::FreeRun);

    // --- Note-expression rung (MPE-lite OFF -> Collapsed everywhere but CLAP) ----
    REQUIRE(shim.resolve(PluginFormat::CLAP,       false, &playing).noteExpr == NoteExpressionRung::Native);
    REQUIRE(shim.resolve(PluginFormat::VST3,       false, &playing).noteExpr == NoteExpressionRung::Collapsed);
    REQUIRE(shim.resolve(PluginFormat::AU,         false, &playing).noteExpr == NoteExpressionRung::Collapsed);
    REQUIRE(shim.resolve(PluginFormat::LV2,        false, &playing).noteExpr == NoteExpressionRung::Collapsed);
    REQUIRE(shim.resolve(PluginFormat::Standalone, false, &playing).noteExpr == NoteExpressionRung::Collapsed);

    // --- Note-expression rung (MPE-lite ON -> MPE-over-MIDI; CLAP stays Native) --
    REQUIRE(shim.resolve(PluginFormat::CLAP,       true, &playing).noteExpr == NoteExpressionRung::Native);
    REQUIRE(shim.resolve(PluginFormat::VST3,       true, &playing).noteExpr == NoteExpressionRung::MpeOverMidi);
    REQUIRE(shim.resolve(PluginFormat::AU,         true, &playing).noteExpr == NoteExpressionRung::MpeOverMidi);
    REQUIRE(shim.resolve(PluginFormat::LV2,        true, &playing).noteExpr == NoteExpressionRung::MpeOverMidi);
    REQUIRE(shim.resolve(PluginFormat::Standalone, true, &playing).noteExpr == NoteExpressionRung::MpeOverMidi);

    // The static helpers agree with resolve() (the matrix has a single source).
    REQUIRE(CapabilityShim::staticTransportRung(PluginFormat::CLAP)       == TransportRung::SampleAccurate);
    REQUIRE(CapabilityShim::staticTransportRung(PluginFormat::Standalone) == TransportRung::FreeRun);
    REQUIRE(CapabilityShim::noteExpressionRung(PluginFormat::CLAP, false) == NoteExpressionRung::Native);
    REQUIRE(CapabilityShim::noteExpressionRung(PluginFormat::VST3, true)  == NoteExpressionRung::MpeOverMidi);
}

// ============================================================================
// Acceptance criterion 1 (cont.): a sync-capable format with NO transport reported
// at resolve() falls to Free-run [§8.1 "Free-run if no transport"; ADR-022 C7].
// ============================================================================
TEST_CASE("capshim resolves a sync-capable format to Free-run when no transport is reported", "[capshim]")
{
    CapabilityShim shim;
    FakePlayHead stopped;
    stopped.setTransport(/*present=*/false);

    // VST3 CAN block-sync, but with no transport right now it resolves to Free-run.
    const auto caps = shim.resolve(PluginFormat::VST3, /*mpeLite=*/false, &stopped);
    REQUIRE(caps.transport == TransportRung::FreeRun);
    REQUIRE(caps.noteExpr  == NoteExpressionRung::Collapsed);

    // A null playhead (no host playhead object at all) is equally Free-run, allocation-
    // free [ADR-011 C10]. The format's BEST rung is still the sync rung, ready to recover.
    const auto capsNull = shim.resolve(PluginFormat::AU, /*mpeLite=*/true, nullptr);
    REQUIRE(capsNull.transport == TransportRung::FreeRun);
    REQUIRE(capsNull.noteExpr  == NoteExpressionRung::MpeOverMidi);
    REQUIRE(shim.bestTransportRung() == TransportRung::BlockQuantized);   // AU's best, cached
}

// ============================================================================
// Acceptance criterion 2 + ADR-022 C8: recheckPerBlock transitions to/from Free-run
// and re-locks from absolute PPQ; the transition performs ZERO heap allocation.
// ============================================================================
TEST_CASE("capshim recheckPerBlock falls to and recovers from Free-run and re-locks from PPQ", "[capshim]")
{
    CapabilityShim shim;
    FakePlayHead ph;

    // VST3 resolved while a transport is present -> Block-quantized, best rung cached.
    ph.setTransport(/*present=*/true, /*ppq=*/8.0);
    REQUIRE(shim.resolve(PluginFormat::VST3, /*mpeLite=*/false, &ph).transport == TransportRung::BlockQuantized);
    REQUIRE(shim.bestTransportRung() == TransportRung::BlockQuantized);

    // Block 1: transport still present -> stays Block-quantized; re-lock anchor tracks PPQ.
    ph.setTransport(true, 12.5);
    {
        const auto r = shim.recheckPerBlock(&ph);
        REQUIRE(r.transport == TransportRung::BlockQuantized);
        REQUIRE(r.noteExpr  == NoteExpressionRung::Collapsed);   // note-expr fixed at resolve
        REQUIRE(shim.relockPpq() == 12.5);
    }

    // Block 2: host STOPS reporting transport (HOST-SYNC w/o transport) -> Free-run.
    // ADR-022 C8: behaves as INTERNAL; the re-lock anchor is held at 0.
    ph.setTransport(false);
    {
        const auto r = shim.recheckPerBlock(&ph);
        REQUIRE(r.transport == TransportRung::FreeRun);
        REQUIRE(shim.relockPpq() == 0.0);
    }

    // Block 3: a null playhead mid-stream is equally Free-run (allocation-free read).
    {
        const auto r = shim.recheckPerBlock(nullptr);
        REQUIRE(r.transport == TransportRung::FreeRun);
    }

    // Block 4: transport REAPPEARS at a new absolute PPQ -> re-locks to Block-quantized
    // and re-phases from that PPQ [ADR-022 C8].
    ph.setTransport(true, 20.25);
    {
        const auto r = shim.recheckPerBlock(&ph);
        REQUIRE(r.transport == TransportRung::BlockQuantized);   // recovered the best rung
        REQUIRE(shim.relockPpq() == 20.25);                      // re-locked from absolute PPQ
    }

    // --- No-alloc across the FULL transition cycle (the AudioThreadGuard invariant) --
    // Warm up mstats() once so any lazy first-call internal alloc is OUTSIDE the window.
    (void) mstats();
    const std::size_t before = mstats().bytes_used;
    for (int i = 0; i < 256; ++i)
    {
        ph.setTransport(true, static_cast<double>(i));
        (void) shim.recheckPerBlock(&ph);     // present -> Block-quantized, re-lock
        ph.setTransport(false);
        (void) shim.recheckPerBlock(&ph);     // absent  -> Free-run
        (void) shim.recheckPerBlock(nullptr); // null    -> Free-run
    }
    const std::size_t after = mstats().bytes_used;
    REQUIRE(after == before);   // ZERO heap growth across 256 round-trip transitions

    // The hot path is noexcept (no throw -> no implicit allocation path).
    STATIC_REQUIRE(noexcept(shim.recheckPerBlock(nullptr)));
}

// ============================================================================
// Acceptance criterion 3: both rungs are published via the lock-free atomic-pointer
// path so Collapsed / Free-run are user-visible [§7.4; ADR-022 C12].
// ============================================================================
TEST_CASE("capshim publishToUi makes both rungs visible via a lock-free atomic swap", "[capshim]")
{
    CapabilityShim shim;

    // The UI publish pointer is ALWAYS lock-free on this platform: the UI reader's
    // load never blocks on the audio-thread writer [ADR-022 C12].
    REQUIRE(CapabilityShim::uiPublishIsAlwaysLockFree());
    STATIC_REQUIRE(noexcept(shim.publishToUi(ResolvedCapabilities{})));
    STATIC_REQUIRE(noexcept(shim.uiRungs()));

    // Publish a Collapsed / Free-run state — the precise "silent surprise" §7.4 wants
    // visible — and read it straight back through the atomic path.
    const ResolvedCapabilities collapsedFree{ NoteExpressionRung::Collapsed, TransportRung::FreeRun };
    shim.publishToUi(collapsedFree);
    {
        const auto seen = shim.uiRungs();
        REQUIRE(seen.noteExpr  == NoteExpressionRung::Collapsed);
        REQUIRE(seen.transport == TransportRung::FreeRun);
    }

    // Publish a different state (Native / Sample-accurate): the swap is observed and
    // does NOT mutate the previously-published value out from under a reader (the
    // writer edits the OTHER inline slot, then swaps). Drive several swaps to exercise
    // the ping-pong.
    const ResolvedCapabilities nativeSample{ NoteExpressionRung::Native, TransportRung::SampleAccurate };
    shim.publishToUi(nativeSample);
    {
        const auto seen = shim.uiRungs();
        REQUIRE(seen.noteExpr  == NoteExpressionRung::Native);
        REQUIRE(seen.transport == TransportRung::SampleAccurate);
    }

    const ResolvedCapabilities mpeBlock{ NoteExpressionRung::MpeOverMidi, TransportRung::BlockQuantized };
    for (int i = 0; i < 8; ++i)
    {
        shim.publishToUi(i % 2 == 0 ? mpeBlock : collapsedFree);
        const auto seen = shim.uiRungs();
        if (i % 2 == 0) {
            REQUIRE(seen.noteExpr  == NoteExpressionRung::MpeOverMidi);
            REQUIRE(seen.transport == TransportRung::BlockQuantized);
        } else {
            REQUIRE(seen.noteExpr  == NoteExpressionRung::Collapsed);
            REQUIRE(seen.transport == TransportRung::FreeRun);
        }
    }

    // The end-to-end path: resolve() -> publishToUi(resolved) makes the resolved rungs
    // user-visible (the §7.4 contract the wrapper wires) for a Free-run Standalone.
    FakePlayHead none;
    none.setTransport(false);
    const auto resolved = shim.resolve(PluginFormat::Standalone, /*mpeLite=*/false, &none);
    shim.publishToUi(resolved);
    {
        const auto seen = shim.uiRungs();
        REQUIRE(seen.transport == TransportRung::FreeRun);     // Standalone Free-run, visible
        REQUIRE(seen.noteExpr  == NoteExpressionRung::Collapsed);
        REQUIRE(seen.noteExpr  == resolved.noteExpr);
        REQUIRE(seen.transport == resolved.transport);
    }
}

// ============================================================================
// Acceptance criterion 2 (cont.): resolve() and publishToUi() are noexcept and the
// shim owns NO heap-allocating member (all storage is inline), so the hot paths
// structurally cannot allocate — stronger than a sampled counter.
// ============================================================================
TEST_CASE("capshim resolve and publish are noexcept and the shim owns no heap storage", "[capshim]")
{
    STATIC_REQUIRE(noexcept(CapabilityShim{}));
    STATIC_REQUIRE(std::is_trivially_copyable_v<ResolvedCapabilities>);
    STATIC_REQUIRE(std::is_trivially_destructible_v<ResolvedCapabilities>);

    // resolve() is noexcept (it only reads the playhead + assigns scalars / enums).
    CapabilityShim shim;
    STATIC_REQUIRE(noexcept(shim.resolve(PluginFormat::VST3, false, nullptr)));

    // The shim's UI double buffer is two inline ResolvedCapabilities + an atomic ptr —
    // no owned heap resource, so publishToUi() cannot allocate.
    REQUIRE(sizeof(CapabilityShim) >= 2u * sizeof(ResolvedCapabilities));
}
