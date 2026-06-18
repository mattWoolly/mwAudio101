<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 049
title: Envelope/LFO/VCA (PI) calibration constants block
status: done
depends-on: [007, 006, 001]
component: core
estimated-size: S
stream: core-env-lfo-vca
tag: envlfovca_calib
---

## Objective

Add this subsystem's (PI) tunable constants (envelope shaping, LFO shape/rate/mod-bus, VCA taper/anti-thump, velocity) to the shared core/calibration/Calibration.h so no DSP call site inlines a literal.

## Context

- `docs/design/03-dsp-envelope-lfo-vca.md §2.4` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §3.5` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §4.3` — read first
- `docs/design/03-dsp-envelope-lfo-vca.md §5.2` — read first
- `plan/decisions/020-parameter-smoothing-policy.md S13` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `envlfovca_calib`.

## Scope

- Add kEnvAttackOvershoot=1.25, kEnvTimeScale=0.20, kEnvCurve=1.0, kEnvSnapThreshold=1.0e-4, kEnvTimeMin=1.0e-4 (§2.4 table)
- Add kLfoSmoothShape=0.85, kLfoRateSkew=0.3, kModBusLpHz=16000 (§3.5 table)
- Add kVcaTaperExp=2.0, kVcaOtaDrive=1.0, kVcaAntiThumpMs=2.0, kVcaOffsetNull=0.0 (§4.3 table)
- Add kVelToVca=0.7, kVelToCutoff=0.5, kVelCurve=1.0 (§5.2 table)
- Each constant carries a (PI) tunable-default comment; no measured-spec assertion

## Out of scope

- Parameter IDs/ranges/skews (doc 06)
- Smoothing time-constant class values (doc 06/ADR-020 registry)
- Any DSP algorithm

## Acceptance criteria

- [ ] All 15 named constants exist with the §2.4/§3.5/§4.3/§5.2 default values
- [ ] ctest --preset default -R envlfovca_calib --no-tests=error passes; test envlfovca_calib_values asserts each default
- [ ] Each constant tagged (PI) per ADR-020 S13; values referenced from Calibration.h not inlined

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R envlfovca_calib --no-tests=error
```
