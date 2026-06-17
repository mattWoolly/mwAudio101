<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# mwAudio101 — Orchestration Plan

**Project:** mwAudio101 is a circuit-accurate, GPLv3, cross-platform software synthesizer
inspired by the Roland SH-101 (monophonic analog synth, 1982), extended with the modern
essentials a plugin needs to be market-viable (poly/unison, oversampling, drift, integrated FX,
host-synced arp/seq, MPE-lite, ~64 curated presets). Built by an AI agent fleet following the
`orchestration-notes/` playbook. Full design: `docs/superpowers/specs/2026-06-17-mwaudio101-design.md`.

> **Trademark.** "Roland" / "SH-101" are trademarks of Roland Corporation. mwAudio101 is an
> independent, unaffiliated work modeling *documented circuit behavior*; it ships under its own
> name with a modern (non-replica) UI. No Roland marks or trade dress are used.

## Decisions locked with the project owner (2026-06-17)

> This table sits ABOVE the ADRs. An ADR may record tension with a locked item but must never
> silently reverse it — re-affirm the lock or flag the owner for ratification.

| Decision | Choice |
|---|---|
| Product type | **mwAudio101** — circuit-accurate SH-101–inspired monosynth + modern essentials |
| Framework / language | **JUCE**, C++20, CMake |
| Plugin formats | VST3, AU (macOS), CLAP, Standalone; **LV2** = goal-tier (Linux) |
| DSP fidelity | **Circuit-accurate analog modeling** (IR3109 filter, VCO exp converter, sub-osc divider, per-voice drift, VCA taper) |
| Feature scope | Faithful mono SH-101 path + modern essentials (poly/unison, oversampling, drift, Chorus/Delay/Drive, host-synced arp+100-step seq, full automation, MPE-lite, preset browser) |
| License | **GPL-3.0-or-later** (JUCE free tier; binaries distributable/sellable) |
| Calibration oracle | Schematics + service manual + IR3109 datasheet + published measurements. Honest claim: *"modeled from documented circuit behavior."* Recordings = secondary, local-only, gitignored cross-check only. |
| Target platforms | **Reference (bless + bit-exact): macOS arm64.** Co-required (hard gate): Linux x64. Goal (best-effort): Windows x64. |
| Budget / depth | Deep research + aggressive agent fleet (up to ~8–12/wave) + multi-dimension adversarial QA. |
| Repo workflow | Research/plan/ADRs/backlog → main directly. Dev tasks → branch + PR + agent review → merge. |
| CI timing | Added LAST (it slows local iteration); local build/test until then. |
| Out of scope (now) | mod matrix, 2nd LFO/env, multi-FX rack, macros, randomizer, wavetable/sampler, iOS/AUv3 — each deferred via a future ADR. |

## Phases

1. **Research** (`docs/research/`) — deep-research workflow, adversarially verified + cited.
   Cache runnable references in a gitignored `research-cache/`. Two tracks: (a) technical circuit
   (VCO/exp-converter, IR3109 4-pole filter, sub-osc divider, noise, mixer, ADSR, LFO, VCA, arp,
   100-step seq, glide, mod/bender, power/CV), (b) cultural influence (IDM/Aphex Twin, acid,
   notable records & artists, sound-design idioms).
2. **Architecture** (`docs/design/` + `plan/decisions/`) — agent panel (2–4 personas) proposes
   competing designs; the team recommendation wins; ADRs capture each position + critique. Number
   every design-doc section as a permanent citation address space.
3. **Backlog** (`plan/backlog/`) — atomic task files, each independently executable by one agent
   with minimal context. See `plan/backlog/README.md`.
4. **Development** — agents pull backlog tasks in wave order. Each task: branch → implement
   (TDD where it fits) → local build + tests pass → push → PR → reviewer agent → squash-merge.
   Parallel tasks use git worktrees.
5. **QA** — adversarial QA fleet produces `docs/QA-REPORT.md`; HIGH findings spawn tasks. Place
   freeze-gate QA (e.g. filter golden corpus) as early as its deps allow.
6. **CI** — GitHub Actions mirroring local presets 1:1; macOS + Linux hard-gate, Windows goal.

**Hard gate before Phase 3:** no task files until this decisions table exists and the governing
ADRs are `accepted`.

## Operating rules for agents

- Tasks are defined in `plan/backlog/NNN-*.md`. **Do exactly the task's scope; no scope creep.**
- **Never commit directly to main during the dev phase**; always branch + PR.
- Branch naming: `task/NNN-short-slug`. PR title: `NNN: <task title>`.
- **Local verification before any PR:** configure + build + scoped tests must pass; paste real
  output + pass counts. No "it works" without evidence.
- **Trace-or-deviate:** every substantive technical claim must trace to `docs/research/` (with
  section anchors) or be a deliberate deviation recorded in an ADR. No third option.
- Every `ctest -R/-L` selector carries `--no-tests=error`; test names begin with the selector word.
- The in-file `status:` field is the single source of truth for task state — not the filename,
  not the INDEX table.
- Only the reviewer stamps `done`, at squash-merge. An agent never marks its own task done.
- Tag every invented constant `(PI)` and centralize it in the calibration table.
