<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 026: Float-domain clamps for half-open / saturation ranges

Status: accepted
Date: 2026-06-18

## Context

Two wave-4 implementations (task 028 `NoiseSource`, task 033 `FastTanh`) surfaced the same
single-precision rounding reality via their TDD tests:

- The noise design's literal scale `(float)x * (2/2^32) - 1` rounds `(float)0xFFFFFFFF` UP to
  exactly `+1.0f`, violating the **binding** half-open `[-1, 1)` range
  (docs/design/01 §6.3, §8 / acceptance criterion).
- The `FastTanh` Padé rational `x*(27+x^2)/(27+9x^2)` equals exactly `1.0` at `x=3` and *diverges*
  (~`x/9`) beyond the validity edge; float rounding can also nudge the result just past `±1` at
  the clamp boundary, breaking monotonicity / boundedness (docs/design/02 §4.2).

These are not algorithm changes — they are the unavoidable last-ulp behavior of IEEE-754 single
precision at a range boundary. The design docs state the *intended* continuous formula; they do
not (and should not) re-derive float boundary handling.

## Options considered

- **Leave the literal formula** — ships a value that violates a binding range contract (noise can
  emit `+1.0`; tanh can exceed `±1`). Rejected: fails the acceptance criterion and could surprise
  downstream stages relying on the stated range.
- **Clamp at the boundary** (chosen) — pin the noise output below `1.0` (`1 - 2^-24`) and pin the
  `FastTanh` result to `±1`. Preserves the stated range/saturation contract exactly; the deviation
  from the literal formula is confined to a vanishingly rare boundary case and is clearly commented.

## Decision

Where a DSP value carries a **binding** range or saturation contract, the implementation clamps in
the float domain to honor that contract, even if the design's continuous formula would (in exact
arithmetic) already satisfy it. The clamp constants are `(PI)`-tagged and live in the module's
calibration header. This generalizes to future range/saturation-bounded outputs (oscillator levels,
filter self-oscillation amplitude, VCA output).

## Consequences

- Every range/saturation contract in the design docs is honored bit-safely; tests assert the bound
  rather than the literal formula at the boundary.
- A negligible, documented divergence from the literal formula exists at the extreme boundary value
  only — the audible/spectral effect is nil.
- Reviewers must treat a boundary clamp that enforces a stated contract as conformant, not as scope
  creep or an undisclosed change. The single-table `(PI)` discipline still applies (no inline magic).
