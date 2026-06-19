// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// tests/golden/KeyAssignerReference.h — the independent disassembly-semantics golden
// reference for the SH-101 firmware keyboard_read/play note-priority + retrigger logic
// (task 152, voice-control-4). It is the ORACLE that locks the production
// `core/voice/KeyAssigner` to the firmware contract [docs/design/04 §2; ADR-006
// §Decision item 2, C19].
//
// AUTHORITY / SCOPE (task 152):
//   * Emits a {activeNote, gate, retrigger} (plus clockReset) decision per control tick
//     for a sequence of note events, in a given GateTrigMode, modeling the firmware
//     `keyboard_read` / `play` super-loop [research/07 §3.1-§3.3, §5.1-§5.2].
//   * Implements the two firmware priority rules independently:
//       - lowest-note priority (GATE, LFO): banks/keys scanned low->high; "as soon as
//         we find a key down, we are done" [research/07 §3.2; docs/design/04 §5.3].
//       - last-note priority (GATE+TRIG): XOR of newly-changed-down keys against the
//         prior scan, lowest of the just-pressed wins [research/07 §3.2; §5.3, K4].
//   * Supports all three GateTrigMode values and batched multi-down-per-tick events
//     (§5.4 K4) [task 152 Scope].
//   * Deterministic; test code (not the audio thread), so allocation is permitted
//     [task 152 Scope].
//
// INDEPENDENCE (the one rule that makes it an oracle): this reference is coded ONLY from
// the §5.3/§5.4 firmware semantics and MUST NOT include or call the production
// `core/voice/KeyAssigner` — it is the thing the production code is diffed against, not a
// wrapper over it [task 152 Scope/Acceptance; ADR-006 C19]. Concretely: it does NOT
// `#include "voice/KeyAssigner.h"`, and it uses a deliberately DIFFERENT internal
// representation (an explicit per-key press-order ledger rather than a std::bitset) so
// the two implementations cannot share a defect by construction.
//
// OUT OF SCOPE (other voice-control tasks): the conformance test DRIVER that diffs the
// production KeyAssigner against this reference (voice-control-5); the production
// KeyAssigner itself (voice-control-3); and POLY/unison, which are exempt from the golden
// trace per ADR-006 C19 [task 152 Out-of-scope].
//
// Header-only: the design tree lists tests/golden/KeyAssignerReference.{h,cpp}, but a
// header-only realization keeps the primitive self-contained and avoids touching the
// shared tests/CMakeLists glob set (which compiles tests/unit/*.cpp; a tests/golden/*.cpp
// is NOT picked up, and editing tests/CMakeLists.txt is forbidden by the parallel-fleet
// conflict-avoidance rule). It is the same pattern as the sibling tests/golden helpers
// (Sha256.h, GoldenKey.h, Stimulus.h, RenderHarness.h).

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../../core/voice/VoiceTypes.h"  // shared PODs ONLY: GateTrigMode, NoteDecision

namespace mw::golden {

// The independent firmware-semantics reference. Distinct internal representation from
// the production KeyAssigner on purpose (see header note): a flat per-key ledger
// recording, for each MIDI note, whether it is held and a monotonically increasing
// press serial, plus a snapshot of the held set as it stood at the end of the prior
// resolved tick (for the GATE+TRIG changed-down XOR).
class KeyAssignerReference {
public:
    static constexpr int kNumKeys = 128;  // full MIDI range (software instrument)

    void prepare() noexcept { reset(); }

    void reset() noexcept {
        for (int n = 0; n < kNumKeys; ++n) {
            held_[static_cast<std::size_t>(n)]      = false;
            prevHeld_[static_cast<std::size_t>(n)]  = false;
            pressSerial_[static_cast<std::size_t>(n)] = 0;
        }
        nextSerial_      = 1;  // 0 reserved for "never pressed"
        lastActive_      = -1;
        gateWasAsserted_ = false;
        newKeyThisTick_  = false;
    }

    void setMode(GateTrigMode m) noexcept { mode_ = m; }
    GateTrigMode mode() const noexcept { return mode_; }

    // Apply note events that land within the current control tick; events are applied
    // in arrival order. Multiple downs in one tick are batched for the last-note XOR
    // (§5.4 K4) because changed-down is only computed against prevHeld_ at resolve().
    void noteOn(int midiNote) noexcept {
        if (!inRange(midiNote)) {
            return;
        }
        const auto i = static_cast<std::size_t>(midiNote);
        if (!held_[i]) {
            // Fresh press: stamp it with an increasing serial so "most-recently
            // pressed still-held" is well-defined independently of the bitset order.
            pressSerial_[i] = nextSerial_++;
        }
        held_[i]        = true;
        newKeyThisTick_ = true;  // a keypress this tick re-phases the clock in LFO (§5.4 K6)
    }

    void noteOff(int midiNote) noexcept {
        if (!inRange(midiNote)) {
            return;
        }
        held_[static_cast<std::size_t>(midiNote)] = false;
    }

    // Resolve priority + trigger for the current control tick. Once per tick (§5.3).
    NoteDecision resolve() noexcept {
        NoteDecision d;

        const bool anyDown = anyHeld();
        d.gate = anyDown;

        // changedDown = held now AND not held in the prior scan (the firmware XOR of
        // newly-changed-down keys) [research/07 §3.2; §5.3 K4].
        const bool hasNewDown = anyChangedDown();

        int active = -1;
        switch (mode_) {
            case GateTrigMode::Gate:
            case GateTrigMode::Lfo:
                // Lowest-note priority: low->high scan, first held wins (K1/K2/K5).
                active = lowestHeld();
                break;
            case GateTrigMode::GateTrig:
                // Last-note priority (K3/K4): if any new key(s) went down this tick,
                // pick the LOWEST of the just-pressed; else keep the most-recent
                // still-held key (by press serial); if it was released, fall back to
                // the lowest still-held.
                if (hasNewDown) {
                    active = lowestChangedDown();
                } else if (lastActive_ >= 0 && held_[static_cast<std::size_t>(lastActive_)]) {
                    active = lastActive_;
                } else {
                    active = mostRecentHeld();
                }
                break;
        }
        d.activeNote = active;

        // Retrigger (§5.4 NORMATIVE table), a function of mode + event only:
        //   Gate (K1/K2):    only on the gate's leading edge (silence -> held); legato
        //                    new keys keep the single held gate, no retrigger.
        //   GateTrig (K3/K4): on the gate edge AND on every tick with a new just-pressed
        //                    key (exactly once per multi-down tick).
        //   Lfo (K5):        never key-retriggered (the ADSR is clock-driven downstream).
        const bool gateEdge = anyDown && !gateWasAsserted_;
        switch (mode_) {
            case GateTrigMode::Gate:
                d.retrigger = gateEdge;
                break;
            case GateTrigMode::GateTrig:
                d.retrigger = gateEdge || (anyDown && hasNewDown);
                break;
            case GateTrigMode::Lfo:
                d.retrigger = false;
                break;
        }

        // CLOCK RESET (§5.4 K6): any new keypress while in LFO mode re-phases the
        // clock/sequence [research/07 §5.2]. (ARP-active is owned at the §9 boundary,
        // not here.)
        d.clockReset = (mode_ == GateTrigMode::Lfo) && newKeyThisTick_ && anyDown;

        // Snapshot for the next tick (§5.3): prevScan = held; remember the resolved
        // active note and gate state; clear the per-tick new-key flag.
        for (int n = 0; n < kNumKeys; ++n) {
            prevHeld_[static_cast<std::size_t>(n)] = held_[static_cast<std::size_t>(n)];
        }
        lastActive_      = active;
        gateWasAsserted_ = anyDown;
        newKeyThisTick_  = false;

        return d;
    }

    bool anyHeld() const noexcept {
        for (int n = 0; n < kNumKeys; ++n) {
            if (held_[static_cast<std::size_t>(n)]) {
                return true;
            }
        }
        return false;
    }

    // --- Batched driver: run a full event/tick script and collect the trace ---------
    //
    // A TickEvents is the set of note events that land within ONE control tick (applied
    // in arrival order, then a single resolve()). An empty TickEvents is a tick with no
    // events (e.g. modeling a release tail or a clock-only advance). This is the shape
    // the conformance driver (voice-control-5) and the self-check tests below feed.

    struct Event {
        enum class Kind : std::uint8_t { On, Off };
        Kind kind;
        int  note;
    };
    using TickEvents = std::vector<Event>;

    // Apply one tick's events in order, then resolve once. Returns the decision.
    NoteDecision applyTick(const TickEvents& events) noexcept {
        for (const Event& e : events) {
            if (e.kind == Event::Kind::On) {
                noteOn(e.note);
            } else {
                noteOff(e.note);
            }
        }
        return resolve();
    }

    // Run a whole script of per-tick event batches from a clean prepare() in the given
    // mode and return the per-tick decision trace. Pure function of (mode, script).
    std::vector<NoteDecision> runScript(GateTrigMode m,
                                        const std::vector<TickEvents>& script) {
        prepare();
        setMode(m);
        std::vector<NoteDecision> trace;
        trace.reserve(script.size());
        for (const TickEvents& tick : script) {
            trace.push_back(applyTick(tick));
        }
        return trace;
    }

private:
    static bool inRange(int midiNote) noexcept {
        return midiNote >= 0 && midiNote < kNumKeys;
    }

    // Lowest held key, low->high scan; -1 if none.
    int lowestHeld() const noexcept {
        for (int n = 0; n < kNumKeys; ++n) {
            if (held_[static_cast<std::size_t>(n)]) {
                return n;
            }
        }
        return -1;
    }

    bool changedDown(int n) const noexcept {
        const auto i = static_cast<std::size_t>(n);
        return held_[i] && !prevHeld_[i];
    }

    bool anyChangedDown() const noexcept {
        for (int n = 0; n < kNumKeys; ++n) {
            if (changedDown(n)) {
                return true;
            }
        }
        return false;
    }

    // Lowest of the just-pressed (changed-down) keys; -1 if none.
    int lowestChangedDown() const noexcept {
        for (int n = 0; n < kNumKeys; ++n) {
            if (changedDown(n)) {
                return n;
            }
        }
        return -1;
    }

    // The still-held key with the largest press serial (most recently pressed). This is
    // the deterministic "last note" fallback when no new key went down this tick. -1 if
    // nothing held.
    int mostRecentHeld() const noexcept {
        int best = -1;
        std::uint64_t bestSerial = 0;
        for (int n = 0; n < kNumKeys; ++n) {
            const auto i = static_cast<std::size_t>(n);
            if (held_[i] && pressSerial_[i] >= bestSerial) {
                bestSerial = pressSerial_[i];
                best       = n;
            }
        }
        return best;
    }

    std::array<bool, kNumKeys>          held_{};
    std::array<bool, kNumKeys>          prevHeld_{};
    std::array<std::uint64_t, kNumKeys> pressSerial_{};
    std::uint64_t nextSerial_     = 1;
    GateTrigMode  mode_           = GateTrigMode::GateTrig;
    int  lastActive_              = -1;
    bool gateWasAsserted_         = false;
    bool newKeyThisTick_          = false;
};

}  // namespace mw::golden
