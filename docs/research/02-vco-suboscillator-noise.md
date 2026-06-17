<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# VCO (CEM3340), Sub-Oscillator & Noise

## 1. Scope and summary

This document is the citable source of truth for the mwAudio101 oscillator
section: the single Curtis CEM3340 voltage-controlled oscillator (VCO), its
on-die exponential converter and temperature compensation, the digital
sub-oscillator built from a 4013 dual flip-flop, and the transistor white-noise
generator. It also records how these four sources (saw, pulse, sub, noise) are
summed into the filter front end. Architecture and backlog phases reference
this document by section number.

The SH-101 is a monophonic synthesizer with a single VCO built around the
Curtis CEM3340 [service-manual, high; ref 1, 2, 3]. The CEM3340 integrates the
exponential (1V/octave) converter, precision multiplier, triangle-to-saw and
triangle-to-pulse converters, and an on-die temperature-compensation cell on
one die, so the SH-101 does NOT use a discrete matched-transistor expo pair for
the VCO [datasheet/Electric Druid, high; ref 3, 7]. The panel exposes only
sawtooth and pulse (with pulse-width modulation) directly from the 3340
[service-manual, high; ref 1, 2]. A 4013-based sub-oscillator (IC17 = MB84013B)
divides the VCO down to a -1 octave square, a -2 octave square, and a -2 octave
25% pulse [service-manual + reverse-engineered: Electric Druid, high; ref 1, 9].
A reverse-biased transistor junction (TR23, a 2SC945) generates white noise
[service-manual, high; ref 1]. Pitch is generated digitally: the TMP80C49 CPU
drives a D/A converter producing a 1V/octave control voltage (CV) summed with
panel tune, bender, LFO/MOD and PWM at the CEM3340 frequency CV input
[service-manual, high; ref 1, 2].

### 1.1 Frozen reference-designator facts

Per schematic reconciliation against the high-resolution foldout
[service-manual, high; ref 12, 13]:

- VCO CEM3340 = **IC13** (the earlier "IC3" reading was a wrong/early OCR; an
  archive.org djvu OCR token "IC14 CEM3340" was a known mis-row, since IC14 is
  the IR3109). Both the synthfool foldout and ManualsLib page 9 independently
  read "IC13 CEM3340" [service-manual, high; ref 12, 13].
- VCF IR3109 = **IC14**; CPU TMP80C49P = **IC6** [service-manual, high; ref 13,
  12].
- The original "IC3" round-1 reading appears nowhere on the schematic and is
  discarded [service-manual, high; ref 12].

> Note on internal inconsistency: one verified research slice carried the older
> "IC3" label for the CEM3340 in its prose and parameter table. The frozen,
> schematic-reconciled designator is **IC13**, and this document uses IC13
> throughout [service-manual, high; ref 12, 13].

## 2. VCO core (CEM3340, IC13)

### 2.1 Single integrated oscillator

The VCO core is a single Curtis CEM3340 oscillator IC (Roland part 15229810),
designator IC13 on the Synth board OPH177-2 [service-manual, high; ref 1, 12,
13]. The parts list reads "15229810 CEM3340 VCO"; Wikipedia independently states
"It has a single oscillator (the Curtis CEM3340)"; and the service manual
includes a dedicated "CEM3340 BLOCK & CONNECTION DIAGRAM" inset [service-manual
+ Wikipedia, high; ref 1, 2, 5]. This is a single-VCO monophonic instrument
[Wikipedia + service-manual block diagram, high; ref 5, 1].

### 2.2 Exponential converter is on-die

The exponential (V/octave) pitch converter is internal to the CEM3340; the
SH-101 has no discrete matched-transistor expo pair for the VCO
[datasheet/Electric Druid, high; ref 3, 8]. The CEM3340 integrates the expo
converter, a precision multiplier (labeled "PRECISION MULTIPLIER" in the
manual's inset), and a tempco generator ("TEMPCO GEN", pins 1-2) on one die
[Electric Druid + service-manual, high; ref 3, 1]. The 3340 thus replaces the
classic discrete expo-pair plus tempco-resistor topology. The SH-101's separate
matched pair (2SC1583, "Pair-TR (common E)") is used elsewhere (in the
VCF/IR3109 section), not as the VCO expo converter [service-manual, high; ref
1].

> Do not place an expo pair around an IR3109 in the oscillator model: the
> IR3109 (IC14) is the VCF, not the VCO. Early web-search snippets conflated the
> two; the primary parts list ("15229801 IR3109 VCF") resolves it definitively
> [service-manual, high; ref 1, 12].

### 2.3 Directly-available waveforms

The directly-available VCO waveforms are sawtooth and pulse (square) with
pulse-width modulation; both come from the CEM3340 [service-manual, high; ref 1,
2]. The block diagram shows the VCO emitting a pulse output and a sawtooth
output into the Source Mixer [service-manual, high; ref 1]. The CEM3340 can
itself produce triangle, saw, square and pulse, but the SH-101 uses only the saw
and the variable-width pulse; the triangle output is not brought to the panel as
an audio waveform [Electric Druid + service-manual, high; ref 3, 1]. Whether the
3340 triangle is used anywhere internally is an open question (see Section 8).

### 2.4 Range / footage switch

The range switch has four positions, **16', 8', 4', 2'**, implemented digitally
as discrete CV steps by the CPU and D/A rather than as an analog octave switch
in the VCO [service-manual, high; ref 1, 2]. The CPU Program section states "The
CPU sends the VCO Range data ... to the D/A Converter," with the table mapping
16'=1V, 8'=2V, 4'=3V, 2'=4V [service-manual, high; ref 2, 1]. The Range switch
(S3) is read by the CPU, combined with keyboard plus transpose, then converted
to the 1V/oct CV that drives the CEM3340; i.e. the octave switch is a CV offset,
not a separate analog divider [service-manual, high; ref 2].

> Corrected fact: the footage is **16'/8'/4'/2'** (four positions), NOT
> "8'/16'/32'". The "8'/16'/32'" figure originated from the AMSynths AM8110
> clone module page (a different/expanded product), not the original SH-101; it
> is refuted by the schematic (S3 labeled 2'/4'/8'/16'), Roland's official spec,
> and Wikipedia [service-manual + Roland spec + Wikipedia, high; ref 12, 6, 5].

### 2.5 Pulse-width modulation

The PWM source is selectable ENV / MANUAL / LFO, and the pulse width sweeps from
50% (square) down toward a practical minimum of ~5% [service-manual + AMSynths,
medium; ref 1, 4]. The service manual states "Pulse Width Modulation (50% ~
min.)" and the earlier Service Notes spec states "(50% ~ 0%)"
[service-manual, high; ref 1]. AMSynths (designer of the AM8110 SH-101 VCO
clone) states the slider "goes from 50% down to minimum (stated as 0% in the
service manual, but 5% in practice)" [clone-derived: AMSynths,
presumed-equal/measured, medium; ref 4]. The PWM minimum should bottom at ~5%
(not 10%) to match the original [clone-derived: AMSynths, medium; ref 4].

The PWM CV input is **CEM3340 pin 5** (0-5V applied sets 0-100% pulse width with
Vcc=+15V) [datasheet, high; ref 7].

> Corrected fact: an earlier note claimed a "pulse-width set resistor ~1.8K at
> pin 14". This is WRONG. Per the CEM3340 datasheet, pin 14 is the MULTIPLIER
> OUTPUT (internally connected to the exponential generator's base); the ~1.8K
> resistor (R_S) is the multiplier-output load at PIN 2 and is part of frequency
> scaling, unrelated to pulse width [datasheet, high; ref 7]. Pulse width is set
> by the 0-5V PWM CV at pin 5 [datasheet, high; ref 7].

### 2.6 Temperature compensation (on-die; NO external VCO tempco)

The CEM3340 is **fully temperature-compensated on-die**; the SH-101 does NOT add
an external tempco element to the VCO frequency-CV path [datasheet, high; ref 7;
service-manual, high; ref 12].

The CEM3340 datasheet (CES, 1980) front-page Features read "Fully Compensated;
No Q81 Resistor Required," and the body states "Full temperature compensation
makes these VCOs extremely stable and eliminates the need for a temperature
compensation resistor." The tempco generator multiplies the frequency-control
current by a coefficient proportional to absolute temperature that cancels the
q/kT exponential term on the same die, so "cancellation is therefore nearly
perfect" [datasheet, high; ref 7].

> Frozen correction (supersedes an earlier hypothesis): the claim that the
> 3340's on-die compensation is "incomplete" and is supplemented by an external
> thermistor in the VCO freq-CV network is REFUTED. (a) The 3340 tempco is
> documented as complete/on-die, and (b) the TH1 thermistor is NOT in the VCO
> freq-CV path [datasheet, high; ref 7; service-manual, high; ref 12].

The SDT-1000 thermistor (Roland 15229908, designator **TH1**) is real, but it
sits in the **VCF cutoff-CV path** (VCF/VCA region adjacent to IC14 IR3109 and
the BA662 VCA), not in the CEM3340 VCO network [service-manual, high; ref 12,
13]. Functionally this is the canonical Roland split: the CEM3340 carries on-die
compensation so the VCO needs no external tempco, whereas the OTA-based IR3109
filter has no on-die tempco and uses TH1 to stabilise cutoff tracking
[service-manual + datasheet, high; ref 12, 7]. TH1 belongs to the VCF dimension;
it appears here only to close the historical misattribution.

For general context (theory, not an SH-101 measurement): an uncompensated
exponential-converter scale term has a magnitude of roughly **3300 ppm/degC
(~0.33%/degC)** at ~298-300K [theory/inference, unmeasured; ref 10, 11]. The
SIGN is convention-dependent: the volts-per-octave SCALE factor is proportional
to absolute temperature and is therefore POSITIVE (+3300 ppm/degC), whereas the
frequency-per-volt sensitivity is negative; an earlier "-0.33%/degC" label is
ambiguous and should not be stated as settled [theory/inference, unmeasured;
ref 10, 11]. This term is fully cancelled on-die in the CEM3340 and so does not
require external compensation in the SH-101 VCO [datasheet, high; ref 7].

### 2.7 High-frequency tracking

The CEM3340 provides a high-frequency tracking output on **pin 7**, a current
that compensates for the way the internal comparator's switching delay (and the
bulk emitter resistance of Q2) makes the oscillator go flat in the uppermost
octaves, "significant at frequencies greater than 5KHz" [datasheet, high; ref
7]. The current is converted with a grounded resistor to a voltage, a portion of
which is fed back into the frequency control input (**pin 15**), per Electric
Druid via a ~1M resistor [datasheet + reverse-engineered: Electric Druid, high;
ref 7, 3].

> Honest label: the word "trimmed" for the HF-track network is the CEM3340's
> recommended design practice, but the SH-101 adjustment procedure lists VCO
> TUNE / WIDTH / RANGE WIDTH / PW and D/A trims, NOT a dedicated HF-track
> trimmer. Treat SH-101 HF tracking as "present per chip design" rather than
> "user-trimmed in the SH-101" unless a high-resolution schematic confirms a
> trim pot on pin 7 [theory/inference, unmeasured; ref 3, 1].

### 2.8 Pitch CV, tuning, and supplies

Pitch CV is a digitally-generated 1V/octave control voltage. The internal CV
spans 0.415V to 5V; the external CV In jack accepts 0-7V [service-manual, high;
ref 1]. The CPU (TMP80C49P-6-7301, 6 MHz ceramic resonator) computes pitch and
feeds a D/A converter; the resulting key CV is summed with the analog modulation
sources into the CEM3340 frequency CV input (pin 15) [service-manual +
reverse-engineered: Electric Druid, high; ref 1, 2, 3].

Tuning range is +/-50 cents (panel TUNE), referenced to **A4 = 442 Hz at 8'
range with Transpose = Middle** [service-manual, high; ref 1, 2]. Calibration
trimmers: VR7 = VCO TUNE, VR6 = VCO WIDTH, VR5 = RANGE WIDTH, VR9 = panel TUNE,
VR4 = PW; D/A side VR2 = D/A TUNE, VR1 = +5V, VR3 = D/A LINEAR [service-manual,
high; ref 1, 2].

Supplies: +15V on CEM3340 pin 16; the negative supply at pin 3 is limited by an
internal 6.5V +/-10% zener, with direct connection valid for ~-4.5V to -6.0V
(the SH-101 inset shows -5V at pin 3) [datasheet, high; ref 7; service-manual,
high]. Note the instrument-level rail set (+15V/-15V/+5V plus a +5V memory
backup, derived from a 9-12V DC input via a DC/DC converter) is documented at
medium confidence; the exact PSU regulator topology (whether a +14V and -5V rail
exist and how each is generated) is a labelled dispute, not a frozen fact
[service-manual, medium; ref 12; disputed/low, ref 12].

### 2.9 Drift and stability model

The same-die CEM3340 is regarded as a very stable VCO with good thermal
tracking. The datasheet lists "Low Temperature Drift," "High Exponential Scale
Accuracy," Exponential Scale Error (trimmed) typ 0.05% / max 0.3%, and
Oscillator Drift typ +/-50 ppm [datasheet, high; ref 7]. Per-voice drift in a
clone should therefore be modeled as a small, slow warm-up offset/scale error,
not large free-running pitch wander [engineering inference grounded in CEM3340
specs; ref 7, 3].

> Honest label: no SH-101-specific published pitch-drift or warm-up figure
> (cents/degC or Hz over time) was found. The +/-50 ppm and 0.05-0.3% scale
> figures are CEM3340 datasheet (Curtis original) values; modern clones
> (AS3340, V3340) match the pinout but may differ slightly in tempco/drift, and
> should be verified against the specific clone datasheet for a clone-based
> reproduction [theory/inference, unmeasured for SH-101; ref 7].

## 3. Sub-oscillator (4013 / MB84013B, IC17)

### 3.1 Divider IC

The sub-oscillator is generated by a 4013 dual D-type CMOS flip-flop (IC17),
Roland part **MB84013B** (Fujitsu CD4013-equivalent, Roland part 15159105F0)
[service-manual + reverse-engineered: Electric Druid, high; ref 1, 9]. The
control-board schematic labels the divider IC "IC17 4013," and the parts list
lists the dual D-type flip-flop as MB84013B; the two flip-flops in the single
package implement the two divide-by-2 stages [service-manual, high; ref 1, 9].

### 3.2 Clocked from the sawtooth

The 4013 divider is clocked from the VCO **sawtooth/ramp** output, NOT the
square wave [reverse-engineered: Electric Druid + clone-derived: AMSynths, high;
ref 9, 4, 1]. The sawtooth is buffered by transistor TR41 and fed through R183
(68k, clearly legible) to the IC17 clock input; the single rising edge per VCO
cycle clocks the first flip-flop [service-manual, high; ref 1, 9]. AMSynths
notes the subs are "unusually generated from the sawtooth" and that the sawtooth
is buffered and voltage-adjusted to clear the 4013's logic-high input threshold
[clone-derived: AMSynths, high; ref 4]. The ramp amplitude is described as
"0-10V," but that figure is derived/approximate and was not independently
verified off the schematic [theory/inference, unmeasured; ref 9].

### 3.3 Three sub waveforms

Three sub-osc waveforms are available, selected by switch **S5** (3-position):
square at -1 octave, square at -2 octaves, and 25% pulse at -2 octaves
[service-manual + reverse-engineered: Electric Druid, high; ref 5, 9, 1].
Roland's spec (via Wikipedia) reads "Sub wave (selectable -1 Oct. Square, -2
Oct. Square or -2 Oct Pulse)" [Wikipedia/Roland spec, high; ref 5]. The S5
switch positions are labeled "1OCT DOWN" and "2OCT DOWN" on the schematic (the
third position being the diode-OR pulse) [service-manual, high; ref 1].

### 3.4 Division mechanism

The first flip-flop divides the VCO by 2 (-1 octave square); the second
flip-flop divides that again by 2 (-2 octave square) [reverse-engineered:
Electric Druid + service-manual, high; ref 9, 1]. Each flip-flop output is a 50%
square at its respective octave. The schematic shows the two stages with
coupling resistors R181 = 47k and R182 = 47k [service-manual, high; ref 1].
This is standard binary frequency division, implemented exactly as the SH-101
circuit shows [theory + service-manual, high; ref 9, 1].

### 3.5 The 25% pulse via diode-OR

The -2 octave 25% pulse is made by a **diode-OR** of the two flip-flop square
outputs (diodes **D38, D39**): when either flip-flop output is high, the OR
output is high, producing a signal high 75% of the time / low 25% of the time
[reverse-engineered: Electric Druid + service-manual, high; ref 9, 1]. This is
NOT a continuous blend pot: VR15 only sets overall sub LEVEL; the pulse is a
fixed diode-OR selected via S5 (see Section 7) [service-manual, high; ref 1, 9].

The 25% pulse retains a strong -1 octave character because a 25% duty cycle has
the strongest 2nd harmonic of all pulse widths, so the -2 oct pulse sounds like
a blend of -1 and -2 octaves [reverse-engineered: Electric Druid + Fourier
theory, high; ref 9].

### 3.6 Diode part number

The D38/D39 reference designators are confirmed on the schematic, but the
specific catalog diode is **inferred**. The earlier "1S188" part number does NOT
appear in the parts list; the small-signal silicon diodes listed are **1S1585**
and 1S2473 (most plausibly 1S1585) [service-manual, medium; ref 1]. The
schematic does not print which catalog diode sits at D38/D39, so this is stated
at medium confidence [community/schematic-inferred, medium; ref 1].

### 3.7 Supply rail and pitch tracking

The 4013 supply rail (Vdd) is reported as **+14V** in the original SH-101 (the
CD4013 is rated +5V to +20V) [reverse-engineered: Electric Druid, medium; ref
9]. This sets the logic-high level of the sub-osc square waves before mixer
attenuation. AMSynths' clone uses a 12V rail and retuned the R183 threshold
resistor (to ~56k) to clock reliably, consistent with the original +14V rail
[clone-derived: AMSynths, medium; ref 4].

> Honest label: the +14V Vdd rests on a SINGLE source (Electric Druid); it is
> not legible on the rendered schematic and not independently corroborated.
> Treat as "reported by Electric Druid; not independently confirmed"
> [reverse-engineered: Electric Druid, medium; ref 9].

Because the sub is a hard digital division of the VCO frequency, it always sits
an exact 1 or 2 octaves below whatever footage the RANGE switch selects, with no
independent tuning or drift between VCO and sub [theory + service-manual, high;
ref 1]. (The sub tracks the actual SH-101 footage range 16'/8'/4'/2'; see
Section 2.4 for the correction of the "8'/16'/32'" clone figure.)

## 4. Noise generator (2SC945, TR23)

### 4.1 Source

The noise source is WHITE noise generated by a reverse-biased transistor
junction: TR23 = **2SC945** (NZ), annotated "Noise generator" in the parts list
[service-manual, high; ref 1]. The NOISE GEN schematic block shows TR23 as the
noise-source transistor feeding TR22 as a following amplifier, then an op-amp
gain stage [service-manual, high; ref 1]. The "reverse-biased junction"
mechanism is standard for this stage; the schematic does not explicitly annotate
the bias [service-manual, high/inference; ref 1].

### 4.2 Amplifier stage

The noise-source transistor TR23 feeds amplifier transistor **TR22**, then an
op-amp gain stage [service-manual, high; ref 1]. The op-amp feedback network is
R86 = 220k with C32 = 240pF, with R83 = 47k, C33 = 10uF/16V and C31 = 0.01uF in
the path [service-manual, high; ref 1].

> Clarification: the noise-amp op-amp is a section of **IC8 (a 4556 quad
> op-amp)**, which is distinct from the source-mixer op-amp IC18 that feeds the
> VCF (see Section 7) [service-manual, high; ref 1].

### 4.3 Noise color

The output is **white noise** (uniform power per Hz): there is no pink
(-3 dB/oct) shaping filter in the NOISE GEN stage [service-manual + Wikipedia,
high; ref 1, 5]. The 220k/240pF feedback only sets a gentle high-frequency
rolloff for stability, not a pinking network [service-manual, high; ref 1].

> Honest label: Roland's own support spec page (which would give the verbatim
> "white noise" wording) returned HTTP 403 and could not be captured verbatim;
> the "white noise" designation is corroborated by Wikipedia plus the schematic
> (absence of any pinking filter) [Wikipedia + service-manual, high; ref 5, 1].

## 5. Source mixer (saw + pulse + sub + noise into VCF)

The Source Mixer sums Saw, Pulse, Sub and Noise; each has a 100k-linear (100kB)
front-panel level slider [service-manual + Wikipedia, high; ref 1, 5]. The Sub
level is VR15 (100kB, after switch S5); the Noise level is VR16 (100kB)
[service-manual, high; ref 1]. The summed sources feed the input op-amp **IC18**
(a dual op-amp) of the VCF (IR3109, IC14) [service-manual, high; ref 1].

Mix resistors read off the schematic: R185 = 100k, R186 = 200k, and
**R187 = 680k** (additional R184 = 220k, R188 = 47k, R192 = 33k present)
[service-manual, medium; ref 1].

> Corrected fact: R187 = **680k**, NOT 68k. The "68k" value belongs to R183 in
> the sub-osc clock path (Section 3.2); the earlier note transposed the two
> [service-manual, high; ref 1].

## 6. Key parameters

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| VCO core IC | Curtis CEM3340 (Roland 15229810), IC13 on Synth board OPH177-2 | part | high | ref 1, 12, 13 |
| Number of VCOs | 1 | oscillator | high | ref 5, 1 |
| Directly-available waveforms | sawtooth + pulse (variable width) | waveforms | high | ref 1, 2 |
| Range/octave switch positions | 16', 8', 4', 2' | feet | high | ref 1, 6, 12 |
| Range data CV mapping | 16'=1V, 8'=2V, 4'=3V, 2'=4V | V (digital range data) | high | ref 2, 1 |
| Pulse-width modulation range | 50% to ~5% (spec states 50% to min/0%) | % duty | medium | ref 1, 4 |
| PWM mode select | ENV / MANUAL / LFO | switch positions | high | ref 1 |
| CEM3340 PWM CV input pin | pin 5 (0-5V sets 0-100% PW) | pin / V | high | ref 7 |
| CEM3340 HF-tracking pin | pin 7 (current fed back to freq CV pin 15 via ~1M) | pin | high | ref 7, 3 |
| CEM3340 supply pins | +15V on pin 16; V- pin 3 (internal 6.5V zener; ~-5V) | V / pin | high | ref 7 |
| Tune range | +/-50 | cents | high | ref 1 |
| Tuning reference | A4 = 442 Hz at 8' / Transpose Middle | Hz | high | ref 1, 2 |
| Pitch CV scaling | 1V/octave; internal CV 0.415V-5V (CV In jack 0-7V) | V/oct | high | ref 1 |
| External VCO tempco | NONE in VCO freq-CV path (3340 self-compensated on-die) | n/a | high | ref 7, 12 |
| SDT-1000 thermistor TH1 | Roland 15229908; located in VCF cutoff-CV path, not VCO | part / placement | high | ref 12, 13 |
| Expo scale tempco (general theory) | ~3300 ppm/degC (~0.33%/degC); sign convention-dependent | ppm/degC | medium (theory) | ref 10, 11 |
| VCO calibration trimmers | VR7 TUNE, VR6 WIDTH, VR5 RANGE WIDTH, VR9 panel TUNE, VR4 PW; D/A VR1/VR2/VR3 | trimmers | high | ref 1, 2 |
| Pitch CPU / clock | TMP80C49P-6-7301, 6 MHz ceramic resonator | part / MHz | high | ref 1, 2, 12 |
| Sub-osc divider IC | 4013 dual D flip-flop (MB84013B, Roland 15159105F0, IC17) | part | high | ref 1, 9 |
| Sub octave 1 (first FF) | -1 octave square (VCO / 2) | octave / ratio | high | ref 5, 9 |
| Sub octave 2 (second FF) | -2 octaves square (VCO / 4) | octave / ratio | high | ref 5, 9 |
| Sub pulse (third option) | -2 oct 25% duty (high 75% / low 25%), diode-OR of the two squares | octave + duty | high | ref 9, 1 |
| Sub-osc clock source | VCO sawtooth/ramp, buffered by TR41, via R183 68k to 4013 clock | signal / R | high (R183=68k); 0-10V approx | ref 4, 9, 1 |
| Sub flip-flop coupling resistors | R181 = 47k, R182 = 47k | ohms | high | ref 1 |
| Diode-OR diodes (25% pulse) | D38, D39 (likely 1S1585; 1S2473 alt) | component refs | medium | ref 1 |
| Sub waveform select switch | S5 (3-position: -1 sq / -2 sq / -2 pulse) | switch ref | high | ref 1 |
| Sub level slider | VR15 = 100k linear (100kB) | pot / taper | high | ref 1 |
| 4013 supply rail (Vdd) | +14 | V | medium (single source) | ref 9 |
| Noise generator transistor (source) | 2SC945 (NZ), TR23, reverse-biased junction | transistor | high | ref 1 |
| Noise amplifier transistor | TR22 (follows TR23) | schematic ref | high | ref 1 |
| Noise amp op-amp feedback | R86 = 220k, C32 = 240pF (op-amp is IC8 / 4556); R83 47k, C33 10uF, C31 0.01uF | ohms / farads | high | ref 1 |
| Noise color | White noise (no pink shaping filter) | spectral type | high | ref 5, 1 |
| Noise level slider | VR16 = 100k linear (100kB) | pot / taper | high | ref 1 |
| Mixer into VCF | Saw/Pulse/Sub(VR15)/Noise(VR16) summed at IC18; R185 100k / R186 200k / R187 680k | topology / R | medium | ref 1 |

## 7. Design implications for mwAudio101

### 7.1 VCO

Model the VCO as a single integrated oscillator equivalent to a CEM3340: one
core running at the pitch CV, producing a sawtooth and a variable-width pulse,
with PWM CV (0-5V mapped to ~50% -> ~5% duty) from an ENV/MANUAL/LFO-selected
source [service-manual + datasheet, high; ref 1, 7]. The PWM minimum should
bottom at ~5%, not 10%, to match the original [clone-derived: AMSynths, medium;
ref 4].

Implement pitch as a digital 1V/oct CV: the range switch contributes +1V/+2V/
+3V/+4V offsets for 16'/8'/4'/2', summed with key CV, +/-50c tune, bender and
LFO/MOD; the octave switch is a CV offset, not a separate analog divider
[service-manual, high; ref 1, 2]. Account for the transpose offset (+0/+1/+2V
from the L/M/H switch) added on top of the base range data
[service-manual, medium; ref 2].

For per-voice drift, treat the 3340 as intrinsically stable (same-die expo +
tempco): use a SMALL slow drift only -- (a) a warm-up transient (first-order
settle of scale/offset over tens of seconds), (b) a tiny residual scale error if
the kT/q term is not perfectly compensated, and (c) progressive sharpness in the
top octave unless HF tracking (pin 7) is modeled. Avoid large random pitch
wander, which would mischaracterize the 3340 [engineering inference grounded in
CEM3340 specs; ref 7, 3]. Do not add an external VCO tempco element -- the 3340
is self-compensated; TH1 belongs to the VCF model, not the VCO [datasheet +
service-manual, high; ref 7, 12].

### 7.2 Sub-oscillator

Model the sub as an exact integer divider of the VCO PHASE, never an independent
oscillator: derive sub waveforms from the VCO's wrapped phase so they are always
phase-locked and drift-free [theory + service-manual, high; ref 9, 1]. Generate
the -1 oct square at f/2 and the -2 oct square at f/4 (both flipping once per VCO
cycle on the sawtooth wrap). Generate the -2 oct "25% pulse" as the logical OR
of the two squares (out = sq1 OR sq2), yielding a 75%-high / 25%-low rectangle at
f/4 with a strong 2nd harmonic -- implement it as the OR, not a separate PWM
oscillator, to match the harmonic spectrum exactly [reverse-engineered: Electric
Druid, high; ref 9]. Expose a 3-way selector matching S5. The sub needs no
separate tuning, glide, or drift parameters -- it inherits all VCO pitch/PWM/
range behavior [theory + service-manual, high; ref 9, 1].

### 7.3 Noise and mixer

Noise must be flat WHITE noise (uniform power per Hz) with its own independent
100k-linear level; do not pink it [service-manual + Wikipedia, high; ref 1, 5].
The only HF shaping is the gentle op-amp rolloff (R86 220k / C32 240pF); if
matching the analog top-end is desired, optionally model a single-pole LPF
around 1/(2*pi*220k*240p) ~ 3 kHz [service-manual, high; ref 1]. Model the
mixer as a simple LINEAR (not log) sum of four sources (saw, pulse, sub, noise)
with a common headroom, each behind a 100k-linear slider, summed at the VCF
input; clamp the digital sub squares to the oscillator level before summing
[service-manual, high; ref 1].

## 8. Confidence, disputes & honest labels

This section surfaces every disputed, low-confidence, corrected, or
inference-only item, plus residual risks, stated plainly.

### 8.1 Frozen corrections (supersede earlier research)

- **VCO is IC13, not IC3/IC14.** The CEM3340 is IC13; "IC3" was an early/wrong
  reading and "IC14 CEM3340" was a known OCR mis-row (IC14 is the IR3109). One
  research slice still carried "IC3" in prose; IC13 is authoritative
  [service-manual, high; ref 12, 13].
- **CEM3340 tempco is COMPLETE on-die; no external VCO tempco.** The hypothesis
  that the 3340's compensation is "incomplete" and supplemented by an external
  thermistor in the VCO path is REFUTED by the datasheet ("Fully Compensated; No
  Q81 Resistor Required") [datasheet, high; ref 7].
- **TH1 (SDT-1000) is in the VCF cutoff-CV path, not the VCO.** Part number and
  designator are correct; the placement/function attribution to the VCO was
  wrong [service-manual, high; ref 12, 13].
- **PWM is set by CEM3340 pin 5 (0-5V), not by a "1.8K resistor at pin 14."**
  Pin 14 is the multiplier output; the 1.8K (R_S) is the multiplier load at pin
  2 (frequency scaling), unrelated to pulse width [datasheet, high; ref 7].
- **Footage is 16'/8'/4'/2', not 8'/16'/32'.** The "8'/16'/32'" came from the
  AMSynths AM8110 clone module, a different product [service-manual + Roland
  spec + Wikipedia, high; ref 12, 6, 5].
- **Mixer R187 = 680k, not 68k.** The 68k belongs to R183 in the sub-osc clock
  path; the values were transposed [service-manual, high; ref 1].
- **Diode-OR diodes are NOT "1S188."** No "1S188" exists in the parts list;
  most plausibly 1S1585 (1S2473 alternative), stated at medium confidence
  [service-manual, medium; ref 1].

### 8.2 Disputed / low-confidence items

- **DISPUTED (low) -- "100k pot blends the two sub squares at mid position to
  make the pulse."** Refuted as a description of the SH-101. The three sub
  waveforms are selected by the 3-position switch S5, and the 25% pulse is a
  fixed diode-OR (D38/D39); VR15 only sets sub LEVEL and does not crossfade the
  squares. This appears to be a DIY/clone interpretation; the sonic result is
  equivalent but the mechanism differs [community disassembly, partly inferred,
  refuted; ref 9, 1].
- **LOW/DISPUTED -- exact PSU regulator topology.** Whether a +14V rail and a
  -5V rail exist alongside +15V/-15V/+5V, and how each is generated, could not
  be read off the dense PSU corner of the foldout; a third-party (synhouse) rail
  description could not be verified (timeouts/403). Remains a labelled dispute
  [service-manual, low/disputed; ref 12].
- **MEDIUM -- 4013 Vdd = +14V.** Single-sourced (Electric Druid), not legible on
  the rendered schematic, not independently corroborated [reverse-engineered:
  Electric Druid, medium; ref 9].
- **MEDIUM -- PWM minimum ~5%.** The Roland spec states "50% to min/0%"; the ~5%
  practical minimum is from AMSynths (clone designer, measured), not the Roland
  spec [clone-derived: AMSynths, medium; ref 4].
- **MEDIUM (theory) -- expo scale tempco ~3300 ppm/degC.** General
  analog-synth theory, not an SH-101 measurement; the SIGN is
  convention-dependent (volts-per-octave scale is POSITIVE), so "-0.33%/degC"
  must not be stated as settled [theory/inference, unmeasured; ref 10, 11].
- **MEDIUM -- mixer summing-resistor values and relative source gains.** R185
  100k / R186 200k / R187 680k read tentatively in places; whether the sub
  squares are hard-limited (~14V) or clamped before summing was not fully
  traced, affecting the relative-loudness model [service-manual, medium; ref 1].
- **MEDIUM/inference -- D38/D39 part number** (Section 3.6) and **0-10V sawtooth
  clock amplitude** (Section 3.2): both approximate/inferred, not read directly
  [community/schematic-inferred + theory/inference; ref 1, 9].

### 8.3 Residual risks

- TH1's exact net within the VCF/VCA region (filter expo control vs VCA expo
  control) was read from a limited-resolution scan and should be confirmed
  against a high-resolution schematic before being stated as definitive in a
  circuit model [service-manual, medium; ref 12].
- The CEM3340 datasheet figures used are the Curtis original (CES 1980); clones
  (AS3340, V3340) match the pinout but may differ in tempco/drift -- verify
  against the specific clone datasheet for a clone-based reproduction
  [datasheet, high; ref 7].
- HF-track "trimming" is the chip's recommended practice, not confirmed as a
  user trim in the SH-101 adjustment procedure [theory/inference; ref 3, 1].
- Faint-print risk: R181 47k, R183 68k, R187 680k, R185 100k, R186 200k, R86
  220k, C32 240p, C33 10uF were clearly legible; R182, R87, R88, R192 and the
  D38/D39 catalog part are lower-confidence reads from a ~150-dpi render and
  should be re-verified before being treated as load-bearing
  [service-manual, medium; ref 1].
- IC-to-designator mapping (IC13 CEM3340, IC14 IR3109, IC17 4013, IC8 4556, IC6
  CPU, IC18 mixer) comes from reading the schematic, since the parts list lists
  catalog part numbers not cross-indexed to designators -- reliable here, noted
  as a methodology caveat [service-manual, high; ref 1, 12, 13].

### 8.4 Open validation gaps (no physical-unit measurements)

By project decision, mwAudio101 has NO bench measurements of a physical unit.
The following therefore remain OPEN VALIDATION GAPS, not delivered facts:

- Measured pitch-drift / warm-up curve (cents/degC or Hz over time) for the
  SH-101 VCO -- no SH-101-specific figure exists; only CEM3340 datasheet
  +/-50 ppm and 0.05-0.3% scale-error specs are available [open gap; ref 7].
- Oscilloscope-verified sub-osc duty cycles and clock amplitude, and measured
  noise spectrum (to confirm flat white power) -- both inferred from the
  schematic, not bench-measured [open gap; ref 1, 9].
- Measured PWM minimum duty (the ~5% figure is the AMSynths clone observation)
  [open gap; ref 4].
- Measured relative mixer gains (saw vs pulse vs sub vs noise) into the filter
  [open gap; ref 1].

### 8.5 Software-emulation caveat

Where later Roland software (e.g. Roland Cloud SH-01A) adds features that the
hardware lacks, those are emulation artifacts, not hardware facts. The hardware
oscillator section has NO sine wave from the VCO (the 3340 triangle is not
panel-routed; saw and pulse only), NO 32'/64' VCO registers (footage is
16'/8'/4'/2'), and NO external audio input into the mixer. The IR3109 is shared
only with the Juno-6/60 and Jupiter family -- the TB-303 uses a discrete
diode-ladder filter and does NOT share the IR3109; in any case the IR3109 is the
VCF, not part of this VCO/sub/noise section [Wikipedia + service-manual, high;
ref 5, 12].

## 9. References

1. Roland SH-101 Service Manual / Service Notes (Nov 1 1982), schematic and
   parts list (rendered PDF) --
   <https://electricdruid.net/wp-content/uploads/2025/05/SH101-Service-Manual.pdf>
2. Roland SH-101 Service Notes, CPU Program section / Specifications (djvu text)
   -- <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
3. Electric Druid -- CEM3340 VCO Voltage Controlled Oscillator Designs --
   <https://electricdruid.net/cem3340-vco-voltage-controlled-oscillator-designs/>
4. AMSynths -- AM8110 SH-101 VCO (clone module) --
   <https://amsynths.co.uk/home/products/oscillators/am8110-sh-101-vco/>
5. Wikipedia -- Roland SH-101 -- <https://en.wikipedia.org/wiki/Roland_SH-101>
6. Roland -- SH-101 Technical Specifications --
   <https://support.roland.com/hc/en-us/articles/201921519-SH-101-Technical-Specifications>
7. CEM3340 / CEM3345 datasheet (CES, 1980) --
   <https://www.bustedgear.com/images/datasheets/CEM3340-3345.pdf>
8. Electric Druid -- Roland filter designs with the IR3109 or AS3109 --
   <https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/>
9. Electric Druid -- A Study of Sub-Oscillators --
   <https://electricdruid.net/a-study-of-sub-oscillators/>
10. North Coast Synthesis -- Exponential converters and how they work --
    <https://northcoastsynthesis.com/news/exponential-converters-and-how-they-work/>
11. Open Music Labs -- Thermal Compensation of Analog Exponential Converters --
    <http://www.openmusiclabs.com/files/expotemp.pdf>
12. Roland SH-101 Service Manual foldout schematic, synth board OPH177-2 (sheet
    sh101_7) -- <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/sh101_7.gif>
13. ManualsLib -- Roland SH-101 circuit diagram (page 9) / parts list (page 10)
    -- <https://www.manualslib.com/manual/1231849/Roland-Sh-101.html?page=9>
