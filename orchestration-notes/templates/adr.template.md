<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Architecture Decision Records

`plan/decisions/NNN-short-slug.md` — **immutable once accepted** (supersede with a new ADR
instead of editing). Keep this README defining the format; numbers are zero-padded 3-digit.

**Write an ADR (not an inline note) when** a decision is cross-cutting, hard to reverse,
owner-level, defines ambiguous feature semantics, sets a now-vs-later scope cut, or is a
normative contract multiple tasks must implement. Keep small/tunable/local choices inline in
design docs, tagged `(PI)`.

**Trace-or-deviate:** every substantive technical claim must cite a research doc or be an
ADR-recorded deliberate deviation. There is no third option.

---

```markdown
# ADR NNN: Title

Status: accepted | superseded by ADR-MMM
Date: YYYY-MM-DD

## Context
The question and the forces at play. If it touches an owner-locked decision, say so and
re-affirm the lock (do not reverse it here).

## Options considered
Brief, honest pros/cons for each. If an agent panel debated this, summarize EACH persona's
position by name, list which critique findings were adopted, and note any split vote and how
it resolved. (This is the deliberation audit trail, not just the verdict.)

## Decision
What we chose and why.

## Consequences
What this commits us to AND what it makes harder / forecloses (the cost side, not just the
benefit). Add an "Owner ratification item:" line if the decision carries user-expectation or
scope risk the owner has not explicitly signed off on.
```

## Optional: decision-as-contract table

When an ADR defines runtime behavior, embed a normative case table the backlog implements
verbatim, so the task can state "the contract IS the spec":

| Case | Mode A | Mode B |
|---|---|---|
| <condition> | <behavior> | <behavior> |

Follow with "Further contract terms:" bullets and an explicit ordering constraint.
