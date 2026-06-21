// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/MidiFrontEnd.h — the note/gate/bend/pressure/CC -> normalized HostEvent
// translator (task 104). Realizes docs/design/09 §4.1-4.5 / §6.4 and ADR-012 C8/C9/
// C21-C24 / ADR-016 R-2 / ADR-022 C1-C3.
//
// WHAT THIS DOES. processMidi() drains a juce::MidiBuffer (already in the wrapper's
// MPE / raw form) plus the resolved note-expression rung into the fixed-capacity,
// lock-free NormalizedEventBuffer (docs/design/09 §3.2). It:
//   * routes note-on/off and pitch-bend/pressure into normalized HostEvents
//     (the key-assigner / DAC-CV model lives in core; this is the clone-layer
//     translator, never a parallel control path) [docs/design/09 §4];
//   * decodes the 14-bit channel pitch-wheel to a CONTINUOUS signed Pre-Q pitch
//     offset (semitones) via the active channel bend range (default ±2, range 0..24)
//     [docs/design/09 §4.4; ADR-012 C8];
//   * resolves every CC number through the §6 CcLearnMap to a doc-06 param index and
//     pushes it as a ParamValue HostEvent (raw, unmapped CCs are dropped; CC64 -> HOLD
//     sentinel is forwarded as a ParamValue carrying the sentinel index) [§6.2-6.3];
//   * when velocity is ON (the ADR-016 R-2 default) emits per-note VCA-level + VCF-
//     cutoff-amount ParamValue offsets; the no-velocity switch disables both [§4.5];
//   * exposes O(1)/sample one-pole de-zippers for bend + channel pressure with NO
//     branch on message arrival (a message sets the smoother TARGET; tickBend() /
//     tickPressure() advance one sample every sample regardless) [§6.4; ADR-012 C24].
//
// REAL-TIME INVARIANTS [docs/design/09 §1.3; ADR-011 C9; ADR-012 C24]. prepare() is
// the SOLE sizing point (it configures the smoother coefficients off the audio
// thread). processMidi() and the per-sample ticks are noexcept, allocation-free, and
// lock-free: the NormalizedEventBuffer is pre-sized and drops-never-grows, the
// CcLearnMap is read through its single atomic-pointer load, and the smoothers own
// only POD state.
//
// OUT OF SCOPE (other tasks): the 6-bit quantizer / CV scaling (core); MPE per-channel
// reconstruction (MpeReconstructor, task 103); the key-assigner DSP (voice);
// HostEvent -> mw::core::MidiEvent translation (plugin-8). This class assembles the
// continuous offsets and normalized events; core consumes them.

#pragma once

#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>   // juce::MidiBuffer / juce::MidiMessage

#include "host/Capabilities.h"   // mw::plugin::NoteExpressionRung (§7.2)
#include "host/HostEvent.h"      // mw::plugin::HostEvent / NormalizedEventBuffer (§3.2)
#include "midi/CcLearnMap.h"     // mw::plugin::CcLearnMap (§6.3)

#include "params/Smoother.h"     // mw::params::OnePoleSmoother — the canonical de-zipper (task 008)
#include "calibration/MidiFrontEndConstants.h"   // mw::cal::midifront::* (PI) constants
#include "calibration/ControlDispatchCcIngressConstants.h"   // mw::cal::ccingress::kModWheelCcNumber (task 162c)

namespace mw::plugin {

class MidiFrontEnd {
public:
    // --- Resolved doc-06 param indices the velocity router emits against ----------
    // Resolved at COMPILE time from the frozen kParamDefs registry (defined in the .cpp)
    // so a renamed/removed ID is a build error, not a silent mis-route [§4.5].
    [[nodiscard]] static std::int32_t vcaLevelParamIndex() noexcept;
    [[nodiscard]] static std::int32_t vcfCutoffParamIndex() noexcept;

    // Sizes the bend/pressure de-zipper coefficients for the given sample rate (the
    // per-sample tick rate) and resets all state. Off the audio thread; the SOLE
    // sizing point [docs/design/09 §1.3]. maxBlockSize is accepted for the §4.1
    // signature parity but the front-end stores no per-block scratch of its own (the
    // caller owns the NormalizedEventBuffer).
    void prepare(double sampleRate, int maxBlockSize) noexcept;

    // Snap bend + pressure to their rest values (no transient) and re-seed the active
    // parameter state to the documented defaults. RT-safe.
    void reset() noexcept;

    // Drain the wrapper's juce::MidiBuffer + the resolved capability rung into
    // normalized HostEvents. RT-safe: no alloc, no lock [docs/design/09 §4.1].
    // neRung selects how per-note expression is sourced; this task forwards channel-
    // global bend/pressure and notes/CC, and updates the de-zipper TARGETS. (Per-channel
    // MPE reconstruction is task 103; the rung is plumbed here for the §7.2 contract.)
    void processMidi(const juce::MidiBuffer& midi,
                     const CcLearnMap& map,
                     NoteExpressionRung neRung,
                     NormalizedEventBuffer& out) noexcept;

    // --- Parameter setters (read from doc-06 params; stored here) -----------------
    void setTuning(float a4Hz, float tuneCents) noexcept;                  // §5
    void setBendRange(float channelSemis, float mpeNoteSemis,
                      float mpeMasterSemis) noexcept;                      // §4.4
    void setModernUnquantized(bool on) noexcept;                          // §4.3 C7
    // Velocity ON/OFF switch + routing depth [§4.5; ADR-016 R-2]. Default ON.
    void setVelocityEnabled(bool on) noexcept { velocityEnabled_ = on; }

    // --- Continuous-offset query surface + the O(1)/sample de-zipper --------------
    // The de-zipper TARGETs (set by the last bend / pressure message). No smoothing
    // transient — these are where tickBend()/tickPressure() are converging toward.
    [[nodiscard]] float bendTargetSemis()    const noexcept;
    [[nodiscard]] float pressureTargetNorm() const noexcept;

    // Advance the bend / pressure de-zipper ONE sample and return the smoothed value.
    // Called once per sample on the audio thread; there is NO branch on whether a
    // message arrived this sample — the smoother always advances [§6.4; ADR-012 C24].
    float tickBendSemis()    noexcept { return static_cast<float>(bendSmoother_.process()); }
    float tickPressureNorm() noexcept { return static_cast<float>(channelPressureSmoother_.process()); }

    // The current (post-tick) smoothed continuous values.
    [[nodiscard]] float currentBendSemis()    const noexcept { return static_cast<float>(bendSmoother_.current()); }
    [[nodiscard]] float currentPressureNorm() const noexcept { return static_cast<float>(channelPressureSmoother_.current()); }

    // --- Stored parameter accessors (for tests / the engine adapter) --------------
    [[nodiscard]] float channelBendRangeSemis() const noexcept { return channelBendSemis_; }
    [[nodiscard]] float mpeNoteBendRangeSemis() const noexcept { return mpeNoteBendSemis_; }
    [[nodiscard]] float mpeMasterBendRangeSemis() const noexcept { return mpeMasterBendSemis_; }
    [[nodiscard]] float a4Hz()           const noexcept { return a4Hz_; }
    [[nodiscard]] float tuneCents()      const noexcept { return tuneCents_; }
    [[nodiscard]] bool  modernUnquantized() const noexcept { return modernUnquantized_; }
    [[nodiscard]] bool  velocityEnabled()   const noexcept { return velocityEnabled_; }

    // Map a signed unit pitch-wheel offset [-1,+1] to a continuous Pre-Q pitch offset
    // (semitones) via the active channel bend range. Pure helper [§4.4; ADR-012 C8].
    [[nodiscard]] float bendUnitToSemis(float unitOffset) const noexcept {
        return unitOffset * channelBendSemis_;
    }

    // --- Live continuous-controller POSITION for the core ingress seam (task 162d) ----
    // processMidi() tracks the LATEST pitch-wheel + CC1 (mod-wheel) message it saw this
    // block as the RAW controller position the core continuous-controller seam consumes:
    //   * the pitch-wheel as a CENTERED signed unit [-1,+1] (0 == centered; this is the
    //     SAME value mw::ContinuousControllers::pitchBend / a core PitchBend MidiEvent
    //     carries — NOT the bend-range-scaled semitone offset the PitchBend HostEvent
    //     forwards for the §4.4 Pre-Q path, which stays unchanged);
    //   * the mod-wheel as the [0,1] CC1 value (raw 7-bit / 127).
    // The processor reads these AFTER processMidi() and writes them into
    // BlockContext::controllers each block so the engine's 162c bend->{VCO,VCF} and
    // mod-wheel->LFO-depth legs activate END-TO-END (they were inert in the plugin before
    // this task). The values PERSIST across blocks (a real wheel holds its position until
    // the next message), so a block with no controller message keeps the last held position.
    // Message arrival sets these in processMidi(); reset() snaps them to the neutral identity.
    [[nodiscard]] float liveBendUnit()    const noexcept { return liveBendUnit_; }
    [[nodiscard]] float liveModWheelNorm() const noexcept { return liveModWheelNorm_; }

private:
    // Fixed-cost one-pole de-zippers (O(1)/sample) — the canonical smoother (task 008).
    // Bend rides the Pitch de-zipper class, channel pressure the Fast class [§6.4].
    mw::params::OnePoleSmoother bendSmoother_{};
    mw::params::OnePoleSmoother channelPressureSmoother_{};

    // Continuous bend ranges (semitones) [§4.4; ADR-012 C8, C11].
    float channelBendSemis_   = mw::cal::midifront::kDefaultChannelBendSemis;
    float mpeNoteBendSemis_   = mw::cal::midifront::kDefaultMpeNoteBendSemis;
    float mpeMasterBendSemis_ = mw::cal::midifront::kDefaultMpeMasterBendSemis;

    // Tuning reference + TUNE [§5; ADR-012 C21-C23].
    float a4Hz_      = mw::cal::midifront::kDefaultA4Hz;
    float tuneCents_ = mw::cal::midifront::kDefaultTuneCents;

    // Pitch quantizer escape hatch (OFF by default) [§4.3 C7].
    bool  modernUnquantized_ = false;

    // Velocity switch (ON by default) [§4.5; ADR-016 R-2].
    bool  velocityEnabled_ = mw::cal::midifront::kDefaultVelocityEnabled;

    // --- Live continuous-controller POSITION held for the core ingress seam (task 162d) ---
    // The latest pitch-wheel position as a CENTERED signed unit [-1,+1] (0 == centered) and
    // the latest CC1 mod-wheel value normalized to [0,1]. Updated on the matching message in
    // processMidi(); both at the neutral no-controller identity by default + after reset().
    float liveBendUnit_     = 0.0f;   // [-1,+1]; 0 == centered (neutral)
    float liveModWheelNorm_ = 0.0f;   // [0,1];   0 == wheel down (neutral)
};

} // namespace mw::plugin
