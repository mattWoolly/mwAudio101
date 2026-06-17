---
id: NNN
title: One-line task title
status: todo | in-progress | in-review | done | blocked
depends-on: [list of task ids that must be `done` first]
component: core | engine | app | ui | infra | qa | docs
estimated-size: S | M | L   # S: <200 LOC, M: <600 LOC, L: needs splitting consideration
---

## Objective
What EXISTS when this task is done, in one or two sentences. Outcome-framed, not
activity-framed: "AudioBuffer and AudioView exist, unit-tested" — not "write a buffer class".

## Context
The minimum a fresh agent needs, as a pointer list in reading order. Do NOT restate design
content here — point at the single source of truth:
- `docs/design/<doc>.md §X.Y` — <what to read it for>
- `docs/research/<report>.md` — <relevant finding>
- `plan/decisions/NNN-<slug>.md` — <governing ADR; the contract IS the spec>
- TDD: write the tests first (`tests/<path>`).

## Scope
- Precise bullets of exactly what to build/change. Name files to create and key signatures.

## Out of scope
- Explicit non-goals. Name the OTHER task id that owns each deferred item, e.g.
  "MIDI audition (035), drag-out export (036)". Cite the ADR for rejected work
  ("X rejected for v1, ADR-003").

## Acceptance criteria
- [ ] Objectively checkable conditions. Tests that must exist and pass; build targets.
- [ ] Invariants ("no allocation on the audio thread"; "no non-stdlib includes in the header").
- [ ] Cite the design § each criterion enforces ("integer-only per §3.2") so review is mechanical.

## Verification commands
```
cmake --preset default
cmake --build --preset default
ctest --preset default -R <tag> --no-tests=error
```
<!-- For concurrency/memory-critical tasks, append a sanitizer run:
ctest --preset tsan -R <tag> --no-tests=error
-->
