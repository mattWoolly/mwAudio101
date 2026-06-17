<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Power, CV/Gate, Calibration & I/O Inventory

## 1. Scope and sourcing

This document is the citable source of truth for the Roland SH-101 power
system, control-voltage / gate interfacing, the nine factory calibration
trimmers, and the complete external connector inventory, as they bear on the
open-source **mwAudio101** project. It also folds in the schematic
reconciliation that froze several previously disputed circuit facts.

The primary source throughout is the **Roland SH-101 Service Notes, First
Edition, dated November 1, 1982** (synth boards OPH177-1/-2/-3). Where the
1982 service manual is the authority, claims are labelled `[service-manual,
high]`. Where a figure comes from Roland's later web specification page,
from clone/reverse-engineering work, or from general analog theory, it is
labelled accordingly and never stated as a settled measurement of an
original instrument.

The project has made **no physical-unit measurements** [project-decision].
Anything that would require bench data (loaded gate-output voltage, drift in
cents per degree, oscilloscope-traced envelope curves) is flagged here as an
**open validation gap**, not a delivered fact. See section 9.

### 1.1 Confidence-label legend

The following inline tags are used throughout:

- `[service-manual, high]` — read directly from the 1982 Roland Service
  Notes (spec sheet or schematic).
- `[roland-web-spec]` — from Roland's current online Technical
  Specifications page (a later re-publication; not the 1982 manual).
- `[reverse-engineered: Electric Druid / Open Music Labs]` — third-party
  reverse engineering, not Roland-primary.
- `[clone/datasheet-derived]` — figures from the CEM3340 datasheet or
  AS3340/AS3109 clone literature, not SH-101 bench measurements.
- `[theory/inference, unmeasured]` — general analog theory or an engineering
  inference, not a documented SH-101 number.
- `[FROZEN]` — reconciled against the high-resolution schematic and adopted
  as canonical project reference data.

## 2. External I/O inventory (stock hardware)

### 2.1 Complete jack list

The stock SH-101 exposes exactly the following connectors, read directly
from the 1982 Service Notes specifications table and block diagram
[service-manual, high] [1] [3]:

- **OUTPUT** — audio out, 0 dBm max.
- **PHONES** — headphone out, 8 ohm.
- **CV OUT** — 1 V/octave, 0.415 V to 5 V.
- **GATE OUT** — V-trig, OFF = 0 V, ON = 12 V `[FROZEN]`.
- **CV IN** — 1 V/octave, 0 to 7 V (pitch only).
- **GATE IN** — V-trig, +2.5 V or more.
- **EXT CLK IN** — +2.5 V or more (logic clock for arpeggiator/sequencer).
- **HOLD pedal jack** — for the DP-2 momentary footswitch.
- **DC IN** — 9 V to 12 V DC adaptor, or 6 x 1.5 V dry cells internally.
- **MGS-1 grip connector** — proprietary side-panel modulation-grip plug.

There is **no audio input jack, no MIDI, no DCB, and no DIN-sync** of any
kind on the stock instrument [service-manual, high] [1] [2]. The only
inter-device interfacing is the analog CV/Gate pair plus the EXT CLK pulse
input.

### 2.2 No external-audio input (definitive)

The stock SH-101 **cannot** process external audio through its VCF/VCA. No
audio-input jack exists on any board; the VCF/VCA are fed only by the
internal SOURCE MIXER (VCO + sub-oscillator + noise) per the block diagram
[service-manual, high] [1] [4].

Three commonly misread "audio inputs" are not audio inputs:

- **CV IN** sets pitch only (1 V/oct, 0–7 V); it carries no audio
  [service-manual, high] [1].
- **EXT CLK IN** is a logic clock for the arpeggiator/sequencer, not audio
  [service-manual, high] [1].
- The popular **Kenton "filter socket" kit** adds a 0–5 V **control-voltage**
  input for filter cutoff and explicitly warns: *"The filter input is a
  voltage input. It is not an audio input to the filter section."*
  [reverse-engineered: Kenton kit doc] [5].

Routing external audio through the filter is possible **only via aftermarket
hardware mods** (Circuitbenders / MOD WIGGLER style) that physically tap the
VCF input/mixer node [reverse-engineered: Circuitbenders] [4] [6]. Any
external-audio or filter-CV path in mwAudio101 is therefore a clone-design
addition, not an original feature, and must be labelled as such.

### 2.3 No MIDI / DCB / DIN-sync

DCB appeared on the contemporaneous Juno-60 and Jupiter-8, not on the
SH-101. The SH-101 spec table, connector list, and block diagram contain no
digital bus, no sync DIN, and no MIDI port [service-manual, high] [1] [2].
MIDI, DCB, and sine-LFO/extended-register behaviour appear only in later
**software emulations** (e.g. Roland Cloud SH-01A) and are software-emulation
artifacts, not stock-hardware facts [theory/inference, unmeasured].

### 2.4 MGS-1 modulation grip

The MGS-1 is a proprietary side-mounted performance controller joined to the
synth by a captive plug & cord (part 053H157). The exploded parts diagram
shows a 100 kohm bend potentiometer (PB-5RO-100KB, part 13219273) and a
momentary LFO/portamento switch (EVQ-PTR18K, part 13129325)
[service-manual, high] [1] [7]. The grip is **not** a CV/Gate/sync jack: it
carries the bend-pot wiper plus a switch.

The exact **pin count and pinout of the grip connector are undocumented** in
all accessed primary sources — only the part number and that it is a
multi-conductor plug are known. A "multi-pin" characterisation is an
inference [theory/inference, unmeasured]. Treat the MGS-1 pinout as an open
hardware-verification item before building a physical connector.

## 3. CV / Gate electrical interface

### 3.1 CV: 1 V/octave (exponential)

The SH-101 uses the **1 V/octave (volt-per-octave, exponential)** CV
standard — the standard Roland adopted across its CV-equipped synths of the
era (SH-101, MC-202, Juno-6/60, Jupiter family) — **not** the Hz/V (linear)
standard used by Korg/Yamaha [service-manual, high] [1] [8] [9].

- **CV OUT:** 1 V/oct, 0.415 V to 5 V (~4.585 V span) [service-manual,
  high] [1] [8].
- **CV IN:** 1 V/oct, 0 to 7 V (wider input range than the output to
  tolerate transpose and external sources) [roland-web-spec] [8].

Note: the primary 1982 manual OCR is **illegible** on the exact CV-IN range
("o^iy" in the OCR); the 0–7 V figure rests on Roland's modern online spec
page [roland-web-spec]. Treat 0–7 V as confirmed-by-secondary-Roland-source.
A documented quirk is that some units tune slightly differently when played
via CV IN versus the internal keyboard, attributable to the CV-input
summing/scaling circuit [reverse-engineered: community] [10].

### 3.2 Gate: V-trig, ON = 12 V (FROZEN)

The gate is a **positive-going V-trigger** (high = note on, 0 V = note off),
not an S-trigger [service-manual, high] [1] [9].

- **GATE OUT ON level = 12 V, OFF = 0 V** `[FROZEN]` per the 1982 service
  manual spec sheet, read verbatim as "Gate (OFF=0V, ON=12V)"
  [service-manual, high] [1] [11].
- **GATE IN threshold = +2.5 V or more** [service-manual, high] [1].

There is a genuine documented discrepancy with **Roland's current web spec**,
which states gate output ON = 10 V at a 100 kohm load [roland-web-spec] [8].
Both numbers come from Roland documents. The schematic reconciliation **froze
the value at 12 V** because the spec sheet reproduced in the service notes
reads "ON=12V" with no competing 10 V figure on that sheet
[service-manual, high] [11]. The most plausible engineering reconciliation —
that a finite-source-impedance V-trig driver reads ~12 V open-circuit but
sags to ~10 V under a 100 kohm load — is an **inference, not a measured
SH-101 fact** [theory/inference, unmeasured]. Do not silently pick one
without saying which load condition you mean.

### 3.3 EXT CLK

EXT CLK IN triggers at +2.5 V or more and is a **bare logic-pulse input**
that advances the arpeggiator / 100-step sequencer; it does not gate notes
[service-manual, high] [1]. Per the CPU program, the clock voltage is
detected at the CPU's T1 terminal: when a low clock goes high, TR11 inverts
it and signals the CPU, which generates the next step [service-manual,
high] [1]. The SH-101 has no song-position awareness. Whether EXT CLK is
strictly edge-triggered and its minimum pulse width / max rate are **not
specified** — an open gap [service-manual: silent].

## 4. Pitch CV generation (DAC) and scaling

Pitch CV is generated **digitally**: the CPU (Toshiba TMP80C49P-6-7301, IC6)
reads the 4x8 keyboard matrix plus range/transpose switches and feeds a D/A
converter, which produces a stepped key CV (KCV) that the CEM3340 then
exponentiates [service-manual, high] [1] [12].

Documented exact scaling [service-manual, high] [1]:

- **Range CV mapping:** 16' = 1 V, 8' = 2 V, 4' = 3 V, 2' = 4 V.
- **Transpose CV (post-DAC):** lowest F at 16' = 0.417 V (directly
  verified); the full table L = 0.417 V / M = 1.417 V / H = 2.417 V and
  highest C = 5.0 V is reported but only the 0.417 V anchor and the integer
  Range mapping were verbatim-verified — the rest is partly unconfirmed (see
  section 8).
- **Tuning reference:** A4 = 442 Hz (8' range, Transpose Middle; the 442 Hz
  reference and the VR-7 → 442 Hz procedure are verified, but the
  8'/Transpose-Middle qualifier was not separately spelled out in the OCR).

For mwAudio101 this means modelling the CV path as a **DAC-stepped
(per-semitone) value scaled exactly 1 V/oct then exponentiated**, with A4 =
442 Hz, the documented Range/Transpose offsets, and only the front-panel
TUNE (±50 cents) and bender as continuous controls.

## 5. Calibration: the nine trimmers (VR-1..VR-9)

The factory Adjustment Procedures (service manual p.4) use nine trimmers to
set the D/A converter, VCO scale/tune/width/range, and VCF tracking
[service-manual, high] [1]. The D/A adjustments are done first.

### 5.1 D/A converter adjustments

| Trimmer | Function | Target | Mode |
| --- | --- | --- | --- |
| VR-2 | D/A TUNE | 0 V ±1 mV | Test / LOAD |
| VR-1 | +5V / D/A WIDTH | 2.75 V ±1 mV | PLAY |
| VR-3 | D/A LINEAR | 2.5 V ±1 mV | ARPEGGIO DOWN |

All three D/A targets are verified verbatim [service-manual, high] [1].
VR-3 is also referenced in an **LFO MOD OFFSET** step (0 ±2 mV at
TP-1/TP-2 on the bender board), but that second clause was **not located in
the OCR** and is uncertain (see section 8).

### 5.2 VCO adjustments

- **VR-7 (VCO TUNE):** adjust until output = 442 Hz [service-manual,
  high] [1].
- **VR-9 (front-panel TUNE):** confirm set to centre during the procedure
  [service-manual, high] [1].
- **VR-6 (VCO WIDTH):** set together with VR-7 via an F3/F5 Lissajous-figure
  null (the two interact; both must be done). The Lissajous method is
  referenced in the manual but the exact F3/F5 ratio was not verbatim-
  extracted [service-manual, partly inferred].
- **VR-5 (RANGE WIDTH):** set so UP-mode pitch matches U&D-mode pitch
  [service-manual, high] [1].
- **PWM:** VR-2 (D/A TUNE) set for 1:1 mark/space [service-manual, high] [1].

A switchable **+15 V VCO-tune compensation path** absorbs component
variation: the manual notes "a +15V voltage can be supplied or inhibited" by
shorting or opening a foil break on the back of resistor **R102** if VR-7
alone cannot achieve correct high-frequency tracking [service-manual,
high] [1]. This is a documented source of per-unit variation — different
units may be jumpered differently.

### 5.3 VCF tracking adjustment

Hold A4, set cutoff to ~1 kHz, then alternate F4/F5 and adjust **VR-8 (VCF
WIDTH)** until the F5 figure cycle is twice the F4 cycle, i.e. the filter
tracks 1 V/oct (2:1 per octave) [service-manual, high] [1]. The "~1 kHz at
A4" and exact Fn ratios are partly inferred from the OCR (section 8).

### 5.4 Mapping for mwAudio101

Expose virtual trimmers mirroring VR-1..VR-9 so presets/units can be
"tuned": D/A offset/width/linearity, VCO scale (range width), VCO tune/width,
and VCF keyboard tracking (the 2:1 octave relationship). Model the R102
HF-track jumper as a discrete per-unit variation flag.

## 6. Power system

### 6.1 Input and consumption

- **Power source:** 6 x 1.5 V dry cells (UM-2 / C-cell size) OR a 9–12 V DC
  adaptor (Roland PSA-120/220/240) [service-manual, high] [1] [8]. The
  "UM-2/C" cell-size designation is standard general knowledge; the manual
  OCR line shows only "1.5V x 6" [theory/inference].
- **Input:** DC 9 V to 12 V [service-manual, high] [1].
- **Power consumption:** 1 W [service-manual, high] [1].
- **DC barrel polarity:** PSA-series of the era is center-negative, but this
  was **not confirmed** against the schematic — verify for clone PSU design
  [theory/inference, unmeasured].

### 6.2 Internal rails (DC-DC converter)

The unit takes the low external DC input and develops its rails internally
through a **DC/DC converter** (the "S1671140 coil DC/DC converter", part
**12449224**) [service-manual, high] [1] [3]. Note a part-number
transposition in the source dossier: the DC/DC converter is part 12449224,
**not** 15229908 (which is the SDT-1000 thermistor) [service-manual, high,
correction] [3].

The schematic reconciliation read the **full rail set directly off the
high-resolution synth-board connector** (page 7) `[FROZEN]`
[service-manual, high] [3] [13]:

- **+15 V** (connector pin 19; feeds the CEM3340 frequency-control network
  and VCO-tune reference).
- **−15 V** (standard negative analog rail for the dual-supply
  CEM3340/IR3109/op-amps; +15 V read directly, −15 V inferred from
  dual-supply part requirements — see residual risk in section 8).
- **+14 V** (connector pin 21).
- **+9 V** (connector pin 15).
- **+5 V** (connector pin 16; CPU logic VCC).
- **−5 V** and an internal **−2.5 V** reference plus a "−5V Converter" on the
  CEM3340 connection diagram (page 2).
- **GND** (connector pin 17).

So the rail set is richer than a simple ±15 V/+5 V: it includes +14 V, +9 V,
−5 V and −2.5 V `[FROZEN]` [service-manual, high] [3]. The headline
"±15V/+5V via DC-DC" remains a fair summary of the analog and logic rails,
but the **exact regulator topology** (how each sub-rail is generated from the
single DC/DC stage — a switcher built around transformer/inductor T1 with
TR1/TR2 and rectifier diodes) is **only partially reconstructed** and stays a
**low-confidence** item [reverse-engineered, low] [3].

### 6.3 Memory backup

Memory retention is from a **separate ~1.8 V x6 battery string** labelled
"Memory Back-up Battery 1.8V X6" on page 2 — it is **not** a "+5 V backup
rail" [service-manual, high, correction] [3]. The +5 V rail is the CPU logic
VCC; sequencer/patch data is retained by the backup battery string while
keyboard/switch data is cleared on power-up (Initial Set). Caveat: Roland's
"1.8V x6" notation is unusual and was taken from page-2 OCR; it should be
confirmed against a clean scan (could conceivably be a "1.5V x6" OCR
artifact) [service-manual, OCR-uncertain].

### 6.4 Relevance to a digital clone

Power realism (battery vs adaptor, ~1 W, multi-rail) is largely irrelevant to
a digital clone, except that historically the DC/DC rails and battery sag
influenced drift. Model this only as an optional "instability" parameter, if
at all [theory/inference].

## 7. Drift, thermal compensation & component tolerance

### 7.1 VCO thermal compensation (CEM3340, IC13)

The VCO is a **Curtis CEM3340 (IC13)** `[FROZEN]` [service-manual,
high] [3] [13]. It carries an **on-die TEMPCO GEN block** feeding a precision
multiplier, which eliminates the external tempco resistor most VCOs need
[service-manual block diagram + datasheet] [3] [14]. The datasheet markets it
as "Fully Temperature Compensated."

However, the compensation is an **approximation with a spec'd residual
error**, not perfect cancellation: the datasheet's exponential-scale-error
figures are 0.2–1% untrimmed and 0.05–0.3% trimmed, and it leaves the
**KT/q (thermal-voltage) term** only partially handled
[clone/datasheet-derived] [14] [15]. The often-quoted **−0.33%/°C** figure is
the magnitude of the fractional change of Vt = kT/q near 25 °C
(1/298 K ≈ +0.336%/°C ≈ 3356 ppm/°C for Vt itself); the sign convention
reflects the resulting pitch/scale-error direction. **This is exponential-
converter theory, not an SH-101 bench measurement** [theory/inference,
unmeasured]. The SH-101 needs no external thermistor on the VCO because of
the on-die comp.

### 7.2 VCF thermal compensation (IR3109, IC14; TH1/SDT-1000)

The VCF is a **Roland IR3109 (IC14)**, a 4-pole / 24 dB/oct lowpass built
from four OTA integrator stages, with **240 pF integrator caps and 68 kohm
input resistors — the Juno-6/60 topology** `[FROZEN]` [service-manual,
high] [3] [16]. Resonance is diode-clipped (a transistor phase-splitter
feedback with clipping diodes, output-level rather than input-gain
compensated) per Electric Druid / Open Music Labs reverse engineering; the
exact resonance-feedback topology was **not unambiguously traced** off the
Roland schematic and stays uncertain [reverse-engineered: Electric Druid /
Open Music Labs, partly inferred] [16] [17].

Because the OTA-based IR3109 has **no on-die tempco**, the SH-101 places the
**SDT-1000 thermistor (designator TH1, part 15229908)** in the **VCF
cutoff-frequency CV scaling path** — in series with R127 (5.6 kohm) and R126
(1.8 kohm), adjacent to TR28 — **not** in the VCO pitch-CV network `[FROZEN]`
[service-manual, high] [3] [13]. Note the source-dossier part-number error:
the SDT-1000 is part 15229908, not 12389800 (which is the 6 MHz ceramic
resonator) [service-manual, high, correction] [3]. The "thermet" blue
(300 ohm) and black (2 kohm) parts in the trimmer list are cermet/thermet
**trimmer potentiometers**, distinct from the SDT-1000 **thermistor sensor**;
keep them separate in any circuit model [service-manual, high] [1] [3].

IR3109 electrical figures sometimes quoted elsewhere (drive currents,
~20 Vpp self-oscillation) are **Alfa AS3109 clone / AMSynths-module figures,
not original-instrument measurements** — they are not relied on here
[clone-derived: Alfa AS3109/AMSynths, presumed-equal].

### 7.3 VCA (BA662A, IC15)

The VCA is a **BA662A OTA (IC15)**, an offset-selected ("white dot") part
that Roland binned for low offset, implying meaningful part-to-part spread
[service-manual, high] [1] [3]. The **BA662/BA662A internals are
reverse-engineered (Open Music Labs); there is no public datasheet** — its
control scale and offset-binning criteria are not quantified
[reverse-engineered: Open Music Labs]. Like the OTA filter, its gain is
temperature-dependent [theory/inference].

### 7.4 Tolerances and the honest drift position

Pitch-critical resistors are **1% metal-film, 100 ppm/°C**, with the tightest
network elements at 0.1% (CRB25BX) and 0.5% (CRB25DX) in the D/A/scaling path
[service-manual, high] [1]. The front-panel **TUNE range is ±50 cents**
[service-manual, high] [1].

The physically grounded per-unit drift contributors are: (a) CEM3340 partial
temp comp (small, trimmable residual KT/q drift), (b) 1% resistor spread in
the expo/scaling network, (c) thermet-trimmer setting differences, (d) the
R102 HF-track jumper state, (e) VCO timing-cap leakage/tempco, and (f) OTA
(IR3109/BA662) gm temperature dependence affecting cutoff and VCA gain.

There is **no SH-101-specific quantified drift figure** (cents per °C,
warm-up settling time, long-term stability) anywhere in the service manual
[service-manual: silent]. A defensible drift model perturbs each oscillator's
scale/offset by a small random per-unit amount plus a slow temperature-
correlated term, but **must not hardcode a specific cents/°C value** — make
it a tunable parameter explicitly flagged as estimated [theory/inference,
unmeasured].

## 8. Confidence, disputes & honest labels

Every disputed, low-confidence, partly-verified, or residual-risk item for
this dimension, stated plainly:

- **Gate-out 12 V vs 10 V (disputed; FROZEN to 12 V):** the 1982 service
  manual says ON = 12 V (no load stated); Roland's modern web spec says
  10 V at 100 kohm load. Both are Roland documents. The project freezes
  **12 V** as canonical per the service-manual spec sheet, and treats the
  "sags to 10 V under load" reconciliation as **inference, not measurement**.
  Roland's web/Sweetwater pages also returned HTTP 403 to automated fetch, so
  the 10 V/100 k figure rests on consistent third-party snippets, not a
  firsthand render.
- **CV-IN range 0–7 V (primary source illegible):** the 1982 manual OCR is
  garbled on this exact number; 0–7 V comes from Roland's web spec only.
  Confidence is high but the primary line was not directly legible — flag if
  a netlist depends on it.
- **PSU regulator topology (low confidence):** the rail *set* (+15/−15/+14/
  +9/+5/−5/−2.5 V, GND) is FROZEN and read directly, but **how** each
  sub-rail is generated from the DC/DC stage is only partially reconstructed.
  The corroborating synhouse.com page was inaccessible (403/timeout).
- **−15 V rail (inferred label):** +15 V was read directly; the explicit
  "−15V" label was not captured in a legible crop and is inferred from the
  dual-supply requirements of the CEM3340/IR3109/op-amps. A literal label
  reportedly exists on the schematic.
- **Memory-backup voltage (OCR-uncertain):** page 2 reads "Memory Back-up
  Battery 1.8V X6", which is an unusual notation and should be confirmed
  against a clean scan (possible "1.5V x6" OCR artifact). It is **not** a
  +5 V backup rail.
- **VCA / IR3109 / CEM3340 thermal coefficients:** the CEM3340 −0.33%/°C
  residual and the OTA cutoff/gain temperature dependence are **theory/
  datasheet-derived, not SH-101 measurements**. IR3109 electrical figures
  circulating in the community are **Alfa AS3109 clone / AMSynths** figures.
  BA662A internals are **reverse-engineered (Open Music Labs)** with no
  public datasheet.
- **VCF resonance topology (uncertain):** the phase-splitter-with-clipping-
  diodes description is reverse engineering (Electric Druid / Open Music
  Labs); it was not unambiguously traced off the Roland schematic.
- **Calibration sub-clauses not verbatim-verified:** (a) the VR-3 "LFO
  offset 0 ±2 mV at TP-1/TP-2" clause (only the 2.5 V ARPEGGIO-DOWN part is
  confirmed); (b) the full transpose table L/M/H = 0.417/1.417/2.417 and
  highest C = 5.0 V (only lowest-F-16' = 0.417 V confirmed); (c) the
  VR-6/VR-8 F3/F4/F5 Lissajous specifics and "~1 kHz at A4"; (d) the A = 442
  reference being specifically at 8'/Transpose-Middle. Re-verify against a
  clean adjustment-page scan before stating as exact.
- **MGS-1 connector pinout (undocumented):** only the part number (053H157)
  and that it is a captive plug & cord are documented; pin count/pinout is
  not given in any accessed source. "Multi-pin" is an inference.
- **EXT CLK edge vs level / pulse width (silent):** the manual gives only the
  +2.5 V threshold.
- **DC barrel polarity (unverified):** center-negative is assumed from the
  PSA-series convention, not confirmed against the schematic.
- **Part-number transpositions in the source dossier (corrected here):** the
  DC/DC converter is part 12449224 (not 15229908); the SDT-1000 thermistor
  is part 15229908 (not 12389800, which is the 6 MHz ceramic resonator).
- **Software-only artifacts to exclude:** there is **no sine LFO, no 32'/64'
  registers, no external-audio input, and no MIDI/DCB** on stock hardware —
  these appear only in later software emulations (e.g. Roland Cloud
  SH-01A) and must not be presented as SH-101 hardware facts.
- **No physical-unit measurements (project decision):** loaded gate voltage,
  Bode/cutoff-vs-temperature plots, ADSR oscilloscope curves, and harmonic
  spectra are **open validation gaps**, not delivered facts (see section 9).

## 9. Open validation gaps (no bench data)

The project has taken no physical measurements. The following remain
**unmeasured** and should be flagged wherever a downstream phase assumes a
delivered value:

- Loaded vs unloaded gate-output voltage (the 12 V/10 V resolution).
- CEM3340/IR3109/BA662 actual thermal coefficients as wired in the SH-101.
- SH-101-specific tuning drift (cents/°C, warm-up settling, long-term
  stability).
- VCF resonance-feedback topology and clipping-diode behaviour.
- EXT CLK edge behaviour, minimum pulse width, and maximum rate.
- Output/source impedance of CV OUT and GATE OUT (levels given, impedance
  not).
- MGS-1 grip connector pin count and pinout.
- DC barrel-jack polarity and dimensions.

## 10. Design implications for mwAudio101

1. **Pitch CV.** Implement pitch as a quantized DAC value scaled exactly
   1 V/oct then exponentiated, with reference A4 = 442 Hz and the documented
   Range (16'=1V..2'=4V) and Transpose offsets. Model the CV path as DAC-
   stepped, not continuous, except the front-panel TUNE (±50 cents) and
   bender.
2. **CV/Gate I/O.** Model the host-facing interface as: 1 V/oct in/out
   (CV OUT 0.415–5 V; CV IN accept 0–7 V with scaling/clamp), positive
   V-trig gate (input comparator/Schmitt at ~+2.5 V; output high during
   note-on), and an EXT CLK input that advances the arpeggiator/100-step
   sequencer on a rising edge above ~+2.5 V (not a per-step note gate, no
   song-position).
3. **Gate level.** Output OFF = 0 V, ON = **12 V** by default `[FROZEN]`, but
   expose the high level as a build-time/calibration parameter so a "10 V at
   100 k load" target can be selected; document which load condition is
   meant.
4. **Stock jack set only.** Model exactly AUDIO OUT, PHONES, CV IN, CV OUT,
   GATE IN, GATE OUT, EXT CLK IN, HOLD pedal, DC power, and optionally the
   MGS-1 grip. **Do not** add an external audio input, MIDI, DCB, or DIN-sync
   to the faithful/stock model. Any audio-through-filter, filter-CV, or MIDI
   support is an explicit, separately-architected "mod"/enhancement layer.
5. **Virtual trimmers.** Expose VR-1..VR-9 as virtual calibration controls
   (D/A offset/width/linearity, VCO range width, VCO tune/width, VCF 2:1
   tracking), plus an R102 HF-track jumper flag for per-unit high-register
   variation.
6. **Thermal split.** Treat the VCO (CEM3340) as internally tempco-
   compensated and model only small residual drift; apply the SDT-1000/TH1
   compensation to the **VCF cutoff CV scaling**, not the VCO pitch CV. Make
   drift a tunable parameter flagged as estimated — do **not** hardcode a
   cents/°C value, since none is sourced for the SH-101.
7. **Filter topology.** Model the VCF as a 4-pole IR3109-equivalent ladder
   with 68 kohm input resistors and 240 pF integrator caps (Juno-6/60), with
   diode-clipped resonance (output-level compensation), labelling the
   resonance topology as reverse-engineered.
8. **Power.** Design around an external 9–12 V DC input feeding an internal
   DC/DC stage producing ±15 V/+5 V (and the +14/+9/−5/−2.5 V sub-rails), with
   a separate memory-backup battery string; leave the exact regulator chain
   parameterised and flagged for hardware verification, and treat power
   realism as an optional instability parameter only.

## 11. Frozen reference facts (canonical)

The following are FROZEN as canonical project reference data
[service-manual, high; schematic-reconciled] [3] [11] [13]:

| Item | Frozen value |
| --- | --- |
| CEM3340 VCO designator | IC13 |
| IR3109 VCF designator | IC14 |
| BA662A VCA designator | IC15 |
| CPU | IC6 region, TMP80C49P-6-7301 (part confirmed; "IC6" not independently re-located, "IC3" was a wrong OCR) |
| Gate OUT ON level | 12 V (OFF = 0 V) |
| TH1 / SDT-1000 thermistor | VCF cutoff-CV path (not VCO); part 15229908 |
| CEM3340 temp comp | on-die TEMPCO GEN, partial (spec'd residual) |
| VCF integrator caps C47/C48/C50/C51 | 240 pF each (Juno-6/60 topology) |
| VCF input resistors R114/117/118/121 | 68 kohm each |
| VCF series resistors R115/116/120/123 | 560 ohm each |
| DC/DC converter | present, part 12449224 |
| Rail set | +15, −15, +14, +9, +5, −5, −2.5 V, GND |

## 12. Key parameters

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| CV output scale | 1 V/oct (exp), 0.415 to 5 | V/oct | high | [1] [8] |
| CV input scale/range | 1 V/oct, 0 to 7 | V/oct | high (range web-only) | [1] [8] |
| Gate output ON level | 12 (service manual; 10 web at 100 k) | V | medium (disputed) | [1] [8] [11] |
| Gate output load reference | 100 (web spec only) | kohm | medium | [8] |
| Gate input threshold | +2.5 or more | V | high | [1] |
| EXT CLK input threshold | +2.5 or more | V | high | [1] |
| Range CV mapping | 16'=1, 8'=2, 4'=3, 2'=4 | V | high | [1] |
| Transpose CV (post-DAC) | lowest F 16'=0.417 (rest partly unverified) | V | medium | [1] |
| Tuning reference | A4 = 442 | Hz | high | [1] |
| VR-1 (+5V / D/A WIDTH) | 2.75 ±1 mV (PLAY) | V | high | [1] |
| VR-2 (D/A TUNE) | 0 ±1 mV (Test/LOAD) | V | high | [1] |
| VR-3 (D/A LINEAR) | 2.5 ±1 mV (ARPEGGIO DOWN) | V | high | [1] |
| VR-5 (RANGE WIDTH) | UP = U&D pitch | trim | high | [1] |
| VR-6/VR-7/VR-9 | Lissajous null; VR-7 → 442 Hz; VR-9 centred | trim | high/partly inferred | [1] |
| VR-8 (VCF WIDTH) | F5 cycle = 2x F4 (1 V/oct track) | trim | high/partly inferred | [1] |
| Front-panel TUNE range | ±50 | cents | high | [1] |
| VCF cutoff range | 10 to 20,000 | Hz | high | [1] |
| VCF type | IR3109, 4-pole, 24 dB/oct, 4 OTA stages | dB/oct | high | [1] [16] |
| VCF integrator caps C47/48/50/51 | 240 (x4) | pF | high (FROZEN) | [3] |
| VCF input resistors | 68 (x4) | kohm | high (FROZEN) | [3] |
| VCF series resistors | 560 (x4) | ohm | high (FROZEN) | [3] |
| VCO chip | CEM3340 (IC13), partial on-die temp comp | part | high (FROZEN) | [3] [13] |
| VCA chip | BA662A (IC15), offset-selected | part | high (FROZEN) | [1] [3] |
| CPU | TMP80C49P-6-7301, 6 MHz ceramic resonator | part | high | [1] [3] |
| Envelope timing | A 1.5 ms–4 s, D 2 ms–10 s, S 0–100%, R 2 ms–10 s | s / % | high | [1] |
| LFO / clock rate | 0.1 to 30 | Hz | high | [1] |
| Power source | 6 x 1.5 V dry cells OR 9–12 V DC adaptor | V | high | [1] [8] |
| Power consumption | 1 | W | high | [1] |
| Internal rails | +15, −15, +14, +9, +5, −5, −2.5, GND | V | high (FROZEN; −15 inferred) | [3] |
| DC/DC converter | present, part 12449224 | part | high (FROZEN) | [3] |
| Memory backup | separate ~1.8 V x6 battery string | V | medium (OCR) | [3] |
| Pitch-path resistors | 1% (some 0.1%/0.5%), 100 ppm/°C | % / ppm/°C | high | [1] |
| Temp-comp parts | SDT-1000 thermistor (TH1) + thermet trimmers | — | high | [1] [3] |
| CEM3340 residual tempco | KT/q ~−0.33%/°C not fully cancelled | %/°C | medium (theory) | [14] [15] |
| Audio output level | 0 dBm max | dBm | high | [1] |
| Phones output impedance | 8 | ohm | high | [1] |
| Hold/sustain pedal | DP-2 footswitch | switch | high | [8] |
| MGS-1 bend pot | 100 (PB-5RO-100KB) | kohm | medium | [1] [7] |
| MGS-1 connector pinout | undocumented | pins | low | [1] |
| External audio input | NONE (stock) | n/a | high | [1] [4] [5] |

## References

1. Roland SH-101 Service Notes, First Edition, Nov 1 1982 (archive.org OCR):
   <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
2. Roland SH-101 spec table — absence of DCB/MIDI/DIN-sync (1982 Service
   Notes specifications page).
3. Roland SH-101 Service Notes high-resolution scans (synthfool 3000px) and
   ManualsLib structured parts list, pages 7–10:
   <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_7.gif>,
   <https://www.manualslib.com/manual/1231849/Roland-Sh-101.html?page=10>
4. Roland SH-101 Service Notes block diagram (VCF/VCA fed only by Source
   Mixer); Circuitbenders SH-101 mods:
   <https://www.circuitbenders.co.uk/synthmod/SH101.html>
5. Kenton SH-101 filter-socket-kit instructions (filter input is a CV, not
   audio): <https://kentonuk.com/wp-content/uploads/2019/06/SH-101_1_socket_kit-doc.pdf>
6. MOD WIGGLER SH-101 external-audio mod thread:
   <https://modwiggler.com/forum/viewtopic.php?t=38615>
7. Roland SH-101 Service Notes MGS-1 parts detail (ManualsLib page 40):
   <https://www.manualslib.com/manual/1029350/Roland-Sh-101.html?page=40>
8. Roland SH-101 Technical Specifications (Roland support / Sweetwater
   mirror): <https://support.roland.com/hc/en-us/articles/201921519-SH-101-Technical-Specifications>,
   <https://www.sweetwater.com/sweetcare/articles/roland-sh-101-technical-specifications/>
9. Kenton converter compatibility guide (SH-101, 1 V/oct, V-trig):
   <https://kentonuk.com/ordering-info/what-kenton-products-do-i-need-to-work-with-my-synth/roland-sh-101/>
10. Gearspace — SH-101 tuning via CV-IN vs keyboard:
    <https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1284669-sh-101-out-tune-only-if-played-through-cv.html>
11. Roland SH-101 Service Notes spec sheet (synthfool sh101spc / sh101_1),
    "Gate (OFF=0V, ON=12V)":
    <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101spc.gif>
12. Roland SH-101 Service Notes CPU Program sheet (sh101_3):
    <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_3.gif>
13. Roland SH-101 Service Notes synth-board schematic + block diagram
    (synthfool sh101_2, sh101_4, sh101_7):
    <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_2.gif>
14. Electric Druid — CEM3340 VCO designs (on-die comp covers Is, not KT/q):
    <https://electricdruid.net/cem3340-vco-voltage-controlled-oscillator-designs/>
15. CEM3340/CEM3345 datasheet (tempco / exponential-scale-error spec) and
    schmitzbits expo-converter tutorial:
    <https://www.schmitzbits.de/expo_tutorial/>, <https://sdiy.info/wiki/CEM3340>
16. Electric Druid / AMSynths — Roland IR3109 filter (four OTA stages,
    Juno-6/60 68K+240pF ladder):
    <https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/>,
    <https://amsynths.co.uk/2022/04/06/all-about-the-ir3109-chip/>
17. AMSynths / Open Music Labs — BA662 OTA (reverse-engineered, no public
    datasheet): <https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/>
