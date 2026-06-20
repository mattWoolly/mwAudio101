// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/ParamIDs.h — compile-time parameter string-ID constants (task 014).
//
// One constexpr const char* per parameter ID so no call site ever hand-types an ID
// string. Every ID is an immutable mw101.<group>.<name> snake_case string
// [docs/design/06 §3.0, §3.2]. Pure header, no logic, JUCE-free.
//
// COMPLETE CATALOGUE (task 014b): this header now exposes a named constant for EVERY
// canonical parameter ID — all 91 live AudioProcessorParameters in §3.0 index order,
// plus the deprecated mw101.os.factor migration alias (kOsFactorAlias, NOT a live
// param). The strings are VERBATIM from the authoritative registry core/params/
// ParamDefs.h (kParamDefs + kOsFactorAlias); ParamDefs.h owns the contract surface
// (ranges/defaults/skews/smoothing), this header owns ONLY the string-ID constants so
// no UI/plugin call site ever hand-types an ID.
//
// 1:1 coverage is proven objectively by tests/unit/ParamIDsCatalogueTest.cpp ([paramids]):
// the constant set is compared for equality against kParamDefs, so adding a registry
// row without a constant here (or vice versa) fails that test. kCount below is the
// in-header live-constant tally for a quick compile-time cross-check.

#pragma once

namespace mw::params::ids {

// --- VCO / oscillator ---------------------------------------------------------
inline constexpr const char* kVcoTune      = "mw101.vco.tune";
inline constexpr const char* kVcoFine      = "mw101.vco.fine";
inline constexpr const char* kVcoPw        = "mw101.vco.pw";
inline constexpr const char* kVcoPwmDepth  = "mw101.vco.pwm_depth";
inline constexpr const char* kVcoRange     = "mw101.vco.range";

// --- Sub / mixer --------------------------------------------------------------
inline constexpr const char* kSawLevel     = "mw101.saw.level";
inline constexpr const char* kPulseLevel   = "mw101.pulse.level";
inline constexpr const char* kSubLevel     = "mw101.sub.level";
inline constexpr const char* kSubMode      = "mw101.sub.mode";    // canonical; NOT sub.shape
inline constexpr const char* kNoiseLevel   = "mw101.noise.level";

// --- VCF ----------------------------------------------------------------------
inline constexpr const char* kVcfCutoff    = "mw101.vcf.cutoff";
inline constexpr const char* kVcfResonance = "mw101.vcf.resonance";
inline constexpr const char* kVcfEnvMod    = "mw101.vcf.env_mod";
inline constexpr const char* kVcfLfoMod    = "mw101.vcf.lfo_mod";
inline constexpr const char* kVcfKbdTrack  = "mw101.vcf.kbd_track";

// --- Envelope -----------------------------------------------------------------
inline constexpr const char* kEnvAttack    = "mw101.env.attack";
inline constexpr const char* kEnvDecay     = "mw101.env.decay";
inline constexpr const char* kEnvSustain   = "mw101.env.sustain";
inline constexpr const char* kEnvRelease   = "mw101.env.release";

// --- LFO ----------------------------------------------------------------------
inline constexpr const char* kLfoRate         = "mw101.lfo.rate";
inline constexpr const char* kLfoShape        = "mw101.lfo.shape";
inline constexpr const char* kLfoDest         = "mw101.lfo.dest";
inline constexpr const char* kLfoDelay        = "mw101.lfo.delay";
inline constexpr const char* kLfoDepthPitch   = "mw101.lfo.depth_pitch";
inline constexpr const char* kLfoDepthPwm     = "mw101.lfo.depth_pwm";
inline constexpr const char* kLfoDepthCutoff  = "mw101.lfo.depth_cutoff";
inline constexpr const char* kLfoTempoSync    = "mw101.lfo.tempo_sync";
inline constexpr const char* kLfoSyncDiv      = "mw101.lfo.sync_div";

// --- VCA ----------------------------------------------------------------------
inline constexpr const char* kVcaLevel     = "mw101.vca.level";
inline constexpr const char* kVcaMode      = "mw101.vca.mode";

// --- Glide --------------------------------------------------------------------
inline constexpr const char* kGlideTime    = "mw101.glide.time";
inline constexpr const char* kGlideMode    = "mw101.glide.mode";

// --- Mod / bend ---------------------------------------------------------------
inline constexpr const char* kModBendRangeVco = "mw101.mod.bend_range_vco";
inline constexpr const char* kModBendRangeVcf = "mw101.mod.bend_range_vcf";
inline constexpr const char* kModBendDest     = "mw101.mod.bend_dest";
inline constexpr const char* kModLfoModWheel  = "mw101.mod.lfo_mod_wheel";

// --- Arp ----------------------------------------------------------------------
inline constexpr const char* kArpMode      = "mw101.arp.mode";
inline constexpr const char* kArpRange     = "mw101.arp.range";
inline constexpr const char* kArpTempoSync = "mw101.arp.tempo_sync";
inline constexpr const char* kArpSyncDiv   = "mw101.arp.sync_div";
inline constexpr const char* kArpLatch     = "mw101.arp.latch";   // live control; persisted latch is <extras>

// --- Seq ----------------------------------------------------------------------
inline constexpr const char* kSeqMode      = "mw101.seq.mode";
inline constexpr const char* kSeqTempoSync = "mw101.seq.tempo_sync";
inline constexpr const char* kSeqSyncDiv   = "mw101.seq.sync_div";

// --- Key / trigger ------------------------------------------------------------
inline constexpr const char* kKeyTriggerPriority = "mw101.key.trigger_priority";

// --- Tuning -------------------------------------------------------------------
inline constexpr const char* kTuneA4       = "mw101.tune.a4";
inline constexpr const char* kTuneSlop     = "mw101.tune.slop";

// --- Pitch / velocity / expression / MPE --------------------------------------
inline constexpr const char* kPitchModernUnquantized = "mw101.pitch.modern_unquantized";
inline constexpr const char* kVelEnable      = "mw101.vel.enable";
inline constexpr const char* kVelDepth       = "mw101.vel.depth";
inline constexpr const char* kAmpExpression  = "mw101.amp.expression";   // canonical; CC11 VCA scaler
inline constexpr const char* kMpeEnable      = "mw101.mpe.enable";
inline constexpr const char* kMpeBendRange   = "mw101.mpe.bend_range";
inline constexpr const char* kMpePressureDest = "mw101.mpe.pressure_dest";

// --- Vintage / drift / variance / warm-up -------------------------------------
inline constexpr const char* kVintageAge       = "mw101.vintage.age";
inline constexpr const char* kVintageEnable    = "mw101.vintage.enable";
inline constexpr const char* kVintageCalSpread = "mw101.vintage.cal_spread";
inline constexpr const char* kVintageDetuneAmt = "mw101.vintage.detune_amt";
inline constexpr const char* kDriftDepth       = "mw101.drift.depth";
inline constexpr const char* kDriftRate        = "mw101.drift.rate";
inline constexpr const char* kWarmupTime       = "mw101.warmup.time";
inline constexpr const char* kVarCutoff        = "mw101.var.cutoff";
inline constexpr const char* kVarEnvTime       = "mw101.var.env_time";
inline constexpr const char* kVarPw            = "mw101.var.pw";
inline constexpr const char* kVarGlide         = "mw101.var.glide";

// --- FX: Drive ----------------------------------------------------------------
inline constexpr const char* kFxBypass       = "mw101.fx.bypass";   // master bypass; default ON (bypassed)
inline constexpr const char* kFxDriveEnable  = "mw101.fx.drive_enable";
inline constexpr const char* kFxDriveAmount  = "mw101.fx.drive_amount";
inline constexpr const char* kFxDriveTone    = "mw101.fx.drive_tone";
inline constexpr const char* kFxDriveOutput  = "mw101.fx.drive_output";

// --- FX: Chorus ---------------------------------------------------------------
inline constexpr const char* kFxChorusEnable = "mw101.fx.chorus_enable";
inline constexpr const char* kFxChorusMode   = "mw101.fx.chorus_mode";
inline constexpr const char* kFxChorusRate   = "mw101.fx.chorus_rate";
inline constexpr const char* kFxChorusDepth  = "mw101.fx.chorus_depth";
inline constexpr const char* kFxChorusWidth  = "mw101.fx.chorus_width";
inline constexpr const char* kFxChorusMix    = "mw101.fx.chorus_mix";

// --- FX: Delay ----------------------------------------------------------------
inline constexpr const char* kFxDelayEnable   = "mw101.fx.delay_enable";
inline constexpr const char* kFxDelaySync     = "mw101.fx.delay_sync";
inline constexpr const char* kFxDelayDivision = "mw101.fx.delay_division";
inline constexpr const char* kFxDelayTime     = "mw101.fx.delay_time";
inline constexpr const char* kFxDelayFeedback = "mw101.fx.delay_feedback";
inline constexpr const char* kFxDelayDamp     = "mw101.fx.delay_damp";
inline constexpr const char* kFxDelayWidth    = "mw101.fx.delay_width";
inline constexpr const char* kFxDelayMix      = "mw101.fx.delay_mix";
inline constexpr const char* kFxDelayPingpong = "mw101.fx.delay_pingpong";

// --- Output -------------------------------------------------------------------
inline constexpr const char* kOutMono      = "mw101.out.mono";

// --- Structural (non-automatable; applied off the audio thread) — §3.7/§3.8 ---
inline constexpr const char* kQuality        = "mw101.quality";
inline constexpr const char* kVoiceMode      = "mw101.voice.mode";
inline constexpr const char* kVoiceCount     = "mw101.voice.count";
inline constexpr const char* kUnisonCount    = "mw101.unison.count";
inline constexpr const char* kControlVintage = "mw101.control.vintage";

// --- Deprecated migration alias (NOT a live param) ----------------------------
// Retained ONLY for the migration chain [docs/design/06 §3.0, §7.4; ADR-018 Q8];
// it migrates to mw101.quality and is deliberately absent from kParamDefs. Two
// names alias the same string: kOsFactorAlias is the clear catalogue name; the
// foundation-era kDeprecatedOsFactor is kept verbatim for existing call sites.
inline constexpr const char* kOsFactorAlias      = "mw101.os.factor";
inline constexpr const char* kDeprecatedOsFactor = "mw101.os.factor";

// In-header tally of the LIVE named constants above (cross-checks §3.0's 91; the
// authoritative 1:1 coverage proof is ParamIDsCatalogueTest.cpp against kParamDefs).
inline constexpr int kLiveCount = 91;

} // namespace mw::params::ids
