<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 131b
title: Add a 'hardware-accurate' tune.a4=442 reference preset (closes the 131 §10.3 content gap)
status: in-review
depends-on: [131, 144, 025]
component: docs
estimated-size: S
stream: presets
tag: presets_bank
---

## Objective

131 confirmed NO factory preset sets mw101.tune.a4=442, so the §10.3 'hardware-accurate' 442 Hz
reference preset (the 440-vs-442 tuning-duality the 129b signpost + ADR-012 reference) does not exist.
Author ONE such preset into an existing category so the bank includes the documented 442 Hz identity.

## Context

- `docs/design/06 §10.3` (the 'hardware-accurate' tune.a4=442 preset) + `plan/decisions/012` (440/442 duality).
- `presets/` — the 6 category folders; pick the most fitting (e.g. a SubBass or AcidBassLead variant
  named to signal the 442 Hz hardware-reference tuning). FOLLOW presets/AUTHORING.md + the honesty rules.
- It is a full canonical projection (all 91 kParamDefs IDs, in range) like its siblings, differing in
  meta + tune.a4=442; the 131 BinaryData mirror auto-embeds it (the 1:1 mirror test will require it).

## Scope

- Add ONE .mw101preset under an existing category folder with mw101.tune.a4=442, a meta.description
  noting the 442 Hz hardware-accurate reference (honesty discipline: no 'as used on track', no
  'TB-303 filter'; inspired_by null-or-careful), valid §6.5 category, sound_ext consistent.
- The 131 factorypresets §10.3 check (currently a WARN) flips to a hard assertion (a 442 preset exists).

## Acceptance criteria

- [ ] Exactly one bank preset sets tune.a4=442 with the 'hardware-accurate' framing; it validates through loadPresetJson (registry-complete, in-range, honesty-clean) [§10.3]
- [ ] The 131 factorypresets test's §10.3 442 check is a hard assertion (not a WARN) and passes
- [ ] presets_bank / factorypresets / the 1:1 mirror all stay green with the new file embedded
