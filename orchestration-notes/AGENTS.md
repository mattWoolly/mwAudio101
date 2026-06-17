# Operating rules (drop-in)

Condensed rules for any agent working on this project. Copy this into the project root as
`AGENTS.md`/`CLAUDE.md`, or point agents at it. Full rationale: `README.md` of the playbook.

## Always

- **Do exactly the task's scope. No scope creep.** If you want to do more, it belongs to
  another task — name it in `## Out of scope`, don't do it.
- **Trace-or-deviate:** every substantive technical claim must cite a research/source doc
  (with a section anchor) OR be a deliberate deviation recorded in an ADR. No third option.
- **Never commit to `main` during development.** Always branch + PR. (Setup-phase artifacts —
  research, design/ADRs, backlog authoring — may go direct to main with a typed subject prefix.)
- **Verify locally before any PR:** `configure → build → run scoped tests`, all green. Paste
  the real command output + pass counts into the PR. No "it works" without evidence.
- **One task = one file = one branch = one PR.** Branch `task/NNN-short-slug`. PR title
  `NNN: <task title>`.
- **You never mark your own task `done`.** The reviewer stamps `done` at squash-merge.

## Tests (silent-pass prevention — non-negotiable)

- Every `ctest … -R <word>` / `-L <label>` carries `--no-tests=error`. A selector matching
  nothing exits 0 = a fake green. Make it impossible.
- Test-case *names* must begin with the task's tag word (discovery registers names, not tags),
  so `-R <word>` actually selects what you think.
- TDD for core/algorithmic logic: write the test first; it's an acceptance checkbox.
- Weakening an invariant/contract test requires an ADR. The suite is part of the spec.

## ADRs & decisions

- Consequential/cross-cutting/hard-to-reverse decision → an **immutable** ADR
  (`plan/decisions/NNN-*.md`). Supersede with a new ADR; never edit an accepted one.
- The owner-locked decisions table (top of `ORCHESTRATION.md`) sits ABOVE ADRs. An ADR may
  record tension with a lock but must never silently reverse it — flag the owner instead.
- Tag every invented constant `(PI)` and centralize it in a calibration struct/table.

## QA (if you are auditing)

- **Diagnose only — never fix inline.** Every HIGH finding → a new numbered backlog task.
- 3-way verify every finding: reproduce → read source at `file:line` → cite the spec/ADR it
  *Contradicts*. A finding that contradicts nothing → move to Rejected.
- Keep a "Rejected / false positives" section. Verify counts against the built artifact, not
  the prompt.

## CI / cross-platform

- No build/test logic lives only in CI YAML. CI calls the same committed presets/scripts you
  run locally; the mapping is documented in `docs/BUILDING.md`.
- Pin external tools by version + SHA-256 (mismatch = hard fail). Self-skip (exit 77) only
  when a tool is genuinely absent.
- Never build an artifact/format on a platform where no validator for it is wired.
- Re-blessing goldens requires `BLESS_REASON` + a provenance MANIFEST in the same PR.
