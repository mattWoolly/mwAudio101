// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/TuningBendWiring.h — the doc-06 APVTS -> MidiFrontEnd tuning + bend-range
// wiring (task 104b). Realizes docs/design/09 §5 / §4.4 and ADR-012 C8 / C11 / C13 /
// C21-C23 / §Decision item 7.
//
// WHAT THIS DOES. TuningBendWiring is the thin, separately-tested adapter that bridges
// the parameter side (the doc-06 APVTS params) into the continuous Pre-Q pitch-offset
// surface that MidiFrontEnd::setTuning / setBendRange expose (task 104). It does NOT
// own the bend math or the offset path — that lives in MidiFrontEnd — it only marshals
// the right parameters into the right setter calls each block:
//
//   * mw101.tune.a4 (400..460 Hz, default 440) -> the A4 reference; 442 is recalled via
//     a 'hardware-accurate' preset through this same param, never the default
//     [docs/design/09 §5; ADR-012 C21-C23];
//   * mw101.vco.fine (±1.0 semitone, the sole fine-tune) -> the front-panel TUNE,
//     converted to cents (×100) [docs/design/09 §5; ADR-012 C23];
//   * mw101.mod.bend_range_vco (0..1200 CENTS, default 200 == ±2 semitones) -> the
//     channel bend range, converted to semitones (÷100) [docs/design/09 §4.4; ADR-012 C8];
//   * mw101.mpe.bend_range (0..96 semitones, default 48) -> BOTH the MPE per-note and
//     MPE master bend range (the MPE-spec default is ±48 for both) [ADR-012 C11];
//   * mw101.voice.mode == Mono -> the MPE-collapse decision: in mono, MPE collapses to
//     channel bend + channel pressure [docs/design/09 §4.4; ADR-012 C13];
//   * an OPTIONAL, clearly-gated MTS-ESP / MIDI-Tuning-Standard reference (TuningProvider)
//     overrides the A4 reference ONLY when it is both present and cheap; otherwise the
//     single A4 float param remains authoritative [docs/design/09 §5; ADR-012 §Decision
//     item 7].
//
// Per-channel channel bend (raw MIDI) and per-note / master bend (MPE via task 103) all
// route through the SAME continuous Pre-Q offset path inside MidiFrontEnd; this wiring
// just sets the three ranges that scale that one path [docs/design/09 §4.4].
//
// REAL-TIME INVARIANTS [docs/design/09 §1.3; ADR-001 C7; ADR-011 C9]. prepare(apvts) is
// the SOLE non-RT step: it resolves the doc-06 string IDs to their APVTS atomic pointers
// once, off the audio thread (the same id->atomic table pattern ParamBridge uses), so
// apply() never touches an APVTS string map. apply() is noexcept, allocation-free and
// lock-free: one relaxed std::atomic load per wired param + pure arithmetic + the
// noexcept MidiFrontEnd setters.

#pragma once

#include <atomic>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

#include "midi/MidiFrontEnd.h"   // mw::plugin::MidiFrontEnd (the offset path it drives)

namespace mw::plugin {

// Optional MTS-ESP / MIDI-Tuning-Standard reference, consulted by TuningBendWiring ONLY
// when both present and cheap; otherwise the single mw101.tune.a4 float param is
// authoritative [docs/design/09 §5; ADR-012 §Decision item 7]. A trivially-copyable POD
// the host fills in off the audio thread (a snapshot of the current MTS-ESP state) and
// hands to the wiring by borrowed pointer; apply() only reads it. Modelled as a plain
// value here (not a hard MTS-ESP SDK dependency) so the gate is honest and testable —
// the actual MTS-ESP client is out of this task's scope.
struct TuningProvider
{
    bool  present = false;     // is an MTS-ESP master active right now?
    bool  cheap   = true;      // is reading it cheap (no per-block cost)?  Honor only if so.
    float a4Hz    = 440.0f;    // the reference A4 it supplies, when honoured.

    // True iff this provider should override the A4 param this block.
    [[nodiscard]] bool overrides() const noexcept { return present && cheap; }
};

class TuningBendWiring
{
public:
    TuningBendWiring() = default;

    // Resolve the doc-06 param string IDs to their APVTS atomic pointers ONCE, off the
    // audio thread (from prepareToPlay). Idempotent / re-callable. NOT RT-safe (it
    // touches the APVTS string-keyed lookup); never call from process() [§5.4; §1.3].
    void prepare(juce::AudioProcessorValueTreeState& apvts);

    // Attach / detach an OPTIONAL MTS-ESP tuning reference. The wiring borrows the
    // pointer (does not own it); pass nullptr to detach. Safe to call off the audio
    // thread; apply() reads through it lock-free.
    void setTuningProvider(const TuningProvider* provider) noexcept { provider_ = provider; }
    [[nodiscard]] bool hasTuningProvider() const noexcept { return provider_ != nullptr; }

    // Read the wired params (one relaxed atomic load each) and drive
    // MidiFrontEnd::setTuning / setBendRange. RT-safe: noexcept, no alloc, no lock,
    // no string lookup. Call once per block on the audio thread [§5.4; §1.3]. Also
    // updates the mono-collapse decision (queryable via monoCollapse()).
    void apply(MidiFrontEnd& fe) noexcept;

    // The last-applied MPE-collapse decision: true iff mw101.voice.mode == Mono, in
    // which case MPE collapses to channel bend + channel pressure [ADR-012 C13].
    [[nodiscard]] bool monoCollapse() const noexcept { return monoCollapse_; }

    // True once prepare() has bound every wired atomic.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // Read one cached atomic (relaxed); 0 if unbound. Pure helper, RT-safe.
    [[nodiscard]] float read(const std::atomic<float>* a) const noexcept {
        return a != nullptr ? a->load(std::memory_order_relaxed) : 0.0f;
    }

    // Cached APVTS atomic pointers (borrowed; APVTS owns them). Bound in prepare().
    const std::atomic<float>* a4Hz_         = nullptr;   // mw101.tune.a4         (Hz)
    const std::atomic<float>* fineSemis_    = nullptr;   // mw101.vco.fine        (semitones)
    const std::atomic<float>* chanBendCents_= nullptr;   // mw101.mod.bend_range_vco (cents)
    const std::atomic<float>* mpeBendSemis_ = nullptr;   // mw101.mpe.bend_range  (semitones)
    const std::atomic<float>* voiceMode_    = nullptr;   // mw101.voice.mode      (choice index)

    const TuningProvider* provider_ = nullptr;           // optional MTS-ESP gate (borrowed)

    bool monoCollapse_ = true;   // mono is the stock default voice mode
    bool prepared_     = false;
};

} // namespace mw::plugin
