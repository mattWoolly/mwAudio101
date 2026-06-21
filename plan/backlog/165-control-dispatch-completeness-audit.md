<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 165
title: Control-dispatch completeness audit — assert every one of the 91 params has an audible/observable effect
status: in-review
depends-on: [160, 161, 162, 163, 164, 162b, 162c]
component: qa
estimated-size: M
stream: integration
tag: dispatch_complete
---

## Objective

The completeness critic for ADR-028: prove that EVERY live parameter now reaches its DSP target, so the
"computed-but-never-wired" class of bug cannot silently return. This is the assertion class that was
entirely missing (tests only checked non-silent/deterministic, never knob->effect).

## Scope

- A `dispatch_complete` test battery that, for EACH of the 91 kParamDefs params, drives the assembled
  Engine with the param at a low vs high value and asserts the OUTPUT changes in the expected dimension
  (pitch/spectrum/amplitude/time/FX-tail) — or, for structural/off-thread params, documents why it is
  exempt. A param with NO observable effect is a FAILURE (catches a future unwiring).
- A coverage manifest listing each param -> its asserted effect (or exempt + reason).

## Acceptance criteria

- [ ] Every non-structural param has a test asserting an observable output change; structural/exempt params are enumerated with rationale [ADR-028]
- [ ] The battery FAILS if any covered param is disconnected (proven by a deliberate temporary disconnect during dev, then restored)
- [ ] `dispatch_complete` tests pass; full suite green

## Note (from 164 QA MEDIUM/LOW)

The 164 var-spread test did NOT isolate var.cutoff/var.pw (it also set vintage.cal_spread, which perturbs pitch and masks them), and var.env_time/var.glide have no measured-effect test. The legs ARE wired in Voice.cpp. The 165 completeness battery MUST isolate EACH var.* leg (mute the masking params — cal_spread/drift off) and assert its specific observable effect (var.cutoff->cutoff spread, var.pw->duty spread, var.env_time->env-time spread, var.glide->glide-time spread), so every param's wiring is independently proven.
