// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/ParamIDs.h — compile-time parameter string-ID constants (task 014).
//
// One constexpr const char* per parameter ID so no call site ever hand-types an ID
// string. Every ID is an immutable mw101.<group>.<name> snake_case string
// [docs/design/06 §3.0, §3.2]. Pure header, no logic.
//
// FOUNDATION SCOPE: this header declares the real mw101.* IDs the foundation needs
// now (a representative set spanning every group, taken VERBATIM from §3.0), so the
// header compiles into mwcore and the prefix/uniqueness discipline is testable.
// TODO(task-014): complete the full §3.0 catalogue (91 live IDs + the deprecated
// mw101.os.factor alias) and add the exact-count static_assert; that is task 014's
// full scope, deferred out of this bootstrap PR.

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
inline constexpr const char* kLfoRate      = "mw101.lfo.rate";
inline constexpr const char* kLfoShape     = "mw101.lfo.shape";

// --- VCA ----------------------------------------------------------------------
inline constexpr const char* kVcaLevel     = "mw101.vca.level";
inline constexpr const char* kVcaMode      = "mw101.vca.mode";

// --- Glide --------------------------------------------------------------------
inline constexpr const char* kGlideTime    = "mw101.glide.time";
inline constexpr const char* kGlideMode    = "mw101.glide.mode";

// --- Tuning -------------------------------------------------------------------
inline constexpr const char* kTuneA4       = "mw101.tune.a4";
inline constexpr const char* kTuneSlop     = "mw101.tune.slop";

// Deprecated alias slot retained for the migration chain [docs/design/06 §7.4].
inline constexpr const char* kDeprecatedOsFactor = "mw101.os.factor";

} // namespace mw::params::ids
