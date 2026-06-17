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

| ADR | Title | Status |
|---|---|---|
| _(none yet)_ | populated in Phase 2 | — |
