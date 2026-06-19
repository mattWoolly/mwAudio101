<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 027: Resonance taper calibrated for the discrete filter (kMax/exp deviation)

Status: accepted
Date: 2026-06-19

## Context

`docs/design/02-dsp-filter.md` §9 lists the resonance map `(PI)` constants `vcf::kMax = 4.0`
and `vcf::kResoCurveExp = 2.0`, derived from the **ideal continuous** Stilson-Smith/Zavalishin
normalized model whose self-oscillation onset is at loop gain `k = 4`. The shipping engine
(ADR-003) is a **discrete forward-Euler Huovilainen cascade**, whose self-oscillation onset sits
*above* `k = 4` (measured ~4.4 here) due to the half-sample residual feedback-phase error.

Wave-6 QA (PR #60) correctly rejected an initial fix that set `kMax = 8.0` while leaving
`exp = 2.0`: with the quadratic taper that **doubles loop gain across the entire control range**
(reso01 = 0.5 → k = 2.0 instead of the documented 1.0), changing the filter's character
everywhere, and the test fed the oracle the engine's own `k` so it could not detect the drift.

## Options considered

- **kMax = 4.0, exp = 2.0 (literal §9):** at reso01 = 1 the discrete loop is *below* onset — it
  does not self-oscillate (verified: peak ≈ 1e-19, decays) and the resonant peak under-tracks
  cutoff by ~18%. Fails the self-oscillation acceptance criteria. Rejected.
- **kMax = 8.0, exp = 2.0:** self-oscillates, but doubles low/mid loop gain (reso01 = 0.5 → 2.0).
  Rejected by QA (silent character change across the whole range).
- **kMax = 8.0, exp = 3.0 (chosen):** steepening the taper (not uniformly inflating kMax)
  preserves the documented low/mid anchors *and* reaches a firmly self-oscillating top.

## Decision

Re-fit the `(PI)` resonance taper for the discrete engine: **`kMax = 8.0`, `kResoCurveExp = 3.0`**.
This gives `k = kMax · reso01^exp`:

| reso01 | k | note |
|---|---|---|
| 0.5 | **1.0** | exact §9 unity anchor (the F-13 cross-check point) |
| 0.7 | 2.74 | |
| 1.0 | 8.0 | firmly above the ~4.4 discrete onset → robust, clamp-bounded self-oscillation |

The diode clamp (§5.4) sets the self-oscillation amplitude as a bounded fixed point, so the
headroom past onset does not rail. The values remain `(PI)` (the SH-101 pot→feedback law is
unmeasured, research/03 §9.1) and are frozen/versioned per ADR-003 F-14.

The resonance test's F-13 oracle is made honest: it now asserts `loopGainK()` equals the
documented `kMax · reso01^exp` at anchor points (and `reso01 = 0.5 → k = 1.0`), so any future
taper drift fails the suite rather than being rubber-stamped.

## Consequences

- Deviates from the literal §9 values (4.0 / 2.0); those describe the *continuous* model. The
  shipped discrete map is recorded here as the authority. The low/mid documented anchors hold.
- Re-blessing impact: these are bit-exact-bless constants (ADR-003 F-14); this ADR establishes
  the baseline values, so no re-bless of prior goldens is required (filter goldens are blessed
  after this).
- Owner ratification item: none — within the `(PI)` calibration latitude the design grants
  (§5.1 "(PI)", research/03 §9.1); no user-facing scope change.
