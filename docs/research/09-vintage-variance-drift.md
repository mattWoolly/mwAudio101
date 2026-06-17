<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Vintage Variance & Analog-Drift Model

## 1. Scope and purpose

This document is the citable source of truth for how mwAudio101 should model
the unit-to-unit variance, calibration spread, and thermal drift of the Roland
SH-101. It separates three classes of statement, and labels every claim
accordingly:

- **Documented SH-101 facts** — drawn from the SH-101 service manual and chip
  identities [service-manual, high].
- **Clone-derived figures** — drawn from clone/recreation projects (AMSynths
  AM8110, Alfa AS-series) that modernize the original; these are *not* original
  Roland measurements [clone-derived, presumed-equal].
- **General analog-modeling practice** — exponential-converter physics, RC
  tolerance theory, and DSP-forum drift-modeling consensus, plus the u-he Diva
  "Trimmers" panel as the reference control set [theory/inference, unmeasured]
  or [modeling-practice, community].

The project has taken **no physical-unit measurements** [project decision].
Every figure that would require bench data (Bode plots, oscilloscope envelope
curves, cents-vs-time drift, harmonic spectra) is therefore an **open
validation gap**, not a delivered fact. This is stated again per item in
[Section 8](#8-confidence-disputes--honest-labels).

The frozen signal-chain reconciliation used throughout: **CEM3340 VCO = IC13,
IR3109 4-pole VCF = IC14, BA662A VCA = IC15** [service-manual, high]. (Earlier
"IC3" / "IR3R01" readings were OCR errors and are not used here.)

## 2. Documented SH-101 calibration trimmers

### 2.1 The factory trimmer set

The SH-101 service manual specifies a fixed factory-alignment procedure. Each
trimmer is set to a nominal target; the **tolerance band around each target is
the literal source of unit-to-unit variance** [service-manual, high]
[ref:manual-page6]. The enumerated adjustments are:

- **VR-1 (+5V / D-A Width)** — set to 2.75 V +/-1 mV.
- **VR-2 (D/A Tune)** — set to 0 V +/-1 mV.
- **VR-3 (D/A Linear)** — set to 0 V +/-2 mV, then 2.5 V +/-1 mV.
- **VR-5 (Range Width)** — set so output pitch matches in Up & Down mode.
- **VR-6 (VCO Width)** — set until the Lissajous figure is motionless on F3.
- **VR-7 (VCO Tune)** — set to 442 Hz.
- **VR-8 (VCF Width)** — set so the F5 cycle = 2x the F4 cycle.
- **VR-9 (Tune)** — centre, used together with VR-7.

Test/alignment mode is entered by holding **LOAD + KEY TRANSPOSE** at power-on
[service-manual, high] [ref:manual-page6] [ref:archive-manual].

### 2.2 The VR-4 gap

The dimension prompt referenced "9 calibration trimmers." The service-manual
alignment procedure cleanly enumerates only **VR-1, 2, 3, 5, 6, 7, 8, 9** —
eight trimmers. **VR-4 does not appear in the alignment routine**; its function
is unconfirmed (plausibly an LFO mod-offset or a D/A-related fixed reference not
exercised by the alignment procedure) [community disassembly, partly inferred]
[ref:manual-page6]. This is flagged as a dispute in
[Section 8](#8-confidence-disputes--honest-labels) and an open question in
[Section 7](#7-open-questions--validation-gaps).

### 2.3 What is and is not calibrated for the filter

The IR3109 VCF (IC14) has **only the VR-8 "VCF Width" (scale) trim** and **no
documented per-unit cutoff-offset calibration** [service-manual, high]
[ref:manual-page6]. Consequently the cutoff-frequency *offset* spread between
units is essentially uncalibrated on the original hardware and is a legitimate,
relatively generous variance parameter to model. The IR3109 has **no on-chip
resonance control**; resonance/self-oscillation is set by external feedback with
clipping diodes to ground that limit and soften the oscillation level (Juno-6/60
topology) [clone-derived: AMSynths, presumed-equal] [ref:amsynths-ir3109].

## 3. Drift and tempco physics

### 3.1 The exponential-converter scale tempco

The dominant residual VCO tuning-drift mechanism is the exponential (antilog)
converter's inherent scale temperature coefficient. The intrinsic V/octave scale
factor follows the kT/q thermal-voltage mechanism, giving a **negative
coefficient of about -0.33 %/degC (= -3300 ppm/degC)**; this is conventionally
nulled with a positive **+3300 ppm/degC** compensating tempco resistor (e.g. a
Tel Labs Q81 type) or by an OTA whose gain carries the same coefficient
[theory/inference, unmeasured] [ref:xonik-expo].

**Sign convention (important):** the converter scale and OTA transconductance
have the *negative* coefficient; the compensating resistor has the *positive*
coefficient. They are two sides of the same kT/q mechanism
[theory/inference, unmeasured] [ref:xonik-expo]. A correction was applied here:
the originally cited Electric Druid CEM3340 page does **not** contain the
-3300 ppm/degC figure; the sourced value comes from xonik.no and is stated there
as +3300 ppm/degC [ref:xonik-expo] (see
[Section 8](#8-confidence-disputes--honest-labels)).

### 3.2 The CEM3340 is on-die temperature-compensated

The SH-101 VCO is the **CEM3340 IC with on-die temperature compensation**, not a
discrete matched expo pair [service-manual, high] [ref:wikipedia-sh101]
[ref:amsynths-am8110]. Pins 1/2 generate a temperature-dependent voltage that is
multiplied with the control voltage, giving good but finite stability. This is
why the SH-101 is comparatively stable for a vintage analog monosynth — the
chip nulls most of the -3300 ppm/degC scale drift on-die, leaving only the
partially-compensated residual [theory/inference, unmeasured]
[ref:electricdruid-cem3340]. High-frequency flattening above roughly 5 kHz comes
from comparator switching delay and is trimmed via the CEM3340 pin-7
HF-tracking adjustment [theory/inference, unmeasured] [ref:electricdruid-cem3340].

### 3.3 Filter cutoff tempco (inferred, not measured)

The IR3109 sets cutoff via OTA bias current through an expo-style converter, so
its transconductance carries the **same kT/q ~-0.33 %/degC coefficient** as the
VCO converter [theory/inference, unmeasured] [ref:electricdruid-ir3109]. This is
inferred from general OTA theory (gm proportional to I_bias / V_t); **no IR3109
datasheet figure was located**, so the "same -3300 ppm/degC" equivalence is
theory, not a measured IR3109 spec (see
[Section 8](#8-confidence-disputes--honest-labels)). Roland's filter temperature
compensation is described in the literature as marginal ("probably not doing a
great deal") [theory/inference, unmeasured] [ref:electricdruid-ir3109].

### 3.4 Envelope-time spread from RC tolerance

The SH-101 ADSR uses the standard analog approach: a timing capacitor
charged/discharged through resistors with transistor exponential current shaping,
giving exponential attack/decay/release curves [theory/inference, unmeasured]
[ref:modwiggler-adsr] [ref:kassu-adsr]. By construction, component tolerances
scale the RC time constant directly, producing unit-to-unit envelope-time spread.
Typical tolerance practice — electrolytic/film timing caps +/-5 % to +/-20 %,
metal-film resistors +/-1 % — implies a similar percentage spread in envelope
times [theory/inference, unmeasured] [ref:metalfilm-spec]. The exponential
segment-curve law itself is **unmeasured theory/inference** for the SH-101
specifically; the original "shedsynth" citation described a digital EG and is
not used here. The "minimum attack ~1 ms" figure is a generic fast-analog-EG
order-of-magnitude reference, **not an SH-101 spec** [theory/inference,
unmeasured] [ref:schmitz-adsr].

### 3.5 Resistor tolerance classes (clone-derived / inferred)

The "0.1 % precision resistors on the VCO Range switch" figure originates from
the **AMSynths AM8110 clone**, where the source explicitly frames it as a
modernization (the AM8110 runs +/-12 V rails and uses 0.1 % resistors *so that
no trimmers are needed*). The **original Roland SH-101 instead uses alignment
trimmers** (VR-5 Range Width, VR-6 VCO Width) and ran from a +9 V battery
[clone-derived: AMSynths, presumed-equal — does NOT apply to original]
[ref:amsynths-am8110] [ref:manual-page6]. The "1 % metal-film, ~50-100 ppm/degC"
figure is a correct general metal-film spec but **no primary Roland source
confirms which tolerance class Roland used per node** [theory/inference,
unmeasured] [ref:metalfilm-spec].

## 4. Reference control set: u-he Diva "Trimmers"

The reputable VA reference for vintage variance is the **u-he Diva "Trimmers"
panel** [modeling-practice, community] [ref:diva-guide] [ref:diva-product]
[ref:diva-slop]. Per the Diva user guide it exposes:

- **Oscillator Voice Detune** — detunes voices per oscillator.
- **Detune Amt** — a global scaler over all the detune knobs.
- **Voice Drift** — "a slow wavering of the overall pitch."
- **VARIANCE** — "applies random offsets (slop) to cutoff, envelope times,
  pulse widths (waveform) and glide times," randomized **per voice/instance** via
  a per-parameter button rather than as continuous modulation.

Also relevant: a global **Accuracy** mode (draft/fast/great/divine = ZDF filter
quality) and a Transient/Reset-phase model. This is the canonical, well-regarded
VA implementation of vintage variance and the recommended template for
mwAudio101 [modeling-practice, community] [ref:diva-guide].

**Monosynth note:** the SH-101 is monophonic, so "per-voice detune" collapses to
a **per-instance / per-note-on tuning offset plus one global slow thermal drift**.
The full Diva per-voice variance set only becomes relevant if mwAudio101 adds
unison/stacking (see [Section 7](#7-open-questions--validation-gaps)).

## 5. DSP drift-modeling consensus

DSP-forum engineering consensus (KVR, gearspace; mystran et al.) is
[modeling-practice, community] [ref:kvr-drift] [ref:kvr-pink]:

- Model **slow drift** as low-passed / pink (1/f) noise or a bounded random walk
  (a leaky integrator of Gaussian noise). 1/f noise "persists all the way down
  to the lifetime of the device."
- Model **fast 'slop'** as Gaussian per-note-on offsets (Box-Muller, or a cubic
  shaping such as x = (2u-1)^3).
- Use **independent drift per voice** for natural beating (multi-voice only).
- Practical pitch-drift depth: **~+/-5 cents is commonly plenty; +/-20 cents is
  extreme** [modeling-practice, community] [ref:kvr-drift].

These are practitioner/field figures, **not SH-101-measured and not
peer-reviewed**; the underlying noise-process models (1/f, random walk) are
standard signal-processing concepts.

## 6. Proposed mwAudio101 drift/variance control set

A Diva-style "Trimmers / Vintage" page is recommended: a single master
**Vintage (Age/Drift)** macro plus discrete controls. Defaults are set LOW so
the instrument is in tune out of the box; users dial in "age." Each control
below is tagged as reproducing a documented SH-101 calibration point versus a
general analog-modeling embellishment, so the modeling stays honest.

### 6.1 Pitch drift and slop

- **Drift Depth** — 0-50 cents, default ~3-5 cents, driven by low-passed /
  pink-noise or bounded random walk [modeling-practice, community].
- **Drift Rate** — slow, effective ~0.01-1 Hz, separate from note-on slop
  [modeling-practice, community].
- **Tuning Slop** — per-note-on Gaussian offset, default ~+/-2-3 cents, max
  ~+/-20 cents [modeling-practice, community].
- Combine slow drift **and** note-on slop, not one or the other.

### 6.2 Warm-up / temperature model

- Optional **Warm-Up** model ramping an extra pitch/cutoff offset that decays
  over a user-set 0-30 min from "cold," anchored to the -3300 ppm/degC physics
  so VCO and VCF drift are **correlated** (shared temperature state), not
  independent [theory/inference, unmeasured]. Observed vintage-monosynth warm-up
  is a few-to-~10 cents over the first 10-30 min; the SH-101 sits at the more
  stable end due to CEM3340 on-die compensation [modeling-practice, community]
  [ref:kvr-drift] [ref:vse-warmup].

### 6.3 Per-instance calibration state

- Seed a **persistent per-instance calibration state** that perturbs the nominal
  trimmer set-points (VR-2 Tune, VR-7 Tune, VR-8 VCF Width) within their
  tolerance bands, so each loaded instance has a fixed "personality" plus live
  drift on top — mirroring how a real unit has frozen trimmers + thermal wander
  [service-manual, high] for the set-points; [theory/inference, unmeasured] for
  the band widths.

### 6.4 Variance ('slop') group

One 0-100 % spread control each, randomized per note/instance:

- **Cutoff Variance** — offset on filter cutoff; uncalibrated on hardware
  ([Section 2.3](#23-what-is-and-is-not-calibrated-for-the-filter)), so this can
  be relatively generous [service-manual, high] (that it is uncalibrated) /
  [modeling-practice, community] (the control itself).
- **Envelope-Time Variance** — ~+/-5 % to +/-20 % of A/D/R times, from RC
  tolerance [theory/inference, unmeasured]; exact percentages are an unverified
  heuristic.
- **Pulse-Width Variance** [modeling-practice, community].
- **Glide-Time Variance** [modeling-practice, community].

### 6.5 Filter and global quality

- Model the **24 dB/oct lowpass with diode-limited self-oscillation, no
  Q-compensation** — so resonance thins the passband; do **NOT** add level
  make-up gain [clone-derived: AMSynths, presumed-equal] [ref:amsynths-ir3109].
- Give cutoff a small thermal coefficient tied to the shared temperature state
  [theory/inference, unmeasured].
- Keep a Diva-style global **Accuracy** quality switch (ZDF filter on/off)
  [modeling-practice, community] [ref:diva-guide].

## 7. Open questions & validation gaps

1. **VR-4 function** — not in the alignment procedure; needs a clean
   service-manual schematic + parts-list read to confirm the full trimmer
   enumeration [ref:manual-page6].
2. **Per-node original Roland tolerance classes** — which nodes are 1 %
   metal-film vs 5 % carbon, electrolytic vs film timing caps — unconfirmed from
   primary schematic; needed to set realistic per-parameter variance bands.
3. **Measured SH-101 warm-up/drift figures** (cents vs time, cents vs degC) — we
   rely on general vintage-monosynth anecdote; no Roland-published or
   independently instrumented SH-101 figure was located. **Open validation gap
   (no bench data).**
4. **IR3109 cutoff CV scaling and actual temperature coefficient in the SH-101
   specifically** — vs the generic OTA tempco assumption. No IR3109 datasheet
   located. **Open validation gap.**
5. **Per-instance fixed offsets vs time-varying drift split** — should the clone
   persist a seeded per-serial calibration state separate from live thermal
   wander (as real units have frozen trimmers + live drift)? Diva blends these;
   the design phase should decide the split.
6. **Unison/stacking** — if mwAudio101 adds it, the full Diva per-voice
   detune+variance set applies; if not, "per-voice detune" reduces to per-note-on
   tuning slop plus one global drift.

## 8. Confidence, disputes & honest labels

This section surfaces every disputed, low-confidence, corrected, or
residual-risk item for this dimension, plainly.

### 8.1 Disputed / unconfirmed items

- **VR-4 function is unknown** [community disassembly, partly inferred]. It does
  not appear in the alignment procedure; it may be a fixed reference or
  factory-set node. Treated as disputed/low-confidence until a clean schematic +
  parts list is read [ref:manual-page6].
- **Original per-node resistor/capacitor tolerance classes are unconfirmed.**
  The figures used are general component specs or clone choices, not primary
  Roland BOM data.

### 8.2 Corrections applied to the source dossier

- **Source miscitation (high):** the -3300 ppm/degC tempco was originally cited
  to the Electric Druid CEM3340 page, which does **not** contain that figure.
  The correct primary source is **xonik.no**, which states it as **+3300
  ppm/degC** (the positive compensating coefficient) [ref:xonik-expo]. Re-cited
  and sign-reconciled in [Section 3.1](#31-the-exponential-converter-scale-tempco).
- **Sign/semantics (medium):** the converter scale and OTA gm carry the
  *negative* ~-0.33 %/degC coefficient; the compensating resistor carries the
  *positive* +3300 ppm/degC. Both are the same kT/q mechanism. Stated explicitly.
- **VCO is not a discrete expo pair (medium):** the SH-101 VCO is the **CEM3340
  IC with on-die compensation** [service-manual, high] [ref:wikipedia-sh101]. A
  minority of hobbyist sources dispute the exact part attribution, but all agree
  it is an IC, not a discrete matched pair — minor residual uncertainty noted.
- **Clone-vs-original conflation (high):** the "0.1 % range-switch precision
  resistors" figure is an **AMSynths AM8110 clone modernization** to eliminate
  trimmers, explicitly **NOT** the original SH-101 (which is +9 V and uses
  trimmers VR-5/VR-6). Re-labeled as clone-derived; **must not** be cited as
  original-hardware fact [ref:amsynths-am8110] [ref:manual-page6].
- **Wrong citation for envelope claims (medium):** the "shedsynth ADSR" source
  cited for the RC charge/discharge mechanism and the ~1 ms minimum attack
  describes a **digital Arduino EG** and supports neither. Substituted with
  generic analog-EG references (Schmitz, Kassutronics, Electric Druid), labeled
  as generic, not SH-101-specific [ref:schmitz-adsr] [ref:kassu-adsr].
- **Garbled key-parameter string (low):** "metal-film ~1 % / +/-100 % /
  ppm/degC" should read "~1 % tolerance / ~50-100 ppm/degC" (TCR typically
  50-100 ppm/degC; +/-100 is an upper bound). Corrected in the
  [Key parameters](#9-key-parameters) table.
- **Unverified numbers (medium):** the "+/-5 % to +/-20 % of nominal time"
  envelope-variance spread and the "~1 ms" minimum attack are reasonable
  heuristics but were **not** extractable from the cited primary sources. Marked
  as modeling estimates / disputed on exact values.

### 8.3 Residual risks

- **No SH-101 service-manual schematic** (with per-node tolerance/tempco classes)
  was accessed; per-node tolerance classes remain unconfirmed primary-source-wise.
- **No IR3109 datasheet** was located; the OTA transconductance tempco for the
  IR3109 specifically is **inferred from general OTA theory**, not a datasheet
  figure. The "same -3300 ppm/degC" filter equivalence is theory, not measured.
- **Exact VCO chip is mildly contested** in hobbyist literature (CEM3340 vs
  disputed Roland-custom attributions); all sources agree it is an IC with
  on-chip compensation, but precise part attribution carries minor uncertainty.
- **VR-4's function is unestablished** (not in the alignment procedure).
- **Most drift-depth and warm-up figures rest on practitioner/forum anecdote**
  (KVR, Vintage Synth Explorer), not instrumented measurement; mutually
  consistent but labeled engineering practice / field reports.
- **Two cited primary sources could not be parsed/fetched** (the Diva user-guide
  PDF was binary/compressed; a gearspace warm-up thread returned HTTP 403);
  specific numbers attributed to them were corroborated only via adjacent
  sources.
- **The slow-drift vs fast-slop allocation/weights are design choices**, not
  sourced constants.

### 8.4 Software-emulation artifacts (not original hardware)

For honesty: features such as a sine LFO, 32'/64' registers, an external audio
input, and MIDI/DCB do **not** exist on the original SH-101 hardware — they
appear only in later software (e.g. Roland Cloud SH-01A). They are out of scope
for this drift/variance dimension and must not be modeled as hardware behavior
[software-emulation artifact].

## 9. Key parameters

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| Exponential-converter scale tempco (VCO & VCF) | -3300 (converter); +3300 (compensating resistor) | ppm/degC | high (theory) | [ref:xonik-expo] |
| VR-1 D/A Width target | 2.75 (+/-0.001) | V | high | [ref:manual-page6] |
| VR-2 D/A Tune target | 0 (+/-0.001) | V | high | [ref:manual-page6] |
| VR-3 D/A Linear target | 0 then 2.5 | V | high | [ref:manual-page6] |
| VR-7 VCO Tune reference | 442 | Hz | high | [ref:manual-page6] |
| VCF slope / type | 24 dB/oct lowpass, self-osc, no Q-comp | filter config | high (clone-derived) | [ref:amsynths-ir3109] |
| Metal-film resistor tolerance / tempco | ~1 % / ~50-100 | % / ppm/degC | medium (general) | [ref:metalfilm-spec] |
| Range-switch precision resistors (clone only) | 0.1 | % tolerance | medium (clone-derived) | [ref:amsynths-am8110] |
| Recommended modeled pitch-drift depth (default) | ~3-5 (range 0 to ~20) | cents | medium (practice) | [ref:kvr-drift] |
| Observed thermal warm-up drift | ~few to 10 (over 10-30 min) | cents | medium (anecdote) | [ref:vse-warmup] |
| Min envelope attack (generic ref, not SH-101) | ~1 | ms | low (generic) | [ref:schmitz-adsr] |
| Suggested envelope-time variance spread | +/-5 to +/-20 | % of nominal | low (heuristic) | [ref:diva-guide] |

## 10. Design implications for mwAudio101

1. **Adopt a Diva-style "Trimmers / Vintage" page** with a single master Vintage
   (Age/Drift) macro plus the discrete controls in
   [Section 6](#6-proposed-mwaudio101-driftvariance-control-set).
2. **Default everything LOW** so the instrument is in tune out of the box; let
   users dial in "age."
3. **Combine slow drift (pink-noise / random walk) with per-note-on Gaussian
   slop** rather than choosing one. Default drift ~3-5 cents, slop ~+/-2-3 cents.
4. **Correlate VCO and VCF drift through a shared temperature state** anchored to
   the -3300 ppm/degC physics; do not drift them independently.
5. **Persist a per-instance calibration state** perturbing VR-2/VR-7/VR-8
   set-points within tolerance bands, giving each loaded instance a fixed
   personality plus live drift — mirroring frozen trimmers + thermal wander.
6. **Scope drift/slop per-instance and per-note-on** for the monophonic
   instrument; only expose per-voice variants if unison/stacking is added.
7. **Model the filter as 24 dB/oct lowpass with diode-limited self-oscillation
   and no Q-compensation** — resonance thins the passband; add no level make-up.
8. **Tag every control in the UI/docs** as either a documented SH-101 calibration
   point (the VR set) or a general analog-modeling embellishment (pink-noise
   drift, warm-up curve, per-target variance), so the modeling is honest.
9. **Treat all numeric drift/warm-up/envelope-spread figures as tunable defaults,
   not fixed specs** — they rest on community practice and physics inference, and
   we have no bench measurements ([Section 7](#7-open-questions--validation-gaps),
   [Section 8.3](#83-residual-risks)).

## 11. References

- [ref:manual-page6] Roland SH-101 service manual, alignment/calibration page.
  <https://www.manualslib.com/manual/1231849/Roland-Sh-101.html?page=6>
- [ref:archive-manual] Roland SH-101 Service Manual (full text).
  <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
- [ref:synthfool-manual] Roland SH-101 Owners & Service Manuals (synthfool).
  <https://synthfool.com/docs/Roland/SH_Series/Roland_SH-101_Servicemanual/>
- [ref:amsynths-ir3109] AMSynths — "All about the IR3109 chip."
  <https://amsynths.co.uk/2022/04/06/all-about-the-ir3109-chip/>
- [ref:amsynths-am8110] AMSynths — AM8110 SH-101 VCO (clone).
  <https://amsynths.co.uk/home/products/oscillators/am8110-sh-101-vco/>
- [ref:electricdruid-cem3340] Electric Druid — CEM3340 VCO designs.
  <https://electricdruid.net/cem3340-vco-voltage-controlled-oscillator-designs/>
- [ref:electricdruid-ir3109] Electric Druid — Roland filter designs with the
  IR3109/AS3109.
  <https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/>
- [ref:electricdruid-sh101] Electric Druid — SH-101 category.
  <https://electricdruid.net/category/vintage-synths/roland/sh-101/>
- [ref:xonik-expo] Xonik — exponential converter theory (tempco).
  <https://www.xonik.no/theory/vco/expo_converter_2.html>
- [ref:wikipedia-sh101] Wikipedia — Roland SH-101.
  <https://en.wikipedia.org/wiki/Roland_SH-101>
- [ref:diva-guide] u-he Diva user guide (Trimmers / Variance panel).
  <https://uhe-dl.b-cdn.net/manuals/plugins/diva/Diva-user-guide.pdf>
- [ref:diva-product] u-he Diva product page.
  <https://u-he.com/products/diva/>
- [ref:diva-slop] Mark Mosher — "Add variability/slop with u-he Diva Trimmers."
  <https://markmoshermusic.com/2013/01/20/add-variability-slop-u-he-diva-trimmers/>
- [ref:kvr-drift] KVR DSP forum — simulating analog oscillator drift.
  <https://www.kvraudio.com/forum/viewtopic.php?t=525651>
- [ref:kvr-pink] KVR DSP forum — pink/1-f noise drift modeling.
  <https://www.kvraudio.com/forum/viewtopic.php?t=516692>
- [ref:vse-warmup] Vintage Synth Explorer forum — warm-up/drift discussion.
  <https://forum.vintagesynth.com/viewtopic.php?t=63079>
- [ref:modwiggler-adsr] ModWiggler forum — analog ADSR RC charge/discharge.
  <https://www.modwiggler.com/forum/viewtopic.php?t=230849>
- [ref:kassu-adsr] Kassutronics — precision ADSR.
  <https://kassu2000.blogspot.com/2015/05/precision-adsr.html>
- [ref:schmitz-adsr] R. Schmitz "Fastest ADSR in the West" (min attack ~1 ms,
  generic).
  <https://www.eddybergman.com/2024/12/ADSR%20Rene%20Schmitz.html>
- [ref:metalfilm-spec] Electronics Notes — metal-film resistor tolerance/TCR.
  <https://www.electronics-notes.com/articles/electronic_components/resistors/metal-film-resistor.php>
