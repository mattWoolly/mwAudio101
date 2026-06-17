<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADSR Envelope, LFO & VCA

## 1. Scope and summary

This document is the verified research source of truth for the Roland SH-101
modulation and amplitude sections of the mwAudio101 project: the single ADSR
envelope generator, the single LFO (Modulator) with its waveform/routing
behaviour, and the BA662A voltage-controlled amplifier (VCA). Architecture and
backlog phases cite this document by section number.

Key facts, stated up front and developed below:

- One ADSR envelope generator is shared across the VCF, the VCA, and pulse
  width; there is **no** separate filter envelope and amp envelope
  [service-manual, high] (Section 2).
- The VCA can be driven by the envelope (ENV) **or** directly by the gate
  (GATE) via a mode switch [service-manual + Roland docs, high] (Sections 2, 4).
- One LFO: smooth wave (panel sine symbol, core is a triangle), square, RANDOM
  (CPU-generated digital pseudo sample/hold), and NOISE (white noise from the
  audio-path noise generator). Rate 0.1-30 Hz. There is **no sine LFO core**
  and the four-position selector is original-hardware; the six-position
  selector with separate Sine/Triangle/Saw is a later software reissue artifact
  [service-manual + clone-derived + reissue-manual, mixed] (Section 3).
- The VCA core is the Roland/Rohm BA662A OTA (IC15). Its internal architecture
  is **reverse-engineered** (Open Music Labs), with no public datasheet
  [reverse-engineered: Open Music Labs] (Section 4).
- Envelope segment curve law (exponential RC) and the LFO core op-amp identity
  are **unmeasured theory/inference**, not documented SH-101 specs
  [theory/inference, unmeasured] (Sections 2.6, 3.3, 5).

Frozen schematic facts carried from project-wide reconciliation: the CEM3340
VCO is **IC13** (not IC3 - that reading was a wrong OCR), the IR3109 VCF is
IC14, the BA662A VCA is IC15, and the CPU is IC6 (TMP80C49P). The sub-oscillator
octave divider 4013 (MB84013B) is IC17 - this 4013 is **not** the envelope
trigger flip-flop and **not** the LFO [service-manual, high; see Sections 2.5
and 3.6].

We have **no physical-unit measurements** of an SH-101 (project decision).
Anything requiring bench data - oscilloscope ADSR curves, LFO minimum-frequency
verification, VCA dB-vs-control-volts transfer, harmonic spectra - is an **open
validation gap**, not a delivered fact (Sections 5 and 6).

## 2. ADSR envelope generator

### 2.1 One shared envelope

The SH-101 (released 1 November 1982) has a **single** ADSR envelope generator
(one EG, four stages A/D/S/R). The Roland SH-101 Service Notes SPECIFICATIONS
page lists one ENV block; Roland's published spec describes it as an "ADSR
envelope, triggered by gate or LFO." This one envelope is shared, not a
dedicated filter envelope plus a dedicated amp envelope [service-manual, high]
[R1][R2].

The shared envelope routes - via per-destination depth controls - to three
destinations [service-manual + Roland docs, high] [R1][R2]:

- **VCF cutoff** through the filter ENV Depth knob.
- **VCA gain**, selectable by the VCA ENV/GATE switch (Section 2.4).
- **VCO pulse width**, when the PWM Mode switch is set to ENV.

The TH1 thermistor and CEM3340 temperature compensation belong to the VCF/VCO
dimensions and are out of scope here; they are noted only so they are not
mis-attributed to the envelope.

### 2.2 Documented ADSR ranges

All four ADSR ranges are confirmed verbatim against the primary service manual
SPECIFICATIONS page (OCR'd locally) and corroborated by Roland's official
technical specifications [service-manual, high] [R1][R2]:

- Attack Time: 1.5 ms - 4 s.
- Decay Time: 2 ms - 10 s.
- Sustain Level: 0 - 100% of the attack peak (a level, not a time).
- Release Time: 2 ms - 10 s (same range as Decay).

Sustain is normalized to a fraction of the attack peak, not an absolute voltage
[service-manual, high] [R1].

### 2.3 Trigger-source switch (GATE+TRIG / GATE / LFO)

A three-position trigger-source selector governs how the envelope is fired
[service-manual + Roland docs, high] [R1][R3][R2]:

- **GATE+TRIG**: a new trigger fires the envelope on every key, so legato
  playing retriggers (used for trills/rapid restarts).
- **GATE**: the envelope follows the held gate; one shot per held note, legato
  does not retrigger.
- **LFO**: the LFO (Modulator) clocks/triggers the envelope independent of
  keys, repeating at each modulator cycle while a key is held [R2].

### 2.4 VCA ENV/GATE source switch

The VCA has its own ENV/GATE selector. In **ENV** position the VCA follows the
ADSR contour; in **GATE** position the VCA opens/closes with the raw gate
(organ-style fixed level for the gate duration), bypassing the envelope shape.
This is a documented routing option and is important for the model: the envelope
is not permanently hard-wired to the VCA [service-manual + Roland docs, high]
[R1][R2][R4]. See Section 4.3 for the amplifier-side detail.

### 2.5 Circuit: discrete, no envelope IC

There is **no dedicated single-chip envelope IC** in the SH-101. The service
manual IC/parts list enumerates the CEM3340 (VCO, IC13), IR3109 (VCF, IC14),
BA662A (VCA, IC15), the CPU (TMP80C49P, IC6), buffers and multiplexers - but no
ADSR generator chip. AMSynths independently states the ADSR is "6 transistors
and a dual op-amp IC plus a few discrete parts," i.e. a discrete RC-based
generator [service-manual + clone-derived: AMSynths, high] [R1][R5].

Honest correction on the trigger logic (verification downgraded this from
high to medium): the SH-101 gate/trigger is **primarily CPU-generated**
(TMP80C49P / IC6 outputs Gate via Port 2). The CD4013 dual D-type flip-flop
present in the design is **IC17 = MB84013B and serves the sub-oscillator octave
dividers, not the envelope trigger** [service-manual + clone-derived, high]
[R1][R5][R6]. Earlier notes that attributed envelope triggering to a "4013"
and to a transistor "TR34" via "IC7" are **not verified**: IC7 is an HD/MC
14050B hex buffer (CD4050), not a 4013, and the "TR34" designator could not be
confirmed against the (noisy, image-only) schematic OCR. Treat specific
envelope-section transistor/IC reference numbers as
[community disassembly/OCR, partly inferred] until a clean schematic is traced.

### 2.6 Segment curve law (unmeasured theory)

The envelope segment curve shapes are **not stated** in the SH-101 service
manual. An exponential (RC charge/discharge) characteristic is **inferred** from
the discrete RC timing topology: a capacitor charged toward a fixed rail through
a resistor gives the classic fast-then-slow exponential attack and exponential
decay/release. General analog-synth theory (EarLevel, North Coast Synthesis)
supports this for non-constant-current envelopes [theory/inference, unmeasured]
[R7][R8][R5].

This must remain labeled as inference. No primary source confirms the SH-101
specifically uses a plain RC (rather than a constant-current/linear) timing law,
and op-amp buffering plus transistor switching may make the real curve deviate
from an ideal exponential. The SH-101 envelope is anecdotally "snappy," which is
consistent with - but does not prove - an RC charge. The phrase "not linear" is
plausible but unproven. Expose the curve law as a configurable parameter in the
model rather than asserting it (Section 6.1).

### 2.7 Gate/trigger interface voltages

The rear-panel gate/trigger interface levels [service-manual, high] [R1]:

- Gate Output: OFF = 0 V, ON = **12 V** per the service manual.
- Gate Input: +2.5 V or more.
- EXT CLK Input: +2.5 V or more.

Honesty note on gate-output level: the **service manual is authoritative at
12 V** (verbatim "Gate (OFF=0V, ON=12V)"). Roland's current web spec / Sweetwater
list **ON = 10 V at a 100 kΩ load** [R2][R9]. The likely reconciliation is that
12 V is the open-circuit/spec value and the level sags toward ~10 V under load
(the gate is buffered by an HD/MC 14050B on the +12 V rail), but that
reconciliation is itself undocumented. Carry 12 V as the spec value and flag the
10 V loaded figure (Section 5).

### 2.8 Do not conflate with System 100 "Model 101"

The earlier Roland System 100 "Model 101" (a 1975 modular product) has
**different** envelope ranges: Attack 0.4 ms - 3 s, Decay/Release 0.8 ms - 6 s,
+6 V contour peak, +14 V gate. These numbers must be kept out of the SH-101
model [manualslib, high] [R10].

## 3. LFO (Modulator) and modulation routing

### 3.1 One LFO, rate 0.1-30 Hz

The SH-101 has a **single** LFO, labeled MODULATOR / "LFO·CLK," with a rate
range of **0.1 Hz - 30 Hz** (rate pot VR9, 100 kB). The same control doubles as
the internal sequencer/arpeggiator clock, hence "LFO/CLK"
[service-manual, high] [R11][R2].

Disputed alternate figure: some sources state 0.35 Hz - 30 Hz. This 0.35 Hz
figure originates from the **AMSynths clone store page** (which also adds a
range switch), not from a measurement of the original; AMSynths' own detailed
page confirms the **original is 0.1-30 Hz**. Treat 0.1-30 Hz as authoritative;
0.35 Hz is a [clone-derived: AMSynths] artifact, not a credible alternate
original-hardware spec [R11][R12][R13]. The true minimum frequency is otherwise
unverified without bench measurement (Section 5).

### 3.2 Four-position waveform selector (no sine core)

The LFO WAVEFORM selector (rotary switch S2) has **four** positions on the
original hardware [service-manual + clone-derived + Wikipedia, high]
[R11][R13][R3]:

1. A **smooth wave** drawn with a sine (∿) symbol on the panel - but the core is
   a triangle (Section 3.3). There is **no sine oscillator core** on the
   hardware.
2. **Square** (⊓).
3. **RANDOM** (Section 3.4).
4. **NOISE** (Section 3.5).

Only one position is active at a time (it is a selector, not simultaneous
outputs) [R11].

Software-reissue caution: the modern Roland Cloud / SH-01A plug-in expands the
MODULATOR selector to **six** positions (Sine, Triangle, Saw, Square, Random,
Noise). That six-way selector - and the separate sine/triangle/saw options - is
a **software-emulation reissue feature, NOT present on the 1982 hardware**. Do
not model it for mwAudio101's circuit-accurate target [reissue-manual; software
artifact] [R14].

### 3.3 Smooth-wave core: triangle, not sine (clone-derived + theory)

The smooth ("sine"-symbol) waveform is generated by a discrete dual-op-amp
core that natively produces a triangle and a square (an integrator + comparator
relaxation topology). AMSynths, reverse-engineering the circuit, states the
original "uses a dual Op Amp to create a Square and Triangle waveform" and that
"the SH-101 bender PCB converts the triangle waveform to a sine waveform, which
is used to separately modulate the VCO and VCF." Wikipedia lists the smooth wave
as a triangle [clone-derived: AMSynths + Wikipedia, medium] [R13][R15][R3].

Honest labels and nuance:

- The integrator+comparator split is **standard relaxation-oscillator theory**;
  the SH-101-specific schematic node names could **not** be extracted because the
  available service-manual PDFs are image-only scans. Label the component-level
  topology as [theory/inference, unmeasured] [R16][R17].
- The triangle-to-sine shaping is a **fixed** shaper on the bender PCB, not a
  user-optional control. The signal delivered to the VCO/VCF is the shaped
  ("rounded toward sine") wave. State it as "rounded toward sine," not a
  mathematically pure sine [clone-derived: AMSynths] [R15].
- The exact LFO op-amp part (M5218L vs TL062 vs IR9022 - all present in the BOM)
  is **not** established from any primary source. Do not assert a specific LFO
  op-amp part number [theory/inference, unmeasured] [R11][R6].

### 3.4 RANDOM: CPU-generated digital pseudo sample/hold

The RANDOM position is **digitally generated by the CPU**, not an analog
noise-derived sample-and-hold. The service-manual CPU program "Generates Random
data" each clock and outputs it to the D/A converter; AMSynths confirms the
microprocessor "putting a random 6-bit number into the DAC every clock cycle"
and amplifying x2 for a 0 to +10 V pseudo sample-and-hold signal, stepped at the
LFO/clock rate [service-manual + clone-derived: AMSynths, high] [R11][R13].

The owner's manual loosely calls RANDOM the "output signal of S/H," but the
actual circuit is CPU+DAC pseudo-S/H (the AMSynths clone replaces it with a
true analog S&H). Model it as a sample/hold register reloaded with a uniform
random value on each LFO period - digital, as in the original - **not** an
analog noise S&H.

### 3.5 NOISE: white noise from the audio-path generator

The NOISE position routes white noise from the **dedicated noise generator**
(transistor 2SC945, board ref TR23, selected grade) - the same source feeding
the audio Source Mixer. Selecting NOISE on the modulator sends full-bandwidth
noise into the modulation bus (e.g. for noisy pitch/cutoff jitter)
[service-manual + clone-derived, high] [R11][R13]. AMSynths notes a fixed
~-3 dB / ~16 kHz filter on modulation signals [clone-derived: AMSynths] [R13].

### 3.6 Fixed routing (not a patch matrix)

Modulation routing is **fixed** with per-destination depth controls, not a
routable matrix. The selected modulator is always available to its destinations;
each depth slider scales the amount of the selected LFO value reaching that
destination [service-manual, high] [R11][R2]:

- **VCO pitch**: single MOD-depth slider (vibrato) [R11][R2].
- **Pulse width**: its own 3-position source switch **ENV / MANUAL / LFO** plus a
  PWM depth control; pulse width sweeps 50% down to ~0%. PWM can be LFO-driven
  independently of the pitch MOD setting (though both LFO uses share the same
  modulator waveform/rate) [service-manual + reissue-manual, high] [R11][R18].
- **VCF cutoff**: separate MOD, ENV, and KYBD (Key Follow 0-100%) depth sliders;
  cutoff 10 Hz - 20 kHz; resonance 0 to self-oscillation (VCF details belong to
  the filter dimension) [service-manual + reissue-manual, high] [R11][R18][R3].

The LFO can also drive the **VCA/gate** via the GATE+TRIG / GATE / LFO selector
(LFO position pulses amplitude at the modulator rate - tremolo/rhythmic gating;
Section 4.4) and serves as the master **sequencer/arpeggiator clock**, with an
**EXT CLK IN** jack (+2.5 V or more) able to replace the internal clock and a
clock reset on key press [service-manual + reissue-manual, high] [R11][R18].

The LFO and noise are **discrete** circuitry, independent from the CEM3340 (VCO,
IC13) and IR3109 (VCF, IC14) chips. The 4013 (IC17, MB84013B) is the
sub-oscillator octave divider, **not** the LFO [service-manual, high; frozen
fact] [R6][R11].

### 3.7 Bender / performance modulation

The bender lever adds performance modulation: side-to-side bends VCO pitch
and/or VCF cutoff (separate VCO Bend Sens and VCF Bend Sens depths), and pushing
the lever forward engages a `~LFO MOD` switch applying LFO modulation (typically
pitch vibrato) with its own LFO MOD depth slider [service-manual + Wikipedia,
high for the depth controls] [R11][R3].

Uncertain sub-details (verification flagged): the push-forward = vibrato-on
behaviour and the optional modulation grip's exact model number could not be
confirmed from the owner's-manual body text. The grip is labeled **"MG-1"** in
the owner's-manual table of contents, **not "MGS-1"** as earlier written; treat
the grip part number and the push-switch behaviour as tentative
[manualslib + community, partly inferred] [R19][R3].

## 4. VCA and output amplifier (BA662A)

### 4.1 The VCA core is the BA662A (IC15)

The SH-101 VCA is built around the Roland/Rohm **BA662A**, board reference
**IC15**. The service-manual IC list reads IC13 = CEM3340 (VCO), IC14 = IR3109
(VCF, the **filter**, not the VCA), IC15 = BA662A (VCA). The "A" grade is the
low-offset selection. The BA662 is a custom DC-controlled
variable-transconductance amplifier (OTA) that Rohm made for Roland from 1978,
shared across the SH-101, TB-303, Juno-60 and JX-3P [service-manual +
clone-derived: AMSynths, high] [R6][R20][R21].

### 4.2 Internal architecture (reverse-engineered, no datasheet)

The BA662 has **no public datasheet**. Its internal architecture is a
**reverse-engineered** reconstruction by Open Music Labs, derived by probing an
original chip with external voltages and currents - it is a credible third-party
model, **not** a Roland/Rohm-documented fact [reverse-engineered: Open Music
Labs] [R22][R23][R24]:

- A 2-transistor current mirror for the control current (same as the CA3080).
- 3-transistor Wilson current mirrors for the gain stage (same as the LM13700).
- A complementary Darlington output buffer with bias-current control (may be
  used or bypassed for an external buffer).

The functionally equivalent SMD BA662F pinout (Open Music Labs): pin 1 OTA
Control Input, 3 negative signal input, 4 positive signal input, 6/7 buffer
control input, 8 negative power, 9 OTA output, 11 buffer input, 12 buffer
output, 14 positive power [reverse-engineered: Open Music Labs, medium] [R22].

Roland graded BA662 transconductance into 9 categories (paint dots, brown = low
gm to white = high gm); the VCA selection (A grade) is about low **offset**
rather than absolute gm [clone-derived: AMSynths, high] [R20].

### 4.3 Control law: linear-in-current OTA (theory)

As an OTA of the CA3080/LM13700 family, the BA662's transconductance - and thus
the VCA's voltage gain - is **linearly proportional to the control current**
(gm = Ic / (2·Vt) for the ideal differential-pair OTA; the LM13700 datasheet
gives gm = 19.2 × IABC at 25 °C). This is **standard OTA theory**, not an SH-101
measurement [theory, well-established] [R25][R20].

Consequence and open question: to get a perceptually natural exponential/dB
volume contour from a roughly linear envelope, the control current must be
shaped upstream (an exponential converter) - or the linear-in-current law is
accepted, giving the characteristic OTA decay shape. **Whether the original
SH-101 inserts an exp converter ahead of the BA662 control pin is unconfirmed**,
because the schematic is an image-only scan that could not be transcribed
[theory/inference, unmeasured] [R25]. Treat this as a tunable in the model
(Section 6.3).

### 4.4 Amplitude sources: ENV / GATE, plus LFO tremolo

The VCA amplitude is driven by a selectable source via the VCA MODE switch
[Roland docs + clone-derived, high] [R18][R4]:

- **ENV**: the sound follows the ADSR contour.
- **GATE**: the sound has a fixed volume as long as the key/gate is held
  (organ-style; the envelope shape is bypassed).

Additionally, the **LFO (Modulator) can modulate the VCA** for tremolo - a
feature the otherwise-similar MC-202 omits [Wikipedia, high] [R3]. The VCA
control input therefore sums the envelope/gate level with an LFO tremolo
contribution (Section 3.6).

The AMSynths AM8112 clone exposes a 3-position **HOLD / ENV / GATE** switch:
HOLD = constant maximum level, ENV = on-board ADSR, GATE = external gate. The
ENV/GATE pair matches the documented original; **whether the original front
panel exposes a HOLD position or only ENV/GATE is not confirmed** by primary
Roland docs - the AM8112 is a clone and may be extended [clone-derived:
AMSynths, medium] [R4].

### 4.5 Anti-thump (low-offset selection)

The BA662**A** low-offset grade was specifically chosen for VCA duty so residual
DC offset does not produce an audible thump as the VCA opens (low-offset parts
are marked with a white stripe over pin 1). A circuit-accurate model should null
DC offset and control-signal feedthrough at the gate open/close transition to
reproduce the clean, thump-free onset rather than a click [clone-derived:
AMSynths, high] [R20].

### 4.6 Output level and supply rails

- **Output level**: Audio 0 dBm max (~0.775 Vrms reference) at the OUTPUT jack,
  plus a Phones jack [service-manual + Roland docs, high] [R9][R6][R2].
- **CV output**: 1 V/octave, 0.415-5 V [Roland docs, high] [R9][R2].

Supply rails - honest correction (the earlier "±15 V" claim is **refuted**):
the SH-101 does **not** use bipolar ±15 V rails. Per the u-labor.de repair
teardown, the internal supply is a DC-DC step-up to >20 V then linear
regulation to **+15 V (main analog rail, measured ~+14.87 V), +14 V, and ±5 V**;
external input is 9-12 V DC only. The synhouse repair page corroborates a single
~+15 V analog rail with no -15 V rail [community repair: u-labor / synhouse,
high] [R26][R27]. AMSynths' bare "the original uses 15V" was misread as bipolar;
it is a single-positive-rail step-up design. Likewise, "BA662 = positive-supply
device" is only a **relative** distinction from the negative-supply BA6110: in
the typical VCA (Mode A) configuration the BA662 uses both a +VE (pin 7) and a
-VE (pin 4) connection [clone-derived: AMSynths, high] [R20].

### 4.7 No documented saturation stage (argument from absence)

No located source documents an intentional VCA soft-clip/saturation stage for
the SH-101; the service manual lists output simply as "Audio (0 dBm max.)." The
BA662 OTA has a limited linear input range (OTAs distort above ~tens of mV
differential input unless input-attenuated), so typical Roland practice would
attenuate the audio input to keep the OTA linear - but this is **unconfirmed**.
This is an **argument from absence**: the schematic is an image-only scan, so
"no clipping stage" is undocumented, not proven [theory/inference, unmeasured]
[R6][R26]. Expose any OTA-drive/nonlinearity as an optional character parameter,
not as documented original behaviour (Section 6.3).

## 5. Confidence, disputes & honest labels

This section surfaces every disputed, low-confidence, corrected, and residual-
risk item for this dimension. Nothing below is a settled fact.

### 5.1 Disputed / low-confidence findings

- **Envelope segment curve law = exponential (RC)** - DISPUTED, low confidence.
  Not in any Roland document; inferred from discrete RC topology plus general
  theory. The real curve may deviate from an ideal exponential due to op-amp
  buffering and transistor switching. [theory/inference, unmeasured]
  (Section 2.6).
- **LFO smooth-wave core = triangle shaped to sine** - DISPUTED, medium
  confidence. The block-level triangle+square core and bender-PCB sine shaper
  are clone-derived (AMSynths) and corroborated by Wikipedia; the
  component-level integrator/comparator detail is theory; the exact LFO op-amp
  part is unverified. State as "rounded toward sine," and as
  [clone-derived: AMSynths, presumed-equal] / [theory/inference, unmeasured]
  (Section 3.3).
- **LFO rate 0.35 Hz alternate** - DISPUTED. A clone store-page artifact, not an
  original-hardware spec; use 0.1-30 Hz [clone-derived: AMSynths]
  (Section 3.1).
- **VCA control law (linear OTA + need for upstream exp shaping)** - the OTA law
  is settled theory, but **whether the SH-101 has an exp converter ahead of the
  BA662 is DISPUTED/unconfirmed** [theory/inference, unmeasured] (Section 4.3).
- **No VCA saturation stage** - DISPUTED, low confidence; argument from absence
  on an untranscribed schematic [theory/inference, unmeasured] (Section 4.7).

### 5.2 Corrections applied (do not regress)

- **Trigger logic IC mislabel**: the gate/trigger is primarily CPU-generated
  (IC6). IC7 is a CD4050 (HD/MC 14050B) hex buffer, **not** a 4013; the 4013
  (IC17, MB84013B) is the sub-oscillator divider. The "TR34" envelope-transistor
  designator is unverified. Downgraded from high to medium (Section 2.5)
  [R1][R5][R6].
- **CEM3340 designator**: the VCO is **IC13**, not IC3 (the "IC3" reading was a
  wrong OCR). Frozen fact (Sections 1, 3.6) [R6].
- **Supply rails**: **refuted** "±15 V." Actual rails are +15 V, +14 V, ±5 V
  (single-positive-rail step-up design). The BA662 "positive-supply" label is
  only relative to the BA6110 (Section 4.6) [R26][R27][R20].
- **Gate-output level**: service manual is authoritative at **12 V**; Roland web
  spec / Sweetwater say 10 V at 100 kΩ load. Carry 12 V, flag 10 V as the loaded
  figure (Section 2.7) [R1][R2][R9].
- **Modulation grip**: "MG-1" per the owner's-manual TOC, not "MGS-1"
  (Section 3.7) [R19].

### 5.3 Residual risks and open validation gaps

- **No physical-unit measurements** (project decision). Every item below that
  needs bench data is an OPEN VALIDATION GAP, not a delivered fact.
- ADSR curve law (linear vs exponential vs constant-current) - needs schematic
  tracing or oscilloscope measurement [open gap] (Section 2.6).
- Internal ADSR contour peak voltage - not quoted in the SH-101 manual (unlike
  the System 100 Model 101's +6 V); needs schematic confirmation for VCF/VCA
  scaling [open gap].
- Full ENV-GEN timing-network component values (R/C setting the 1.5 ms-4 s /
  2 ms-10 s ranges) - dense scan, not extracted [open gap].
- GATE+TRIG retrigger behaviour - does the envelope snap to zero before
  re-attacking, or re-attack from the current level? Described behaviourally by
  users, not formally specified [open gap].
- True LFO minimum frequency (0.1 Hz spec vs 0.35 Hz clone figure) - needs bench
  measurement of VR9 + timing cap [open gap] (Section 3.1).
- LFO integrator/comparator component-level topology and the specific LFO op-amp
  part - not verifiable from image-only schematic scans [open gap]
  (Section 3.3).
- RANDOM step distribution and any DAC smoothing/glitch behaviour - unconfirmed
  [open gap] (Section 3.4).
- Whether the PWM-LFO and pitch-MOD use the same instantaneous LFO
  value/polarity - block diagram implies shared LFO, separate scaling; needs
  schematic-level polarity confirmation [open gap] (Section 3.6).
- Exact modulation depth/scaling constants (V/oct to VCO, Hz/V to VCF, %/V to
  PWM) - not numeric in the manual; require measurement [open gap].
- BA662 internal architecture is **reverse-engineered** (Open Music Labs), no
  public datasheet - do not present transistor counts/mirror topology as
  manufacturer-confirmed [reverse-engineered: Open Music Labs] (Section 4.2).
- Exact BA662 control-input / output RC component values - service manual is an
  image-only scan; **never fabricate** these [open gap] (Sections 4.3, 4.7).
- Presence/absence of an exp converter ahead of the BA662, and any deliberate
  OTA saturation - unconfirmed [open gap] (Sections 4.3, 4.7).
- Original VCA panel switch = ENV/GATE (vs the clone's HOLD/ENV/GATE) -
  ENV/GATE well-corroborated by secondary sources, but the literal panel
  silkscreen was not transcribed from a primary page [open gap] (Section 4.4).
- Bender push = vibrato-on behaviour and grip model number - tentative
  (Section 3.7) [open gap].
- Software-reissue contamination risk: the Roland Cloud / SH-01A six-position
  modulator selector (separate Sine/Triangle/Saw) and any expanded PWM-source
  labels are **software artifacts**, not on the 1982 hardware - keep them out of
  the circuit-accurate model (Section 3.2) [R14].

## 6. Design implications for mwAudio101

### 6.1 Envelope

- Implement **one** shared ADSR envelope generator feeding three
  switchable/scalable destinations: VCF cutoff (depth control), VCA gain
  (ENV-vs-GATE source switch), and VCO pulse width (PWM = ENV). Do **not** add a
  separate filter envelope and amp envelope - the hardware has exactly one EG.
- Calibrate ranges: Attack 1.5 ms - 4 s, Decay 2 ms - 10 s, Release 2 ms - 10 s
  (Decay and Release share a range), Sustain 0-100% of the attack peak.
- Default to exponential (RC-style) segment shapes - attack as charge toward a
  target above the peak (asymptotic, snappy), decay/release as exponential
  decays - but **expose the curve law as a configurable parameter** because it
  is inferred, not documented (Section 2.6).
- Implement the trigger state machine with three modes: GATE (no legato
  retrigger), GATE+TRIG (retrigger every new note - trills), LFO (envelope
  clocked at the modulator rate). Confirm the retrigger discharge behaviour
  before final tuning (open gap).
- Honor the gate interface levels if modeling CV I/O: gate high = 12 V (spec;
  ~10 V loaded), external trigger threshold +2.5 V. Keep the System 100 Model
  101 numbers out of the model.

### 6.2 LFO and modulation

- Model **one** LFO feeding fixed destinations with per-destination depth gains,
  not a routable matrix. Core: triangle/square, rate 0.1-30 Hz (rate pot VR9,
  exponential-ish feel), with a **four-way** source select: smooth (treat as
  triangle, optionally rounded toward sine), square, RANDOM, NOISE. Do **not**
  add a sine LFO core or the reissue's six-position selector.
- RANDOM = a sample/hold register reloaded with a uniform random value each LFO
  period (digital, as in the original CPU+DAC), **not** an analog noise S&H.
- NOISE = the same white-noise source used in the audio mixer, full-bandwidth,
  routed into the mod bus.
- Destination depths (all scaled from the same selected LFO value): VCO pitch
  (single MOD gain); pulse width (own ENV/MANUAL/LFO switch + PWM depth, PW
  50%→~0%); VCF cutoff (own MOD gain alongside ENV depth and Key Follow 0-100%).
- Add an LFO→VCA/gate path via a GATE+TRIG/GATE/LFO switch for tremolo/rhythmic
  gating, and route the LFO/CLK as the master sequencer/arpeggiator clock with an
  EXT CLK IN override (+2.5 V) and key-press clock reset.
- Overlay bender performance modulation (separate VCO/VCF bend sensitivity plus
  a forward-push LFO-MOD vibrato with its own depth). Implement the LFO
  independently from the CEM3340 (VCO) and IR3109 (VCF) emulations. Flag exact
  mod-depth constants (V/oct, Hz/V, %/V) and the true LFO minimum frequency as
  measurement-required.

### 6.3 VCA

- Model the VCA as a **linear-control-current OTA** (BA662A class): output =
  gain × input, with gain proportional to a control current. Drive the control
  from a summing node combining (a) the amplitude source selected by the VCA
  MODE switch - ENV (full ADSR) or GATE (flat full-level gate-width pulse) - and
  (b) an LFO tremolo contribution (SH-101-specific).
- Decide deliberately whether to feed the OTA a linear envelope (characteristic
  OTA decay shape) or pre-shape with an exp converter for dB-linear control;
  **expose this as a tunable** since the original hardware shaping is unconfirmed.
- Implement low-offset/anti-thump behaviour: null DC offset and control-signal
  feedthrough at gate open/close so the onset is clean rather than a click.
- Calibrate full-scale output to 0 dBm (~0.775 Vrms) at the OUTPUT jack.
  Reference internal headroom to the **actual** rails (+15 V main, +14 V, ±5 V),
  **not** ±15 V.
- Keep the audio input within the OTA's linear window by default (matching
  typical Roland attenuation); expose any OTA-nonlinearity/drive as an optional
  character parameter, not as documented original behaviour. Leave the exact
  control-input RC network and exp-converter presence as parameters pending
  schematic transcription.

## References

- [R1] Roland SH-101 Service Notes, First Edition, 1 November 1982 -
  SPECIFICATIONS, panel reference, BLOCK DIAGRAM, IC/parts-list and ENV-GEN pages.
  <https://www.synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/>
  (mirror: <https://ia600607.us.archive.org/13/items/synthmanual-roland-sh-101-service-notes>)
- [R2] Roland SH-101 Technical Specifications (Roland Support).
  <https://support.roland.com/hc/en-us/articles/201921519-SH-101-Technical-Specifications>
- [R3] Roland SH-101 - Wikipedia.
  <https://en.wikipedia.org/wiki/Roland_SH-101>
- [R4] AMSynths AM8112 (SN-101 VCA & ADSR).
  <https://amsynths.co.uk/home/products/amplifiers/am8112-sn-101-vca-adsr/>
- [R5] AMSynths AM8112 (SN-101 VCA & ADSR) - "6 transistors and a dual op-amp"
  ENV-GEN description (same page as R4).
  <https://amsynths.co.uk/home/products/amplifiers/am8112-sn-101-vca-adsr/>
- [R6] Roland SH-101 Service Manual full-text (archive.org djvu OCR) - IC list
  (IC13 CEM3340, IC14 IR3109, IC15 BA662A, IC17 MB84013B), transistor list, spec
  page.
  <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
- [R7] EarLevel Engineering - Envelope generators / ADSR (RC exponential
  segments, general theory).
  <https://www.earlevel.com/main/2013/06/02/envelope-generators-adsr-part-2/>
- [R8] North Coast Synthesis - Modular synthesis intro, part 13: envelope
  generators (general theory).
  <https://northcoastsynthesis.com/news/modular-synthesis-intro-part-13-envelope-generators/>
- [R9] Sweetwater - Roland SH-101 Technical Specifications.
  <https://www.sweetwater.com/sweetcare/articles/roland-sh-101-technical-specifications/>
- [R10] ManualsLib - Roland System 100 "Model 101" (separate 1975 product; do
  not conflate).
  <https://www.manualslib.com/manual/3489584/Roland-System-100-101.html?page=3>
- [R11] Roland SH-101 Service Manual PDF (Electric Druid mirror) -
  Specifications, Block Diagram, Synth Board OPH177-2 "LFO" section, CPU program,
  switch S2.
  <https://electricdruid.net/wp-content/uploads/2025/05/SH101-Service-Manual.pdf>
- [R12] AMSynths SH-101 LFO clone store page (0.35 Hz clone-figure artifact).
  <https://www.amsynthstore.co.uk/product/am8114-sn-101-lfo>
- [R13] AMSynths SH-101 Modulator (detailed dev page; triangle+square core,
  triangle-to-sine shaper, CPU+DAC RANDOM, 0.1-30 Hz original).
  <https://amsynths.co.uk/home/products/modulators/am8114-sh-101-modulator/>
- [R14] Roland Cloud SH-101 (SH-01A) plug-out owner's manual (reissue;
  six-position modulator selector - software artifact).
  <https://www.rolandcloud.com/getmedia/1e415fd4-c8ca-4719-80ba-ca90b94f98ce/SH-101-Manual-E.pdf>
- [R15] AMSynths SH-101 Modulator dev page - bender-PCB triangle-to-sine shaper
  detail (same as R13).
  <https://amsynths.co.uk/home/products/modulators/am8114-sh-101-modulator/>
- [R16] Circuits & Sounds - single triangle/square-wave LFO (integrator +
  comparator topology, general theory).
  <https://medium.com/@circuitsandsounds/single-triangle-square-wave-lfo-d57188ed4a86>
- [R17] Roland SH-101 Service Manual full-text (archive.org djvu) - LFO/spec
  cross-reference (same as R6).
  <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
- [R18] Roland Cloud SH-101 plug-out owner's manual - VCO/PWM, VCF, ENV/VCA
  GATE+TRIG/GATE/LFO sections (reissue; cross-checked vs original; same as R14).
  <https://www.rolandcloud.com/getmedia/1e415fd4-c8ca-4719-80ba-ca90b94f98ce/SH-101-Manual-E.pdf>
- [R19] ManualsLib - Roland SH-101 owner's manual (Modulation Grip MG-1 / Bender
  / LFO Modulation Button page).
  <https://www.manualslib.com/manual/1029350/Roland-Sh-101.html?page=40>
- [R20] AMSynths - All about the BA662 chip (OTA, Rohm 1978, 9-grade gm
  selection, A-grade low-offset for VCA, Mode-A +VE/-VE supply).
  <https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/>
- [R21] Sunshine Jones - Roland BA662A VCA chip.
  <https://sunshine-jones.com/roland-ba662a-vca-chip/>
- [R22] Open Music Labs - BA662 wiki (reverse-engineered architecture and BA662F
  pinout).
  <http://wiki.openmusiclabs.com/wiki/BA662>
- [R23] Open Music Labs - BA662 clone project.
  <http://www.openmusiclabs.com/projects/ba662-clone/index.html>
- [R24] Synthcube - Open Music Labs BA662 OTA clone (corroborates Wilson mirrors
  / complementary output buffer).
  <https://synthcube.com/open-music-labs-ba662-ota-clone/>
- [R25] Texas Instruments - LM13700 datasheet (OTA gm proportional to bias
  current; OTA-family theory).
  <https://www.ti.com/lit/ds/symlink/lm13700.pdf>
- [R26] u-labor.de - Repair Roland SH-101 (internal PSU teardown: +15 V, +14 V,
  ±5 V step-up regulation; refutes ±15 V).
  <https://www.u-labor.de/repair-roland-sh-101>
- [R27] Synhouse - SH-101 repair (single ~+15 V analog rail measured ~+14.87 V).
  <http://www.synhouse.com/SH-101.html>
