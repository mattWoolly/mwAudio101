<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 189
title: Harden the plugin-side alloc-free checks (mstats net-delta flakes; e.g. #71 gui_datapaths)
status: todo
depends-on: [107, 184]
component: app
estimated-size: S
stream: plugin
priority: low
---

## Objective

The plugin test binary (mw101_plugin_tests) does NOT link the core thread-local AudioThreadGuard
global-new sentinel (that TU is core-only), so plugin alloc-free RT tests (gui_datapaths #71 'processBlock
publishes a telemetry frame with no heap alloc', PresetsRoundtripTest case 4, MpeReconstructorTest) use an
mstats() net-delta heap probe. Net-delta is intermittently sensitive to allocator first-touch / OS memory
return, so #71 flakes rarely under the full plugin suite (passes 4/4 in isolation). NOTE: the plugin suite
is NOT in the main CI matrix (CI is core-only), so this is a local / future-plugin-CI robustness item.

## Scope / options
- Link/compile the AudioThreadGuard global-new sentinel into mw101_plugin_tests so the plugin alloc-free
  tests use the exact per-instruction guard (preferred), OR add a warm-up + retry/median to the mstats
  probe so a single transient page-touch does not trip it, OR mark the affected cases RUN_SERIAL + warm.
- Re-run the full plugin suite repeatedly to confirm the flake is gone.

## Out of scope
- The core AudioThreadGuard tests (already robust + RUN_SERIAL via 184).
