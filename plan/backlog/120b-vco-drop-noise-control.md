<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

---
id: 120b
title: Remove the noise-level control from VcoModule (SourceMixer owns mw101.noise.level)
status: done
depends-on: [120, 121]
component: ui
estimated-size: S
stream: ui
tag: ui_vco
---

## Objective

Close the 120 QA MEDIUM: VcoModule (120) and SourceMixerModule (121) BOTH bind a SliderAttachment
to `mw101.noise.level`, so the assembled UI would show two redundant noise sliders. On the SH-101
noise is a MIXER source level (alongside saw/pulse/sub), so SourceMixerModule (121) is the sole
owner. Remove the noise control + its attachment from VcoModule so the param is bound exactly once.

## Scope

- ui/modules/VcoModule.h + plugin/ui/modules/VcoModule.cpp: delete the noise RotarySlider/Label +
  its SliderAttachment + the kNoiseLevel binding; adjust layoutDesignUnits()/the constants header so
  the freed slot is reclaimed (no dangling layout gap). The VCO module keeps range/tune/fine/pw/
  pwm_depth/sub-mode only.
- tests/plugin/VcoModuleTest.cpp (tag ui_vco): drop the noise assertions; keep all others green.

## Out of scope

- SourceMixerModule (121) — it correctly keeps the noise level; do not touch it.
- Any other module / ParamIDs.h / shared file.

## Acceptance criteria

- [ ] VcoModule no longer binds mw101.noise.level (no kNoiseLevel attachment); SourceMixer remains the sole binder
- [ ] ui_vco tests pass with the noise assertions removed; build green under MW_BUILD_PLUGIN=ON
- [ ] Layout has no orphan gap where the noise control was

## Verification commands

```
cmake -S . -B build/plugin -DMW_BUILD_PLUGIN=ON -DMW101_TESTS=ON -G "Unix Makefiles"
cmake --build build/plugin --target mw101_plugin_tests
ctest --test-dir build/plugin -R ui_vco --no-tests=error
```
