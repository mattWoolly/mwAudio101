<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 188
title: Speed up the dispatch_complete audit renders (perf debt; 186 timeout-mitigated)
status: todo
depends-on: [165, 186]
component: qa
estimated-size: M
stream: dsp
priority: low
---

## Objective

The 165 completeness-audit cases render minutes of audio per param battery — #188 (exempt-param audit)
~570s and #185 (FX audit) ~461s even on fast macOS ARM. 186 mitigated the linux-x64 CI timeout with a
3000s ceiling, but the single-test times remain unhealthy (every full-suite run, local + CI, pays ~9.5min
for one test). Reduce the render budget (shorter signals / fewer redundant sweeps) WITHOUT weakening the
all-91-params "measurably affects output" non-vacuity guarantee.

## Scope / constraints
- Investigate whether shorter renders + tighter measurement windows still prove each param's effect
  (Goertzel/variance with adequate SNR). Trim only where the proof survives; KEEP the audit exhaustive.
- The 165 DispatchCompleteTest is the authority on "all params affect output" — do NOT drop coverage of
  any param. If a render must stay long for SNR, leave it + document why.
- Re-confirm dispatch_complete stays green + non-vacuous; full core suite green.

## Out of scope
- The 186 timeout (already in place as the safety net).
