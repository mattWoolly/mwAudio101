<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# mwAudio101

A circuit-accurate, GPLv3, cross-platform software synthesizer inspired by the Roland SH-101
(monophonic analog synth, 1982), extended with the modern essentials a plugin needs to be
market-viable — built by an AI agent fleet following the [`orchestration-notes/`](orchestration-notes/)
playbook.

- **Formats:** VST3, AU (macOS), CLAP, Standalone (LV2 = goal-tier on Linux)
- **Engine:** JUCE / C++20, circuit-accurate analog modeling (IR3109-class 4-pole filter,
  VCO exponential converter, sub-osc divider, per-voice drift)
- **Modern essentials:** poly/unison, oversampling, Chorus/Delay/Drive, host-synced arp +
  100-step sequencer, MPE-lite, full host automation, ~64 curated IDM/acid-leaning presets
- **Platforms:** macOS arm64 (reference) · Linux x64 (co-required) · Windows x64 (goal)

## Status

Project bootstrap. The pipeline runs in six gated phases (see `plan/ORCHESTRATION.md`):
Research → Architecture/ADRs → Backlog → Development → QA → CI.

## For agents / contributors

Start with [`CLAUDE.md`](CLAUDE.md) → [`AGENTS.md`](AGENTS.md) →
[`plan/ORCHESTRATION.md`](plan/ORCHESTRATION.md). Design spec:
[`docs/superpowers/specs/2026-06-17-mwaudio101-design.md`](docs/superpowers/specs/2026-06-17-mwaudio101-design.md).

## Trademark & license

"Roland" and "SH-101" are trademarks of Roland Corporation. mwAudio101 is an independent,
unaffiliated work that models *documented circuit behavior*; it ships under its own name with a
modern (non-replica) UI. Licensed **GPL-3.0-or-later** (see [`LICENSE`](LICENSE)).
