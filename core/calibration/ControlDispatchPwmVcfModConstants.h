// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDispatchPwmVcfModConstants.h — the (PI) constant set for the
// TWO audit-found unwired params wired into the ParamSnapshot -> DSP control-dispatch seam
// (task 162e, extending the ADR-028 keystone built by 160 / 161 / 162 / 163 / 164):
//   * mw101.vco.pwm_depth — the MANUAL pulse-width-modulation depth (static, LFO-independent;
//                           distinct from the LFO->PWM amount mw101.lfo.depth_pwm).
//   * mw101.vcf.lfo_mod   — the VCF module's OWN LFO->cutoff amount (distinct from the LFO
//                           panel's mw101.lfo.depth_cutoff; both SUM into the cutoff CV).
//
// Per the parallel-fleet conflict-avoidance rule a module's (PI) constants land in a
// DEDICATED calibration header rather than being appended to the shared orchestrator
// core/calibration/Calibration.h, and a sibling task's header (162's
// ControlDispatchLfoConstants.h) is NOT edited [docs/design/00 §1.2; AGENTS.md
// "(PI) discipline"; ADR-028]. The dispatch seam references these by name and NEVER
// inlines a literal at a call site [ADR-020 S13].
//
// Every numeric figure here is (PI) — a *pragmatic invention* / tunable integration anchor,
// bounded by the cited circuit-behavior frames (the CEM3340 manual-PW model docs/design/01
// §4.6; the VCF LFO->cutoff routing docs/design/02 §1.2 / docs/design/05 §3.1), NOT a measured
// SH-101 oracle.

#pragma once

namespace mw::cal::dispatch {

// ---------------------------------------------------------------------------
// Manual PWM depth (mw101.vco.pwm_depth [0,1] linear, docs/design/06 §3.0 "PWM Depth (manual)")
// -> static contribution to the normalized PWM CV (pwmCvNorm [0,1]).
//
// docs/design/01 §4.6 + docs/design/05 §3.1: the PWM source switch (ENV / MANUAL / LFO) feeds
// the oscillator's normalized pwmCvNorm; the MANUAL static-width component is mw101.vco.pwm_depth
// (oscillator-owned; DISTINCT from the LFO->PWM amount mw101.lfo.depth_pwm, already wired as
// lfoPwmDepthNorm). The oscillator maps pwmCvNorm 0 => 50% (square) down toward ~5% at 1
// (duty = kPwmDutyMax - pwmCvNorm * (kPwmDutyMax - kPwmDutyMin), §4.6). So a manual depth is a
// fixed, LFO-INDEPENDENT bias on that same duty control: at full depth the duty narrows by the
// whole CV span (square -> narrow). kManualPwmDepthNorm = 1 lets the param reach the full duty
// range (the apply site clamps the summed pwmCvNorm to [0,1] so it never wraps). (PI) depth
// anchor [docs/design/01 §4.6; docs/design/05 §3.1 (PwmSource::Manual); ADR-028].
inline constexpr float kManualPwmDepthNorm = 1.0f;   // (PI) — full manual-PW duty span (norm)

// ---------------------------------------------------------------------------
// VCF LFO mod (mw101.vcf.lfo_mod [0,1] linear, docs/design/06 §3.0 "VCF LFO Mod") -> peak
// cutoff CV deviation from the VCF module's OWN LFO->cutoff path.
//
// docs/design/02 §1.2 / docs/design/05 §3.1: the VCF cutoff has its OWN MOD depth alongside the
// ADSR env depth (mw101.vcf.env_mod) and Key Follow (mw101.vcf.kbd_track). This VCF-panel LFO
// amount is DISTINCT from the LFO panel's mw101.lfo.depth_cutoff (already wired as
// lfoCutoffDepthOct, which fires only when the single LFO dest switch == Filter). The VCF's own
// lfo_mod routes the per-voice LFO to cutoff REGARDLESS of the dest switch, and SUMS with the
// lfo.depth_cutoff term into the same 1 V/oct cutoff CV the 161/162 legs assemble. At full depth
// the LFO swings the cutoff by +-kVcfLfoModDepthOctaves octaves (the classic filter-wobble),
// matched to the sibling kLfoCutoffDepthOctaves so the two cutoff-LFO paths are comparable. (PI)
// depth anchor [docs/design/02 §1.2; docs/design/05 §3.1; ADR-028].
inline constexpr float kVcfLfoModDepthOctaves = 2.0f;   // (PI) — full-depth VCF LFO->cutoff (oct)

} // namespace mw::cal::dispatch

