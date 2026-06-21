<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 162e
title: Wire the two audit-found unwired params — vco.pwm_depth (manual PWM) + vcf.lfo_mod (VCF LFO amount)
status: done
depends-on: [160, 161, 162, 165]
component: core
estimated-size: S
stream: voice-control
tag: dispatch_gap
---

## Objective

The 165 completeness audit found two LIVE registry params with NO dispatch path (never decoded/
applied -> no audible effect): mw101.vco.pwm_depth and mw101.vcf.lfo_mod. Wire both into the ADR-028
dispatch seam so they affect the sound, and flip their 165-manifest entries from inert -> asserted.

## Context (design semantics — apply EXACTLY these, distinct from the already-wired neighbors)

- `docs/design/06 §3.0`: `mw101.vco.pwm_depth` = "PWM Depth (manual)", 0..1 linear, oscillator —
  MANUAL pulse-width-modulation depth, DISTINCT from `mw101.lfo.depth_pwm` (the LFO->PWM amount,
  already wired as lfoPwmDepthNorm in Engine.cpp:840). So vco.pwm_depth is a static/manual PWM depth
  applied to the pulse-width CV independent of the LFO (read the oscillator design for the exact
  manual-PWM model; it modulates the pulse duty by a fixed depth, not LFO-driven).
- `docs/design/06 §3.0`: `mw101.vcf.lfo_mod` = "VCF LFO Mod", 0..1 linear, filter/LFO — the VCF
  module's OWN LFO->cutoff amount, DISTINCT from `mw101.lfo.depth_cutoff` (the LFO-panel cutoff
  routing). BOTH sum into the cutoff CV: the existing lfo.depth_cutoff term (162) PLUS a new
  vcf.lfo_mod * LFO term. (Read the filter design for whether they're additive or vcf.lfo_mod gates
  the LFO->cutoff path; implement the design's intent.)
- The seam: core/Engine.cpp Engine::applyParamSnapshot / cacheParamSlots / decode + core/voice/
  Voice.cpp Voice::applyControls (the existing 160/161/162 pattern you extend with 2 slots).

## Scope

- Add ParamSlots entries for vco.pwm_depth + vcf.lfo_mod; decode them in applyParamSnapshot; apply
  vco.pwm_depth into the PWM CV (manual depth, distinct from lfoPwmDepthNorm) and vcf.lfo_mod into the
  cutoff CV (LFO-modulated, alongside the existing lfo.depth_cutoff term). RT-safe (POD + arithmetic).
- New tests (tag dispatch_gap, tests/unit/): vco.pwm_depth changes the pulse duty/PWM with the LFO OFF
  (proving it's the MANUAL path, distinct from lfo.depth_pwm); vcf.lfo_mod makes the LFO modulate the
  filter cutoff (proving the VCF-panel LFO-mod path, distinct from lfo.depth_cutoff).
- Update tests/unit/DispatchCompleteTest.cpp (165): move vco.pwm_depth + vcf.lfo_mod from the
  findings/inert manifest entries to asserted-audio (they now have an observable effect).

## Out of scope

- The other dispatch legs (160-164, done); any param-schema change (don't add/remove params).

## Acceptance criteria

- [ ] vco.pwm_depth produces a manual PWM/duty change with the LFO disabled (distinct from lfo.depth_pwm) — asserted via rendered spectrum/duty [§3.0]
- [ ] vcf.lfo_mod makes the LFO modulate the filter cutoff (distinct from lfo.depth_cutoff) — asserted via rendered brightness wobble [§3.0]
- [ ] The 165 DispatchCompleteTest manifest flips these 2 from inert -> asserted-audio and the battery stays green (no longer 2 findings)
- [ ] dispatch_gap tests green; full core suite green; RT-safe

## Verification commands

```
cmake --preset default && cmake --build --preset default
ctest --preset default -R 'dispatch_gap|dispatch_complete' --no-tests=error --output-on-failure
ctest --preset default --no-tests=error
```
