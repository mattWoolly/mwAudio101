<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 129b
title: UI signposts: 440-vs-442 tuning-duality banner + 'running unblessed at this host rate' banner
status: done
depends-on: [129, 112, 115]
component: ui
estimated-size: S
stream: ui
tag: ui_signpost
---

## Objective

Surface two non-modal UI signposts via the StatusBanner: the 440-vs-442 tuning-duality note (so users do not mistrust tuning) and the 'running unblessed at this host rate' notice when the host SR is above the blessed set or 2x oversampling is clamped to 1x at OS_CEILING.

## Context

- `docs/design/09 §5` — read first
- `docs/design/00 §8.5` — read first
- `ADR-023 V16` — read first
- `ADR-012 §Consequences` — read first
- `plan/ORCHESTRATION.md` — owner-locked decisions; `AGENTS.md` — operating rules (scope, TDD, silent-pass).
- TDD: write the failing test(s) first under `tests/`; test-case names begin with `ui`.

## Scope

- Tuning-duality signpost: a non-modal banner/affordance noting the A4 440-default vs 442-hardware-accurate duality, shown contextually (e.g. when the 442 preset/param is active), driven on the message thread via the StatusBanner (129) AsyncUpdater path (§5; ADR-012 §Consequences)
- Unblessed-rate banner: consume the engine/CapabilityShim-published provenance flag (via 112's atomic-pointer UI publish, polled by the telemetry timer 115) and surface 'running unblessed at this host rate' when host SR is above the blessed {44.1/48/88.2/96k} set or the OS factor is clamped to 1x at OS_CEILING (§8.5; ADR-023 V16)
- Both signposts are non-modal, message-thread only, reuse StatusBanner severity/dismissal, and never block host load (§9.4 reuse)
- Reduce-motion / dismissal behavior consistent with the existing banner; strings are static (no audio-thread work)

## Out of scope

- The StatusBanner widget itself (129)
- Computing/publishing the unblessed-rate flag in the engine (owned by core/CapabilityShim provenance, surfaced by 112)
- Authoring the 442 preset or the tuning param wiring (104b)

## Acceptance criteria

- [ ] The 440-vs-442 tuning-duality is surfaced as a non-modal signpost on the message thread, never a modal dialog [docs/design/09 §5; ADR-012 §Consequences]
- [ ] When host SR is above the blessed set or OS is clamped to 1x at OS_CEILING, a 'running unblessed at this host rate' banner appears, driven by the published provenance flag (112) [docs/design/00 §8.5; ADR-023 V16]
- [ ] Both banners reuse StatusBanner async/dismissal and never block load; updates occur via AsyncUpdater/ChangeBroadcaster (§9.4)
- [ ] Tests named ui_banner_signpost* assert both signposts render non-modally and react to the unblessed-rate flag; verify via 'ctest --preset default -R ui_banner_signpost --no-tests=error'

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R ui --no-tests=error
```
