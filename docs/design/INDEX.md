<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Design index (Phase 2)

Numbered, implementation-ready specs — the **citation address space** the backlog points at
(e.g. "implement per `docs/design/02-dsp-filter.md §3.2`"). Authored by a 15-ADR agent panel +
12 design-doc fan-out, then a coherence pass that reconciled parameter IDs and signatures.
Every substantive claim traces to `docs/research/` or an ADR (`plan/decisions/`).

> **Authority chain:** owner-locked decisions (`plan/ORCHESTRATION.md`) → ADRs
> (`plan/decisions/`, immutable) → these design docs → backlog tasks. Parameter IDs are owned
> by **doc 06 §3.0** (the canonical index); every other doc references those IDs, never re-mints.

## Documents

| # | Doc | Owns |
|---|-----|------|
| 00 | [architecture-overview](00-architecture-overview.md) | Module tree, signal flow, core/plugin seam (ADR-001), threading (019), PDC (017), versioning (023) |
| 01 | [dsp-oscillators](01-dsp-oscillators.md) | VCO (CEM3340), sub-osc (4013 divider), noise; PolyBLEP/minBLEP (ADR-002/018) |
| 02 | [dsp-filter](02-dsp-filter.md) | **IR3109 4-pole VCF** — Huovilainen core, diode-clamp resonance, 2× zone (ADR-003/004) |
| 03 | [dsp-envelope-lfo-vca](03-dsp-envelope-lfo-vca.md) | ADSR, LFO (tri/sq/random/noise), BA662 VCA, velocity routing |
| 04 | [voice-and-control](04-voice-and-control.md) | Voice/VoiceManager, mono/poly/unison, note priority, control model (ADR-005/006/016/019) |
| 05 | [modulation-arp-seq](05-modulation-arp-seq.md) | Mod routing, arpeggiator, 100-step sequencer (note/rest/tie/gate — no accent), host-sync |
| 06 | [parameters-state-presets](06-parameters-state-presets.md) | **The parameter/state/preset contract** + §3.0 canonical ID index (91 params) |
| 07 | [fx-section](07-fx-section.md) | Post-voice Drive→Chorus→Delay (ADR-010/017) |
| 08 | [vintage-variance](08-vintage-variance.md) | Drift/variance model + Age macro (ADR-009/016) |
| 09 | [formats-io-midi](09-formats-io-midi.md) | VST3/AU/CLAP/Standalone/LV2, MIDI/MPE-lite, tuning, `mw::core::MidiEvent` (ADR-011/012/022/024) |
| 10 | [ui](10-ui.md) | Modern vector UI, APVTS binding, preset browser (ADR-015) |
| 11 | [testing-build-ci](11-testing-build-ci.md) | Catch2 + golden harness, bless guard, CMake/CPM toolchain (ADR-013/014/023) |

## Conventions enforced

- Real-time invariants: no heap alloc / no lock on the audio thread; `noexcept` hot paths; sized in `prepare`.
- `(PI)` (pragmatic-invention) constants are tagged and centralize in `core/calibration/Calibration.h`.
- Bit-exactness: integer/control paths bit-exact on macOS+Linux; FP stages blessed on macOS arm64,
  tolerance-compared on Linux (design spec §5; ADR-013).
- Quality is one structural param `{Eco/Standard/HQ}` (ADR-018); `mw101.quality` is the live ID.
