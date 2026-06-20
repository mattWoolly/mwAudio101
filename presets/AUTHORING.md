<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Preset authoring conventions (the ~64-preset bank)

Every factory `.mw101preset` is a human-readable JSON projection of the canonical
`MW101_STATE` ValueTree (one file per preset, validated in CI against the parameter
registry). This note is the short, normative checklist each category task follows so
the whole bank is built against the ratified modern-default poles and the project's
honesty discipline.

Single sources of truth: the format and validator are
`plugin/preset/PresetFormat.{h,cpp}` (the §6.4 rules); the parameter set is
`core/params/ParamDefs.h` (`kParamDefs`); the out-of-box poles are
`plan/decisions/016-owner-ratifications-2026-06-18.md` (R-1..R-4); the schema contract
is `plan/decisions/008-parameter-state-preset-schema.md` (C13–C18). `INIT.mw101preset`
in this folder is the baseline every preset starts from.

## File shape

A preset is a JSON object with four top-level keys (`docs/design/06 §6.2`):

```
{ "schemaVersion": 1, "meta": { … }, "params": { … }, "seq": { … }, "arp": { … } }
```

- `params` MUST list **every** live registry ID (all 91 in `kParamDefs`) with a value
  in range (continuous within `[min,max]`; choice/bool an integer index `< choiceCount`).
  A missing ID or an out-of-range value is a hard validation failure (ADR-008 C18).
- `seq` carries `stepCount` + `steps` (`note`/`gate`/`tie`/`rest` only). There is **no**
  per-step `accent` field — its presence rejects the preset (ADR-025).
- `arp` carries `latch`.

## The six valid categories (`meta.category`)

`meta.category` MUST be exactly one of these enum values (ADR-008 C14;
research/11 §7.1). Any other string fails validation.

| Category | Use |
| --- | --- |
| `AcidBassLead` | Squelchy resonant acid bass/lead; high resonance, fast-decay zero-sustain filter envelope, glide; sequencer/arp- and overdrive-friendly. |
| `SubBass` | Independent-level sub-oscillator at -1/-2 oct under a clipped VCA; sits beneath a 303-style line. |
| `Lead` | Bright saw/square leads with resonance, vibrato/PWM motion. (INIT's category.) |
| `PWMStrings` | Mono PWM stylization (LFO-swept pulse width + chorus). Label it a **mono** stylization — the synth has one VCO, so true polyphonic pads are impossible. |
| `BlipsFX` | Very short A/D envelopes on a resonant filter plus noise FX. General-practice character, not a sourced idiom. |
| `SeqArpRiff` | Presets authored together with a stored `seq` pattern / arp settings; the riff is the identity. |

## Out-of-box poles every preset is authored against (ADR-016 R-1..R-4)

Unless a preset deliberately demonstrates a faithful pole, start from the modern
defaults `INIT.mw101preset` selects:

- **R-1 — Control: MODERN-SMOOTH.** `mw101.control.vintage = 0` (Modern). The 6-bit
  "Vintage" stepping is one toggle away.
- **R-2 — Velocity: ON → VCA + VCF.** `mw101.vel.enable = 1`, `mw101.vel.depth` low-mid.
- **R-3 — Voice mode: MONO.** `mw101.voice.mode = 0`. Poly/Unison are the modern toggle.
- **R-4 — Drift: subtle ON, Age LOW.** `mw101.vintage.enable = 1`, `mw101.vintage.age`
  low (in tune, but alive). The `vintage.age` *parameter* default stays 0; INIT moves it
  as a patch.
- **FX: engine-default OFF** (accepted-without-veto). `mw101.fx.bypass = 1`,
  `mw101.fx.*_enable = false`, `mw101.fx.chorus_mode = 0`. FX is **bakeable** into a
  preset where the research prescribes it (notably `PWMStrings` = mono PWM + chorus).

## inspired-by / disputed rule

- Per-artist references render as **inspired-by / disputed**, never "as used on track X"
  (ADR-008 C16; research/11 §7.3). The exact phrase `as used on track` anywhere in
  `name` / `description` / `tags` / `inspired_by` rejects the preset.
- `meta.inspired_by` is JSON `null` when there is no attribution, or a short
  inspired-by string (e.g. `"acid-house idiom"`). It is never a track-reconstruction
  claim. Squarepusher's debut is the one documented track-level exception, and even
  then is cited with care — not as a literal patch reconstruction.
- The Aphex-style "modded character" option is framed **inspired-by**: his real unit was
  heavily customised and his stock-101 track use is undocumented.
- Do **not** name Vince Clarke as an SH-101 user (dropped claim, research/11 §3.6).

## no "TB-303 filter" rule

- No preset text may ship a `TB-303 filter` descriptor (ADR-008 C16; research/11 §4.2).
  The phrase rejects the preset. The SH-101's self-oscillating VCF is the **IR3109**
  (shared with the Juno-6/60 and Jupiter family); it is sonically adjacent to but
  circuit-distinct from the TB-303's discrete diode-ladder filter. Describe the filter
  that way, never as "the 303 filter".

## sound_ext rule

- `meta.sound_ext` MUST be `true` **iff** the preset uses a software-only feature, and
  `false` otherwise — the validator enforces the "iff" both ways (ADR-008 C15).
- The software-only features are the artifacts that were **not** on the 1982 hardware
  (research/11 §6.1, §6.2):
  - `mw101.vco.range` index ≥ 4 — the `32'` / `64'` registers (hardware = 16'/8'/4'/2').
  - `mw101.lfo.shape` index 4 — the `Sine` shape (hardware = Tri/Sq/Random/Noise).
- A preset that touches neither sets `sound_ext: false` (as `INIT.mw101preset` does).
  A preset that uses either MUST set `sound_ext: true`, and must scope the feature as a
  "software-style" extension in its description — never as vintage hardware behaviour.
