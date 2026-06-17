<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# IR3109 4-Pole VCF (the tone-defining module)

## 1. Scope and status

This document is the citable source of truth for the Roland SH-101 voltage-controlled
filter (VCF) as it pertains to the open-source mwAudio101 project. It covers the IR3109
four-pole OTA lowpass topology, its diode-clipped external resonance network, its
self-oscillation behaviour, its lineage within the Juno-6/60/Jupiter IR3109 family, and
the component values reconciled directly from the official Roland service notes.

The filter is THE tone-defining module of the instrument, so the precision and honesty of
this document directly bound the fidelity of any clone. Where a figure is disputed,
inferred, or sourced from a clone rather than the original part, it is labelled inline. We
hold NO physical-unit bench measurements (project decision): anything that would require an
oscilloscope, network analyser, or spectrum analyser is flagged as an OPEN VALIDATION GAP,
not a delivered fact.

### 1.1 Frozen schematic facts (from reconciliation)

The following are FROZEN after primary-source reconciliation against the synthfool 3000px
GIF scans of the official Roland SH-101 Service Notes (First Edition, Nov 1 1982; boards
OPH177-1/-2/-3) and the ManualsLib structured extraction [SM, high] [SF7, high] [ML, high]:

- The VCF chip is the **IR3109 = IC14** [SF7, high] [ML, high]; the VCO is the
  **CEM3340 = IC13** (the earlier "IC3" reading was a wrong OCR) [SF7, high]; the CPU is
  **IC6 (TMP80C49P)** [ML, high].
- The VCA is the **BA662/BA662A = IC15** [SF7, high] [ML, high].
- The VCF integrator/pole capacitors are **240 pF**, designators **C47, C48, C50, C51**
  (note: NOT a contiguous C47-C50 range; C49 is a separate 10 uF/16 V cap) [SF7, high].
- The thermistor **TH1 (SDT-1000)** sits in the **VCF cutoff-CV path** (in series with
  R127 5.6 k and R126 1.8 k, near TR28), NOT in the VCO pitch-CV path; the CEM3340 is
  on-die temperature-compensated, so the OTA-based IR3109 VCF is the part that needs the
  external thermistor [SF7, high] [SM, high].
- The topology is the **Juno-6/60 68 k + 240 pF ladder** with **diode-clipped resonance**
  [SF7, high] [ED, reverse-engineered].

## 2. Device identity and topology

### 2.1 The IR3109 quad-OTA IC

The SH-101 VCF is built around the **IR3109** (IC14), a DIP-16 quad operational
transconductance amplifier (OTA) IC developed by International Rectifier to Roland's
specification, first used in the Jupiter-4 (March 1979) to replace hand-matched discrete
BA662 OTAs [SM, high] [AMS-IR, high] [EM-IR, high] [PN, high]. Internally it integrates:

- four matched variable-transconductance amplifiers,
- four high-impedance P-channel MOS output buffers (one after each OTA), and
- an on-chip antilog (exponential V-to-I) converter that derives all four OTA bias
  currents from a single cutoff control voltage [SM, high] [AMS-IR, high].

The chip identity (IC14 = IR3109) is confirmed directly on the service-manual schematic and
in the parts list (part 15229801, "IR3109 VCF") [SF7, high] [SM, high].

### 2.2 Four-pole 24 dB/octave cascade

The four OTA+buffer cells are cascaded externally as four one-pole RC integrator stages.
Each stage is an OTA acting as a voltage-controlled resistor feeding an integrating
capacitor, buffered before the next stage. The four cascaded one-pole sections give a
**24 dB/octave (4-pole) lowpass** [FA, high] [ED, high] [SM, high].

Each pole contributes approximately 45 degrees of phase shift at the cutoff frequency, so
all four total 180 degrees at cutoff [ED, theory]. This 180-degree total is the reason the
resonance feedback must be **inverting** (constructive) rather than non-inverting — see
Section 4. The 45-degrees-per-pole / 180-degrees-total relationship is standard analog-filter
theory applied to the topology by Electric Druid; it is **[theory/inference, unmeasured]**,
not a measured SH-101 phase plot.

This identical 4-pole core is reused across most Roland IR3109 designs "without significant
changes" [ED, high].

### 2.3 Pole component values

Read directly off the high-resolution VCF crop of the official service notes [SF7, high]:

- Pole integrator capacitors: **C47, C48, C50, C51 = 240 pF each** [SF7, high].
- Pole input resistors: **R114, R117, R118, R121 = 68 k each** [SF7, high].
- Per-stage series resistors: **R115, R116, R120, R123 = 560 ohm each** [SF7, high].

These match the Juno-6/60 standard 68 k + 240 pF ladder independently cited by Electric
Druid from the IR3109 reference design (which also notes 240 pF is an E24 value; 220 pF is
an acceptable ~9% substitute) [ED, high]. The earlier OCR token "260p" was a misread of
"240p", and the earlier single-reader "C47-C50" framing was a designator-range error — the
caps are C47/C48/C50/C51, with C49 being a separate 10 uF/16 V cap [SF7, high].

> NOTE ON PROVENANCE: In the first research pass these component designators could not be
> confirmed from any text-extractable source because the service-manual PDF is image-only
> and would not OCR. They were subsequently FROZEN by direct reading of the synthfool
> 3000px scan at sufficient resolution [SF7, high]. They are no longer single-reader-inferred.

## 3. Cutoff control and modulation

### 3.1 Range and CV scaling

The documented cutoff frequency range is **10 Hz to 20 kHz** [SM, high] [RS, high]. The
cutoff control "cuts down to zero" at the low end [SM, high].

The IR3109's internal antilog converter makes cutoff **exponential** in the applied CV.
Keyboard tracking is factory-calibrated to **1 V/octave** via trimmer **VR8 ("VCF WIDTH")**:
the service-manual VCF adjustment procedure holds A4, sets CUTOFF FREQ to ~1 kHz, then
adjusts VR8 while alternately playing F4 and F5 "until the F5 figure cycle is twice the F4
cycle" — i.e. one octave of keyboard equals one octave (2x) of cutoff [SM, high].

> CONFIDENCE NOTE: The specific "VR8 VCF WIDTH" designation and the exact "1 V/oct" figure
> derive from the service-manual ADJUSTMENT section. 1 V/oct is a safe Roland-standard prior
> and is consistent with the exp-converter architecture, but in the first pass the VR8
> procedure could not be read in the image PDF; it was subsequently confirmed in the
> reconciliation pass (VR8 = 47 k8 in the VCF-WIDTH/resonance area) [SF7, high].

### 3.2 Temperature compensation (TH1 / SDT-1000)

Cutoff tracking is temperature-compensated by the **SDT-1000 thermistor (TH1)** in the
cutoff-CV path, in series with R127 (5.6 k) and R126 (1.8 k), near TR28 [SF7, high]
[SM, high]. This is the canonical Roland split: the CEM3340 VCO carries on-die temperature
compensation (a TEMPCO GEN block) and needs no external thermistor, whereas the OTA-based
IR3109 VCF has no on-die tempco and relies on TH1 to stabilise cutoff tracking [SF7, high]
[CEM-DS, high].

> CORRECTION (part-number transposition): an earlier dossier listed TH1/SDT-1000 as part
> 12389800; the correct Roland part number is **15229908**. Part 12389800 is actually the
> 6 MHz ceramic resonator [SF8, high].

### 3.3 Keyboard tracking (Key Follow)

Key Follow is variable **0-100%** [SM, high] [RS, high]. At 100% the cutoff tracks the
keyboard 1:1 in V/oct (the VR8 calibration target); at 0% the cutoff does not follow the
keyboard [SM, high].

### 3.4 Envelope and LFO modulation

Cutoff is modulated by a single **ADSR envelope generator shared between the VCF and the
VCA** (ENV depth) and by the **LFO/clock** (MOD depth, LFO/CLK rate 0.1-30 Hz), summed with
the keyboard CV and the CUTOFF FREQ control into the filter's cutoff CV [SM, high]
[WP, high] [RS, high].

Two honesty caveats apply here:

- The exact calibrated **modulation depths** the ENV-depth and MOD-depth knobs apply to the
  cutoff CV (in V/oct or Hz) are **NOT documented numerically** in any authoritative source
  — only control ranges are published. This is confirmed as a documentation gap, not a
  delivered figure [SM, confirmed-gap].
- That the ENV control is **unipolar (positive-only)** is **[theory/inference, unmeasured]**:
  it is consistent with general synth design (no center detent, no documented negative-ENV
  mode) but is NOT stated by any SH-101-specific primary source [WP, inference]. The
  summing of ENV + LFO at the cutoff CV node is architecturally implied but not explicitly
  documented in a primary source.

## 4. Resonance, Q compensation, and self-oscillation

### 4.1 No on-chip resonance — external feedback network

The IR3109 provides only the four cascaded poles; it has **NO on-chip resonance control**
[ED, high] [AMS-IR, high]. The SH-101 builds resonance externally: the filter output feeds
a transistor **phase-splitter (TR26)** producing inverted and non-inverted copies; the
inverted copy is fed back to the filter input to create regeneration [SF7, high]
[ED, reverse-engineered]. Because total phase is 180 degrees at cutoff (Section 2.2), the
feedback must be inverting for positive Q.

> CONFIDENCE NOTE: The phase-splitter-plus-feedback resonance mechanism is corroborated by
> Electric Druid's reverse engineering (which labels the splitter generically "TR2") and by
> OpenMusicLabs PCB analysis [ED, reverse-engineered]. On the Roland schematic the
> resonance/feedback transistor cluster (TR26/TR27 and associated diodes) is visible
> adjacent to IC14, but the exact designator-to-function mapping and the resonance-feedback
> node were NOT traced unambiguously at the resolution obtained — this specific topology
> claim is **uncertain pending a higher-DPI trace** [SF7, partly-inferred]. Treat the
> phase-splitter/diode-clamp description as reverse-engineered, not Roland-primary.

### 4.2 Diode-clipped amplitude limiting

As resonance is raised, **clipping diodes to ground** in the feedback path begin to conduct
and limit the loop amplitude, preventing the oscillation from slamming the rails and keeping
self-oscillation a fairly clean sine with only mild distortion [ED, reverse-engineered]
[AMS-AM, reverse-engineered].

> MECHANISM CORRECTION (important): It is NOT correct to describe the SH-101's limiting as
> "OTA soft-clipping PLUS diodes." Electric Druid explicitly contrasts the two chips: the
> Juno uses OTA soft-clipping; the SH-101's limiter is the **diode clamp to ground** (driven
> via the resonance-network transistor), which "fulfils the same purpose as the OTA clipping
> in the Juno" but "doesn't cause huge levels of distortion ... because it reduces the level
> as soon as it starts to conduct." So the SH-101's primary resonance limiter is the diode
> clamp, not OTA soft-clip [ED, reverse-engineered] [AMS-AM, reverse-engineered].

### 4.3 Q compensation is OUTPUT-side, not input-side

OTA 4-pole filters lose passband level as resonance increases. The Juno-6/60 and Jupiter-8
compensate by boosting the signal re-injected into the filter **INPUT** (input-side Q
compensation). The SH-101 instead routes the non-inverted phase-splitter copy to the filter
**OUTPUT** (VCA drive), so raising resonance increases the level going into the VCA rather
than into the filter [ED, high] [AMS-AM, reverse-engineered].

This reconciles an apparent conflict in the literature: AMSynths describes the SH-101 as
having "no Q compensation," meaning no INPUT-side compensation like the Juno/Jupiter; Electric
Druid and the AMSynths AM8101 page confirm an OUTPUT-level boost compensation does exist. The
two statements are reconciled by which side is boosted [ED, high] [AMS-AM, high]
[AMS-IR, high]. This output-side topology is widely credited as a key contributor to the
SH-101's distinct resonance character.

### 4.4 Tonal character — honest labelling

Resonance is fully variable from **0 to self-oscillation** [RS, high] [SM, high]. At high
settings the filter self-oscillates as a near-pure sine.

> HONESTY LABEL (tonal framing): The popular descriptor that the SH-101 is "drier / rawer /
> more aggressive" than the Juno-60 or Jupiter-8 is **editorial, NOT sourced**. No
> authoritative source uses "drier" or "rawer." What sources actually say: the SH-101 is
> "fatter, glassier," gets "more squelchy/plasticy" with resonance (the Juno gets "more
> nasal"), and is admired for a "growl-like aggressive filter sound" — and that single use of
> "aggressive" is attributed to **LFO-into-cutoff modulation**, not to the filter's intrinsic
> clipping character. At low resonance the Juno/Jupiter differences are "quite small"
> [ED, refuted-as-worded] [AMS-AM, medium] [VS, community] [WP, high]. The genuine,
> defensible technical claim is that the SH-101 has a distinct resonance character driven by
> its **output-side Q compensation** and diode-clamp limiting, on the **same IR3109 chip** as
> the Jupiter-8 — i.e. it is an implementation difference, not a chip difference [FA, high]
> [ED, high].

### 4.5 Self-oscillation amplitude — clone figure only

A self-oscillation amplitude of **~20 V peak-to-peak** is sometimes quoted. This is a
**[clone-derived: AMSynths AM8101 / Alfa AS3109, NOT original-equal]** figure: it is
single-sourced from the AMSynths AM8101 module page and explicitly describes that module
running on **±12 V rails** with the AS3109 clone — NOT a measurement of an original SH-101,
which runs on a low-voltage internal rail scheme (Section 6). It cannot be assumed for the
original instrument [AMS-AM, low].

## 5. Device electrical figures — clone vs original

The original International Rectifier IR3109 datasheet is **not publicly archived**:
datasheetarchive.com returns zero IR3109 sheets, and Polynominal states "IR3109 Datasheet not
available" [DSA, high] [PN, high]. A **minimal** datasheet does exist, reproduced in the
Jupiter-4 service manual, and is the source of the only published OEM electrical figures
[AMS-IR, high].

### 5.1 Published (partial, second-hand) original-device figures

- Transconductance variable range: **1 uA to 10 mA** [AMS-IR, medium].
- Input offset voltage: **less than ±3 mV** [AMS-IR, medium].

These are the only published electrical figures and are partial and second-hand (relayed via
AMSynths from Roland's minimal sheet, not from an OEM Sharp/International Rectifier datasheet).
Various web pages also quote a transconductance "1u to 10" with **inconsistent units** (uS vs
siemens vs amps); treat any such figure cautiously [PN, low] [AMS-IR, medium].

### 5.2 Clone-derived figures (Alfa AS3109) — label explicitly

The following are **[clone-derived: Alfa AS3109/AMSynths, presumed-equal]** — they come from
the Alfa AS3109 clone datasheet "Electrical Characteristics" and from AMSynths modules built
on the AS3109, NOT from the original IR3109:

- Per-stage output-buffer drive currents: **stages 1 and 3 ~0.6 mA, stage 2 ~1.0 mA, stage 4
  ~1.3 mA** [ED, clone-derived] [AMS-IR, clone-derived]. The non-identical buffers are why
  Jupiter designs sometimes tap stages 2 and 4; the SH-101 takes its output after stage 4.
  Relevant for faithfully modelling per-stage headroom/clipping.
- Self-oscillation ~20 Vpp on ±12 V (see Section 4.5) [AMS-AM, low].

These AS3109 figures are presumed to mirror the original IR3109 but are **not proven** for
the original part.

## 6. Power-rail context (from reconciliation)

For completeness, the VCF runs in the SH-101's analog-rail environment, reconciled from the
official service notes [SF7, high] [SF2, high] [SM, high]:

- External input: **DC 9-12 V** (or 6 x 1.5 V drycells) via an internal DC/DC converter
  (part 12449224, "S1671140 coil DC/DC converter") [SM, high] [SF8, high].
- Analog rails: **+15 V / -15 V** (the +15 V is read directly off the schematic and feeds the
  CEM3340 tune network; the -15 V is the standard negative rail for the dual-supply
  CEM3340/IR3109/op-amps) [SF7, high] [SF2, high].
- Additional rails on the synth-board connector: **+14 V, +9 V, +5 V (logic)**, plus the
  CEM3340-side **-5 V and -2.5 V** lines [SF7, high] [SF2, high]. The +14 V and -5 V sub-rails
  are now PRIMARY-SOURCE VERIFIED (they were earlier mislabelled as low-confidence third-party
  hearsay) [SF7, high].
- Memory backup is a **1.8 V x6 battery string**, NOT a "+5 V backup rail" (a recurring
  earlier error). +5 V is the CPU logic VCC [SF2, high].

> CONFIDENCE NOTE: The exact regulator chain that generates each sub-rail from the single
> DC/DC converter was only partially reconstructed; the rail SET is frozen but the precise
> generation method per rail remains an OPEN VALIDATION GAP [SF7, medium].

## 7. Lineage and family comparison

The same IR3109 4-pole core appears in the MC-202, Juno-6/60/106, and Jupiter-8; the
Jupiter-6 uses two IR3109s configured as state-variable filters (HP/LP); the Juno-106 hides
the IR3109 inside the potted 80017A VCF/VCA module [FA, high] [AMS-IR, high] [ED, high]. The
Jupiter-8 picks off the 2nd stage for a 12 dB mode and adds input-side Q compensation
[FA, high]. The earlier **SH-09 used discrete BA662 OTAs**, not the IR3109 — the IR3109 is
essentially Roland's IC integration of cascaded BA662-style OTA cores [AMS-IR, high]
[FA, high]. The SH-101 and MC-202 share the same IR3109-based filter design [FA, high].

> CORRECTION (do NOT propagate): The TB-303 does **NOT** share the IR3109. The TB-303 uses a
> discrete diode-ladder filter; only the Juno-6/60 and Jupiter family share the IR3109 with
> the SH-101 [FA, high]. Any claim that the TB-303 uses the IR3109 is incorrect.

The SH-101 vs Jupiter-8 comparison is therefore a **same-chip / different-implementation**
comparison, which strengthens the "it is the implementation, not the chip" framing of the
SH-101's character [FA, high] [ED, high].

## 8. Key parameters

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| Filter slope / poles | 24 dB/oct, 4-pole (4 cascaded OTA integrators) | dB/oct | high | [FA] [ED] [SM] |
| Cutoff frequency range | 10 - 20000 | Hz | high | [SM] [RS] |
| Pole integrating caps (C47, C48, C50, C51) | 240 | pF (each) | high | [SF7] [SM] |
| Pole input resistors (R114, R117, R118, R121) | 68 | kohm (each) | high | [SF7] |
| Per-stage series resistors (R115, R116, R120, R123) | 560 | ohm (each) | high | [SF7] |
| Cutoff CV scaling (keyboard tracking) | 1 (exp; F5 cutoff = 2x F4 after VR8 trim) | V/oct | high | [SM] [SF7] |
| Key Follow range | 0 - 100 | % | high | [SM] [RS] |
| Resonance range | 0 to full self-oscillation | unitless control | high | [RS] [SM] |
| Exp-converter tempco thermistor (TH1) | SDT-1000 (in cutoff-CV path; part 15229908) | thermistor | high | [SF7] [SF8] |
| Resonance network transistors | TR26 (phase splitter) + TR27 (inverter), clipping diodes to ground | — | medium (RE/partly-inferred) | [SF7] [ED] |
| LFO / clock modulation rate range | 0.1 - 30 | Hz | high | [SM] |
| Self-oscillation output amplitude (AS3109 clone, ±12 V) | ~20 | Vpp | low (clone-derived) | [AMS-AM] |
| Q compensation type | Output-side boost (raises VCA drive with resonance), NOT input-side | — | high | [ED] [AMS-AM] |
| IR3109 buffer drive currents per stage | stage1/3 ~0.6; stage2 ~1.0; stage4 ~1.3 | mA | medium (clone-derived AS3109) | [ED] [AMS-IR] |
| Transconductance variable range | 1 uA - 10 mA | A | medium (partial OEM, second-hand) | [AMS-IR] |
| Input offset voltage | < ±3 | mV | medium (partial OEM, second-hand) | [AMS-IR] |
| Phase shift at cutoff (per pole / total) | ~45 per pole / 180 total (requires inverted feedback) | degrees | high (theory) | [ED] |
| VCF chip / designator | IR3109 / IC14 (part 15229801) | — | high | [SF7] [SF8] |
| VCO chip / designator (context) | CEM3340 / IC13 | — | high | [SF7] |
| VCA chip / designator (context) | BA662 / BA662A / IC15 | — | high | [SF7] [SF8] |

## 9. Confidence, disputes and honest labels

This section surfaces every disputed, low-confidence, clone-derived, inferred, and
open-validation item for this dimension, stated plainly. It is the canonical place to check
before treating any figure as settled.

### 9.1 Disputed / low-confidence research items

- **Modulation depths undocumented (disputed, low).** The exact ENV-depth and MOD(LFO)-depth
  values applied to the cutoff CV (in V/oct or Hz) are NOT published anywhere. Only control
  ranges exist. Requires hardware measurement or a full annotated netlist [SM, confirmed-gap].
- **ENV control polarity (inference, unmeasured).** "Unipolar / positive-only ENV amount" is
  inferred from general synth design, not a sourced SH-101 statement [WP, inference]. The
  shared VCF+VCA envelope IS confirmed [WP, high]; the summing of ENV+LFO at the cutoff CV
  node is architecturally implied but not primary-documented.
- **Original IR3109 datasheet not archived (disputed, low).** No standalone OEM datasheet is
  publicly archived; only a minimal sheet (via the Jupiter-4 manual / AMSynths) yields the
  transconductance (1 uA-10 mA) and offset (<±3 mV) figures, and even those are
  second-hand with inconsistent units across sources [DSA, high] [PN, low] [AMS-IR, medium].
- **No measured frequency response (disputed, low — OPEN VALIDATION GAP).** No measured
  magnitude/phase Bode plot of an original SH-101 IR3109 filter was located. The 24 dB/oct
  slope is a design/spec statement and the 45-degrees-per-pole phase is theory — neither is a
  measured curve. Resonance peak height vs control and self-oscillation frequency vs CV are
  likewise unmeasured [SM, theory] [ED, theory].

### 9.2 Clone-derived figures (NOT original-instrument measurements)

- **Per-stage buffer drive currents (0.6 / 1.0 / 1.3 mA).** [clone-derived: Alfa
  AS3109/Electric Druid, presumed-equal] — from the AS3109 clone datasheet, not the original
  IR3109 [ED] [AMS-IR]. (An earlier dossier carried a "1.3 mA mA" transcription typo; the
  correct figure is 1.3 mA.)
- **Self-oscillation ~20 Vpp.** [clone-derived: AMSynths AM8101 / AS3109 @ ±12 V,
  NOT original-equal] — single-sourced, ±12 V module context, not an original SH-101 on its
  low-voltage rails [AMS-AM, low].
- **General "~20 Vpp self-osc" and IR3109 drive-current electrical framing.** Per project
  policy, all IR3109 electrical figures of this kind are Alfa AS3109 clone / AMSynths-module
  figures, NOT original-instrument measurements.

### 9.3 Reverse-engineered (no primary Roland trace) items

- **Resonance topology (phase-splitter + diode clamp).** [reverse-engineered: Electric
  Druid / OpenMusicLabs; partly-inferred]. Transistors TR26/TR27 and diodes are visible near
  the VCF-WIDTH/resonance control on the schematic, consistent with the claim, but the exact
  resonance-feedback node and designator-to-function mapping were not traced unambiguously and
  remain **uncertain** pending a higher-DPI trace [SF7, partly-inferred] [ED,
  reverse-engineered].
- **Mechanism correction.** The SH-101's resonance limiter is the **diode clamp**, not OTA
  soft-clip (OTA soft-clip is the JUNO mechanism). Do not write "OTA soft-clipping plus diodes"
  for the SH-101 [ED, reverse-engineered].

### 9.4 Tonal-framing honesty

- "Drier / rawer / more aggressive than Juno-60 / Jupiter-8" is **editorial, refuted as
  worded**. Sourced descriptors are "fatter/glassier," "squelchy/plasticy with resonance," and
  a "growl-like aggressive filter sound" (the last tied to LFO modulation). The defensible
  technical statement is the output-side Q compensation on the same IR3109 chip [ED] [AMS-AM]
  [VS] [WP].

### 9.5 Software-emulation artefacts to NOT attribute to this hardware

The hardware filter has no features beyond those documented above. In particular, the original
SH-101 has **no sine LFO, no 32'/64' registers, no external audio input, and no MIDI/DCB** —
those appear only in later software (e.g. Roland Cloud SH-01A). Do not let any
software-emulation feature leak into the hardware filter model as if it were original.

### 9.6 Residual open questions (validation gaps)

- Calibrated ENV/LFO modulation depths in V/oct or Hz [needs hardware].
- ENV-to-cutoff polarity (bipolar vs unipolar) confirmation [needs front-panel/schematic read].
- Measured magnitude/phase response, resonance peak vs control, self-osc frequency vs CV
  [needs bench — and we hold NO physical units, so this stays open].
- gm-vs-control-current law and supply voltages from a primary IR3109 datasheet [datasheet not
  located].
- Whether the input drive into the first OTA stage is deliberately scaled into soft saturation
  (overdrive character) [unquantified].
- Exact resonance-feedback component values / clipping-diode part numbers and their precise
  designator-to-function mapping [partly reverse-engineered].

## 10. Design implications for mwAudio101

Model the VCF as a classic 4-pole cascade of one-pole lowpass sections with cutoff set
exponentially from a summed CV (CUTOFF knob + keyboard x KeyFollow + ENV x EnvDepth +
LFO x ModDepth), 1 V/oct scaling, 10 Hz-20 kHz range [SM] [ED]. Anchor the per-stage time
constant / default cutoff to the documented **68 k + 240 pF** per pole [SF7] [ED].

Implement resonance as global feedback of the **inverted** 4th-stage output back to the input;
because total phase is 180 degrees at cutoff, the feedback must be inverting for positive Q
[ED, theory]. Model self-oscillation as feedback gain reaching unity, producing a sine.

Reproduce the SH-101's distinguishing nonlinearities, but with the corrected mechanism: the
primary resonance limiter is a **diode clamp in the feedback path** that limits loop amplitude
and injects mild distortion which is then re-filtered (NOT generic OTA soft-clip as the main
limiter) [ED, reverse-engineered]. Per-stage buffer asymmetry (stage 4 drives hardest) is a
secondary detail for high-fidelity saturation, and its values are clone-derived (AS3109), so
treat them as a tunable assumption [ED] [AMS-IR].

Critically, implement **Q compensation on the OUTPUT** (post-filter gain that rises with
resonance, increasing VCA drive) rather than boosting the filter input — this is the specific
topological choice that gives the SH-101 its distinct resonance voice versus the Juno/Jupiter
(which compensate at the input) [ED] [AMS-AM].

For temperature/tuning modelling: treat the VCO (CEM3340) as internally tempco-compensated
(model only residual drift) and apply the **SDT-1000/TH1 compensation to the VCF cutoff CV**,
not to VCO pitch — this matters if the clone models thermal drift or warm-up behaviour
[SF7] [CEM-DS]. Treat the envelope as a single shared VCF/VCA ADSR [WP].

Flag the undocumented modulation-depth magnitudes, the unconfirmed ENV polarity, and the
absence of any measured response curve as items requiring hardware capture before final
calibration — and remember the project holds NO physical-unit measurements, so these remain
OPEN VALIDATION GAPS rather than deliverable targets.

## 11. References

- [SM] Roland SH-101 Service Manual / Service Notes (First Edition, Nov 1 1982): spec page,
  VCF adjustment procedure, schematic.
  <https://electricdruid.net/wp-content/uploads/2025/05/SH101-Service-Manual.pdf>
- [SF1] Roland SH-101 Service Notes scan, title/spec page (synthfool).
  <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_1.gif>
- [SF2] Roland SH-101 Service Notes scan, block diagram + CEM3340 BLOCK & CONNECTION DIAGRAM
  (synthfool).
  <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_2.gif>
- [SF7] Roland SH-101 Service Notes scan, synth-board foldout schematic (VCO/VCF/VCA/PSU
  sections; IC13/IC14/IC15, C47-C51, R114-R123, TH1, rail labels) (synthfool).
  <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_7.gif>
- [SF8] Roland SH-101 Service Notes parts list (part numbers; DC/DC converter 12449224;
  SDT-1000 thermistor 15229908; IR3109 15229801; CEM3340 15229810; BA662A) (ManualsLib /
  synthfool).
  <https://www.manualslib.com/manual/1231849/Roland-Sh-101.html?page=10>
- [ML] Roland SH-101 circuit-diagram / parts pages (ManualsLib structured extraction;
  IC13/IC14/IC6 designators).
  <https://www.manualslib.com/manual/1231849/Roland-Sh-101.html?page=9>
- [RS] Roland SH-101 Technical Specifications (official support).
  <https://support.roland.com/hc/en-us/articles/201921519-SH-101-Technical-Specifications>
- [ED] Electric Druid — "Roland filter designs with the IR3109 or AS3109" (topology,
  resonance, Q compensation, buffer currents, phase theory; reverse-engineering).
  <https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/>
- [CEM-DS] CEM3340/CEM3345 datasheet (TEMPCO GEN block, exponential-scale-error figures).
  <https://electricdruid.net/cem3340-vco-voltage-controlled-oscillator-designs/>
- [AMS-IR] AMSynths — "All about the IR3109 chip" (history, minimal datasheet figures,
  family use).
  <https://amsynths.co.uk/2022/04/06/all-about-the-ir3109-chip/>
- [AMS-AM] AMSynths AM8101 SH-101 filter module (AS3109 clone; output-side Q compensation;
  ~20 Vpp @ ±12 V self-oscillation).
  <https://amsynths.co.uk/home/products/filters/am8101-sh-101-filter/>
- [FA] Florian Anwander — Roland filters version table (IR3109 family; SH-09 BA662; TB-303 is
  a separate diode ladder).
  <https://www.florian-anwander.de/roland_filters/>
- [PN] Polynominal — Roland IR3109 page ("datasheet not available").
  <https://www.polynominal.com/roland-ir3109/>
- [EM-IR] Electronic Music Wiki — IR3109.
  <https://electronicmusic.fandom.com/wiki/IR3109>
- [DSA] DatasheetArchive IR3109 query (returns no IR3109 datasheets).
  <https://datasheetarchive.com/IR%203109-datasheet.html>
- [WP] Wikipedia — Roland SH-101 (single shared ADSR for VCF and VCA; LFO can modulate filter).
  <https://en.wikipedia.org/wiki/Roland_SH-101>
- [VS] Vintage Synth Explorer forum — SH-101 vs Juno filter character (community/subjective).
  <https://forum.vintagesynth.com/viewtopic.php?f=18&t=48248>
