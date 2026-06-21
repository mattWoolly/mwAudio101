// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/unit/EngineGateTrigModeTest.cpp — KeyAssigner-ownership reconciliation tests
// (task 118b). Test-case display names begin with "engine_s7" so `ctest -R engine_s7`
// selects exactly these under the silent-pass rule (AGENTS.md "Tests"); '[' is kept
// OUT of the display text (Catch2 parses it as a tag and it would break -R selection).
//
// Closes the wave-11 QA MEDIUM on PR #71 (task 118): the Engine owned a SECOND
// KeyAssigner (keys_) and routed notes to it via a ControlTickBridge, never calling
// VoiceManager::handleNoteEvent, so VoiceManager::keyAssigner_ (the documented sole
// authority, doc 04 §5.1/§9, ADR-006 C12) was dead and setGateTrigMode was unreachable
// through the engine.
//
// Covers every Acceptance criterion of plan/backlog/118b against docs/design/04 §5.1,
// §5, §9 and ADR-006 C12/C17:
//   - Engine::setGateTrigMode exists; GATE (lowest-note) vs GATE+TRIG (last-note)
//     changes which held note sounds through Engine::process — an oracle test on the
//     resolved/rendered note [§5.1; ADR-006 C12].
//   - the LFO/clock-reset trigger mode is reachable through the engine [§5; ADR-006 C17].
//   - exactly ONE KeyAssigner is the authority: the engine's note path drives the
//     VoiceManager's own keyAssigner_, not a dead duplicate [§9].
//   - RT invariants preserved: noexcept process/reset; no alloc/lock under the
//     AudioThreadGuard while the S7 selector is switched and the block processed.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "Engine.h"
#include "BlockContext.h"
#include "voice/VoiceTypes.h"
#include "voice/VoiceManager.h"

#include "../invariants/AudioThreadGuard.h"

namespace {

constexpr double kSr        = 48000.0;
constexpr int    kMaxBlock  = 512;
constexpr int    kMaxVoices = mw::kMaxVoices;

mw::MidiEvent noteOn(int note, float vel, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOn;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.data0        = static_cast<float>(note);   // task 118e: note number = pitch
    e.value        = vel;
    e.sampleOffset = offset;
    return e;
}

mw::MidiEvent noteOff(int note, int offset) noexcept {
    mw::MidiEvent e{};
    e.type         = mw::NormalizedType::NoteOff;
    e.channel      = 0;
    e.noteId       = static_cast<std::int16_t>(note);
    e.data0        = static_cast<float>(note);   // task 118e: note number = pitch
    e.value        = 0.0f;
    e.sampleOffset = offset;
    return e;
}

// A self-contained block driver: owns the stereo output, fills a BlockContext, runs one
// Engine::process. No ParamSnapshot is needed by this assembly (seam holds a pointer
// only; the consumed FX chain defaults to FX-OFF / dry).
struct Block {
    std::vector<float> L, R;
    float*             ch[2];

    explicit Block(int n) : L(static_cast<std::size_t>(n), 0.0f),
                            R(static_cast<std::size_t>(n), 0.0f) {
        ch[0] = L.data();
        ch[1] = R.data();
    }

    mw::BlockContext ctx(const std::vector<mw::MidiEvent>& events, int n) {
        mw::BlockContext c{};
        c.audio.channels    = ch;
        c.audio.numChannels = 2;
        c.audio.numFrames   = n;
        c.params            = nullptr;
        c.transport         = { 120.0, 0.0, true, kSr };
        c.midi.events       = events.empty() ? nullptr : events.data();
        c.midi.numEvents    = static_cast<int>(events.size());
        return c;
    }
};

float maxAbs(const std::vector<float>& v, int from, int to) noexcept {
    float m = 0.0f;
    for (int i = from; i < to; ++i) m = std::max(m, std::fabs(v[static_cast<std::size_t>(i)]));
    return m;
}

// Drive an engine through a held-low-then-higher legato overlap in a given S7 mode and
// return the note slot 0 resolves to after the second key. Both keys land at offset 0
// of distinct blocks so the control tick resolves each before the next render.
int resolvedNoteAfterLegato(mw::GateTrigMode mode) {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    eng.setGateTrigMode(mode);

    constexpr int N = 256;

    // Block 1: hold the LOW note (60).
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    // Block 2: add a HIGHER note (67) while 60 is still held (legato overlap).
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(67, 1.0f, 0) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    return eng.voiceManager().voice(0).currentNote();
}

} // namespace

// ---------------------------------------------------------------------------
// §5.1 — Engine::setGateTrigMode exists with the seam-consistent noexcept shape.
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: Engine exposes setGateTrigMode with a noexcept signature",
          "[engine_s7]") {
    using mw::Engine;
    using mw::GateTrigMode;

    STATIC_REQUIRE(std::is_same_v<decltype(&Engine::setGateTrigMode),
                                  void (Engine::*)(GateTrigMode) noexcept>);

    Engine eng;
    STATIC_REQUIRE(noexcept(eng.setGateTrigMode(GateTrigMode::Gate)));
}

// ---------------------------------------------------------------------------
// §5.1 / ADR-006 C12 — ORACLE: in MONO+GATE (lowest-note priority, no legato
// retrigger) a legato overlap of a higher key over a held lower key keeps the LOWEST
// note sounding; in MONO+GATE+TRIG (last-note priority) the LAST (higher) key wins.
// The selector is settable through the Engine and audibly takes effect.
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: GATE selects lowest-held while GATE+TRIG selects last-pressed through the engine",
          "[engine_s7]") {
    // GATE = lowest-note priority: the held low note (60) stays the active note.
    REQUIRE(resolvedNoteAfterLegato(mw::GateTrigMode::Gate) == 60);

    // GATE+TRIG = last-note priority: the just-pressed higher note (67) wins.
    REQUIRE(resolvedNoteAfterLegato(mw::GateTrigMode::GateTrig) == 67);
}

// ---------------------------------------------------------------------------
// §5.1 / ADR-006 C12 — the priority difference is AUDIBLE through Engine::process:
// the two S7 modes render different pitch for the same legato input. (The lowest-note
// vs last-note resolution drives a different VCO pitch, so the two output blocks differ
// sample-for-sample.)
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: GATE vs GATE+TRIG render audibly different output for the same input",
          "[engine_s7]") {
    constexpr int N = 256;

    auto renderSecondBlock = [](mw::GateTrigMode mode, std::vector<float>& outL) {
        mw::Engine eng;
        eng.prepare(kSr, kMaxBlock, kMaxVoices);
        eng.setGateTrigMode(mode);
        {
            Block b(N);
            std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
            auto c = b.ctx(ev, N);
            eng.process(c);
        }
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(72, 1.0f, 0) }; // a full octave above
        auto c = b.ctx(ev, N);
        eng.process(c);
        outL = b.L;
    };

    std::vector<float> gateL, trigL;
    renderSecondBlock(mw::GateTrigMode::Gate, gateL);
    renderSecondBlock(mw::GateTrigMode::GateTrig, trigL);

    // Both modes sound SOMETHING (a held note resolves and the voice is gated on).
    REQUIRE(maxAbs(gateL, 0, N) > 0.0f);
    REQUIRE(maxAbs(trigL, 0, N) > 0.0f);

    // The selector audibly takes effect: lowest-note (60) vs last-note (72) is a
    // different VCO pitch, so the two rendered blocks are NOT identical.
    bool differs = false;
    for (int i = 0; i < N; ++i) {
        if (gateL[static_cast<std::size_t>(i)] != trigL[static_cast<std::size_t>(i)]) {
            differs = true;
            break;
        }
    }
    REQUIRE(differs);
}

// ---------------------------------------------------------------------------
// §5 / ADR-006 C17 — the LFO/clock-reset trigger mode is REACHABLE through the engine.
// In MONO+Lfo, pitch uses lowest-note priority (so a legato higher key does NOT steal
// the active note: the lowest stays), distinguishing it from GATE+TRIG. Reaching Lfo
// mode through Engine::setGateTrigMode and observing the lowest-note resolution proves
// the Lfo selector is settable and propagates to the sole authority.
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: the Lfo trigger mode is reachable through the engine",
          "[engine_s7]") {
    // Lfo uses lowest-note priority (like Gate), so the held low note (60) stays active
    // even after a higher legato key — unreachable before the reconcile because
    // setGateTrigMode never touched the keyassigner the engine actually resolves.
    REQUIRE(resolvedNoteAfterLegato(mw::GateTrigMode::Lfo) == 60);

    // And it is a DISTINCT, settable selector value through the engine: switching to it
    // is a no-throw call on the seam.
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);
    STATIC_REQUIRE(noexcept(eng.setGateTrigMode(mw::GateTrigMode::Lfo)));
    eng.setGateTrigMode(mw::GateTrigMode::Lfo);
    SUCCEED("Lfo mode set through the engine seam");
}

// ---------------------------------------------------------------------------
// §9 — exactly ONE KeyAssigner is the authority: the engine's note path drives the
// VoiceManager's OWN keyAssigner_ (via handleNoteEvent), so a GateTrigMode set through
// the engine is the SAME selector the render path resolves. If a dead duplicate existed
// (the pre-reconcile bug), setGateTrigMode would change nothing about the sounding note.
// This is exercised by the oracle test above; here we pin that the mode change is
// observable at the engine's single resolved-note output and that switching modes
// between blocks re-resolves with the new selector (no stale second authority).
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: setGateTrigMode reaches the single authority the engine renders",
          "[engine_s7]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int N = 256;

    // Start in GATE+TRIG: hold 60, then 67 -> last-note 67 wins.
    eng.setGateTrigMode(mw::GateTrigMode::GateTrig);
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(67, 1.0f, 0) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    REQUIRE(eng.voiceManager().voice(0).currentNote() == 67);

    // Release everything; switch the selector to GATE; replay the SAME legato. Now the
    // single authority resolves lowest-note, so 60 wins. Different result from the SAME
    // input proves setGateTrigMode reaches the one authority the engine actually uses.
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOff(60, 0), noteOff(67, 8) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    eng.setGateTrigMode(mw::GateTrigMode::Gate);
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    {
        Block b(N);
        std::vector<mw::MidiEvent> ev{ noteOn(67, 1.0f, 0) };
        auto c = b.ctx(ev, N);
        eng.process(c);
    }
    REQUIRE(eng.voiceManager().voice(0).currentNote() == 60);
}

// ---------------------------------------------------------------------------
// §9 — VoiceManager exposes the no-arg controlTick() the ControlCore duck-types on, and
// it resolves the manager's OWN keyAssigner_ (the single authority). The two surfaces
// stay consistent: the no-arg form (driven by handleNoteEvent into keyAssigner_) yields
// the same resolution as feeding the explicit NoteDecision form.
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: VoiceManager no-arg controlTick resolves its own keyassigner",
          "[engine_s7]") {
    using mw::VoiceManager;
    using mw::NoteEvent;
    using mw::GateTrigMode;

    // The no-arg overload must exist and be a noexcept hot path.
    STATIC_REQUIRE(std::is_same_v<decltype(static_cast<void (VoiceManager::*)() noexcept>(
                                      &VoiceManager::controlTick)),
                                  void (VoiceManager::*)() noexcept>);

    VoiceManager vm;
    vm.prepare(kSr, /*oversample=*/2, /*seed=*/0xC0FFEEu);
    vm.setGateTrigMode(GateTrigMode::Gate);   // lowest-note priority

    // Feed a low then a higher key through the SINGLE ingress (handleNoteEvent), then
    // drive the no-arg controlTick (the ControlCore path). Lowest-note must win.
    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 60, 1.0f, 0});
    vm.controlTick();
    vm.handleNoteEvent({NoteEvent::Type::NoteOn, 67, 1.0f, 0});
    vm.controlTick();

    REQUIRE(vm.voice(0).currentNote() == 60);
    REQUIRE(vm.activeCount() == 1);
}

// ---------------------------------------------------------------------------
// §9.1 RT-1/RT-2 / ADR-006 C17 — RT invariants preserved across the reconcile: setting
// the S7 selector and processing a block performs no heap allocation and acquires no
// lock under an armed AudioThreadGuard. setGateTrigMode is a lock-free flag write; the
// note path and control tick only flip pre-sized state.
// ---------------------------------------------------------------------------
TEST_CASE("engine_s7: setGateTrigMode plus process is alloc-free and lock-free under the guard",
          "[engine_s7]") {
    mw::Engine eng;
    eng.prepare(kSr, kMaxBlock, kMaxVoices);

    constexpr int N = 256;
    Block blk(N);
    std::vector<mw::MidiEvent> ev{ noteOn(60, 1.0f, 0), noteOn(64, 1.0f, 64) };
    auto c = blk.ctx(ev, N);

    // Warm once before arming so any lazy init is realized OUTSIDE the armed window.
    eng.setGateTrigMode(mw::GateTrigMode::GateTrig);
    eng.process(c);

    mw::test::AudioThreadGuard guard;
    guard.arm();
    eng.setGateTrigMode(mw::GateTrigMode::Gate);  // S7 flip is lock-free.
    eng.process(c);                                // steady-state hot path.
    eng.setGateTrigMode(mw::GateTrigMode::Lfo);
    eng.process(c);
    eng.reset();                                   // reset is a hot path too (§5.5).
    guard.disarm();

    REQUIRE_FALSE(guard.violated());
    REQUIRE(guard.violations().empty());
}
