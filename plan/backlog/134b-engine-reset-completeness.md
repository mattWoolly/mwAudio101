<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 134b
title: Complete Engine::reset() to a deterministic fixed point (reset all consumed modules)
status: done
depends-on: [118, 118b, 074, 071]
component: core
estimated-size: S
stream: integration
tag: engine_reset
---

## Objective

`Engine::reset()` returns the assembled engine to its known start WITHOUT re-prepare:
clearing the consumed VoiceManager and ControlCore (not just keys_/fx_/scratch), so two
divergent histories followed by `reset()` + the same input produce bit-identical output
(the §5.5 known-start contract holds via `reset()`, not only `prepare()`).

## Context

- Originating finding: wave-12 QA on PR #75 (task 134) — `core/Engine.cpp` `reset()` clears only
  keys_/fx_/scratch; VoiceManager + ControlCore state is left dirty, so `reset()` is not a
  deterministic fixed point (two histories + reset + identical block differ by ~0.28 max-abs).
- Verified facts (read first): `core/Engine.cpp` `reset()` (the incomplete clear);
  `core/voice/VoiceManager.{h,cpp}` `reset()` (added by 118b — panic/all-notes-off);
  `core/control/ControlCore` (check for a public `reset()`; add one if absent, mirroring what
  `prepare()` zeroes: sampleCounter_/tickCount_/xfade_/jitter seed). Design: `docs/design/00 §5.5`.
- TDD: write the failing test first under `tests/`; test names begin with `engine_reset`.

## Scope

- `Engine::reset()` calls `voices_.reset()` and `control_.reset()` (add `ControlCore::reset()`
  if it does not exist, re-deriving the same known start `prepare()` establishes) plus the
  existing keys_/fx_/scratch clears. Keep `reset()` noexcept and alloc/lock-free.

## Out of scope

- JUCE/plugin reset marshalling (plugin stream); re-blessing goldens (none affected — reset is
  a runtime path, not a blessed render input).

## Acceptance criteria

- [ ] After two divergent play histories, `reset()` + an identical block yields BIT-IDENTICAL
      output to a freshly-prepared engine fed the same block [§5.5].
- [ ] `Engine::reset()` (and any new `ControlCore::reset()`) is noexcept and allocates/locks
      nothing under an armed AudioThreadGuard.
- [ ] Strengthen the task-134 lifecycle fuzz's reset assertion to exercise `reset()` (not only
      the `prepare()` re-init path).

## Verification commands

```
cmake --preset default
cmake --build --preset default
ctest --preset default -R engine_reset --no-tests=error
```
