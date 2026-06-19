// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/midi/TuningBendWiring.cpp — implementation of the doc-06 APVTS -> MidiFrontEnd
// tuning + bend-range wiring (task 104b). See TuningBendWiring.h for scope + the
// real-time invariants. Realizes docs/design/09 §5 / §4.4 and ADR-012 C8 / C11 / C13 /
// C21-C23 / §Decision item 7.
//
// prepare() does the ONLY non-RT work (the APVTS string lookups, off the audio thread);
// apply() is allocation-free + lock-free: one relaxed atomic load per wired param, pure
// arithmetic, and the noexcept MidiFrontEnd setters [§5.4; §1.3; ADR-011 C9].

#include "midi/TuningBendWiring.h"

#include <string_view>

#include "params/ParamDefs.h"   // mw::params::kParamDefs — the frozen doc-06 registry
#include "../../core/calibration/TuningBendWiringConstants.h"  // mw::cal::tunebend::* named consts

namespace mw::plugin {

namespace cal = mw::cal::tunebend;

namespace {

// Compile-time guard: the doc-06 string IDs this wiring reads MUST exist in the frozen
// registry, so a renamed/removed ID is a BUILD error, not a silent mis-wire [§4.5; §6.2].
// (The same consteval scan MidiFrontEnd / CcLearnMap use.)
consteval bool registryHasId(std::string_view id) noexcept {
    for (const auto& def : mw::params::kParamDefs)
        if (std::string_view{ def.id } == id)
            return true;
    return false;
}

// The five doc-06 params this wiring binds, taken VERBATIM from the registry [doc-06 §3.0].
inline constexpr const char* kIdA4         = "mw101.tune.a4";
inline constexpr const char* kIdFine       = "mw101.vco.fine";
inline constexpr const char* kIdChanBend   = "mw101.mod.bend_range_vco";
inline constexpr const char* kIdMpeBend    = "mw101.mpe.bend_range";
inline constexpr const char* kIdVoiceMode  = "mw101.voice.mode";

static_assert(registryHasId(kIdA4),        "TuningBendWiring: mw101.tune.a4 not in kParamDefs [doc-06 §3.0].");
static_assert(registryHasId(kIdFine),      "TuningBendWiring: mw101.vco.fine not in kParamDefs [doc-06 §3.0].");
static_assert(registryHasId(kIdChanBend),  "TuningBendWiring: mw101.mod.bend_range_vco not in kParamDefs [doc-06 §3.0].");
static_assert(registryHasId(kIdMpeBend),   "TuningBendWiring: mw101.mpe.bend_range not in kParamDefs [doc-06 §3.0].");
static_assert(registryHasId(kIdVoiceMode), "TuningBendWiring: mw101.voice.mode not in kParamDefs [doc-06 §3.0].");

} // namespace

// -----------------------------------------------------------------------------------

void TuningBendWiring::prepare(juce::AudioProcessorValueTreeState& apvts)
{
    // Resolve the doc-06 string IDs to their RAW (engineering-unit) atomic pointers ONCE,
    // off the audio thread. getRawParameterValue returns the engineering value the host
    // writes (Hz / cents / semitones / choice index) — exactly what setTuning/setBendRange
    // and the mono decision want — so apply() needs no NormalisableRange conversion [§5.4].
    a4Hz_          = apvts.getRawParameterValue(kIdA4);
    fineSemis_     = apvts.getRawParameterValue(kIdFine);
    chanBendCents_ = apvts.getRawParameterValue(kIdChanBend);
    mpeBendSemis_  = apvts.getRawParameterValue(kIdMpeBend);
    voiceMode_     = apvts.getRawParameterValue(kIdVoiceMode);

    // The layout is a pure function of kParamDefs (buildParameterLayout), so every wired
    // id resolves; assert loudly if the layout ever drifts from the registry.
    jassert(a4Hz_          != nullptr);
    jassert(fineSemis_     != nullptr);
    jassert(chanBendCents_ != nullptr);
    jassert(mpeBendSemis_  != nullptr);
    jassert(voiceMode_     != nullptr);

    prepared_ = true;
}

void TuningBendWiring::apply(MidiFrontEnd& fe) noexcept
{
    // --- Tuning (A4 + TUNE) [§5; ADR-012 C21-C23] -------------------------------------
    //
    // The master reference is the single mw101.tune.a4 float param. An OPTIONAL MTS-ESP
    // reference overrides it ONLY when both present and cheap; otherwise we defer to the
    // param [ADR-012 §Decision item 7]. setTuning re-clamps A4 to its 400..460 span.
    float a4 = read(a4Hz_);
    if (provider_ != nullptr && provider_->overrides())
        a4 = provider_->a4Hz;                       // honoured MTS-ESP master

    // TUNE: mw101.vco.fine is ±1.0 SEMITONE; setTuning takes CENTS (×100) [ADR-012 C23].
    const float tuneCents = read(fineSemis_) * cal::kCentsPerSemitone;
    fe.setTuning(a4, tuneCents);

    // --- Bend ranges [§4.4; ADR-012 C8, C11] ------------------------------------------
    //
    // Channel bend: mw101.mod.bend_range_vco is CENTS; setBendRange takes SEMITONES
    // (÷100). MPE per-note AND master both come from the single mw101.mpe.bend_range
    // (semitones); both default ±48 [ADR-012 C11]. setBendRange re-clamps each to its
    // documented span (channel 0..24, MPE 0..96). All three scale the SAME continuous
    // Pre-Q offset path inside MidiFrontEnd [§4.4].
    const float channelSemis = read(chanBendCents_) / cal::kCentsPerSemitone;
    const float mpeSemis      = read(mpeBendSemis_);
    fe.setBendRange(channelSemis, /*mpeNote=*/mpeSemis, /*mpeMaster=*/mpeSemis);

    // --- Mono-collapse decision [§4.4; ADR-012 C13] -----------------------------------
    //
    // mw101.voice.mode == Mono (index 0) -> MPE collapses to channel bend + channel
    // pressure. The raw atomic IS the choice index; round defensively. The collapse
    // itself is enacted by the voice / MPE-reconstruction layer (task 103) reading this
    // decision; the same Pre-Q offset path serves both routings [§4.4].
    const int mode = static_cast<int>(read(voiceMode_) + 0.5f);
    monoCollapse_ = (mode == cal::kVoiceModeMonoIndex);
}

} // namespace mw::plugin
