// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/ParamDefs.h — THE single declarative parameter registry (task 019).
//
// Declares one constexpr array `kParamDefs` with one entry per LIVE parameter ID in
// the canonical §3.0 index (91 live AudioProcessorParameters), plus the deprecated
// `mw101.os.factor` migration-alias slot (kOsFactorAlias, NOT a live entry). This is
// the SINGLE place a parameter is declared; the APVTS layout (params-4), the preset
// (de)serializer, the CI validators and the docs reference are all a pure function of
// this table [docs/design/06 §3.0, §3.1; ADR-008 §1].
//
// JUCE-FREE: this header is pure C++20 POD/constexpr data — no juce::* type appears.
// The juce ParameterLayout build (buildParameterLayout) is a separate plugin-stream
// task (params-4) that consumes this table [docs/design/00 §3.3; ADR-001 C1].
//
// Numeric discipline: ranges/skews/defaults that are (PI) are referenced by NAME from
// the calibration table (core/calibration/ParamDefsConstants.h, which extends
// Calibration.h); none is inlined here [docs/design/06 §3.10; ADR-020 S13].
//
// The §3.1 invariants are enforced at COMPILE time by the consteval/static_assert
// block at the bottom (IDs unique + mw101.-prefixed; choice params have
// choices != nullptr and choiceCount >= canonicalChoiceCount; structural params are
// non-automatable + NoSmooth; software-ext indices sit >= canonicalChoiceCount) and
// re-verified at runtime by tests/unit/ParamDefsTest.cpp.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "../calibration/ParamDefsConstants.h"
#include "SmoothingClass.h"

namespace mw::params {

// §3.1 — the registry entry value types.
enum class ParamType : std::uint8_t { Continuous, Choice, Bool };

enum class ParamGroup : std::uint8_t {
    Vco, Sub, Noise, Mixer, Vcf, Env, Lfo, Vca,
    Glide, Mod, Arp, Seq, Key, Tune, Vel, Mpe,
    Vintage, Drift, Var, Warmup, Fx, Out, Voice, Global
};

// §3.1 — one declarative entry per parameter.
struct ParamDef {
    const char*    id{};                    // immutable "mw101.*" snake_case  [ADR-008 C1]
    const char*    label{};                 // advisory display name; may change freely
    ParamGroup     group{ ParamGroup::Global };
    ParamType      type{ ParamType::Continuous };
    float          minValue{ 0.0f };        // normalized modeled units (Continuous)
    float          maxValue{ 1.0f };
    float          step{ 0.0f };            // 0 => continuous; Choice uses choiceCount
    float          defaultValue{ 0.0f };    // engine default (NOT the INIT patch; §11)
    float          skew{ 1.0f };            // NormalisableRange skew; 1.0 => linear
    bool           symmetricSkew{ false };  // skew about the centre (e.g. fine tune)
    const char*    unit{ "" };              // advisory display string only  [ADR-008 C4]
    const char* const* choices{ nullptr };  // Choice/Bool labels; nullptr otherwise
    std::uint8_t   choiceCount{ 0 };        // Choice option count (append-only)  [C5]
    std::uint8_t   canonicalChoiceCount{ 0 }; // hardware-canon indices; extras above [C6]
    bool           isAutomatable{ true };   // false => structural / not in automation list
    bool           isDiscrete{ false };     // host hint for stepped params
    SmoothingClass smoothing{ SmoothingClass::NoSmooth }; // §3.9; default NoSmooth [S9]
    std::uint16_t  versionAdded{ 1 };       // schemaVersion in which this ID first shipped
    bool           isSoftwareExt{ false };  // true => software-only artifact  [C6/C15]
};

// Deprecated alias slot — retained for the migration chain ONLY, never a live host
// parameter and deliberately NOT in kParamDefs (§3.0, §3.7, §7.4) [ADR-018 Q8].
struct ParamAlias {
    const char* id{};
    const char* migratesTo{};
};
inline constexpr ParamAlias kOsFactorAlias{ "mw101.os.factor", "mw101.quality" };

// ---------------------------------------------------------------------------
// Choice / bool label lists (§3.4, §3.5, §3.7, §3.8). Indices are fixed and
// append-only; software-only options append ABOVE the canonical count [ADR-008 C5/C6].
// ---------------------------------------------------------------------------
namespace detail {
    // Shared tempo-sync subdivision ladder (§3.4) — lfo/arp/seq sync_div.
    inline constexpr const char* kSyncDiv[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

    inline constexpr const char* kBoolOnOff[]      = { "Off", "On" };
    inline constexpr const char* kBoolModernVint[] = { "Modern", "Vintage" };

    inline constexpr const char* kVcoRange[]   = { "16'", "8'", "4'", "2'", "32'", "64'" };
    inline constexpr const char* kSubMode[]    = { "-1 Oct Sq", "-2 Oct Sq", "-2 Oct Pulse" };
    inline constexpr const char* kLfoShape[]   = { "Tri", "Sq", "Random", "Noise", "Sine" };
    inline constexpr const char* kLfoDest[]    = { "Pitch", "Filter", "PWM" };
    inline constexpr const char* kVcaMode[]    = { "ENV", "GATE" };
    inline constexpr const char* kGlideMode[]  = { "Off", "Auto", "On" };
    inline constexpr const char* kBendDest[]   = { "VCO", "VCF", "Both" };
    inline constexpr const char* kArpMode[]    = { "Off", "Up", "Down", "Up-Down" };
    inline constexpr const char* kArpRange[]   = { "1 Oct", "2 Oct", "3 Oct" };
    inline constexpr const char* kSeqMode[]    = { "Off", "Play", "Record" };
    inline constexpr const char* kTrigPrio[]   = { "GATE", "GATE+TRIG", "LFO" };
    inline constexpr const char* kPressDest[]  = { "VCF Cutoff", "VCA Level", "PW" };
    inline constexpr const char* kChorusMode[] = { "Off", "I", "II", "I+II" };
    inline constexpr const char* kDelayDiv[]   = { "1/4", "1/8", "1/8.", "1/8T", "1/16", "1/16T" };
    inline constexpr const char* kQuality[]    = { "Eco", "Standard", "HQ" };
    inline constexpr const char* kVoiceMode[]  = { "Mono", "Poly", "Unison" };
    inline constexpr const char* kVoiceCount[] = { "2", "4", "6", "8" };
    inline constexpr const char* kUnisonCount[]= { "2", "3", "4" };

    // --- entry builders keep the table rows compact and self-documenting --------

    constexpr ParamDef cont(const char* id, const char* label, ParamGroup g,
                            float lo, float hi, float def, float skew,
                            SmoothingClass sm, const char* unit,
                            bool symmetric = false) {
        ParamDef d{};
        d.id = id; d.label = label; d.group = g; d.type = ParamType::Continuous;
        d.minValue = lo; d.maxValue = hi; d.defaultValue = def; d.skew = skew;
        d.symmetricSkew = symmetric; d.unit = unit; d.smoothing = sm;
        return d;
    }

    constexpr ParamDef choice(const char* id, const char* label, ParamGroup g,
                              const char* const* labels, std::uint8_t count,
                              std::uint8_t canon, int def,
                              bool automatable = true, bool softwareExt = false) {
        ParamDef d{};
        d.id = id; d.label = label; d.group = g; d.type = ParamType::Choice;
        d.choices = labels; d.choiceCount = count; d.canonicalChoiceCount = canon;
        d.defaultValue = static_cast<float>(def); d.isDiscrete = true;
        d.isAutomatable = automatable; d.smoothing = SmoothingClass::NoSmooth;
        d.isSoftwareExt = softwareExt;
        return d;
    }

    constexpr ParamDef boolean(const char* id, const char* label, ParamGroup g,
                               bool def, const char* const* labels = kBoolOnOff,
                               bool automatable = true) {
        ParamDef d{};
        d.id = id; d.label = label; d.group = g; d.type = ParamType::Bool;
        d.choices = labels; d.choiceCount = 2; d.canonicalChoiceCount = 2;
        d.defaultValue = def ? 1.0f : 0.0f; d.isDiscrete = true;
        d.isAutomatable = automatable; d.smoothing = SmoothingClass::NoSmooth;
        return d;
    }
} // namespace detail

// ---------------------------------------------------------------------------
// kParamDefs — the 91 live entries, in §3.0 index order. Skews/(PI) defaults/(PI)
// ceilings are referenced from mw::cal::* by name (never inlined) [§3.10].
// ---------------------------------------------------------------------------
inline constexpr std::array<ParamDef, 91> kParamDefs = {{
    // --- VCO / oscillator -----------------------------------------------------
    detail::cont  ("mw101.vco.tune", "VCO Coarse", ParamGroup::Vco,
                   -24.0f, 24.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Pitch, "semitones"),
    detail::cont  ("mw101.vco.fine", "VCO Fine", ParamGroup::Vco,
                   -1.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Pitch, "semitones",
                   /*symmetric=*/true),
    detail::cont  ("mw101.vco.pw", "Pulse Width", ParamGroup::Vco,
                   0.0f, 1.0f, 0.5f, cal::skew::kLinear, SmoothingClass::PulseWidth, "duty"),
    detail::cont  ("mw101.vco.pwm_depth", "PWM Depth", ParamGroup::Vco,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::PulseWidth, "depth"),
    detail::choice("mw101.vco.range", "VCO Range", ParamGroup::Vco,
                   detail::kVcoRange, 6, 4, /*def=*/1, /*auto=*/true, /*ext=*/true),

    // --- Source mixer / sub / noise ------------------------------------------
    detail::cont  ("mw101.saw.level", "Saw Level", ParamGroup::Mixer,
                   0.0f, 1.0f, 0.8f, cal::skew::kLinear, SmoothingClass::Level, "level"),
    detail::cont  ("mw101.pulse.level", "Pulse Level", ParamGroup::Mixer,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "level"),
    detail::cont  ("mw101.sub.level", "Sub Level", ParamGroup::Mixer,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "level"),
    detail::choice("mw101.sub.mode", "Sub Mode", ParamGroup::Sub,
                   detail::kSubMode, 3, 3, /*def=*/0),
    detail::cont  ("mw101.noise.level", "Noise Level", ParamGroup::Mixer,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "level"),

    // --- VCF ------------------------------------------------------------------
    detail::cont  ("mw101.vcf.cutoff", "Cutoff", ParamGroup::Vcf,
                   0.0f, 1.0f, 1.0f, cal::skew::kCutoff, SmoothingClass::Fast, "norm freq"),
    detail::cont  ("mw101.vcf.resonance", "Resonance", ParamGroup::Vcf,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "resonance"),
    detail::cont  ("mw101.vcf.env_mod", "Env Mod", ParamGroup::Vcf,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),
    detail::cont  ("mw101.vcf.lfo_mod", "VCF LFO Mod", ParamGroup::Vcf,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),
    detail::cont  ("mw101.vcf.kbd_track", "Keyboard Track", ParamGroup::Vcf,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "amount"),

    // --- Envelope (A/D/R times are NoSmooth; sustain is a Level) --------------
    detail::cont  ("mw101.env.attack", "Env Attack", ParamGroup::Env,
                   0.0f, 1.0f, 0.0f, cal::skew::kEnvTime, SmoothingClass::NoSmooth, "time"),
    detail::cont  ("mw101.env.decay", "Env Decay", ParamGroup::Env,
                   0.0f, 1.0f, 0.3f, cal::skew::kEnvTime, SmoothingClass::NoSmooth, "time"),
    detail::cont  ("mw101.env.sustain", "Env Sustain", ParamGroup::Env,
                   0.0f, 1.0f, 1.0f, cal::skew::kLinear, SmoothingClass::Level, "level"),
    detail::cont  ("mw101.env.release", "Env Release", ParamGroup::Env,
                   0.0f, 1.0f, 0.1f, cal::skew::kEnvTime, SmoothingClass::NoSmooth, "time"),

    // --- LFO ------------------------------------------------------------------
    detail::cont  ("mw101.lfo.rate", "LFO Rate", ParamGroup::Lfo,
                   0.1f, 30.0f, 5.0f, cal::skew::kLfoRate, SmoothingClass::Fast, "Hz"),
    detail::choice("mw101.lfo.shape", "LFO Shape", ParamGroup::Lfo,
                   detail::kLfoShape, 5, 4, /*def=*/0, /*auto=*/true, /*ext=*/true),
    detail::choice("mw101.lfo.dest", "LFO Dest", ParamGroup::Lfo,
                   detail::kLfoDest, 3, 3, /*def=*/0),
    detail::cont  ("mw101.lfo.delay", "LFO Delay", ParamGroup::Lfo,
                   0.0f, 1.0f, 0.0f, cal::skew::kLfoDelay, SmoothingClass::NoSmooth, "time"),
    detail::cont  ("mw101.lfo.depth_pitch", "LFO->Pitch", ParamGroup::Lfo,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),
    detail::cont  ("mw101.lfo.depth_pwm", "LFO->PWM", ParamGroup::Lfo,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),
    detail::cont  ("mw101.lfo.depth_cutoff", "LFO->Cutoff", ParamGroup::Lfo,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),
    detail::boolean("mw101.lfo.tempo_sync", "LFO Tempo Sync", ParamGroup::Lfo, /*def=*/false),
    detail::choice("mw101.lfo.sync_div", "LFO Sync Div", ParamGroup::Lfo,
                   detail::kSyncDiv, 6, 6, /*def=*/1),

    // --- VCA ------------------------------------------------------------------
    detail::cont  ("mw101.vca.level", "VCA Level", ParamGroup::Vca,
                   0.0f, 1.0f, 0.8f, cal::skew::kLinear, SmoothingClass::Level, "level"),
    detail::choice("mw101.vca.mode", "VCA Mode", ParamGroup::Vca,
                   detail::kVcaMode, 2, 2, /*def=*/0),

    // --- Glide ----------------------------------------------------------------
    detail::cont  ("mw101.glide.time", "Glide Time", ParamGroup::Glide,
                   0.0f, 5.0f, 0.0f, cal::skew::kGlideTime, SmoothingClass::Glide, "s"),
    detail::choice("mw101.glide.mode", "Glide Mode", ParamGroup::Glide,
                   detail::kGlideMode, 3, 3, /*def=*/0),

    // --- Mod / bend -----------------------------------------------------------
    detail::cont  ("mw101.mod.bend_range_vco", "Bend Range (VCO)", ParamGroup::Mod,
                   0.0f, 1200.0f, 200.0f, cal::skew::kLinear, SmoothingClass::Pitch, "cents"),
    detail::cont  ("mw101.mod.bend_range_vcf", "Bend Range (VCF)", ParamGroup::Mod,
                   0.0f, 1200.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "cents"),
    detail::choice("mw101.mod.bend_dest", "Bend Dest", ParamGroup::Mod,
                   detail::kBendDest, 3, 3, /*def=*/0),
    detail::cont  ("mw101.mod.lfo_mod_wheel", "Mod Wheel->LFO", ParamGroup::Mod,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),

    // --- Arp ------------------------------------------------------------------
    detail::choice("mw101.arp.mode", "Arp Mode", ParamGroup::Arp,
                   detail::kArpMode, 4, 4, /*def=*/0),
    detail::choice("mw101.arp.range", "Arp Range", ParamGroup::Arp,
                   detail::kArpRange, 3, 3, /*def=*/0),
    detail::boolean("mw101.arp.tempo_sync", "Arp Tempo Sync", ParamGroup::Arp, /*def=*/true),
    detail::choice("mw101.arp.sync_div", "Arp Sync Div", ParamGroup::Arp,
                   detail::kSyncDiv, 6, 6, /*def=*/1),
    detail::boolean("mw101.arp.latch", "Arp Latch", ParamGroup::Arp, /*def=*/false),

    // --- Seq ------------------------------------------------------------------
    detail::choice("mw101.seq.mode", "Seq Mode", ParamGroup::Seq,
                   detail::kSeqMode, 3, 3, /*def=*/0),
    detail::boolean("mw101.seq.tempo_sync", "Seq Tempo Sync", ParamGroup::Seq, /*def=*/true),
    detail::choice("mw101.seq.sync_div", "Seq Sync Div", ParamGroup::Seq,
                   detail::kSyncDiv, 6, 6, /*def=*/3),

    // --- Key / trigger --------------------------------------------------------
    detail::choice("mw101.key.trigger_priority", "Trigger / Priority", ParamGroup::Key,
                   detail::kTrigPrio, 3, 3, /*def=*/0),

    // --- Tuning ---------------------------------------------------------------
    detail::cont  ("mw101.tune.a4", "A4 Reference", ParamGroup::Tune,
                   400.0f, 460.0f, 440.0f, cal::skew::kLinear, SmoothingClass::Pitch, "Hz"),
    detail::cont  ("mw101.tune.slop", "Tuning Slop", ParamGroup::Tune,
                   0.0f, 20.0f, 2.5f, cal::skew::kLinear, SmoothingClass::Pitch, "cents"),

    // --- Pitch / velocity / expression / MPE ----------------------------------
    detail::boolean("mw101.pitch.modern_unquantized", "Modern Un-Quantized Pitch",
                   ParamGroup::Global, /*def=*/false),
    detail::boolean("mw101.vel.enable", "Velocity Sensing", ParamGroup::Vel, /*def=*/true),
    detail::cont  ("mw101.vel.depth", "Velocity Depth", ParamGroup::Vel,
                   0.0f, 1.0f, cal::paramdefault::kVelDepth, cal::skew::kLinear,
                   SmoothingClass::Fast, "amount"),
    detail::cont  ("mw101.amp.expression", "Expression", ParamGroup::Vca,
                   0.0f, 1.0f, 1.0f, cal::skew::kLinear, SmoothingClass::Level, "scaler"),
    detail::boolean("mw101.mpe.enable", "MPE-lite Enable", ParamGroup::Mpe, /*def=*/false),
    detail::cont  ("mw101.mpe.bend_range", "MPE Bend Range", ParamGroup::Mpe,
                   0.0f, 96.0f, 48.0f, cal::skew::kLinear, SmoothingClass::Pitch, "semitones"),
    detail::choice("mw101.mpe.pressure_dest", "MPE Pressure Dest", ParamGroup::Mpe,
                   detail::kPressDest, 3, 3, /*def=*/0),

    // --- Vintage / drift / variance / warm-up ---------------------------------
    detail::cont  ("mw101.vintage.age", "Vintage Age", ParamGroup::Vintage,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "amount"),
    detail::boolean("mw101.vintage.enable", "Drift Enable", ParamGroup::Vintage, /*def=*/false),
    detail::cont  ("mw101.vintage.cal_spread", "Cal Spread", ParamGroup::Vintage,
                   0.0f, 1.0f, 0.25f, cal::skew::kLinear, SmoothingClass::Level, "percent"),
    detail::cont  ("mw101.vintage.detune_amt", "Detune Amount", ParamGroup::Vintage,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "percent"),
    detail::cont  ("mw101.drift.depth", "Drift Depth", ParamGroup::Drift,
                   0.0f, 50.0f, 4.0f, cal::skew::kLinear, SmoothingClass::Level, "cents"),
    detail::cont  ("mw101.drift.rate", "Drift Rate", ParamGroup::Drift,
                   0.01f, 1.0f, 0.1f, cal::skew::kDriftRate, SmoothingClass::Level, "Hz"),
    detail::cont  ("mw101.warmup.time", "Warm-Up Time", ParamGroup::Warmup,
                   0.0f, 30.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "min"),
    detail::cont  ("mw101.var.cutoff", "Cutoff Variance", ParamGroup::Var,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "percent"),
    detail::cont  ("mw101.var.env_time", "Env-Time Variance", ParamGroup::Var,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "percent"),
    detail::cont  ("mw101.var.pw", "PW Variance", ParamGroup::Var,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "percent"),
    detail::cont  ("mw101.var.glide", "Glide Variance", ParamGroup::Var,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "percent"),

    // --- FX -------------------------------------------------------------------
    detail::boolean("mw101.fx.bypass", "FX Master Bypass", ParamGroup::Fx, /*def=*/true),
    detail::boolean("mw101.fx.drive_enable", "Drive Enable", ParamGroup::Fx, /*def=*/false),
    detail::cont  ("mw101.fx.drive_amount", "Drive", ParamGroup::Fx,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Fast, "amount"),
    detail::cont  ("mw101.fx.drive_tone", "Drive Tone", ParamGroup::Fx,
                   0.0f, 1.0f, 0.5f, cal::skew::kLinear, SmoothingClass::Fast, "tilt"),
    detail::cont  ("mw101.fx.drive_output", "Drive Output", ParamGroup::Fx,
                   0.0f, 1.0f, 0.5f, cal::skew::kLinear, SmoothingClass::Level, "makeup"),
    detail::boolean("mw101.fx.chorus_enable", "Chorus Enable", ParamGroup::Fx, /*def=*/false),
    detail::choice("mw101.fx.chorus_mode", "Chorus Mode", ParamGroup::Fx,
                   detail::kChorusMode, 4, 4, /*def=*/0),
    detail::cont  ("mw101.fx.chorus_rate", "Chorus Rate", ParamGroup::Fx,
                   0.0f, 1.0f, 0.3f, cal::skew::kLinear, SmoothingClass::Fast, "rate"),
    detail::cont  ("mw101.fx.chorus_depth", "Chorus Depth", ParamGroup::Fx,
                   0.0f, 1.0f, 0.5f, cal::skew::kLinear, SmoothingClass::Fast, "depth"),
    detail::cont  ("mw101.fx.chorus_width", "Chorus Width", ParamGroup::Fx,
                   0.0f, 1.0f, 1.0f, cal::skew::kLinear, SmoothingClass::Level, "width"),
    detail::cont  ("mw101.fx.chorus_mix", "Chorus Mix", ParamGroup::Fx,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "wet"),
    detail::boolean("mw101.fx.delay_enable", "Delay Enable", ParamGroup::Fx, /*def=*/false),
    detail::boolean("mw101.fx.delay_sync", "Delay Tempo Sync", ParamGroup::Fx, /*def=*/false),
    detail::choice("mw101.fx.delay_division", "Delay Division", ParamGroup::Fx,
                   detail::kDelayDiv, 6, 6, /*def=*/1),
    detail::cont  ("mw101.fx.delay_time", "Delay Time", ParamGroup::Fx,
                   0.0f, 1.0f, 0.3f, cal::skew::kDelayTime, SmoothingClass::Fast, "time"),
    detail::cont  ("mw101.fx.delay_feedback", "Delay Feedback", ParamGroup::Fx,
                   0.0f, cal::paramrange::kDelayFeedbackMax, 0.3f, cal::skew::kLinear,
                   SmoothingClass::Fast, "amount"),
    detail::cont  ("mw101.fx.delay_damp", "Delay Damp", ParamGroup::Fx,
                   0.0f, 1.0f, 0.5f, cal::skew::kLinear, SmoothingClass::Fast, "tone"),
    detail::cont  ("mw101.fx.delay_width", "Delay Width", ParamGroup::Fx,
                   0.0f, 1.0f, 1.0f, cal::skew::kLinear, SmoothingClass::Level, "width"),
    detail::cont  ("mw101.fx.delay_mix", "Delay Mix", ParamGroup::Fx,
                   0.0f, 1.0f, 0.0f, cal::skew::kLinear, SmoothingClass::Level, "wet"),
    detail::boolean("mw101.fx.delay_pingpong", "Delay Ping-Pong", ParamGroup::Fx, /*def=*/false),
    detail::boolean("mw101.out.mono", "Mono Output", ParamGroup::Out, /*def=*/false),

    // --- Structural (non-automatable, NoSmooth) — §3.7 / §3.8 -----------------
    detail::choice("mw101.quality", "Quality", ParamGroup::Global,
                   detail::kQuality, 3, 3, /*def=*/1, /*auto=*/false),
    detail::choice("mw101.voice.mode", "Voice Mode", ParamGroup::Voice,
                   detail::kVoiceMode, 3, 3, /*def=*/0, /*auto=*/false),
    detail::choice("mw101.voice.count", "Poly Voices", ParamGroup::Voice,
                   detail::kVoiceCount, 4, 4, /*def=*/1, /*auto=*/false),
    detail::choice("mw101.unison.count", "Unison Count", ParamGroup::Voice,
                   detail::kUnisonCount, 3, 3, /*def=*/0, /*auto=*/false),
    detail::boolean("mw101.control.vintage", "Vintage Control", ParamGroup::Global,
                   /*def=*/false, detail::kBoolModernVint, /*auto=*/false),
}};

// ---------------------------------------------------------------------------
// §3.1 invariants — enforced at COMPILE time. A drift in the table that violates any
// of these FAILS the build, not just a test [docs/design/06 §3.1; ADR-008 C1/C5/C6/C7;
// ADR-018 Q1-Q2; ADR-020 S8-S9].
// ---------------------------------------------------------------------------

// 91 live entries (the deprecated alias is excluded by construction) [§3.0].
static_assert(kParamDefs.size() == 91,
              "ParamDefs: kParamDefs MUST hold exactly 91 live entries [§3.0].");

// Helper: does this ID name a structural param (§3.7/§3.8)?
consteval bool mwIsStructural(std::string_view id) {
    return id == std::string_view{"mw101.quality"}
        || id == std::string_view{"mw101.voice.mode"}
        || id == std::string_view{"mw101.voice.count"}
        || id == std::string_view{"mw101.unison.count"}
        || id == std::string_view{"mw101.control.vintage"};
}

consteval bool mwHasPrefix(std::string_view id) {
    return id.size() > 6 && id.substr(0, 6) == std::string_view{"mw101."};
}

// IDs unique + mw101.-prefixed.
consteval bool mwIdsUniqueAndPrefixed() {
    for (std::size_t i = 0; i < kParamDefs.size(); ++i) {
        if (!mwHasPrefix(kParamDefs[i].id)) return false;
        for (std::size_t j = i + 1; j < kParamDefs.size(); ++j) {
            if (std::string_view{kParamDefs[i].id} == std::string_view{kParamDefs[j].id})
                return false;
        }
    }
    return true;
}
static_assert(mwIdsUniqueAndPrefixed(),
              "ParamDefs: IDs MUST be unique and mw101.-prefixed [§3.1; ADR-008 C1].");

// Choice params: choices != nullptr and choiceCount >= canonicalChoiceCount.
consteval bool mwChoiceInvariant() {
    for (const auto& d : kParamDefs) {
        if (d.type == ParamType::Choice || d.type == ParamType::Bool) {
            if (d.choices == nullptr) return false;
            if (d.choiceCount < d.canonicalChoiceCount) return false;
            if (d.canonicalChoiceCount == 0) return false;
        }
    }
    return true;
}
static_assert(mwChoiceInvariant(),
              "ParamDefs: choice params MUST have choices!=nullptr and "
              "choiceCount>=canonicalChoiceCount [§3.1; ADR-008 C5/C6].");

// Structural params: isAutomatable==false && smoothing==NoSmooth.
consteval bool mwStructuralInvariant() {
    int structural = 0;
    for (const auto& d : kParamDefs) {
        if (mwIsStructural(d.id)) {
            ++structural;
            if (d.isAutomatable) return false;
            if (d.smoothing != SmoothingClass::NoSmooth) return false;
        }
    }
    return structural == 5;
}
static_assert(mwStructuralInvariant(),
              "ParamDefs: the 5 structural params MUST be non-automatable + NoSmooth "
              "[§3.7; §3.8; §3.1; ADR-018 Q1-Q2; ADR-020 S8].");

// Software-ext: a flagged param exposes its extra indices ABOVE the canonical count.
consteval bool mwSoftwareExtInvariant() {
    for (const auto& d : kParamDefs) {
        if (d.isSoftwareExt && !(d.choiceCount > d.canonicalChoiceCount)) return false;
    }
    return true;
}
static_assert(mwSoftwareExtInvariant(),
              "ParamDefs: any isSoftwareExt choice index MUST sit at/above "
              "canonicalChoiceCount [§3.4; ADR-008 C6].");

// Continuous/choice defaults are in range.
consteval bool mwDefaultsInRange() {
    for (const auto& d : kParamDefs) {
        if (d.type == ParamType::Continuous) {
            if (!(d.minValue < d.maxValue)) return false;
            if (d.defaultValue < d.minValue || d.defaultValue > d.maxValue) return false;
        } else {
            const int def = static_cast<int>(d.defaultValue);
            if (def < 0 || def >= d.choiceCount) return false;
        }
    }
    return true;
}
static_assert(mwDefaultsInRange(),
              "ParamDefs: every default MUST lie inside its declared range/index set.");

// The alias slot is a migration alias to mw101.quality and is NOT live.
static_assert(std::string_view{kOsFactorAlias.id} == std::string_view{"mw101.os.factor"},
              "ParamDefs: the deprecated alias slot MUST be mw101.os.factor [§7.4].");
static_assert(std::string_view{kOsFactorAlias.migratesTo} == std::string_view{"mw101.quality"},
              "ParamDefs: mw101.os.factor MUST migrate to mw101.quality [§3.7; ADR-018 Q8].");

} // namespace mw::params
