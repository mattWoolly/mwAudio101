<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Validation Gaps, Open Disputes & Honesty Ledger

## 1. Purpose and scope

This document is the **honesty ledger** for the mwAudio101 project: a single,
citable place that catalogs everything we do **not** know for certain about the
Roland SH-101, everything that is **disputed** across the 18 research
dimensions, and everything that has been **frozen** after schematic
reconciliation. It exists so that later architecture and backlog phases can cite
a stable address (e.g. "per ledger 4.3") instead of re-litigating a dispute, and
so that no shipped "authentic" claim outruns its evidence.

It synthesises three inputs:

- The per-dimension `verification.residualRisks`, `verification.corrections`,
  and `research.openQuestions` for all 18 research dimensions
  [project-research-corpus, internal].
- The round-1 completeness-critic report [round-1-critique, internal].
- The schematic-reconciliation pass against the 1982 Service Notes
  [schematic-reconcile dimension, service-manual].

### 1.1 The calibration-oracle decision (read this first)

mwAudio101 is **modeled from documented circuit behavior**. We have made a
deliberate project decision that **we hold no physical SH-101 unit and will take
no bench measurements as the source of truth**. Where a fact requires
oscilloscope captures, Bode plots, or spectra of a real instrument, it is an
**open validation gap**, not a delivered fact (see section 5). Audio recordings,
if any are made locally, are a **secondary, local-only cross-check** and are
**never the oracle**. This is not a temporary limitation to be "fixed later" in
the modeling track; it is the calibration policy. Sections 5 and 6 are therefore
**permanent** gaps under the current policy, not TODOs.

### 1.2 How to read the honesty labels

Every disputed or inferred fact in this project carries an explicit label. The
controlled vocabulary is:

- `[service-manual, high]` — read from the 1982 Roland SH-101 Service Notes
  (primary), legible and corroborated.
- `[service-manual-OCR, medium]` — rests on the archive.org OCR of the service
  manual, where character mis-reads are possible.
- `[clone-derived: Alfa AS3109 / AMSynths, presumed-equal]` — a figure measured
  on a clone chip or clone module, assumed (not proven) equal to the original.
- `[reverse-engineered: Open Music Labs]` — recovered by probing a real chip; no
  manufacturer datasheet exists.
- `[community disassembly: joebritt, partly inferred]` — recovered from a
  third-party firmware disassembly where the author flags parts as inferred.
- `[theory/inference, unmeasured]` — derived from general analog-synth or DSP
  theory, never confirmed against an SH-101.
- `[software-emulation artifact]` — a "feature" that exists only in later
  software (Roland Cloud SH-01A / plug-out), NOT on the 1982 hardware.
- `[disputed]` — primary sources genuinely conflict; ship as a range, not a
  single value.

A claim with no label is one of the **frozen resolutions** in section 2.

---

## 2. Frozen resolutions (settled — cite these, do not re-debate)

These were contradictory or uncertain during research and are now **resolved**
by the schematic-reconciliation pass against the official 1982 Service Notes
[schematic-reconcile, service-manual]. They are FROZEN: downstream netlist and
modeling work must use these and these only.

### 2.1 IC reference designators

| Function | Chip | Designator | Roland part |
| --- | --- | --- | --- |
| VCO | Curtis CEM3340 | **IC13** | 15229810 |
| VCF | Roland IR3109 | **IC14** | — |
| VCA | BA662A | **IC15** | — |
| CPU | TMP80C49P-6-7301 | **IC6** | 15179136 |

The earlier "IC3 = CEM3340" reading was a **wrong WebFetch/OCR transcription**;
IC3 is one of the IR9022 op-amps ("IC3,5,12,16,18 IR9022")
[schematic-reconcile, service-manual; power-cv-cal correction]. The `vco`
dimension's own note still asserts IC3 — that note is **superseded** by the
reconciliation. The CPU = IC6 designator is confirmed by the parts list; note
that the schematic page where IC6 itself appears (Control board) was captured
only as a board overview, so the **designator** IC6 is frozen by parts-list
identity while its exact schematic position is a minor residual
[schematic-reconcile, residual].

### 2.2 Gate output ON level (the canonical dispute)

The project standard is: **Gate output ON = 12V per the 1982 Service Manual,
which is authoritative for mwAudio101.** We note for completeness that Roland's
current web/Sweetwater spec says **10V at 100 kΩ load**, and the most likely
physical reconciliation is that ~12V open-circuit sags toward ~10V under a
100 kΩ load [io-external, power-cv-cal, arp-seq]. We therefore implement a
~10–12V V-trig output (OFF = 0V) and **never** present a single bare "+12V at
high confidence" as if undisputed. This remains a flagged source conflict; see
section 3.1.

### 2.3 TH1 thermistor location

The SDT-1000 thermistor (designator **TH1**, Roland part **15229908**) is in the
**VCF cutoff-CV path** (IR3109 region), **not** in the CEM3340 VCO frequency-CV
network. The CEM3340 is **temperature-compensated on-die** (its tempco generator
cancels the q/KT term on the same die), so the VCO needs no external tempco
element [vco correction; schematic-reconcile, service-manual]. Earlier text
placing TH1 in the VCO scaling network, or describing the CEM3340 compensation
as "incomplete and externally supplemented," is **refuted and frozen out**. Two
part-number transpositions are also frozen: the DC/DC converter is part
**12449224** (not 15229908), and the 6 MHz ceramic resonator is part
**12389800** [schematic-reconcile correction].

### 2.4 VCF integrator caps and topology

The four VCF integrator capacitors are **240 pF**, with designators **C47, C48,
C50, C51** (C49 is a separate 10 µF/16V cap — the range is **not** the contiguous
"C47–C50"). The filter is **Juno-6/60 topology** built on the IR3109, with
**diode-clipped resonance** (a transistor phase-splitter with clipping diodes to
ground via TR27), not Juno-style input-gain compensation
[schematic-reconcile, service-manual; filter]. The exact resonance-feedback
component values remain a residual (section 3.4).

### 2.5 Supply rails

The SH-101 is a **single-positive-rail step-up design**, **not** bipolar ±15V.
The synth-board connector carries **+9V, +5V, +15V, +14V (plus GND)**, with
**-5V and -2.5V** generated on the CEM3340 side via a "-5V Converter"; external
input is **9–12V DC** only [schematic-reconcile, service-manual; vca-amp
correction]. The earlier "±15V bipolar" claim is **refuted and frozen out**. The
"+9V/+4.5V pseudo-ground" framing used in the filter dimension was a
clone-context approximation, also superseded.

### 2.6 Memory backup

Memory retention is from a separate **1.8V × 6** battery string (per page 2:
"Memory Back-up Battery 1.8V X6"), **not** a "+5V backup rail." +5V is the CPU
logic VCC only [schematic-reconcile correction]. The "inner 3 of 6 cells" detail
is community/empirical and stays medium confidence [cpu-keyassign, community].

### 2.7 LFO / modulator waveform set (hardware)

The original 1982 hardware modulator selector is **four positions: Triangle,
Square, Random (6-bit CPU+DAC pseudo-S/H), Noise**. The "smooth" wave is a
**triangle** (rounded toward sine only on the bender PCB for the pitch-mod
path), **not a sine** [overview correction; lfo correction; mixer-mod-glide
correction; acid-culture correction]. Any sine LFO, and the six-position
selector, are software-only (section 7.1).

### 2.8 VCO range / footage

The RANGE switch is **16'/8'/4'/2' (four positions)**. There is **no 32' and no
64'** on the hardware [subosc-noise correction; acid-culture correction]. The
8'/16'/32' figure was an AMSynths AM8110 clone artifact; 32'/64' are
software-only (section 7.2).

### 2.9 Sequencer feature set

The sequencer encodes, per step, a **note value plus REST and TIE/legato (slide)
flags — and NO accent**. Accent is a TB-303/MC-202 feature the SH-101 lacks
[cpu-keyassign correction]. Storage is the **TMP80C49's internal 128-byte RAM**
(RAM locations 1B–7F for ~100 steps), which is why no RAM chip appears in the
parts list [arp-seq correction; cpu-keyassign]. The exact bit layout is **not**
frozen (section 4.6).

---

## 3. Standing disputes (ship as ranges or labels — do NOT pick one silently)

These are genuine conflicts between credible sources. They must be shipped as a
range or with an explicit dispute label. Stating any of them as a single settled
value would be dishonest.

### 3.1 Gate output ON level: 10V vs 12V `[disputed]`

Two **official Roland documents** disagree: the 1982 Service Notes table reads
"Gate (OFF=0V, ON=12V)"; the modern Roland support / Sweetwater spec reads
"ON = 10V at 100 kΩ load" [overview, io-external, arp-seq, vca-amp,
power-cv-cal]. This is **not** a manual-vs-forum issue — both are Roland. Project
resolution (section 2.2): service manual is authoritative, implement ~10–12V,
verify against a 100 kΩ load, and label the condition (unloaded vs loaded).
**Shipping an "authentic" claim:** advertise the gate as "V-trig, ~10–12V (12V
per 1982 service spec, ~10V under 100 kΩ load)" — never bare "+12V."

### 3.2 Expo-converter tempco sign and magnitude `[theory/inference, unmeasured]`

The magnitude **~0.33%/°C (~3300 ppm/°C, = 1/298 K)** is correct general
expo-converter theory, but the **sign is convention-dependent**: the intrinsic
V/oct scale and OTA gm have a negative coefficient (~-0.33%/°C); the compensating
tempco resistor is positive (+3300 ppm/°C) [vco, power-cv-cal, variance-drift].
This is **general analog-synth theory, not an SH-101 measurement**. One source
miscitation is corrected: the figure belongs to xonik.no, not the cited
electricdruid CEM3340 page [variance-drift correction]. **Shipping:** state the
magnitude, declare the sign convention explicitly, label as theory.

### 3.3 Full internal rail generation method `[service-manual, medium]`

The rail **set** is frozen (section 2.5), but **how** each of +14V / +9V / -5V is
generated from the single DC/DC converter (discrete series regulators vs taps off
the switcher) was only partially traced; the PSU corner did not fully resolve
even at ~3000 px [schematic-reconcile, residual]. **Shipping:** model the rails
as specified; treat the generation topology as an internal detail not load-
bearing for audio behavior.

### 3.4 VCF resonance-feedback component values `[schematic-read, single-reader]`

The 240 pF caps and Juno-6/60 diode-clipped topology are frozen (section 2.4),
but the specific resonance-network values (R136 ≈ 15k / R131 ≈ 10k / R129 ≈ 5.6k
/ R125 ≈ 820R; 68k series; 560R inputs; D33/D34, R155/R156/R159/R160) rest on a
**single reader of an image-only schematic** that WebFetch could not OCR
[filter, schematic-reconcile]. **Shipping:** these are unverified by a second
reader; treat exact values as provisional, label "schematic-read, single-reader."

### 3.5 MC-202 IR3109 usage `[disputed]`

Whether the MC-202 uses the IR3109 is **genuinely disputed** (AMSynths says the
SH-101 was the IR3109's only monosynth appearance; retailer listings disagree)
[idm-aphex, acid-culture]. **Shipping:** do not assert SH-101/MC-202 filter
identity without the MC-202 service manual. See also the corrected filter
lineage in section 8.3.

### 3.6 VCF/VCO ENV-amount polarity `[theory/inference, unmeasured]`

Whether the front-panel ENV-to-cutoff control is **bipolar or unipolar-positive**
is inferred from "no center detent, no documented negative-ENV mode," not from a
source [overview, filter]. The shared VCF+VCA envelope is confirmed; the ENV+LFO
summing at the cutoff-CV node is architecturally implied, not documented.
**Shipping:** label polarity as inference.

### 3.7 LFO low-end frequency: 0.1Hz vs 0.35Hz `[disputed; original = 0.1Hz]`

Roland's spec says **0.1Hz**; the AMSynths clone **store** page says 0.35Hz (and
adds a range switch). AMSynths' own detail page agrees the **original is
0.1–30Hz** [lfo correction]. **Shipping:** original = 0.1–30Hz; 0.35Hz is a
clone artifact, do not present as an alternate original spec.

### 3.8 VCO chip attribution (minor) `[disputed, minority]`

A minority of hobbyist sources dispute the exact VCO chip, but **all** agree it
is an IC with on-chip compensation; the consensus and the schematic say CEM3340
[variance-drift, vco]. **Shipping:** CEM3340 (IC13); note the minority dispute
exists but is not credible against the schematic.

---

## 4. Clone-derived & reverse-engineered figures (label provenance, never "original")

These numbers are real and usable, but they come from **clone chips, clone
modules, or chip-probing**, not from an original instrument or a Roland
datasheet. They must be labelled with their provenance whenever used.

### 4.1 IR3109 electrical figures `[clone-derived: Alfa AS3109 / AMSynths, presumed-equal]`

The buffer drive currents **0.6 / 1.0 / 1.3 mA** come from the **Alfa AS3109
clone** datasheet, and the **~20Vpp self-oscillation amplitude** comes from the
**AMSynths AM8101 module on ±12V rails** — neither is an original IR3109 or a
measured original SH-101 (which runs single-supply, not ±12V) [filter,
subosc-noise]. No standalone original IR3109 datasheet is archived; only a
**minimal partial datasheet** exists in the Jupiter-4 service manual
(transconductance 1µA–10mA, offset <±3mV) [filter correction]. **Shipping:** the
self-osc amplitude in particular must stay LOW confidence and labelled "AM8101
clone @ ±12V" — do not generalise to the original.

### 4.2 BA662 / BA662A VCA internals `[reverse-engineered: Open Music Labs]`

The VCA internal architecture (transistor counts, mirror topology) was
**reverse-engineered by Open Music Labs by probing a chip**; **no public BA662
datasheet exists** [vca-amp, dsp-techniques]. Do not present transistor counts as
manufacturer-confirmed. The "positive-supply device" framing is imprecise: in the
typical Mode A VCA config the BA662 uses **both** +VE (pin 7) and -VE (pin 4); it
is "positive-supply" only relative to the BA6110 [vca-amp correction]. Whether a
linearizing-diode / exponential control-port shaping is present in the SH-101 VCA
specifically is **unconfirmed** [dsp-techniques, open].

### 4.3 Sub-osc clock and +14V Vdd `[clone/secondary, single-source]`

The **+14V Vdd** for the 4013 divider rests on a **single source (Electric
Druid)** — though it is now corroborated by the schematic-reconcile pass
(connector pin 21 explicitly labelled +14V), so it has effectively moved toward
section 2.5. The **0–10V sub-osc clock swing** is derived/approximate, not read
off the schematic [subosc-noise]. The **25% sub-pulse duty cycle** is
Electric-Druid-attributed, **not** manufacturer-documented [mixer-mod-glide,
dsp-techniques]. The diode-OR diodes D38/D39 are **1S1585** (1S2473 alternative),
medium confidence; the earlier "1S188"/"1S2473" reads were wrong/unconfirmed
[subosc-noise correction]. A corrected transposition: mixer resistor **R187 =
680k**, not 68k (68k is R183 in the clock path) [subosc-noise correction].

### 4.4 0.1% range-switch precision resistors `[clone-derived: AMSynths AM8110]`

The "0.1% range-switch precision resistors" are an **AMSynths clone
modernization** to eliminate trimmers — explicitly **not** the original Roland
SH-101, which is battery-powered and uses alignment trimmers (VR-5 Range Width,
VR-6 VCO Width) [variance-drift correction]. **Shipping:** label as clone design
choice, not original-hardware fact.

### 4.5 Noise spec and 6-bit random S&H `[per AMSynths analysis, medium]`

The noise "-3 dB at ~16 kHz" filtering and the "6-bit random S&H" mechanism come
from a single high-quality secondary source (AMSynths reverse-engineering) and
were not cross-checked node-by-node against the raw schematic [overview]. The
noise is white (no pinking filter) per Wikipedia + schematic [subosc-noise].
RANDOM is **CPU+DAC pseudo-S/H (6-bit)**, not a true analog sample-and-hold [lfo
correction].

### 4.6 Sequencer per-step byte format `[community disassembly: joebritt, partly inferred]`

The per-step bit encoding (which bit = REST, which = TIE/legato, how the note
number is packed) exists **only** in the joebritt/SH101_Disassembled project,
where the author explicitly flags the rest/tie/legato encoding as **inferred**
from bit-7/bit-6 test instructions [arp-seq, cpu-keyassign]. The note-priority
mapping (GATE = lowest-note; GATE+TRIG = last-note; LFO = lowest-note via flag
F1) is likewise from disassembly comments + community consensus, not a
Roland-authored statement [cpu-keyassign]. **Shipping:** label
community-recovered, not authoritative.

### 4.7 MGS-1 / MG-1 modulation grip `[unverified pinout]`

The set is **MGS-1**; the grip unit is **MG-1** in the owner's manual. It is an
**analog** interface (pitch bend + LFO-mod switch via side jacks), **not MIDI**.
The factory connector pinout / pin count (plug 053H157) is **undocumented** in
all sources reviewed; any pinout statement is unverified and would require
reverse-engineering a physical unit [overview, lfo, io-external].

---

## 5. Hardware-measurement gaps (cannot close without a unit — permanent under current policy)

Per the calibration-oracle decision (section 1.1), the following require
physical-hardware measurement we deliberately do **not** have. They are **open
validation gaps**, not delivered facts. mwAudio101 models them from documented
circuit behavior and theory; we do **not** claim measured fidelity for any of
them.

### 5.1 ADSR segment-curve law `[theory/inference, unmeasured]`

Whether Attack/Decay/Release are **exponential (RC) or linear/constant-current**
is **inferred from topology**; the service manual does not state it [envelope].
The internal contour peak voltage is not quoted (unlike the System 100 Model 101's
+6V), and the discrete timing-network R/C values setting the 1.5ms–4s / 2ms–10s
ranges were not fully extracted [envelope, open]. The ENV-GEN designators
"TR34" and "IC7-as-4013" are **wrong/unverified** — IC7 is a CD4050 hex buffer
and gate/trigger is primarily **CPU-generated** (TMP80C49 Port 2) [envelope
correction]. Max attack = **4s** for the original (ignore the 8s forum figure;
5s is the AMSynths replica) [envelope]. **Validation gap:** no oscilloscope
capture of the segment curves exists or will be taken.

### 5.2 Filter frequency / phase / resonance response `[spec + theory, unmeasured]`

There is **no measured Bode plot, no measured phase response, no resonance-peak-
height-vs-control curve, and no self-oscillation-frequency-vs-CV curve** for an
original SH-101 IR3109 filter [filter]. The 24 dB/oct slope is a design spec; the
45°/pole phase is theory [filter]. **Validation gap.**

### 5.3 Oscillator / sub-osc / noise spectra `[unmeasured]`

No measured VCO harmonic spectra, sub-osc spectra, or noise spectra of a real
unit exist [round-1-critique]. The anti-aliased rendering target (section 5.6) is
theory-set, but the **golden reference to validate against does not exist**.

### 5.4 Thermal / tuning drift and warm-up `[field-report / theory, unmeasured]`

No SH-101-specific quantified drift figure (cents/°C, warm-up settling time,
long-term stability) is documented [vco, power-cv-cal, variance-drift]. Most
drift-depth and warm-up figures rest on **practitioner/forum anecdote** (KVR,
Vintage Synth Explorer), not instrumentation; they are mutually consistent but
must be labelled engineering practice / field reports. The "±5%–20% envelope-time
spread" and "~1 ms min attack" are **modeling estimates, not extractable from
primary sources** [variance-drift correction]. **Validation gap.**

### 5.5 Gate output voltage under load `[unmeasured]`

The 10V-vs-12V reconciliation (section 3.1) is **plausible engineering inference,
not a measured SH-101 fact**; resolving it requires bench measurement of a real
unit under a defined load [io-external, power-cv-cal]. **Validation gap.**

### 5.6 DSP-vs-circuit fidelity `[theory/inference, unmeasured]`

The plugin DSP theory (PolyBLEP/BLEP anti-aliasing; oversampling; TPT/ZDF vs
Huovilainen tanh ladder; Stilson-Smith k=4 self-oscillation threshold) is sound
but **derived from the Moog-transistor-ladder literature applied by analogy to
the IR3109 OTA ladder** [dsp-techniques]. Key caveats: **k=4 is a dimensionless
normalized-model threshold, NOT the SH-101's physical resonance-pot value**; the
SH-101 is an IR3109 OTA filter, not a discrete Moog ladder; the diode-clamped
self-oscillation behavior was not pinned to a primary source [dsp-techniques].
Without measured references (5.2, 5.3) we **cannot validate** that the DSP matches
the circuit — only that it implements the documented topology. **Validation gap.**

---

## 6. Round-1 completeness-critic findings (acknowledged gaps)

The round-1 critic [round-1-critique, internal] rated overall research confidence
**medium** and identified dimensions that the corpus does not yet model. These
are recorded here as honest scope gaps so later phases do not mistake silence for
coverage.

### 6.1 Missing or under-modeled dimensions

- **CPU / key-assigner firmware as a system.** Now partially covered by the
  `cpu-keyassign` dimension (note priority, EXT-CLK takeover that disables the
  RATE knob, clock-reset-on-keypress, H→L T1-pin edge sensing, 1.5–3.5 ms loop),
  but the per-step bit layout and arp step-ordering arithmetic remain
  community-inferred (sections 4.6, 4.7) [cpu-keyassign].
- **Plugin DSP / anti-aliasing strategy.** Now covered by `dsp-techniques`, but
  the IIR-vs-FIR decimator design and the ZDF-vs-Huovilainen engine choice are
  **open architectural decisions** needing an ADR + CPU/aliasing benchmark
  [dsp-techniques, open].
- **Calibration-to-parameter / unit variance model.** Covered by
  `variance-drift`, but the slow-drift vs fast-slop split, per-node tolerance
  classes, and per-serial frozen-offset vs live-drift design are **design
  choices, not sourced constants** [variance-drift, open].
- **Measured curve / spectral reference data.** Not covered — this is the
  hardware-measurement gap of section 5 and is permanent under our policy.
- **MIDI / modern plugin control surface.** The hardware has no MIDI; a market
  plugin still needs a CC/automation map, parameter ranges, and preset format.
  Not yet specified [round-1-critique].
- **Legal / trademark / IP.** Now covered by `market-legal` (section 8.4).
- **Competitive market landscape.** Covered by `market-legal`.
- **Factory preset library from validated hardware panel positions.** The acid /
  IDM patch numbers are **software GUI fader values**, not hardware panel
  positions (section 7.3) — a hardware-validated preset library does not yet
  exist [round-1-critique, acid-culture].
- **External-audio path inventory.** Now covered by `io-external`: there is **no
  stock external audio input** (section 7.5).

### 6.2 Under-verified claims flagged by the critic

The critic specifically flagged, and this ledger tracks: the gate ON level
(3.1), the IC designators (now frozen, 2.1), the TH1 location (now frozen, 2.3),
the rail topology (now frozen, 2.5), the filter component values (3.4), the
tonal-character framing (8.1), the IR3109 clone-derived figures (4.1), the ADSR
curve law (5.1), the sub-osc clock/+14V (4.3), the LFO core op-amp (7.4), the
sequencer byte format (4.6), the BA662 architecture (4.2), the Aphex/Vince Clarke
attributions (8.2), the filter-chip lineage (8.3), and the CV-input range / A4
reference / transpose table (8.5). The critic's root-cause recommendation — that
the **single biggest lever is one clean high-resolution scan of the 1982 Service
Notes** — was acted on in the schematic-reconcile pass, which is what froze
sections 2.1–2.6.

---

## 7. Software-emulation artifacts (NOT on the 1982 hardware)

These "facts" appear in later software (Roland Cloud SH-01A / plug-out) and are
routinely mistaken for hardware features. The hardware has **none** of them. In
the project's "authentic" mode, each must be **absent or flagged software-only**.

### 7.1 No sine LFO

The hardware modulator set is Triangle / Square / Random / Noise (section 2.7).
The **sine** LFO and the **six-position** modulator selector (Sine, Triangle,
Saw, Square, Random, Noise) are **Roland Cloud software additions**
`[software-emulation artifact]` [lfo correction; acid-culture correction;
mixer-mod-glide correction].

### 7.2 No 32' / 64' registers

Hardware RANGE is 16'/8'/4'/2' (section 2.8). **32' and 64'** exist **only** on
the Roland Cloud plugin / SH-01A `[software-emulation artifact]` [acid-culture
correction]. Any "32' acid bass" / "64' lead" guidance is software-scoped.

### 7.3 Acid/IDM patch numbers are software GUI values

Resonance "175–200/255", pulse width "180–230/219", LFO rate "70–110", decay
"~200", portamento "5–20/80", and the sub-bass mixer levels are **Roland Cloud /
MusicRadar software GUI fader values**, not original-hardware panel calibrations
`[software-emulation artifact]` [acid-culture correction]. The "~2 o'clock"
resonance (panel position) and "175–200/255" (GUI value) are from **different
sources on different scales** and were mis-merged — present separately
[acid-culture correction].

### 7.4 LFO core op-amp identity is unknown (not a software artifact, but unmeasured)

The integrator+comparator relaxation-oscillator description is **AMSynths
block-level + general theory, not schematic-verified**, and the specific op-amp
(M5218L vs TL062 vs IR9022 — all three appear in the parts list) is
**unconfirmed** `[theory/inference, unmeasured]`. Do **not** assert a specific
LFO op-amp part [lfo correction].

### 7.5 No external audio input

The 1982 block diagram shows **no audio-in**. CV IN is **pitch-only** (1V/1OCT,
0–7V) and EXT CLK is a **logic/clock** input (+2.5V or more); neither is an audio
path. The "external audio input" and "VCF CV input" seen in some write-ups are
**aftermarket mods** (circuitbenders), not original features — any clone ext-audio
or filter-CV path is a **clone-design addition**, label it as such [io-external].

---

## 8. Cultural-attribution cautions (marketing-soft — hedge or drop)

Cultural attributions are **marketing-soft**: keep documented general use, but
do not let them harden into technical fact.

### 8.1 Filter tonal character `[editorial — refuted as worded]`

"Drier/rawer/more aggressive than Juno-60 or Jupiter-8" is **editorial and
unsourced** [filter correction]. Sourced descriptors are "fatter/glassier,"
"squelchy/plasticy with resonance," and "growl-like aggressive" — and the
"aggressive/growl" descriptor is tied to **LFO-into-cutoff modulation**, not
intrinsic clipping. The mechanism wording is also wrong: OTA soft-clipping is the
**Juno** limiter; the SH-101's limiter is the **diode clamp to ground** (via
TR27). **Shipping:** do not advertise "drier/rawer" as a sourced tonal claim.

### 8.2 Aphex Twin (track-level) and Vince Clarke

Aphex Twin's **general** SH-101 use is documented (1993 Future Music interview);
his rig's synth subset is SH-101/MS-20/DX7 (plus a Casio FZ sampler — so "core
trio" is not the full sound source set) [idm-aphex]. But **track-level claims are
weak**: "Digeridoo" is likely DIY-box-based and is mildly contradicted by the
same interview; "Polynomial-C"/"Actium" rest on speculative forum analysis —
**hedge or drop** track-level attributions [idm-aphex, acid-culture]. **Vince
Clarke as a canonical SH-101 user is DROPPED**: it rests on a single passing
Wikipedia list entry and is contradicted by artist-specific sources, which name
the ARP 2600, Sequential Pro-One, and Roland System 100M as his favourites
[acid-culture correction].

### 8.3 Filter-chip lineage — TB-303 does NOT share the IR3109 `[corrected]`

The SH-101 IR3109 is shared with the **Juno-6/60** and the **Jupiter family**
(and the Juno-106 via the D80017A SMD descendant). The **TB-303 does NOT use the
IR3109** — it uses a **discrete 4-pole diode/transistor-ladder filter**, a
categorically different topology [idm-aphex correction; acid-culture]. The MC-202's
IR3109 use is disputed (section 3.5). **Shipping:** any "same family as the
303" acid-lineage framing is **wrong** and must be corrected to "sonically
adjacent to but circuit-distinct from the TB-303."

### 8.4 Trademark and naming `[market-legal]`

The SH-101 wordmark is registered (US Serial 79216816 / Reg. 5505499) in
**International Class 009** (downloadable music-production software), **not**
Class 15 (musical instruments) [market-legal correction, USPTO TSDR, high].
Roland holds documented **trade-dress/design marks for the TB-303/TR-808/TR-909**,
but **no SH-101-specific panel trade-dress registration was found** [market-legal,
residual]. Open: whether "mwAudio101" risks confusing similarity with "SH-101"
(shared "101"); EU/UK/JP registrations; nominative-use disclaimer language —
all need IP-counsel review before launch [market-legal, open]. SH-101 circuit
patents (1982) are almost certainly expired, leaving trademark/trade-dress as the
live risk [market-legal, open].

### 8.5 Spec figures resting on OCR / secondary Roland web only `[service-manual-OCR / web-only]`

Several panel/spec values rest on the archive.org **OCR** of the service manual,
where the very "-"-glyph and the 10-vs-12 ambiguity can be mis-read [overview].
Specifically: **CV input range 0–7V** is illegible in the primary OCR ("o^iy")
and rests on Roland's **modern web spec only** (corroborated by snippets, not a
direct render — the page returned HTTP 403) [power-cv-cal, io-external]. The
**A4 = 442Hz at 8'/Middle reference and the transpose table** (0.417 / 1.417 /
2.417 / 5.0) were not fully verbatim-verified, and the VR-3 LFO-offset, VR-6/VR-8
Lissajous specifics, and "cutoff ~1kHz at A4" need a clean adjustment-procedure
scan [power-cv-cal, residual]. The "thermet" RVS0707/RVG0707 parts are **cermet
trimmer pots, not thermistors** — keep them distinct from the SDT-1000
[power-cv-cal correction]. **Shipping:** treat these as confirmed-by-secondary,
flag any netlist dependency on them.

---

## 9. Key parameters

This table collects the load-bearing parameters surfaced by this ledger, with the
honest confidence and source. (Per the project decision, none is a bench
measurement.)

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| VCO chip designator | CEM3340 = IC13 | — | high (frozen) | service-manual [s1] |
| VCF chip designator | IR3109 = IC14 | — | high (frozen) | service-manual [s1] |
| VCA chip designator | BA662A = IC15 | — | high (frozen) | service-manual [s1] |
| CPU designator | TMP80C49 = IC6 | — | high (frozen) | service-manual [s1] |
| Gate output ON | 10–12 (12 service / 10 @100kΩ web) | V | disputed/medium | service-manual + Roland web [s1][s5] |
| TH1 location | VCF cutoff-CV path | — | high (frozen) | service-manual [s1] |
| VCF integrator caps | 240 (C47/C48/C50/C51) | pF | high (frozen) | service-manual [s1] |
| Supply rails | +15, +14, +9, +5, GND, -5, -2.5 | V | high (frozen) | service-manual [s1] |
| Memory backup | 1.8 × 6 | V | high (frozen) | service-manual [s1] |
| LFO range (hardware) | 0.1–30 | Hz | high (original) | Roland spec + AMSynths [s5][s2] |
| LFO smooth wave | triangle (not sine) | — | high (frozen) | service-manual + Wikipedia [s1][s4] |
| VCO RANGE | 16'/8'/4'/2' | foot | high (frozen) | service-manual [s1] |
| IR3109 buffer currents | 0.6 / 1.0 / 1.3 | mA | clone-derived | Alfa AS3109 / AMSynths [s2][s6] |
| IR3109 self-osc amplitude | ~20 | Vpp | low (clone @ ±12V) | AMSynths AM8101 [s2] |
| Expo tempco magnitude | ~0.33 (~3300 ppm) | %/°C | theory/inference | xonik.no [s7] |
| Max attack (original) | 4 | s | high | service-manual [s1] |
| CV input range | 0–7 | V | secondary-Roland-web | Roland web spec [s5] |
| Sub-pulse duty | 25 | % | Electric-Druid-attributed | Electric Druid [s3] |
| Self-osc model threshold (k) | 4 | dimensionless | theory (not physical pot) | Stilson-Smith/Zavalishin [s8] |
| SH-101 trademark class | Class 009 | — | high | USPTO TSDR [s9] |

---

## 10. Confidence, disputes & honest labels (REQUIRED summary)

Stated plainly, the things mwAudio101 must **not** present as settled:

1. **Gate ON level is disputed (10V vs 12V).** Ship as ~10–12V with the
   condition labelled; service manual (12V) is our authority. [3.1]
2. **All IR3109 electrical figures and the ~20Vpp self-oscillation are
   clone-derived** (Alfa AS3109 / AMSynths module on ±12V), not original-
   instrument measurements. Label clone-derived, presumed-equal. [4.1]
3. **The BA662 VCA internals are reverse-engineered** (Open Music Labs probing a
   chip); no datasheet exists. Never present as manufacturer-confirmed. [4.2]
4. **The ADSR segment-curve law and the LFO core op-amp identity are unmeasured
   theory/inference.** Label as such; do not name a specific LFO op-amp. [5.1, 7.4]
5. **The sequencer per-step byte format and note-priority mapping are community
   disassembly (joebritt), partly inferred** — not Roland-authoritative. [4.6]
6. **The hardware has NO sine LFO, NO 32'/64' registers, NO external audio input,
   and NO MIDI/DCB.** These are software-emulation artifacts; the acid/IDM patch
   numbers are software GUI values, not hardware panel positions. [7.1–7.5]
7. **We have NO physical-unit measurements (project decision).** Every Bode plot,
   ADSR oscilloscope curve, and harmonic spectrum is an **open validation gap**,
   not a delivered fact. We model from documented circuit behavior and do not
   claim measured fidelity. [1.1, 5]
8. **Cultural claims are marketing-soft:** keep documented general use; **drop
   the Vince Clarke association**; **hedge/drop track-level Aphex Twin claims**;
   and **correct the lineage so the TB-303 (discrete diode ladder) is NOT grouped
   with the IR3109 family** (Juno-6/60 + Jupiter only; MC-202 disputed). [8.2,
   8.3, 3.5]
9. **Filter "drier/rawer" tonal framing is editorial, not sourced**; the SH-101's
   limiter is the diode clamp, not OTA soft-clipping. [8.1]
10. **The CEM3340 is on-die temp-compensated; TH1 is in the VCF path, not the
    VCO path.** Earlier "external VCO thermistor / incomplete compensation"
    framing is refuted. [2.3]

### 10.1 Residual risks that remain open even after freezing

- Rail **generation** topology partially traced (3.3); resonance-feedback
  component **values** single-reader only (3.4); CPU IC6 schematic position not
  legibly captured (2.1).
- CV input range, A4 reference, and full transpose/Lissajous calibration sub-
  clauses rest on OCR/secondary-web and need a clean adjustment-procedure scan
  (8.5).
- MGS-1/MG-1 connector pinout undocumented (4.7).
- DSP engine choice (ZDF/TPT vs Huovilainen), decimator design, and IR3109-
  specific deviations from the generic 4-pole model are open ADRs (5.6, 6.1).
- Variance/drift weights, per-node tolerance classes, and the frozen-offset vs
  live-drift split are design choices, not sourced constants (5.4, 6.1).
- VR-4's exact function is unestablished (it is absent from the alignment
  procedure) [variance-drift, open].

---

## 11. Design implications for mwAudio101

- **Cite this ledger, do not re-debate.** Architecture and backlog phases should
  reference frozen items by section (e.g. "rails per ledger 2.5") and treat
  sections 3–8 as the canonical list of things to label rather than assert.
- **Build an "authentic mode" gate.** Ship section 7 as a hard switch: authentic
  mode exposes only Triangle/Square/Random/Noise, 16'–2', no external audio, no
  MIDI-only-derived behaviors; an "extended" mode may add software-only features
  but must visibly mark them non-original.
- **Carry provenance labels into the code and UI.** Clone-derived (4.1, 4.3,
  4.4), reverse-engineered (4.2), and theory (5.1, 5.6, 7.4) figures should keep
  their label in comments / parameter metadata so future contributors do not
  promote them to "fact."
- **Implement the gate as ~10–12V V-trig** verified against a 100 kΩ load, with
  the condition documented (3.1). Implement TH1 in the VCF cutoff path and a
  self-compensated VCO core (2.3). Use the frozen rail set (2.5) for any
  headroom/clipping reasoning.
- **Treat the DSP model as topology-faithful, not measurement-faithful.** Because
  the golden-reference captures of section 5 do not exist, validation is against
  the documented circuit and theory, plus optional local-only recording cross-
  checks that are never the oracle. State this in any "accuracy" marketing claim.
- **Write the open ADRs** the critic and residuals call for: anti-aliasing /
  decimator choice, ZDF-vs-Huovilainen engine, variance/drift model split, MIDI/
  CC map, and preset format (5.6, 6.1, 10.1).
- **Run the legal guardrails before launch:** Class 009 trademark, nominative-use
  disclaimer, "mwAudio101 vs SH-101" similarity, and EU/UK/JP marks (8.4).
- **Rebuild the factory preset library from validated hardware panel positions,**
  not the software GUI values in 7.3.

---

## References

- [s1] Roland SH-101 Service Notes (Nov 1 1982), schematic + parts list +
  adjustment procedure — primary citation of record (electricdruid.net /
  synthfool.com scans; archive.org OCR full text). Authoritative for frozen
  facts (sections 2.x) and the 12V gate spec.
- [s2] AMSynths — IR3109/AS3109 filter analysis, AM8101/AM8110/AM8112 clone
  module pages, BA662 notes, original-vs-clone spec commentary
  (amsynths.co.uk).
- [s3] Electric Druid — SH-101 sub-oscillator / divider reverse-engineering and
  IR3109-family "68k + 240p" notes (electricdruid.net).
- [s4] Wikipedia — Roland SH-101 (LFO waveform set, white noise, IR3109 lineage
  corroboration).
- [s5] Roland support Technical Specifications / Sweetwater spec mirror — modern
  10V@100kΩ gate spec and CV-input 0–7V (support.roland.com; HTTP 403 on direct
  fetch, corroborated via search snippets).
- [s6] Alfa AS3109 clone datasheet — buffer drive currents (0.6/1.0/1.3 mA),
  electrical characteristics (clone-derived figures).
- [s7] xonik.no — exponential-converter theory, ~+3300 ppm/°C (0.33%/°C) tempco
  (xonik.no/theory/vco/expo_converter_2.html).
- [s8] Stilson & Smith / Zavalishin "The Art of VA Filter Design" / Huovilainen —
  VA filter theory, k=4 self-oscillation threshold, TPT/ZDF and tanh-ladder
  models; Välimäki and Martin Finke (PolyBLEP) for anti-aliasing.
- [s9] USPTO TSDR — SH-101 wordmark, Serial 79216816 / Reg. 5505499, Int. Class
  009 (tsdr.uspto.gov/statusview/sn79216816).
- [s10] joebritt / SH101_Disassembled — community firmware disassembly; sequencer
  RAM layout (locs 1B–7F, ~100 steps), note-priority flags (partly inferred).
- [s11] Open Music Labs — BA662 reverse-engineering (chip probing; no public
  datasheet).
- [s12] u-labor.de — professional SH-101 repair teardown; internal rail set
  (+15V measured ~14.87V, +14V, ±5V; 9–12V DC external input).
- [s13] Project round-1 completeness-critic report and per-dimension verification
  corpus (internal).
