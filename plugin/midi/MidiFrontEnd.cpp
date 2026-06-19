// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/MidiFrontEnd.cpp — implementation of the note/gate/bend/pressure/CC
// translator (task 104). See MidiFrontEnd.h for scope + the real-time invariants.
// Realizes docs/design/09 §4.1-4.5 / §6.4 and ADR-012 C8/C9/C21-C24 / ADR-016 R-2.
//
// Every method below is allocation-free and lock-free: processMidi() only reads the
// borrowed juce::MidiBuffer, indexes the CcLearnMap through its single atomic load, and
// pushes into the caller's pre-sized NormalizedEventBuffer (drop-never-grow); the
// per-sample de-zipper ticks live in the header and only touch POD smoother state
// [docs/design/09 §1.3; ADR-011 C9; ADR-012 C24].

#include "midi/MidiFrontEnd.h"

#include <algorithm>
#include <string_view>

#include "params/ParamDefs.h"   // mw::params::kParamDefs — the frozen doc-06 registry

namespace mw::plugin {

namespace cal = mw::cal::midifront;

namespace {

// Resolve a doc-06 string ID to its index in the kParamDefs registry at COMPILE time
// (the same consteval scan CcLearnMap uses). Returns -1 if the ID is absent — caught by
// the static_asserts below so a renamed/removed ID is a build error [§4.5; §6.2].
consteval std::int32_t paramIndexOf(std::string_view id) noexcept {
    for (std::size_t i = 0; i < mw::params::kParamDefs.size(); ++i)
        if (std::string_view{ mw::params::kParamDefs[i].id } == id)
            return static_cast<std::int32_t>(i);
    return -1;
}

// The two documented velocity-routing destinations [§4.5; ADR-016 R-2; docs/research/08
// §7.2, §5.3]: VCA level and VCF cutoff amount. Resolved once, at compile time.
inline constexpr std::int32_t kVcaLevelIndex  = paramIndexOf("mw101.vca.level");
inline constexpr std::int32_t kVcfCutoffIndex = paramIndexOf("mw101.vcf.cutoff");

static_assert(kVcaLevelIndex >= 0,
              "MidiFrontEnd: velocity routing target mw101.vca.level is not in kParamDefs "
              "(param ID renamed/removed?) [docs/design/09 §4.5].");
static_assert(kVcfCutoffIndex >= 0,
              "MidiFrontEnd: velocity routing target mw101.vcf.cutoff is not in kParamDefs "
              "(param ID renamed/removed?) [docs/design/09 §4.5].");

// Push a ParamValue HostEvent (paramIndex carried in data0; value verbatim). Returns the
// push() result so the caller respects the drop-never-grow overflow policy [§3.2].
inline bool pushParamValue(NormalizedEventBuffer& out, std::int32_t paramIndex,
                           float value, std::uint8_t channel, std::int32_t sampleOffset) noexcept {
    HostEvent e{};
    e.type         = HostEventType::ParamValue;
    e.channel      = channel;
    e.sampleOffset = sampleOffset;
    e.data0        = paramIndex;     // index into the doc-06 registry (or HOLD sentinel)
    e.value        = value;
    e.noteId       = -1;             // MIDI-derived
    return out.push(e);
}

} // namespace

// -----------------------------------------------------------------------------------

std::int32_t MidiFrontEnd::vcaLevelParamIndex() noexcept  { return kVcaLevelIndex; }
std::int32_t MidiFrontEnd::vcfCutoffParamIndex() noexcept { return kVcfCutoffIndex; }

void MidiFrontEnd::prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
    // The de-zipper ticks once per SAMPLE, so the smoother's tick rate is the sample
    // rate. Configure the per-class time constants (bend ~Pitch, pressure ~Fast) into
    // the one-pole coefficient off the audio thread [§6.4; ADR-020].
    bendSmoother_.prepare(cal::kBendSmoothSeconds, sampleRate);
    channelPressureSmoother_.prepare(cal::kPressureSmoothSeconds, sampleRate);
    reset();
}

void MidiFrontEnd::reset() noexcept {
    // Snap both de-zippers to rest (no transient) and re-seed the active parameter
    // state to the documented defaults [§4.3-4.5; §5; ADR-012 C8/C21-C23; ADR-016 R-2].
    bendSmoother_.reset(static_cast<double>(cal::kBendRestSemis));
    channelPressureSmoother_.reset(static_cast<double>(cal::kPressureRestNorm));

    channelBendSemis_   = cal::kDefaultChannelBendSemis;
    mpeNoteBendSemis_    = cal::kDefaultMpeNoteBendSemis;
    mpeMasterBendSemis_ = cal::kDefaultMpeMasterBendSemis;
    a4Hz_               = cal::kDefaultA4Hz;
    tuneCents_          = cal::kDefaultTuneCents;
    modernUnquantized_  = false;
    velocityEnabled_    = cal::kDefaultVelocityEnabled;
}

void MidiFrontEnd::setTuning(float a4Hz, float tuneCents) noexcept {
    // A4 clamps to ~400..460 Hz; TUNE is a separate ±kTuneRangeSemis-semitone control
    // [§5; ADR-012 C21-C23].
    a4Hz_      = std::clamp(a4Hz, cal::kA4MinHz, cal::kA4MaxHz);
    const float tuneMaxCents = cal::kTuneRangeSemis * 100.0f;   // ±1 semitone == ±100 cents
    tuneCents_ = std::clamp(tuneCents, -tuneMaxCents, tuneMaxCents);
}

void MidiFrontEnd::setBendRange(float channelSemis, float mpeNoteSemis,
                                float mpeMasterSemis) noexcept {
    // Channel bend 0..24, MPE per-note + master 0..96 [§4.4; ADR-012 C8, C11].
    channelBendSemis_   = std::clamp(channelSemis,  cal::kChannelBendMinSemis, cal::kChannelBendMaxSemis);
    mpeNoteBendSemis_    = std::clamp(mpeNoteSemis,   cal::kMpeBendMinSemis,     cal::kMpeBendMaxSemis);
    mpeMasterBendSemis_ = std::clamp(mpeMasterSemis, cal::kMpeBendMinSemis,     cal::kMpeBendMaxSemis);
}

void MidiFrontEnd::setModernUnquantized(bool on) noexcept {
    modernUnquantized_ = on;
}

float MidiFrontEnd::bendTargetSemis() const noexcept {
    return static_cast<float>(bendSmoother_.target());
}

float MidiFrontEnd::pressureTargetNorm() const noexcept {
    return static_cast<float>(channelPressureSmoother_.target());
}

void MidiFrontEnd::processMidi(const juce::MidiBuffer& midi,
                               const CcLearnMap& map,
                               NoteExpressionRung /*neRung*/,
                               NormalizedEventBuffer& out) noexcept {
    // Drain the borrowed buffer (JUCE delivers it ordered by sampleOffset). No
    // allocation: we only push into the caller's pre-sized out buffer and read the
    // atomic CcLearnMap pointer [§4.1; §1.3].
    for (const auto meta : midi) {
        const juce::MidiMessage& m = meta.getMessage();
        const int sampleOffset = meta.samplePosition;
        const auto channel = static_cast<std::uint8_t>(std::max(0, m.getChannel()));  // 1..16; 0 if none

        if (m.isNoteOn()) {
            const float vel = m.getFloatVelocity();   // 0..1

            HostEvent on{};
            on.type         = HostEventType::NoteOn;
            on.channel      = channel;
            on.sampleOffset = sampleOffset;
            on.data0        = m.getNoteNumber();
            on.value        = vel;
            on.noteId       = -1;
            out.push(on);

            // Velocity ON (default) -> per-note VCA-level + VCF-cutoff offsets; the
            // no-velocity switch disables both [§4.5; ADR-016 R-2]. The offset is signed
            // about the nominal centre so mezzo-forte contributes ~0 and the routing is
            // additive over the real circuitry.
            if (velocityEnabled_) {
                const float signedVel = vel - cal::kVelocityCentre;
                pushParamValue(out, kVcaLevelIndex,  signedVel * cal::kVelToVcaLevelDepth,
                               channel, sampleOffset);
                pushParamValue(out, kVcfCutoffIndex, signedVel * cal::kVelToVcfCutoffDepth,
                               channel, sampleOffset);
            }
            continue;
        }

        if (m.isNoteOff()) {
            HostEvent off{};
            off.type         = HostEventType::NoteOff;
            off.channel      = channel;
            off.sampleOffset = sampleOffset;
            off.data0        = m.getNoteNumber();
            off.value        = m.getFloatVelocity();
            off.noteId       = -1;
            out.push(off);
            continue;
        }

        if (m.isPitchWheel()) {
            // 14-bit wheel -> signed unit [-1,+1] -> continuous Pre-Q offset (semitones)
            // via the active channel bend range [§4.4; ADR-012 C8].
            const float unit = (static_cast<float>(m.getPitchWheelValue()) - cal::kPitchWheelCentre)
                             / cal::kPitchWheelHalfSpan;
            const float semis = bendUnitToSemis(unit);
            bendSmoother_.setTarget(static_cast<double>(semis));   // O(1)/sample de-zipper TARGET only

            HostEvent bend{};
            bend.type         = HostEventType::PitchBend;
            bend.channel      = channel;
            bend.sampleOffset = sampleOffset;
            bend.data0        = 0;
            bend.value        = semis;        // continuous, signed, Pre-Q (semitones)
            bend.noteId       = -1;
            out.push(bend);
            continue;
        }

        if (m.isChannelPressure()) {
            const float norm = static_cast<float>(m.getChannelPressureValue()) / cal::kSevenBitMax;
            channelPressureSmoother_.setTarget(static_cast<double>(norm));  // de-zipper TARGET only

            HostEvent cp{};
            cp.type         = HostEventType::ChannelPressure;
            cp.channel      = channel;
            cp.sampleOffset = sampleOffset;
            cp.data0        = 0;
            cp.value        = norm;
            cp.noteId       = -1;
            out.push(cp);
            continue;
        }

        if (m.isAftertouch()) {
            HostEvent pp{};
            pp.type         = HostEventType::PolyPressure;
            pp.channel      = channel;
            pp.sampleOffset = sampleOffset;
            pp.data0        = m.getNoteNumber();
            pp.value        = static_cast<float>(m.getAfterTouchValue()) / cal::kSevenBitMax;
            pp.noteId       = -1;
            out.push(pp);
            continue;
        }

        if (m.isController()) {
            // Resolve the CC number through the learn map to a doc-06 param index (or the
            // HOLD sentinel for CC64). An unmapped / disabled CC returns kUnmapped (-1)
            // and is DROPPED — no CC invents a control the hardware lacks [§6.2-6.3;
            // ADR-012 C15, C20]. The lookup is branch-free + lock-free (single atomic load).
            const auto ccNumber = static_cast<std::uint8_t>(m.getControllerNumber());
            const std::int32_t paramIndex = map.lookup(ccNumber);
            if (paramIndex != CcLearnMap::kUnmapped) {
                const float norm = static_cast<float>(m.getControllerValue()) / cal::kSevenBitMax;
                pushParamValue(out, paramIndex, norm, channel, sampleOffset);
            }
            continue;
        }

        // Program change, clock, sysex, etc. are not ingested by this front-end here
        // (program change is consumed in plugin/ for preset recall; clock is the
        // transport path, task 102) [§3.3].
    }
}

} // namespace mw::plugin
