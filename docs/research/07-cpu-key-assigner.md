<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Digital Control Subsystem (TMP80C49 firmware)

## 1. Scope and provenance

This document describes the SH-101 digital control subsystem — the single
microcontroller that does key assignment, note-priority resolution, the
arpeggiator, the 100-step sequencer, clock handling, and the time-multiplexed
control-voltage (CV) generation. It is the behavioral source of truth that the
mwAudio101 voice-allocation and clock logic will reference.

### 1.1 Authoritative sources and how to read the labels

Two primary sources underpin this dimension, and they have different
authority levels that are carried into every claim below:

- The Roland SH-101 **Service Manual** ("CPU PROGRAM" section, parts list,
  block diagram, specifications). This is the manufacturer-authored
  reference. Labeled `[service-manual, high]`.
- The community-recovered, fully-commented **disassembly by Joe Britt**
  (`joebritt/SH101_Disassembled`), with the mask ROM dumped from real
  hardware (`80C49_SH101.bin`, 2048 bytes). This is the authoritative
  *behavioral* reference for firmware logic but is the work of a community
  reverse-engineer, not Roland. Labeled
  `[community disassembly: joebritt, partly inferred]` where the author's
  own comments are sparse or the bit-level meaning is read from instruction
  structure rather than from a named comment.

Where the two corroborate each other the claim is firm. Where only the
disassembly speaks (e.g., the exact per-step sequencer bit layout) the claim
is explicitly flagged as reverse-engineered/inferred.

This project holds **no physical-unit measurements** (project decision).
Anything that would require bench data — DAC settling oscilloscope traces,
loop-time jitter capture, gate-level-shift verification — is an **open
validation gap**, not a delivered fact (see Section 9).

### 1.2 Schematic-reconciliation note (frozen facts)

This subsystem is the **CPU = IC6** of the frozen schematic reconciliation.
An earlier "IC3" reading was a wrong OCR and is not used here. The control
CPU is not in the audio path; it sets CVs and gates that drive the CEM3340
VCO (IC13) and the IR3109 VCF (IC14), which are documented in their own
dimension docs.

## 2. Microcontroller and execution model

### 2.1 The microcontroller (IC6)

The control CPU is a Toshiba **TMP80C49** — a mask-ROM, 8049-class MCS-48
8-bit microcontroller, marked `TMP80C49P-6-7301`, clocked by a **6 MHz
ceramic resonator** (parts list: "Ceramic resonator CSA 6MHz" with a CSC
trimmer; the `-6` suffix denotes the 6 MHz speed grade)
`[service-manual, high]` [S1] [S2]. The disassembly header independently
notes "80C49 clocked by 6MHz ceramic resonator"
`[community disassembly: joebritt, partly inferred]` [S1].

The 8049 carries **2 KB on-chip mask ROM** and **128 bytes on-chip RAM**.
The dumped image is exactly 2048 bytes, consistent with the on-chip 2 KB ROM
[S1]. The **CMOS** 80C49 (as opposed to the NMOS 8049) is used for low
standby power, which is relevant to battery-backed RAM (Section 6)
`[community disassembly: joebritt, partly inferred]` [S1].

### 2.2 The firmware is one polling super-loop (no interrupts)

The firmware is a single polling **super-loop** with **no interrupts**, and
it **does not use the 8049 hardware Timer/Counter**. The disassembly header
states: "No external interrupt"; "Does not use START or STOP instructions!
This means Timer/Counter function is not used. Uses T as another register";
and "Does not use SEL RB instruction (no BANK 1 working regs)"
`[community disassembly: joebritt, partly inferred]` [S1].

There are only four subroutines (`dac_out`, `delay2`, `keyboard_read`,
`cv_dac_val`), none recursive, so the stack only ever holds one return
address. The `T` register is repurposed as a byte of status flags:

- b6 = current LFO/clock state
- b5 = LFO high-to-low edge (set for exactly one loop)
- b4 = HOLD state
- b2 / b1 = scanner / function-read flags

This matters for our model: **the whole instrument is one loop, not an
event-driven system** `[community disassembly: joebritt, partly inferred]`
[S1].

### 2.3 Loop timing (1.5–3.5 ms) and the numbered steps

The main control loop runs every **1.5 to 3.5 ms** and executes a fixed
sequence of numbered steps. The service manual CPU PROGRAM section states:
"Steps 3 through 13 are a series of program steps that are sequentially
executed by the CPU at 1.5 to 3.5msec intervals"
`[service-manual, high]` [S2]. The disassembly's labeled steps match
`[community disassembly: joebritt, partly inferred]` [S1]:

| Step | Name |
| ---- | ---- |
| 1 | Test Mode |
| 2 | Initial Set |
| 3 | Range Data Read |
| 4 | Range Data Output |
| 5 | Keyboard Read |
| 6 | Clock Check |
| 7 | Random Data Output |
| 8 | Function Switch Read |
| 9 | Load |
| 10 | Play |
| 11 | Arpeggio |
| 12 | CV Output |
| 13 | Gate & LED Data Output |

After step 13 the loop jumps back to step 3. The **variable** loop time
means the CV/gate update rate — and thus the effective portamento stepping
and scan granularity — is **not perfectly constant**
`[service-manual, high]` [S2] [S1].

## 3. Key scanning and note priority

### 3.1 Key matrix

The keyboard is a **4 banks × 8 keys (32 keys)** matrix, read **active-low**
with 50k pull-ups; switch lines are pulled by the data-bus D-bits. "When a
switch is on it reads as 0 (inverted)"
`[community disassembly: joebritt, partly inferred]` [S1]; the service
manual likewise describes a 4×8 matrix `[service-manual, high]` [S2].

### 3.2 Note priority is monophonic and set by the GATE / GATE+TRIG switch

The instrument is monophonic. **Note priority is selected directly by the
GATE / GATE+TRIG (S7) switch**, not by a separate control:

- **GATE = lowest-note priority**
- **GATE+TRIG = last-note priority**
- **LFO trigger mode = lowest-note priority**

The disassembly `keyboard_read` comment reads: "GATE: lower note priority;
GATE + TRIG: last note priority; LFO: lower note priority. Flag F1 = 0 if
ADSR mode = GATE+TRIG ... F1=0 -> last note priority, F1=1 -> lower note
priority." Implementation: banks are scanned low→high and low key→high key;
in low-note mode the first key found wins ("as soon as we find a key down,
we are done"); in last-note mode the firmware XORs the newly-changed-down
keys against the prior scan and picks the lowest of the just-pressed keys
`[community disassembly: joebritt, partly inferred]` [S1].

**Correction to forum hearsay:** GATE mode is **LOWEST-note** priority, not
"high note priority." This correction is supported by the recovered code and
by broad community consensus [S1] [S3] [S4]. Honesty note: this mapping comes
from the disassembly comments plus community consensus and was **not**
independently confirmed against a Roland-authored statement, though it is
internally consistent with the reverse-engineered code
`[community disassembly: joebritt, partly inferred]`.

### 3.3 Trigger source semantics (coupled to the same switch)

The same S7 switch also sets envelope retrigger behavior, so priority and
trigger are **one physical control with two coupled behaviors**:

- **GATE** — no envelope retrigger on legato; a single gate is held across a
  legato keypress (mono-legato).
- **GATE+TRIG** — retrigger the envelopes on every new key.
- **LFO** — envelopes are (re)triggered by the LFO/clock instead of the
  keyboard; the envelope fires on each LFO cycle.

The disassembly tracks GATE as R6 bit7 (0 = gate high) and an "LFO ADSR
trigger" as R6 bit6 (set when S7 = LFO, values 0x40/0x4c/0x58)
`[community disassembly: joebritt, partly inferred]` [S1]. The service manual
states the Clock Reset signal "resets the Clock signal whenever a key on the
keyboard is pressed while either the GATE/TRIG Selector is set to LFO or the
ARPEGGIO mode is activated" `[service-manual, high]` [S2]; community sources
corroborate the legato/retrigger behavior [S5] [S6].

## 4. Pitch / CV generation and the time-multiplexed DAC

### 4.1 One 6-bit DAC, time-multiplexed via a 4052 to three destinations

A **single 6-bit DAC is time-multiplexed via a 4052 analog mux** into three
sample-and-hold (S/H) destinations: **CV OUT**, **VCO CV**, and a **RANDOM**
voltage. The route is selected by DAC data bits D7:D6
`[community disassembly: joebritt, partly inferred]` [S1]:

- `00` = CV OUT
- `01` = VCO
- `10` = RANDOM
- `11` = unused (idle / bus parked)

Each loop the CPU writes the 6-bit value (D0–D5) plus the two route bits,
lets it settle, and the corresponding S/H captures it; `dac_out` writes
`11xxxxxx` ("DAC not routed anywhere") to **park** the bus between updates.
Code masks `anl #3fh` ("4052 output 0 => CV out"), `anl #7fh`
("output 1 => VCO"), `anl #0bfh` ("output 2 => Random") confirm the routing
[S1]. So VCO pitch, the rear-panel CV jack, and the random/S&H voltage are
all produced by one DAC scanned in rotation.

The service-manual circuit-level detail of the 4052 and the S/H stages lives
in the **image-only schematic**, not in the OCR text, so the **4052 part
identity and S/H topology are disassembly-derived**, not read from the
service-manual text `[community disassembly: joebritt, partly inferred]`
[S1] [S2].

### 4.2 Pitch CV assembly and 1 V/octave scaling

The pitch CV is built additively: **key number + range offset + octave-
transpose offset + key-shift**. Range base values are spaced **exactly 12
DAC counts (one octave) apart**: R5 = 0x0C (16'), 0x18 (8'), 0x24 (4'),
0x30 (2') — "12 apart, each range an octave." The octave switch adds R6 =
0x00 (down / −12), 0x0C (mid), 0x18 (up / +12). KEY TRANSPOSE adds a stored
key-shift (RAM 0x16/0x17) before DAC output
`[community disassembly: joebritt, partly inferred]` [S1].

The service manual maps the Range Selector to Range Data: 16' = 1 V, 8' =
2 V, 4' = 3 V, 2' = 4 V — i.e. **1 V/octave**, with the assembled DAC value
driving the VCO via the VCO S/H `[service-manual, high]` [S2].

Honesty note: a clone should reproduce the **6-bit quantization** so that
portamento glides through quantized, slightly stair-stepped CV rather than
continuous float pitch.

### 4.3 Hardware has no sine LFO / no extra registers (software-only artifacts)

For the avoidance of doubt: the **hardware** SH-101 has **no sine LFO**,
**no 32'/64' registers**, **no external audio input**, and **no MIDI/DCB**.
These appear only in later software emulations (e.g. Roland Cloud SH-01A)
and are **not** part of this firmware's behavior. Range constants in the
firmware cover only 16'/8'/4'/2' as documented above
`[community disassembly: joebritt, partly inferred]` [S1].

## 5. Clock, arpeggiator, and sequencer

### 5.1 Clock edge detection on T1; external clock disconnects the LFO

The clock — internal LFO or external jack — is sensed on the **T1 pin**. A
**high-to-low edge** at T1 advances the arpeggiator and sequencer and
triggers a fresh random value. The disassembly Clock Check (step 6): "Any
variation in the voltage of the Clock signal (LFO or EXT CLK) is detected at
the T1 terminal. If a low clock signal turns high, TR11 inverts it...
Normally the LFO is the CLK. If you plug a cable into the EXT CLK IN jack,
the jack disconnects the LFO and feeds the external signal in." Edge
detection: the T-flag b6 holds the last state and b5 marks the H→L edge for
exactly one loop, on which `update_arp_seq` runs
`[community disassembly: joebritt, partly inferred]` [S1].

Reconciling the two edge descriptions: the **external** clock event is a
**rising** edge (low→high) crossing the **+2.5 V** threshold; TR11 **inverts**
it so the **CPU's T1 pin sees a high-to-low edge** `[service-manual, high]`
[S2] [S1]. Both statements are correct depending on whether you reference the
jack or the T1 pin.

Because the jack switches the LFO out of the clock node, with EXT CLK
connected the front-panel RATE fader **no longer sets the arp/seq tempo** (it
still affects the LFO when used as a modulation source); the
sequencer/arpeggio follow the external pulse `[service-manual, high]` [S2]
[S7]. (This last "RATE only affects LFO mod" consequence is strongly implied
but flagged as an open check — see Section 9.)

### 5.2 CLOCK RESET on keypress

**CLOCK RESET** (asserted on P2 bit P24) restarts the clock/sequence whenever
a key is pressed while in **LFO trigger mode** or **ARPEGGIO mode**. Service
manual: "The Clock Reset signal resets the Clock signal whenever a key on the
keyboard is pressed while either the GATE/TRIG Selector is set to LFO or the
ARPEGGIO mode is activated" `[service-manual, high]` [S2]. The disassembly P2
map lists "P24 CLOCK RESET," computed/cleared in the GATE & LED output
routine (step 13) `[community disassembly: joebritt, partly inferred]` [S1].
Behavioral consequence: arpeggios and LFO-clocked patterns **re-phase** to
the moment of a new keypress.

### 5.3 Random voltage (software S&H)

On each clock H→L edge the CPU also generates a **RANDOM** voltage — a
sample-and-hold of a software pseudo-random value — output via the third DAC
route. Disassembly step 7 "RANDOM DATA OUTPUT" / `update_random`: a running
random byte (RAM 0x13) is updated using ROM-table reads and key-state
entropy, masked to 6 bits, and sent to the DAC with the `10xxxxxx` ("4052
output 2 => Random waveform out") route. It is regenerated on each clock H→L
edge, so its rate tracks the LFO/EXT clock
`[community disassembly: joebritt, partly inferred]` [S1].

### 5.4 Arpeggiator modes

The arpeggiator has three modes: **UP, DOWN, UP&DOWN** — mutually-exclusive
front-panel buttons — clocked by the **same** LFO/EXT-CLK edge as the
sequencer. The service manual specifications list "UP button," "U & D
button," and "DOWN button," and the disassembly P1 input comments label
S10 = UP, S11 = U&D, S12 = DOWN. The CPU PROGRAM section confirms common
clocking: step 6 detects the clock at T1 and prepares both arpeggio and
sequencer data; step 11 (ARPEGGIO) advances on the same edge step 10 (PLAY)
uses `[service-manual, high]` [S2] [S1] [S8].

Honesty note: the disassembly's arpeggiator **direction-selection logic is
sparse** (the author did not fully annotate the mode arithmetic), so while
the three modes are firmly established from the panel/button decode and the
service manual, the **exact step-ordering math for UP&DOWN** (whether the top
and bottom notes repeat or are skipped on direction change) is only partially
traced in the recovered code
`[community disassembly: joebritt, partly inferred]` (open question, Section
9).

### 5.5 The 100-step sequencer

The sequencer holds up to **100 steps**, stored in on-chip RAM from **0x1B to
0x7F** ("RAM loc 1B - 7F is used for 100 sequencer steps")
`[community disassembly: joebritt, partly inferred]` [S1]. The service-manual
CPU PROGRAM flowchart confirms step 9 LOAD ("the CPU stores that information
in the RAM") and step 10 PLAY ("the CPU reads the Sequencer data stored in
the RAM and prepares both the KCV and Gate data") `[service-manual, high]`
[S9].

Each step byte packs a **note value in the low ~6 bits** (matching the 6-bit
DAC range, `anl #3fh`) plus **flag bits in b7/b6** for **REST** and
**TIE/legato (slide via portamento)**. `handle_load` uses R0 = 0x1B as the
base pointer and sets high flag bits via `orl a,#80h` and `orl a,#40h`;
`handle_play` increments the pointer, reads the step, and on a TIE step keeps
the envelope/portamento running [S1].

**There is NO per-step accent.** Accent is a TB-303 / MC-202 feature; the
SH-101 sequencer lacks it. The disassembly contains no accent handling, and
community/spec sources confirm the absence
`[community disassembly: joebritt, partly inferred]` [S1] [S10] [S11]. (This
explicitly corrects the original research draft, which listed "accent" — see
Section 8.)

Honesty note: the **exact per-step bit layout** (which of b7/b6 is REST vs
TIE, how an empty/unused step is marked) is **inferred** from the
instruction structure (`jb7`, `anl a,#7fh`), not from named comments by the
author. Treat the precise bit assignment as **reverse-engineered / probable**
(open question, Section 9).

### 5.6 LEGATO and REST during step entry

A **TIE (legato)** step engages portamento (slide) and holds the envelope
(no re-gate); a **REST** step inserts a gap with the gate off. On the
keyboard, the LEGATO and REST functions are the dual-function of the HOLD and
KEY TRANSPOSE buttons used during step entry. Service manual step 9 (LOAD):
"If a Keyboard key, the LEGATO (HOLD) button or the REST (KEY TRANSPOSE)
button is pressed, the CPU stores that information in the RAM"
`[service-manual, high]` [S9]. `handle_play` branches on a step's high flag
bits to retrigger (non-tie) or sustain (tie), and PORTAMENTO OFF (P23) is set
per step in the GATE & LED routine `[community disassembly: joebritt, partly
inferred]` [S1]; community usage corroborates [S6].

Honesty note: whether a TIE step **strictly** holds the envelope (no re-gate)
versus merely sustains depends on the GATE/TRIG selector and PORTAMENTO mode;
the manual describes the dependency (step 5 KEYBOARD READ) but does **not**
give a single unambiguous sentence that TIE alone holds the envelope. The
functional description rests partly on community usage (residual risk,
Section 8).

## 6. Memory backup

Sequence data **survives power-off** via battery backup. The service-manual
block diagram shows a dedicated "Memory Back-up Battery" line from the
"UM2 × 6" cell pack to the CPU, and step 2 (INITIAL SET) "deletes all the
data ... but does not delete the Sequencer data," confirming sequence
persistence across power cycles `[service-manual, high]` [S9].

The widely-repeated specific — that **only the inner three of the six cells**
(closest to the control panel) are needed, or that leaving the **AC adaptor**
connected also retains memory — is **community/empirical knowledge**
(ModWiggler / Gearspace consensus), **not** stated verbatim in the manual,
which only documents the six-cell pack and that sequencer data survives. It
is consistent with the CMOS 80C49's low standby current
`[community disassembly: joebritt, presumed-equal]` [S7] [S12]. The exact
backup voltage/current is **unconfirmed** (the schematic is image-only;
medium confidence — see Section 8).

## 7. Panel I/O and gate output

### 7.1 Port map

The panel I/O is inverted-logic and multiplexed
`[community disassembly: joebritt, partly inferred]` [S1]:

- **P1 (input port)** reads the keyboard columns and the multiplexed function
  switches (RANGE, OCTAVE, ADSR-mode, HOLD, KEY TRANSPOSE, and the
  UP/U&D/DOWN/PLAY/LOAD buttons); a switch reads as 0 when on.
- **Data BUS** drives DAC bits D0–D5, key-bank/range/ADSR/button commons on
  D4/D5, and the DAC route on D6/D7.
- **P2 (output port)**: P20–P22 LED select (seq/arp), P23 PORTAMENTO OFF,
  P24 CLOCK RESET, P25 HOLD LED, P26 KEY TRANSPOSE LED, P27 GATE.
- **T0** reads the external HOLD (sustain) input; **T1** reads the clock.
- LEDs are driven through a 4556 dual 2→4 decoder (IC8) so only one
  transistor is off at a time.

### 7.2 Gate output level (keep the three "2.5 V" uses separate)

The rear-panel **GATE jack is 0 V OFF / +12 V ON** per the service-manual
specifications ("Gate OFF=0V, ON=12V") `[service-manual, high]` [S2]. Note
that Roland's current web spec lists 10 V; the **service manual is
authoritative** for this project, so we use **12 V**.

There are **three distinct uses of "2.5 V"** in the source material that must
not be conflated:

1. **GATE / EXT-CLK input threshold** = "+2.5 V or more" — an *input*
   threshold, distinct from the gate *output* `[service-manual, high]` [S2].
2. **TEST MODE / D-A calibration** values (PLAY = 2.75 V, ARPEGGIO DOWN =
   2.5 V on KCV; D/A LINEARITY adjustment at 2.5 V) — calibration points,
   **not** a gate-logic reference `[service-manual, high]` [S2].
3. The original research draft's note that "~2.5 V is an internal D/A
   calibration point" tied to gate logic is **not** supported by the manual
   and has been corrected — see Section 8.

## 8. Confidence, disputes & honest labels

This section surfaces, plainly, every disputed / low-confidence item,
verification correction, and residual risk for this dimension.

### 8.1 Corrections applied during verification

- **No per-step accent (refutation).** The original research draft stated the
  sequencer encodes "note value plus flag bits for REST, TIE/legato and
  **accent**." The "accent" element is **refuted**: the SH-101 sequencer has
  **no accent** (accent is a TB-303/MC-202 feature). Confirmed by the absence
  of accent handling in the disassembly and by community/spec sources. The
  accurate statement is: "note value plus REST and TIE/legato (slide via
  portamento) flags only; no accent" [S1] [S10] [S11].
- **Gate level vs the three "2.5 V" values (clarification).** The Gate output
  is OFF = 0 V / ON = 12 V (confirmed). The draft's claim that "~2.5 V is an
  internal D/A calibration point" tied to gate logic is **not** supported;
  2.5 V appears only as the EXT-CLK *input* threshold and as TEST MODE / D/A
  LINEARITY calibration values. These three uses are kept separate
  (Section 7.2) `[service-manual, high]` [S2].
- **Clock edge direction (clarification).** The CPU senses a **high-to-low**
  edge at T1; the *external* event is a rising edge crossing +2.5 V, inverted
  by TR11. Both descriptions are correct depending on reference point
  (Section 5.1) `[service-manual, high]` / `[community disassembly: joebritt,
  partly inferred]` [S1] [S2].

### 8.2 Disputed / corrected community claims

- **GATE-mode priority is LOWEST-note**, not the "high note priority"
  sometimes claimed on forums — corrected per the recovered code and broad
  consensus (Section 3.2) [S1] [S3].

### 8.3 Medium-confidence items

- **Sequencer per-step bit layout** — which high bit is REST vs TIE, how the
  note packs into the low bits — is **medium confidence**, inferred from
  `jb7` / `anl a,#7fh` instruction structure, not from named author comments
  `[community disassembly: joebritt, partly inferred]` [S1].
- **Memory backup "inner 3 of 6 cells"** — medium confidence; community/
  empirical, not a manual quote. Cannot be raised above medium from primary
  sources `[community disassembly: joebritt, presumed-equal]` [S7] [S12].
- **EXT CLK threshold (+2.5 V)** — listed medium in the source key-parameter
  set; the service-manual specification text confirms "+2.5 or more"
  `[service-manual, high]`, so the value itself is solid, the residual
  uncertainty is mainly the OCR/image-read caveat below [S2].
- **Arpeggiator UP&DOWN step-ordering math** — partially traced only; the
  disassembly comments for arp mode arithmetic are sparse (Section 5.4)
  `[community disassembly: joebritt, partly inferred]` [S1].
- **TIE "holds the envelope"** — corroborated by community usage rather than a
  single unambiguous manual sentence; depends on GATE/TRIG + PORTAMENTO
  state (Section 5.6) `[service-manual, high]` + community [S9] [S6].

### 8.4 Residual risks

- The exact bit-level step encoding (REST vs TIE bit, note packing,
  empty/unused-step marker) is **not** documented with named comments in the
  disassembly; it is inferred. Treat as reverse-engineered / inferred, not
  officially specified [S1].
- The "inner 3 of 6 cells nearest the panel" detail is community/empirical
  (ModWiggler, Gearspace), plausible from the battery-pack wiring but not in
  the Roland service manual, which documents only "UM2 × 6" feeding the
  backup line. Cannot exceed medium confidence from primary sources [S7]
  [S12].
- Service-manual PDF **plain-text extraction failed**; findings rely on
  visually-rendered page images (electricdruid.net PDF), legible and
  consistent with the archive.org full text, so confidence is high — but a
  small risk of a misread digit in a voltage table remains [S2] [S13].
- Whether a TIE step **strictly** holds the envelope vs merely sustains
  depends on the GATE/TRIG selector and PORTAMENTO mode; the manual describes
  the dependency but gives no single unambiguous statement [S9].
- The note-priority mapping (GATE = lowest, GATE+TRIG = last, LFO = lowest via
  flag F1) comes from disassembly comments plus community consensus; it was
  **not** independently confirmed against a Roland-authored statement, though
  it is internally consistent with the code [S1].

### 8.5 Open validation gaps (no bench data, project decision)

We have no physical-unit measurements. The following require bench data and
are **open validation gaps**, not delivered facts: per-route DAC
settling/delay budget within the 1.5–3.5 ms loop; how variable loop time
interacts with internal-LFO clocking jitter (swing/jitter modeling);
verification of the 12 V gate-output level-shift stage; and confirmation that
EXT CLK presence leaves the RATE fader controlling only LFO modulation
(not arp/seq tempo) `[theory/inference, unmeasured]`.

## 9. Open questions

1. Exact per-step bit layout: which high bit encodes REST vs TIE/legato
   (b7/b6) — inferred from `orl #80h` / `orl #40h` paths, not named. Needs
   confirmation by stepping the recovered code or matching a hardware
   capture [S1].
2. Precise arpeggiator step-ordering arithmetic, especially UP&DOWN (do top
   and bottom notes repeat or get skipped on direction change?) — disassembly
   comments are sparse [S1].
3. Quantitative memory-backup spec: 80C49 RAM standby voltage/current and
   which exact cells feed it — service-manual OCR surfaced no numbers
   (schematic image-only) [S2].
4. Whether the rear CV OUT jack carries the same per-loop multiplexed value
   as the VCO S/H or is filtered/offset differently — S/H and 4052 circuit
   detail is image-only [S2].
5. Exact DAC settling/delay budget per route within the 1.5–3.5 ms loop
   (`delay2` ≈ 2 `djnz` cycles) and the practical CV update granularity /
   portamento smoothness it sets [S1].
6. How variable loop time (1.5–3.5 ms) interacts with internal-LFO clocking
   jitter for arp/seq timing — needs measurement to model swing/jitter [S2].
7. Reconcile the 12 V GATE jack spec with the internal logic-level gate used
   by the envelope — confirm the level-shift stage [S2].
8. Confirm EXT CLK presence leaves the RATE fader controlling only LFO mod
   depth/rate (not arp/seq tempo) — strongly implied by "jack disconnects the
   LFO," worth a hardware check [S1] [S2] [S7].

## 10. Key parameters

| Name | Value | Unit | Confidence | Source |
| ---- | ----- | ---- | ---------- | ------ |
| CPU | Toshiba TMP80C49P-6-7301 (CMOS 8049-class, MCS-48) | part | high | Service manual parts list / disassembly header [S1] [S2] |
| CPU clock | 6 | MHz (ceramic resonator) | high | Service manual parts list "Ceramic resonator CSA 6MHz" + disassembly [S1] [S2] |
| On-chip ROM | 2048 | bytes (mask ROM; dump = 2 KB) | high | joebritt/SH101_Disassembled (binary size, 8049 spec) [S1] |
| On-chip RAM | 128 | bytes (0x00–0x7F) | high | joebritt/SH101_Disassembled RAM map (8049 spec) [S1] |
| Sequencer step storage | 0x1B–0x7F (100 steps) | RAM address range | high | joebritt/SH101_Disassembled [S1] |
| Main loop interval | 1.5 to 3.5 | ms per loop (steps 3–13) | high | Service manual CPU PROGRAM section [S2] |
| DAC resolution | 6 | bits (D0–D5; D6/D7 = route select) | high | joebritt/SH101_Disassembled DATA OUTPUT map [S1] |
| DAC mux routes | 00=CV OUT, 01=VCO, 10=RANDOM, 11=unused | D7:D6 select (via 4052) | high | joebritt/SH101_Disassembled [S1] |
| Pitch scaling | 1 | V/octave (16'=1V,8'=2V,4'=3V,2'=4V; ranges 12 counts apart) | high | Service manual Range Data table + disassembly constants [S1] [S2] |
| GATE output level | OFF = 0, ON = 12 (rear GATE jack) | V | high | Service manual specifications ("Gate OFF=0V, ON=12V") [S2] |
| EXT CLK trigger threshold | +2.5 or more | V (rising edge inverted to T1) | medium | Service manual / owner's manual EXT CLK IN spec [S2] |
| Clock edge sensed | High-to-low (1-loop edge flag) on T1 | edge | high | joebritt/SH101_Disassembled Clock Check [S1] |
| Note-priority selector | GATE = lowest; GATE+TRIG = last; LFO = lowest | mode (S7 / flag F1) | high | joebritt/SH101_Disassembled keyboard_read + community consensus [S1] [S3] |
| Key matrix | 4 banks × 8 keys (32 keys); active-low, 50k pullups | matrix | high | joebritt/SH101_Disassembled + service manual ("4×8 matrix") [S1] [S2] |
| Arpeggiator modes | UP, DOWN, UP&DOWN | modes (mutually exclusive) | high | joebritt/SH101_Disassembled button/LED decode + service manual [S1] [S2] |
| Memory backup | inner 3 of 6 cells, or AC adaptor left connected | battery | medium | Community (ModWiggler / VSE); service-manual block diagram "Memory Back-up Battery" [S7] [S12] |

## 11. Design implications for mwAudio101

- **Model the control core as a fixed-order polling loop**, not an event/
  interrupt system: range read → range/CV DAC → keyboard scan → clock check →
  random → function-switch read → load/play/arp → CV out → gate/LED, running
  every 1.5–3.5 ms. A clone may run a faster fixed tick, but to capture the
  original feel, emulate the **variable, relatively coarse update rate** — it
  affects portamento stepping and CV/gate timing [S1] [S2].
- **Implement a single virtual "DAC + analog mux + sample-and-hold"** that is
  time-multiplexed across VCO CV, CV OUT, and RANDOM, rather than independent
  CV generators. The time-sharing, the parked `11xxxxxx` idle state, and the
  fact that the random/S&H rate is tied to the clock edge are all part of the
  behavior [S1].
- **Make note priority a single switch** (GATE / GATE+TRIG) that simultaneously
  sets (a) envelope retrigger and (b) priority rule: GATE ⇒ lowest-note + no
  legato retrigger; GATE+TRIG ⇒ last-note + retrigger every key; LFO ⇒
  lowest-note + envelope retriggered by clock. Do **not** treat priority and
  trigger as independent parameters [S1].
- **Use 6-bit pitch quantization at 1 V/oct** with range constants exactly 12
  counts apart and additive octave/key-transpose offsets, so the clone
  reproduces the original's quantized, slightly stair-stepped CV (especially
  audible through portamento) rather than continuous float pitch [S1] [S2].
- **Sequencer:** store 100 steps as note (6-bit) + flag bits for **REST** and
  **TIE/legato (slide)** — **no accent**. A TIE step sustains the envelope and
  engages portamento; a REST drops the gate. PLAY advances on each clock H→L
  edge; LOAD writes steps with the LEGATO/REST buttons setting the flags.
  Provide **battery-backed (persisted)** storage to match save-between-
  sessions behavior [S1] [S9].
- **Clock:** derive arp/seq advance from an H→L edge detector on a clock node
  fed by either the internal LFO or an external input; when an external clock
  is patched, route it in place of the LFO so RATE stops affecting tempo
  (it then only affects the LFO as a mod source). Assert **CLOCK RESET**
  (restart sequence/arp phase) on any keypress while in LFO-trigger or
  ARPEGGIO mode [S1] [S2].
- **Expose a TEST MODE** entry (LOAD + KEY TRANSPOSE held at power-up)
  producing the fixed calibration CVs/gates seen in the disassembly — useful
  for clone self-test and DAC/tuning calibration [S1] [S2].
- **Replicate the inverted-logic, multiplexed panel I/O conceptually**
  (active-low switch reads, one-transistor-off LED multiplexing,
  per-loop XOR-of-changed-bits debounce) at the model boundary so button/
  keypress latching matches the original [S1].

## 12. References

- [S1] Joe Britt — SH-101 commented disassembly (ROM dumped from hardware):
  <https://github.com/joebritt/SH101_Disassembled> ;
  source listing: <https://raw.githubusercontent.com/joebritt/SH101_Disassembled/master/SH101.asm>
- [S2] Roland SH-101 Service Manual (archive.org full text):
  <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
- [S3] Wikipedia — Roland SH-101: <https://en.wikipedia.org/wiki/Roland_SH-101>
- [S4] Vintage Synth Explorer forum thread (note priority):
  <https://forum.vintagesynth.com/viewtopic.php?t=39338>
- [S5] KVR Audio forum (gate/trigger behavior):
  <https://www.kvraudio.com/forum/viewtopic.php?t=543399>
- [S6] Vintage Synth Explorer forum (legato/trigger):
  <https://forum.vintagesynth.com/viewtopic.php?t=36010>
- [S7] KVR Audio forum (EXT CLK / RATE behavior):
  <https://www.kvraudio.com/forum/viewtopic.php?t=249250>
- [S8] Vintage Synth Explorer — Roland SH-101 (arpeggiator patterns):
  <https://www.vintagesynth.com/roland/sh-101>
- [S9] Roland SH-101 Service Manual (electricdruid.net rendered PDF):
  <https://electricdruid.net/wp-content/uploads/2025/05/SH101-Service-Manual.pdf>
- [S10] Vintage Synth Explorer forum (sequencer / no accent):
  <https://forum.vintagesynth.com/viewtopic.php?t=39466>
- [S11] Tubbutec SH-1oh1 modification (sequencer features):
  <https://tubbutec.de/sh-1oh1/>
- [S12] ModWiggler forum (battery backup / sequencer):
  <https://www.modwiggler.com/forum/viewtopic.php?t=114709>
- [S13] Gearspace — SH-101 sequencer workflow:
  <https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1223753-sh101-sequencer-workflow.html>
