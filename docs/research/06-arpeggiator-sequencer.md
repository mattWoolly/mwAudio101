<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Arpeggiator & 100-Step Sequencer

## 1. Scope and summary

This document is the source-of-truth research record for the Roland SH-101
arpeggiator and built-in 100-step step sequencer, written for the
circuit-accurate mwAudio101 project. It covers the three arpeggiator modes
(UP / U&D / DOWN) and HOLD latch, the 100-step sequencer with rest and tie
entry, the shared internal LFO/CLK clock and EXT CLK IN external clocking, and
the firmware-level clock-reset-on-keypress behavior. Other sections of the
project (architecture, backlog) reference this document by section number.

Both the arpeggiator and the sequencer are implemented entirely in firmware on
a single microcontroller, the Toshiba TMP80C49P (mask-ROM program
`80C49-6-7301`), which is also the key-assigner, gate/trigger generator,
transpose handler and clock detector; there is no analog sequencing
[service-manual + community disassembly: joebritt, high] [S1][S2][S5]. In the
project's frozen schematic reconciliation this CPU is IC6. The arpeggiator has
three latching panel-button modes (UP, U&D up-and-down, DOWN) plus a HOLD latch
(panel button and DP-2 pedal) and has no dedicated octave-range control
[owner's-manual + service-manual, high] [S1][S2]. The sequencer stores up to
100 steps entered from the SH-101's own keyboard, where each note, each REST,
and each tie/long-note extension consumes one step [owner's-manual, high] [S1].
Both engines advance one step per clock pulse from the shared LFO/CLK generator
(0.1-30 Hz) or, when a plug is inserted into EXT CLK IN, from an external clock
at one note per pulse, with the internal clock connection cut [owner's-manual +
service-manual, high] [S1][S2]. A Clock-Reset signal re-phases the clock on any
keypress while in LFO or arpeggio mode [service-manual + community disassembly:
joebritt, high] [S2][S5].

The exact per-step RAM byte format (note / rest / tie / accent bit fields) is
NOT in any Roland document; it is known only from a third-party reverse
engineering disassembly and is therefore labeled throughout as
[community disassembly: joebritt, partly inferred] [S5].

## 2. Arpeggiator

### 2.1 Modes (UP / U&D / DOWN)

The SH-101 arpeggiator has exactly three modes selected by mutually-exclusive
latching panel buttons, each with its own indicator LED: UP, U&D (up-and-down)
and DOWN [owner's-manual + service-manual, high] [S1][S2]. The owner's manual
(p.32) labels them "(1) UP, (2) U&D, (3) DOWN — These buttons are to determine
the Arpeggio pattern," and instructs: "Press any one of the UP, U&D, DOWN
buttons (the indicator lights up), then press a chord, and Arpeggio patterns
will be played" [owner's-manual, high] [S1]. The Specifications page (p.56)
lists "UP button and indicator / U & D button and indicator / DOWN button and
indicator," and the service-manual CPU program confirms these as ARPEGGIO UP /
ARPEGGIO U&D / ARPEGGIO DOWN function buttons [owner's-manual + service-manual,
high] [S1][S2].

The community disassembly corroborates the panel-button decode as the
mutually-exclusive function buttons `D42 UP`, `D43 UP & DOWN`, `D44 DOWN` (plus
`D45 PLAY`, `D46 LOAD`) [community disassembly: joebritt, high] [S5]. However,
the disassembly author did not fully annotate the U&D step-ordering arithmetic;
the three modes are firmly established, but the exact U&D cycling math (whether
the top and bottom notes repeat at the turnaround) is only partially traced in
the recovered code [community disassembly: joebritt, partly inferred] [S5]. The
precise U&D turnaround repeat behavior is therefore an open question
(see Section 8).

### 2.2 No octave-range control

The arpeggiator has NO dedicated octave-range setting. Held notes are
arpeggiated exactly as played; there is no automatic octave expansion
[owner's-manual + service-manual, high] [S1][S2]. The CPU prepares KCV data
"according to the order of the key numbers stored in the 4-byte (32 keys)
Arpeggio Key Buffer," i.e. it cycles exactly the keys held [service-manual,
high] [S2]. The only pitch-shift influence is the global TRANSPOSE (L/M/H)
switch (one octave up/down) [owner's-manual, high] [S1]. Note that while the
built-in sequencer is running the TRANSPOSE switch does not function, although
KEY TRANSPOSE can transpose a running sequence [owner's-manual, high] [S1].

### 2.3 HOLD latch and engagement

The arpeggiator only runs while keys are held, unless HOLD is engaged
[owner's-manual, high] [S1]. The owner's manual (p.32) states: "An Arpeggio can
only play while the keys are being held down, unless the HOLD button is
pressed," and "If you press the Hold button while an Arpeggio is being played,
it will continue to be played even after the keys are released. In this
condition, if you press a new chord, a new Arpeggio pattern will be played"
[owner's-manual, high] [S1]. A DP-2 pedal can also toggle HOLD (p.39), and the
CPU detects HOLD at both the panel button and the pedal switch [owner's-manual +
service-manual, high] [S1][S2]. In the disassembly, HOLD state is tracked in the
T-flags byte (bit 4), and the external HOLD/sustain pedal input is read on the
T0 pin [community disassembly: joebritt, high] [S5].

Automatic arpeggio is engaged only by pressing a chord; pressing a single tone
non-legato gives normal playing, so arpeggio versus normal play depends on how
the keyboard is played [owner's-manual, high] [S1]. The owner's manual cautions:
"If you fail to press each key of the chord at precisely the same moment, the
first pattern of the Arpeggio may prove imperfect" [owner's-manual, high] [S1].

### 2.4 Arpeggio Key Buffer

The held keys are stored in a 4-byte / 32-key Arpeggio Key Buffer
[service-manual, high] [S2]. Service-manual CPU program step 11 states: "the CPU
prepares the KCV data according to the order of the key numbers stored in the
4-byte (32 keys) Arpeggio Key Buffer, then jumps to step 12"
[service-manual, high] [S2]. This is a 32-bit bitmap (4 bytes x 8 bits, one flag
per keyboard key), NOT a 4-note polyphony limit — see the residual risk in
Section 7 [service-manual, high] [S2].

## 3. 100-step sequencer

### 3.1 Capacity and transport

The built-in digital sequencer stores and plays a maximum of 100 steps
[owner's-manual, high] [S1]. The owner's manual states (p.33): "The SH-101
contains a digital sequencer which can store and play up to 100 steps," the
Specifications page (p.56) lists "Sequencer (100 steps max.)," and the service
notes specifications agree [owner's-manual + service-manual, high] [S1][S2].

LOAD and PLAY are the only two transport controls: "LOAD button: Press this
button when you wish to store notes, and press it again to stop storing. PLAY
button: Press this button when you wish to play the stored notes, and press it
again to stop playing" [owner's-manual, high] [S1]. Sequencer data can be loaded
ONLY from the SH-101's own keyboard: "You can load into the built-in sequencer
only from the keyboard of the SH-101. Loading from any other unit is impossible"
[owner's-manual, high] [S1].

The sequencer loops: "When the last note is played, it will go back to the
beginning of the piece and be repeated until you press the PLAY button again."
If all 100 steps fill during LOAD, the unit auto-exits LOAD: "If all 100 steps
are stored, the SH-101 will return to normal playing condition, even if the
sequencer is in LOAD mode" [owner's-manual, high] [S1].

### 3.2 Step model: notes, rests, ties

Notes, rests and ties are each step-quantized. The shortest time value counts as
one step, and longer values cost more steps [owner's-manual, high] [S1]:

- A keyboard note in LOAD mode stores only its pitch; "No matter how you play,
  the time values will turn out the same" [owner's-manual, high] [S1].
- A REST is entered with the REST button (which is the KEY TRANSPOSE button, and
  "works only in LOAD mode"): "Press the REST button, the shortest rest will be
  memorized" — one step [owner's-manual, high] [S1].
- A TIE/slur is entered by playing the next note while holding the LEGATO (HOLD)
  button: "Load the first note, then press the next note while holding the
  LEGATO button down" [owner's-manual, high] [S1].
- Longer note values are built from multiple shortest-value steps: "When you
  wish to load the notes, divide the longer time values by the shortest time
  value... the shortest time value is counted as one step, and the longer time
  values cost more steps" [owner's-manual, high] [S1].

### 3.3 Articulation (no per-step gate-time)

There is NO independent per-step gate-time parameter. Articulation is determined
by stored LEGATO ties plus the ENV section's GATE/TRIG selector
[owner's-manual, high] [S1]. The owner's manual states: "All are played in
non-legato except for those stored in a legato manner," and "When storing slurs,
remember to set the GATE/TRIG selector switch in the Envelope Generator to the
GATE position before playing" [owner's-manual, high] [S1]. The envelope retrigger
behavior is set by the ENV GATE/TRIG switch (three positions: GATE+TRIG / GATE /
LFO) — see Section 5 [owner's-manual, high] [S1].

### 3.4 Persistence across power cycles

Sequencer data is held in battery-backed RAM and survives an Initial Set that
clears other RAM [service-manual, high] [S2]. Service-manual CPU program step 2
(INITIAL SET): "This operation deletes all the data that is stored in the
built-in RAM, such as Keyboard and switch mode data, but does not delete the
Sequencer data," and the block diagram shows a "Memory Back-up Battery" feeding
the CPU/RAM [service-manual, high] [S2]. Community/owner sources add that only
the three batteries nearest the control panel (of the six cells), or leaving the
AC adaptor connected, are needed to retain memory; this is consistent with the
CMOS 80C49's low standby current [community: modwiggler/vintagesynth, medium]
[S6][S7]. The exact backup voltage/standby-current spec was not surfaced in the
OCR'd service notes and is unconfirmed [theory/inference, unmeasured] [S2].

## 4. Clocking

### 4.1 Internal LFO/CLK clock (shared)

Both the arpeggiator and sequencer tempo are set by the shared LFO/CLK
generator, range 0.1 Hz to 30 Hz, via the LFO/CLK RATE knob [owner's-manual +
service-manual, high] [S1][S2]. The owner's manual states for arpeggio
(p.32): "LFO/CLK RATE — This knob determines the tempo of an Arpeggio," and for
the sequencer (p.33): "The tempo of the sequencer can be controlled by the
LFO/CLK RATE in the Modulator section" [owner's-manual, high] [S1]. The
specifications list "Modulator — LFO/CLK RATE (0.1Hz ~ 30Hz)," and the block
diagram shows a single LFO/CLK block feeding both the modulator and the CPU
clock-check (SYNC) path [owner's-manual + service-manual, high] [S1][S2].

The mapping (taper/curve) between the LFO/CLK RATE knob position and the actual
clock frequency within the 0.1-30 Hz range is NOT published; only the endpoints
are specified [open question, see Section 8] [S1][S2].

### 4.2 External clock (EXT CLK IN)

External clock sync is via EXT CLK IN at one step per pulse ("1 note per 1
pulse") [owner's-manual, high] [S1]. The owner's manual (p.38) states: "If you
connect an external unit to the CLK IN jack of the SH-101, the Arpeggio playing
or built-in sequencer of the SH-101 will synchronize with the external unit,"
with the diagram annotation "1 note per 1 pulse" [owner's-manual, high] [S1].
Inserting a plug cuts the internal clock and removes the panel RATE knob from the
tempo path: "As soon as you connect a plug to this jack, internal connections of
built-in clocks are cut. The LFO/CLK RATE Knob ... controls only the rate of the
LFO," and "When the external unit is connected to the EXT CLK IN jack, the
Arpeggio RATE Knob on the Front Panel does not function"
[owner's-manual, high] [S1].

The input threshold is "+2.5V or more" [owner's-manual + service-manual, high]
[S1][S2]. The disassembly confirms the physical mechanism: the clock (LFO or
EXT CLK) is sensed at the T1 pin, and "If you plug a cable into the EXT CLK IN
jack, the jack disconnects the LFO and feeds the external signal in"; the
front-panel RATE fader then no longer sets arp/seq tempo but still affects the
LFO used as a modulation source [community disassembly: joebritt, high] [S5].
Whether the external clock input has any internal debounce or minimum-pulse-width
requirement beyond the +2.5V threshold is not documented [open question, see
Section 8] [S1][S2].

Recommended external clock sources listed in the manual include CR-8000/5000
TRIGGER OUT; DR-55 DBS/CSQ; TR-606/808 TRIGGER OUT; TB-303 and CSQ-600 GATE OUT;
and MC-4 GATE OUT / MPX OUT [owner's-manual, high] [S1].

### 4.3 Clock-reset on keypress

A Clock-Reset signal re-phases the clock whenever a key is pressed while in LFO
trigger mode or arpeggio mode [service-manual + community disassembly: joebritt,
high] [S2][S5]. The service manual states: "The Clock Reset signal resets the
Clock signal whenever a key on the keyboard is pressed while either the
GATE/TRIG Selector is set to LFO or the ARPEGGIO mode is activated"
[service-manual, high] [S2]. The disassembly maps this to output-port pin
`P24 CLOCK RESET`, computed in the GATE & LED output routine; the audible
consequence is that arpeggios and LFO-clocked patterns lock rhythmically to the
moment of a new keypress [community disassembly: joebritt, high] [S5].

## 5. Gate / trigger interaction

The articulation and envelope-retrigger behavior of both the arpeggiator and
sequencer is governed by the ENV GATE/TRIG selector, which has three positions:
GATE+TRIG / GATE / LFO [owner's-manual + service-manual, high] [S1][S2].

- GATE+TRIG: retriggers the envelopes on every new key (every step)
  [community: kvraudio/vintagesynth, high] [S8][S9].
- GATE: a legato keypress does NOT retrigger the AMP/filter envelope
  (mono-legato); a single held gate sustains [community + disassembly, high]
  [S5][S8].
- LFO: the envelopes are (re)triggered by the LFO/clock instead of the keyboard,
  repeating at the clock rate [owner's-manual + disassembly, high] [S1][S5].

This GATE/TRIG selector is intrinsically coupled to monophonic note priority
(one physical control, two behaviors): in GATE mode the priority is lowest-note,
in GATE+TRIG it is last-note, and LFO-trigger mode also uses lowest-note priority
[community disassembly: joebritt, high] [S5]. The disassembly's `keyboard_read`
comment states: "GATE: lower note priority; GATE + TRIG: last note priority;
LFO: lower note priority," which CORRECTS an earlier forum claim of "high note
priority in gate mode" [community disassembly: joebritt, high — corrects
forum claim] [S5][S10].

The gate output drives 0 V when OFF. The ON level is a documented spec conflict
between Roland's own primary sources (see Section 7): the owner's manual and
official Roland/Sweetwater specs say 10 V at 100 kohm load, while the 1982
service notes say 12 V [owner's-manual + service-manual, conflicting] [S1][S2]
[S3][S4]. Per the project's frozen facts, the gate-output ON level is taken as
12 V per the service manual (note Roland's current web spec says 10 V; the
service manual is treated as authoritative for this project).

## 6. Firmware / CPU model

The arpeggiator, sequencer, key-assigner, gate/trigger generation, transpose and
clock detection are all handled in firmware by one CPU: Toshiba TMP80C49P
(program ROM mask `80C49-6-7301`, Roland p/n 15229810), Toshiba's CMOS version
of the Intel MCS-48 8049 (2 KB internal ROM, 128 bytes internal RAM), clocked by
a 6 MHz ceramic resonator [service-manual + community disassembly: joebritt,
high] [S2][S5]. This is IC6 in the project's frozen schematic reconciliation.
The dumped ROM image (`80C49_SH101.bin`) is 2048 bytes, consistent with the
on-chip 2 KB mask ROM [community disassembly: joebritt, high] [S5].

### 6.1 Polling super-loop

The firmware is a single polling super-loop with no interrupts; it does not use
the 8049 hardware Timer/Counter (the T register is repurposed as a byte of status
flags) [community disassembly: joebritt, high] [S5]. The disassembly header
notes "No external interrupt" and "Does not use START or STOP instructions! This
means Timer/Counter function is not used. Uses T as another register"; the
"T flags" byte encodes bit 6 = current LFO/clock state, bit 5 = LFO H→L edge
(set for one loop), bit 4 = HOLD state, bits 2/1 = scanner/function-read flags
[community disassembly: joebritt, high] [S5].

The main loop runs every 1.5 to 3.5 ms and executes a fixed sequence of numbered
steps, then jumps back to step 3 [service-manual + community disassembly:
joebritt, high] [S2][S5]. The service manual states: "Steps 3 through 13 are a
series of program steps that are sequentially executed by the CPU at 1.5 to
3.5msec intervals" [service-manual, high] [S2]. The labeled steps are:
1 Test Mode, 2 Initial Set, 3 Range Data Read, 4 Range Data Output,
5 Keyboard Read, 6 Clock Check, 7 Random Data Output, 8 Function Switch Read,
9 Load, 10 Play, 11 Arpeggio, 12 CV Output, 13 Gate & LED Data Output
[community disassembly: joebritt, high] [S5]. The variable loop time means the
CV/gate update rate (and thus effective scan granularity) is not perfectly
constant [community disassembly: joebritt, high] [S5].

### 6.2 Sequencer storage in RAM (byte format)

The 100-step sequencer is stored in the CPU's on-chip RAM. No discrete RAM IC
appears in the service-notes parts list — the complete IC list contains no
SRAM/DRAM chip — so the sequencer data lives in the 80C49's 128 bytes of
internal RAM [service-manual, high] [S2]. The service notes refer only generically
to the "built-in RAM" (step 2 INITIAL SET, step 10 PLAY) and publish NO
byte/bit-per-step format anywhere [service-manual, high] [S2].

The per-step byte format is known ONLY from the community disassembly and is
labeled [community disassembly: joebritt, partly inferred]: the disassembly RAM
map states "RAM loc 1B - 7F is used for 100 sequencer steps." `handle_load` uses
R0 = `0x1B` as the base of the step data and stores the computed note (key plus
key-shift) into successive locations, setting high flag bits via `orl a,#80h`
and `orl a,#40h` — these correspond to the TIE/legato (slide/portamento) and
REST markers — while the step note occupies the low ~6 bits (`anl #3fh`),
matching the 6-bit DAC range. `handle_play` increments the `0x1B` pointer, reads
the step, and on a TIE step keeps the envelope/portamento running. The exact bit
assignments (which of bit 7 / bit 6 is REST vs TIE vs accent) are inferred from
code structure and not all explicitly named, so the per-bit mapping is
community-recovered and probable, NOT authoritative
[community disassembly: joebritt, partly inferred] [S5][S6][S11].

### 6.3 Clock and random on the clock edge

The clock (LFO or external) is sensed on the T1 pin; a high-to-low edge advances
the arpeggiator and sequencer and triggers a fresh random value
[community disassembly: joebritt, high] [S5]. In the T-flags byte, bit 6 holds
the last clock state and bit 5 marks the H→L edge for exactly one loop; on that
edge `update_arp_seq` runs [community disassembly: joebritt, high] [S5]. On the
same edge the CPU also generates a RANDOM voltage: step 7 RANDOM DATA OUTPUT and
`update_random` update a running random byte (RAM `0x13`) masked to 6 bits and
send it to the DAC via the "10xxxxxx → 4052 output 2 → Random" route, so its
rate tracks the LFO/EXT clock [community disassembly: joebritt, high] [S5].
(Note: this random source is part of the LFO/modulation behavior and is included
here only because it shares the clock edge that drives the arp/seq.)

### 6.4 CV path shared with keyboard

A single 6-bit DAC is time-multiplexed via a 4052 analog mux into three
sample-and-hold destinations — CV OUT, VCO CV and the RANDOM voltage — selected
by DAC data bits D6/D7 (`00`=CV OUT, `01`=VCO, `10`=RANDOM, `11`=unused)
[community disassembly: joebritt, high] [S5]. Pitch CV is built by adding range,
octave-transpose and key-shift offsets to the key number, with ranges spaced 12
DAC counts (one octave) apart, giving 1 V/octave [community disassembly:
joebritt + service-manual, high] [S2][S5]. Because the arp/seq drive the same
1 V/oct KCV (0.415-5 V) and the same gate/trigger logic as the keyboard, the
gate generator and envelope retrigger logic are downstream of and shared by
keyboard, arp and sequencer [owner's-manual + community disassembly: joebritt,
high] [S1][S5]. The 4052/S&H part identity itself is disassembly-derived (the
service-manual circuit detail is image-only and not in OCR text)
[community disassembly: joebritt, high] [S5].

## 7. Key parameters

| Name | Value | Unit | Confidence | Source |
|------|-------|------|------------|--------|
| Sequencer maximum step capacity | 100 | steps | high | Owner's Manual p.33 & p.2; Specs p.56; Service Notes [S1][S2] |
| LFO/CLK RATE range (arp + seq tempo) | 0.1 to 30 | Hz | high | Specs p.56; Service Notes [S1][S2] |
| External clock division | 1 | step per pulse (1 note/1 pulse) | high | Owner's Manual p.38 [S1] |
| EXT CLK IN trigger threshold | +2.5 or more | V | high | Specs p.56; Service Notes [S1][S2] |
| Arpeggio modes | 3 (UP, U&D, DOWN) | modes | high | Owner's Manual p.32; Specs p.56 [S1] |
| Arpeggiator dedicated octave-range control | none (octave only via global TRANSPOSE L/M/H) | n/a | high | Owner's Manual p.31/p.32; Specs p.56 [S1] |
| Arpeggio Key Buffer size | 4 bytes / 32 keys (bitmap) | keys buffered | high | Service Manual CPU step 11 [S2] |
| Rest entry cost | 1 (shortest rest) | step per REST press | high | Owner's Manual p.34 [S1] |
| Tie/slur entry | play next note holding LEGATO; longer notes +1 step each | steps | high | Owner's Manual p.33-34 [S1] |
| Gate/Trigger selector positions | 3 (GATE+TRIG / GATE / LFO) | positions | high | Specs p.56; Owner's Manual p.28-29 [S1][S2] |
| Gate output level | OFF=0 V; ON=10 V (manual/Roland) vs 12 V (service notes) | V | medium (conflict) | Owner's Manual p.56; Service Notes; Roland; Sweetwater [S1][S2][S3][S4] |
| CV output scaling (arp/seq KCV) | 1 V/oct, 0.415 to 5 | V | high | Specs p.56; Service Notes; Roland [S1][S2][S3] |
| CPU firmware step-loop interval | 1.5 to 3.5 | ms per loop (steps 3-13) | high | Service Notes CPU Program [S2] |
| Controller CPU | Toshiba TMP80C49P (`80C49-6-7301`; p/n 15229810) | 8049-family MCU | high | Service Notes; Block Diagram; ElectricDruid [S2][S5][S12] |
| Sequencer RAM region (byte format) | on-chip RAM `0x1B`-`0x7F`, ~6-bit note + flag bits | RAM bytes | medium | Community disassembly (joebritt) [S5] |

## 8. Confidence, disputes & honest labels

This section surfaces every disputed, low-confidence or inferred item and every
residual risk for this dimension, stated plainly. These MUST NOT be presented
elsewhere as settled facts.

### 8.1 Disputed / conflicting

- **Gate ON voltage is a real conflict in Roland's own primary documents.** The
  owner's manual, the official Roland technical-specifications page and the
  Sweetwater mirror all say "ON = 10 V at 100 kohm load," while the 1982 service
  notes say "ON = 12 V" [owner's-manual + service-manual, conflicting]
  [S1][S2][S3][S4]. OFF = 0 V is unambiguous across all sources. Per the
  project's frozen decision the ON level is taken as 12 V (service manual
  authoritative); the discrepancy must be carried, not hidden. The likely
  explanation (open-state swing toward a ~12-15 V rail versus the 100 kohm-loaded
  value) is [theory/inference, unmeasured].

### 8.2 Community-derived / partly inferred (not Roland-documented)

- **Per-step byte format.** Which bit is REST, which is TIE/legato, and how the
  note number is packed into the low bits is NOT in any Roland document. It
  exists only as a third-party reverse-engineering inference where the
  disassembler explicitly notes the rest-vs-tie-vs-legato encoding is inferred
  [community disassembly: joebritt, partly inferred] [S5][S6][S11]. The
  finding-level confidence on this is medium. Treat any specific bit assignment
  as community-recovered/uncertain.

- **Sequencer RAM location.** "On-chip RAM `0x1B`-`0x7F`" comes from the
  disassembly; the service manual only confirms there is NO external RAM IC and
  refers generically to "built-in RAM" [community disassembly: joebritt +
  service-manual absence-of-evidence, medium] [S2][S5]. Whether storage is
  on-chip versus an undocumented external IC is, strictly from Roland docs, an
  inference (no dedicated external sequencer RAM appears in the parts list) and
  was flagged as needing PCB-level confirmation.

- **U&D step-ordering math.** The three arpeggiator modes are firmly established,
  but the exact U&D turnaround behavior (whether top/bottom notes repeat) is only
  partially traced in the recovered code [community disassembly: joebritt, partly
  inferred] [S5].

- **Battery-backup detail.** The "only the inner three of six cells / AC adaptor
  retains memory" detail is community-sourced; the exact backup voltage/standby
  current is not in the OCR'd service notes [community, medium; theory/inference,
  unmeasured for the numeric spec] [S2][S6][S7].

### 8.3 Misreading guards (residual risks)

- The "4-byte (32 keys) Arpeggio Key Buffer" is a 32-key held-note bitmap (one
  bit per keyboard key), NOT a 4-note polyphony limit. Do not misread it as
  4-note polyphony [service-manual, high] [S2].

- Note priority in GATE mode is LOWEST-note, not high-note; an earlier forum
  claim of "high note priority in gate mode" is corrected by the recovered code
  and community consensus [community disassembly: joebritt — corrects forum
  claim, high] [S5][S10].

### 8.4 Undocumented / open mappings

- The LFO/CLK RATE knob-to-frequency taper/curve within 0.1-30 Hz is not given
  numerically; only the endpoints are specified [open, S1][S2].

- Any internal debounce/minimum-pulse-width on EXT CLK IN beyond the +2.5 V
  threshold is undocumented [open, S1][S2].

### 8.5 Open validation gaps (no physical-unit measurements)

The project has made the decision that no physical-unit (bench) measurements are
available. Therefore the following are OPEN VALIDATION GAPS, not delivered facts:
the actual loaded versus open-circuit gate ON voltage; the real LFO/CLK
RATE-to-Hz taper; the actual EXT CLK pulse-width tolerance; and any
oscilloscope-confirmed timing of the 1.5-3.5 ms loop and clock-reset re-phasing.
These require bench data the project does not have.

### 8.6 Source caveats

- Only the 8-page "Service Notes" (1 Nov 1982) was located and OCR'd verbatim;
  references to a longer "Service Manual" appear to be the SAME document. No
  separate, more detailed Roland manual documenting the sequencer data structure
  was found [S2][S5].

- Owner's-manual page numbers (p.31-34, p.38, p.56) were confirmed via internal
  cross-references rather than by rendering every page; minor page-number drift
  between editions/printings is possible [S1].

### 8.7 Out-of-scope / software-only artifacts

For honesty against later software emulations: the SH-101 hardware sequencer is
real and on-CPU as documented above. Features such as MIDI/DCB sync, pattern
storage beyond the single 100-step buffer, or any expanded sequencer
functionality that appears only in later software (e.g. Roland Cloud SH-01A) are
software-emulation artifacts and are NOT part of the original hardware behavior
described here. mwAudio101 should model the hardware behavior; any modern
convenience features must be labeled as additions, not SH-101 fidelity.

## 9. Design implications for mwAudio101

Model the arpeggiator and sequencer as a single firmware/state-machine module
that mirrors the SH-101's one-CPU design (IC6, TMP80C49P) and shares one clock
source. Specifically:

1. **Clock.** Implement a clock generator spanning 0.1-30 Hz mapped to the
   LFO/CLK RATE control, plus an external-clock path that, when active, cuts the
   internal clock from the tempo node and advances exactly one step per rising
   edge (1 note/pulse, threshold ~+2.5 V if emulating CV input). With EXT CLK
   active the internal RATE control must affect only LFO modulation, not tempo.
   Implement a Clock-Reset that re-phases the clock on any new keypress when in
   LFO or arpeggio mode — this is audible, musically load-bearing behavior
   [S1][S2][S5].

2. **Arpeggiator.** Three modes (UP, U&D, DOWN); HOLD latch from both panel and
   pedal; buffer up to 32 held keys; cycle them with NO automatic octave
   expansion (octave only via a global transpose). Arp engages only on chord /
   legato detection; a single non-legato note plays normally. Treat the exact
   U&D turnaround repeat behavior as an implementation choice to be validated,
   since the original code is only partially traced [S1][S2][S5].

3. **Sequencer.** A 100-step buffer where one step = one slot; a note, a rest,
   and each extension of a tied/long note each consume one slot. Only the
   instrument's own keyboard writes steps (LOAD toggles record; PLAY toggles loop
   playback that wraps at the end; auto-exit LOAD at 100 steps). Persist contents
   across power cycles (battery-backed semantics). Leave the exact RAM step
   byte/bit encoding as an internal implementation choice, since the original
   format is undocumented by Roland and only community-inferred [S1][S2][S5].

4. **Articulation, not per-step gate-time.** Do NOT add a per-step gate-time
   value. Model articulation as the combination of stored LEGATO ties plus the
   global GATE/TRIG mode: GATE+TRIG retriggers the envelope every step; GATE
   retriggers only across non-legato transitions (mono-legato); LFO repeats the
   envelope at the clock rate. Keep note priority coupled to this switch
   (lowest-note for GATE and LFO, last-note for GATE+TRIG) [S1][S5].

5. **Shared output stage.** The arp/seq drive the same 1 V/oct KCV (0.415-5 V)
   and the same gate/trigger logic as the keyboard, so the gate generator and
   envelope retrigger logic must be downstream of and shared by keyboard, arp and
   sequencer. Use the service-manual gate ON = 12 V per the project's frozen
   decision (carry the 10 V vs 12 V conflict as a documented note) [S1][S2][S5].

## 10. References

- [S1] Roland SH-101 Owner's Manual (p.2, p.28-39, p.56 Specifications) —
  <https://synthfool.com/docs/Roland/SH_Series/Roland%20SH-101%20Owners%20&%20Service%20Manuals.pdf>
  and OCR mirror
  <https://archive.org/download/synthmanual-roland-sh-101-owners-manual/rolandsh-101ownersmanual_djvu.txt>
- [S2] Roland SH-101 Service Manual / Service Notes (1 Nov 1982), CPU Program,
  Block Diagram, Specifications —
  <https://archive.org/stream/roland_Roland_SH101_Service_Manual/Roland_SH101_Service_Manual_djvu.txt>
  and <https://notebook.zoeblade.com/Downloads/Documentation/Roland/SH-101_service_notes.pdf>
- [S3] Roland Corporation, SH-101 Technical Specifications —
  <https://support.roland.com/hc/en-us/articles/201921519-SH-101-Technical-Specifications>
- [S4] Sweetwater, Roland SH-101 Technical Specifications —
  <https://www.sweetwater.com/sweetcare/articles/roland-sh-101-technical-specifications/>
- [S5] joebritt, SH101_Disassembled (community reverse-engineering disassembly of
  the TMP80C49P firmware) — <https://github.com/joebritt/SH101_Disassembled>
- [S6] Modwiggler forum, SH-101 sequencer / RAM discussion —
  <https://www.modwiggler.com/forum/viewtopic.php?t=114709>
- [S7] Vintage Synth Explorer, Roland SH-101 —
  <https://www.vintagesynth.com/roland/sh-101>
- [S8] KVR Audio forum, SH-101 gate/trigger behavior —
  <https://www.kvraudio.com/forum/viewtopic.php?t=543399>
- [S9] Vintage Synth forum, SH-101 envelope retrigger —
  <https://forum.vintagesynth.com/viewtopic.php?t=36010>
- [S10] Vintage Synth forum, SH-101 note-priority discussion —
  <https://forum.vintagesynth.com/viewtopic.php?t=39338>
- [S11] Gearspace, SH-101 sequencer workflow —
  <https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1223753-sh101-sequencer-workflow.html>
- [S12] ElectricDruid, SH-101 replacement processor feasibility study —
  <https://electricdruid.net/sh101-replacement-processor-a-feasibility-study/>
- [S13] Wikipedia, Roland SH-101 — <https://en.wikipedia.org/wiki/Roland_SH-101>
- [S14] Wikipedia, Intel MCS-48 — <https://en.wikipedia.org/wiki/Intel_MCS-48>
