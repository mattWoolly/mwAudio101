<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# <Product> v<N> — QA Report (Adversarial Audit)

**Date:** <date>
**QA lead:** <owner>
**Scope:** <product, core lib, CLI, harnesses/validators audited>
**Method:** <N>-dimension adversarial audit (<dim1>, <dim2>, <dim3>, <dim4>). A fresh,
adversarial auditor per dimension whose job is to *break* the product. Every reported finding
was **3-way verified**: (1) reproduce with a real command/probe, (2) read and cite the source
at exact `file:line`, (3) cite the specific design doc/ADR section it *contradicts* (or the
spec that blesses it). Findings the auditors could not refute are *confirmed*; everything an
auditor knocked down is under *rejected / false positives* so the record shows it was checked.

---

## 1. Executive summary — verdict

<!-- After remediation, prepend a dated UPDATE block and PRESERVE the original verdict verbatim. -->
> **UPDATE <date> — both HIGH findings RESOLVED; v<N> ship-ready.**
> F1 … fixed by task **0NN** (commit `<sha>`). F2 … fixed by task **0MM** (commit `<sha>`).
> Final on-main sweep: <test counts>, sanitizers clean, validators green. The original verdict
> is preserved below for the record.

**Original verdict (pre-fix):** <blunt go / no-go>. <one sentence on what blocks ship>.

**Confirmed findings by severity:** critical N · high N · medium N · low N.

Release is gated on **HIGH + critical only**; medium/low ship as documented known limitations.
Severity is judged against documented core guarantees/contracts — a HIGH violates a stated
ship promise, not an auditor's intuition.

## 2. Confirmed findings

| # | Dimension | Sev | Title | Location | Recommended fix | Contradicts |
|---|-----------|-----|-------|----------|-----------------|-------------|
| F1 | <dim> | **high** | <title> | `<file:line>` | <fix> | <spec §/line + ADR> |
| F2 | <dim> | **high** | <title> | `<file:line>` | <fix> | <spec §/line + ADR> |

> A finding that "Contradicts: nothing" is self-refuting — move it to §3.
> Auditors **diagnose only**. Every HIGH finding becomes a new numbered backlog task (below).

## 3. Rejected / false positives (checked and dismissed)

Investigated and **refuted**. Listed so the record shows they were verified, not skipped.

- **<claim>** (<dimension>). *Refuted.* <reproduction result + refuting evidence + why it's
  the documented contract / a stale premise>. <!-- e.g. verified count against the BUILT
  artifact (485, not the prompt's "499"). -->

## 4. Known limitations / follow-ups

**Created backlog tasks (HIGH findings):**
- `plan/backlog/0NN-<slug>.md` — F1 — status: <…> (PR #__, `<sha>`)
- `plan/backlog/0MM-<slug>.md` — F2 — status: <…> (PR #__, `<sha>`)

Bugs surfaced later (e.g. by CI) from a QA fix get their OWN numbered follow-up tasks — never
a silent patch.

## 5. (PI) / magic-number ledger

```
grep -rn '(PI)' docs/design/
grep -rn '(PI)' libs/ plugin/ tools/ tests/   # code comments
```

Each constant resolves to exactly one of: **Confirmed** (pinned by a named test + tuning note)
or **Ticketed** (mapped to an open backlog task / freeze gate).

**Exit criterion:** every uncited constant is Confirmed or Ticketed — none ships silently
unvalidated.

<!-- If a claim can't yet be validated against ground truth, write a validation plan with an
external primary oracle, disjoint cal/val sets (enforced by tooling), a planted-answer
self-test, and a pre-written FAIL path: "downgrade the 'authentic' label honestly in release
notes; no constant changes; the gate stays ticketed." -->
