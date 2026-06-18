<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Architecture Decision Records

`plan/decisions/NNN-short-slug.md` — **immutable once accepted** (supersede with a new ADR
instead of editing). Numbers are zero-padded 3-digit.

**Write an ADR (not an inline note) when** a decision is cross-cutting, hard to reverse,
owner-level, defines ambiguous feature semantics, sets a now-vs-later scope cut, or is a
normative contract multiple tasks must implement. Keep small/tunable/local choices inline in
design docs, tagged `(PI)`.

**Trace-or-deviate:** every substantive technical claim must cite a research doc or be an
ADR-recorded deliberate deviation. There is no third option.

The owner-locked decisions table (top of `plan/ORCHESTRATION.md`) sits ABOVE every ADR. An ADR
may record tension with a lock but must never silently reverse it — re-affirm the lock or flag
the owner for ratification.

---

```markdown
# ADR NNN: Title

Status: accepted | superseded by ADR-MMM
Date: YYYY-MM-DD

## Context
The question and the forces at play. If it touches an owner-locked decision, say so and
re-affirm the lock.

## Options considered
Brief, honest pros/cons for each. If an agent panel debated this, summarize EACH persona's
position by name, list which critique findings were adopted, and note any split vote and how it
resolved.

## Decision
What we chose and why.

## Consequences
What this commits us to AND what it makes harder / forecloses. Add an "Owner ratification item:"
line if the decision carries user-expectation or scope risk the owner has not explicitly signed off.
```

## Optional: decision-as-contract table

When an ADR defines runtime behavior, embed a normative case table the backlog implements
verbatim, so the task can state "the contract IS the spec".

## ADR ledger

All accepted 2026-06-17/18 (Phase 2). ADRs 016–025 are reconciliation/gap decisions from the
coherence pass; "refined-by" notes appear in the affected ADRs' Status lines.

| ADR | Title | Notes |
|---|---|---|
| 001 | DSP core / plugin-shell boundary & real-time contract | core (mwcore) has zero JUCE deps; prepare/process/reset seam |
| 002 | Anti-aliased oscillator generation | PolyBLEP default + minBLEP HQ (see 018) |
| 003 | IR3109 4-pole VCF modeling method | Huovilainen core; TPT/ZDF = bless oracle |
| 004 | Oversampling strategy | per-voice 2× nonlinear zone (Drive moved out, see 017) |
| 005 | Control-rate & 6-bit CV authenticity | default superseded by 016 (modern-smooth default) |
| 006 | Voice architecture, polyphony & unison | mono default affirmed by 016 |
| 007 | Modulation routing, arpeggiator & 100-step seq | host-sync arp/seq |
| 008 | Parameter / state / preset schema (the contract) | Quality param via 018; load-failure via 021; C8 accent removed by 025 |
| 009 | Vintage variance / analog-drift model | INIT default set by 016 (subtle drift) |
| 010 | Built-in FX section (Chorus/Delay/Drive) | Drive placement/PDC via 017 |
| 011 | Plugin formats & wrapper strategy | LV2/AAX via 024 |
| 012 | MIDI / MPE-lite mapping & tuning reference | velocity default superseded by 016 (on) |
| 013 | Testing: golden/regression + calibration harness | honesty-labels into provenance |
| 014 | Build system, dependency management & toolchain | CPM-pinned JUCE/Catch2/clap-juce-extensions |
| 015 | GUI architecture (modern reimagined, vector) | no faceplate skin |
| 016 | Owner ratifications: out-of-box defaults | supersedes 005/012 default clauses; sets 009 INIT; affirms 006 |
| 017 | Plugin latency (PDC) policy & Drive placement | Drive = post-voice FX; constant reported PDC |
| 018 | Quality-tier parameter registration | one structural Quality {Eco/Standard/HQ} param |
| 019 | Voice-rendering threading model | single-threaded in processBlock for v1 |
| 020 | Parameter smoothing / de-zipper policy | table-driven per-class de-zipper |
| 021 | State / preset load-failure handling | fall back to INIT, never crash, warn |
| 022 | MPE-lite & arp/seq cross-format behavior | per-format fallback contract |
| 023 | Engine versioning, bless comms & blessed sample-rates | renderVersion in state; blessed SR set |
| 024 | LV2 export path & AAX exclusion | LV2 via JUCE-native export; AAX out |
| 026 | Float-domain range/saturation clamps | noise/tanh clamp to honor binding ranges (wave-4 TDD finding) |
| 025 | Sequencer per-step accent — removed for v1 | supersedes 008 C8 accent field; aligns docs 05/06 to 007 (no accent); accent deferred |
