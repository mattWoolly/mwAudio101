<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Cultural Influence: IDM, Acid & Sound-Design Idioms

## 1. Scope and honesty framing

This document records the *documented* cultural use of the Roland SH-101 in
IDM, acid house, techno and adjacent electronic genres, and distils the
sound-design idioms that should seed the mwAudio101 factory preset bank. It is
deliberately **marketing-soft**: cultural attribution is reputation and
journalism, not bench fact. Where a "fact" is actually an artifact of later
software emulation (Roland Cloud SH-101 / SH-01A), or where it is general
analog-synth theory rather than SH-101-specific documentation, this document
says so explicitly.

Three editorial corrections are applied throughout and should be carried into
all downstream phases:

- **Aphex Twin track-level claims are hedged.** Ownership and heavy modding are
  primary-sourced; specific-track attribution is not. See section 2.
- **The Vince Clarke association is dropped** as a canonical SH-101 claim. It
  rests on a single thin list entry and is contradicted by artist-specific
  sources. See sections 3.6 and 6.
- **The TB-303 filter-lineage claim is corrected.** The TB-303 does **not**
  share the SH-101's filter; only the Juno-6/60 and Jupiter family share the
  IR3109 chip. See sections 4.2 and 6.

No physical-unit measurements exist for this project. Every numeric patch value
quoted below is a software-emulation GUI figure, not a calibrated hardware
panel position; it is an idiom recipe only, never a DSP calibration source. See
section 6.

## 2. Aphex Twin: documented ownership, undocumented tracks

### 2.1 The core synth trio

Aphex Twin's (Richard D. James) early-1990s core synth setup is documented in a
contemporary primary source — the April 1993 *Future Music* interview "The
Aphex Effect" (Issue 6) — as the **Roland SH-101 + Korg MS-20 + Yamaha DX7**,
run through an Alesis Quadraverb [service-grade journalism, high]
[S1][S2][S3]. James owned **three** MS-20s, described as the only keyboard he
had *not* modified [S1]. This trio is the synth subset of his rig; the same
interview credits a Casio FZ-series sampler on a large share of tracks, so the
"core trio" framing must not be read as his complete sound source set
[honest caveat] [S1][S2].

### 2.2 The unit was heavily modded — not a stock 101

James stated verbatim that his SH-101 "doesn't look like a 101 anymore — I use
the sliders, but for different things," within a broader practice of building
his own filters and oscillators [primary interview, high] [S1][S2]. The design
consequence is direct: a stock-accurate clone reproduces the *platform*, not
his specific modded signal path. Any Aphex homage in the preset bank is
therefore *inspired-by*, not a reconstruction of his instrument.

### 2.3 No primary track-level attribution exists

No primary source attributes the SH-101 to any specific Aphex Twin track. The
detailed Reverb Machine reconstruction of *Selected Ambient Works 85-92*
concentrates on the DX7 and a Prophet-5 and does **not** isolate SH-101
contributions to named tracks [S3][S4]. Secondary/forum attributions exist
("a brighter version shows up in Polynomial-C") but are speculative, not
primary [community forum, speculative] [S5]. The "Digeridoo" attribution is
mildly **contradicted**: interview material indicates that track used custom DIY
"boxes that do one sound each," not the SH-101 [S2][S4]. **Treat all
Aphex-track-specific SH-101 claims as inspired-by / disputed, never as
"as used on track X."**

### 2.4 Process favoured sequencing

James described his process as "99% sequenced … Music that's out of time does
my head in," and wrote his own sequencing software on a Sinclair Spectrum
[primary interview, high] [S1][S2]. This supports the interpretation that an
onboard step sequencer/arpeggiator would suit his workflow, but the primary
interview does **not** confirm he used the *SH-101's internal* sequencer
specifically — secondary sources only say "likely" [theory/inference,
unconfirmed].

### 2.5 The "Roland 100M" provenance note

James said he bought a "Roland 100M monosynth" at age 13 and *himself* compared
it to the SH-101 ("it's like an [SH-]101") [S2][S6]. The two are distinct
instruments (the System 100M is a separate semi-modular line). Frame the
distinction as one James invited, not a pure secondary-source error, and avoid
asserting which specific 100-series unit it was [provenance ambiguous].

## 3. The wider IDM / Warp scene

### 3.1 Squarepusher — the strongest track-level documentation

The clearest track-level IDM documentation of the SH-101 is for Squarepusher
(Tom Jenkinson). *The Wire*'s gear reconstruction of his debut *Feed Me Weird
Things* credits the SH-101 with three specific uses [journalism, high] [S7]:

- The opening sound of "Dimotane CO" — "a fair amount of noise modulation on
  the VCO pitch … about as brutal a sound as I could muster from it."
- The main melody of "Theme from Ernest Borgnine."
- A sawtooth wave used as time-stretch source material on "Windscale 2."

Note that the **TB-303**, not the SH-101, carries the slide melodies on "UFO's
over Leytonstone" and "Dimotane CO" [S7].

### 3.2 Squarepusher acquisition

Jenkinson reportedly bought his SH-101 from **Future Music — a gear shop in
Chelmsford** — in **April 1992**, and called it "the first thing I got"
[journalism, medium] [S8][S9]. He later said: "I've had the 101 forever, but
didn't use it on the record … The old monosynths are very charming, but I have
used them to such a degree that there's not much more in them for me" [S10].
The month/shop detail rests on a single corroborated journalistic interview
chain and is artist recollection ~25 years after the fact — medium confidence
[honest caveat].

> **Disambiguation (load-bearing):** "Future Music" appears twice in this
> dimension with two unrelated meanings: (a) *Future Music* magazine, publisher
> of the April 1993 Aphex interview (section 2.1); and (b) Future Music, a
> Chelmsford gear shop where Squarepusher bought his unit. These must not be
> cross-linked [S11].

### 3.3 Autechre — association is largely retrospective

Autechre are listed as period SH-101 users, but their *documented*
sound-defining gear is other equipment. FACT's "gear that defined Autechre"
feature centres the Clavia Nord Lead, circuit-bent Casio SK-1, Ensoniq EPS,
Roland R-8 and a Roland **MC-202** ("a sort of cross between a TB-303 and
SH-101"); the SH-101 is **not** among the seven listed [S12]. Secondary
summaries do credit early albums (*Incunabula* / *Amber*) to "TR-606, SH-101 and
Juno-106," so it was a period tool [S13], but the "classic SH-101 track"
attribution of their "Nine" comes from a curated list, not an Autechre
interview [attribution, not artist-confirmed]. Sean Booth has called the IDM
label "silly."

### 3.4 Broad scene adoption

The SH-101 was a documented studio staple across the early-90s Warp/UK
electronic scene. RBMA: "By 1990, units could be found in the studios of Aphex
Twin, Orbital, the Prodigy, 808 State, the Grid, the Future Sound of London and
many others" [S14]. Wikipedia adds Squarepusher, Boards of Canada, the Chemical
Brothers, Jimmy Edgar and Nitzer Ebb [S15]. This establishes broad
*scene-association*, but most entries are ownership lists without track-level
citations — useful for cultural framing, weak for preset provenance
[honest label].

### 3.5 What the IDM/acid reputation actually rests on

The 101's IDM/acid relevance rests on documented circuit traits: a
self-oscillating resonant low-pass filter, a -1/-2 octave sub-oscillator, and a
sequencer with REST and slur/tie steps for 303-style slides [S16][S17][S18].
These are the practical preset hooks the reputation maps to (see section 5).

### 3.6 Vince Clarke — dropped

Per the editorial policy, the Vince Clarke association is **dropped** as a
canonical SH-101 claim. It is supported only by a single passing Wikipedia list
entry ("Vince Clarke of Erasure") and is effectively undercut by
artist-specific sources, which associate him with the ARP 2600, Sequential
Pro-One, Roland System 100M, Jupiter-4 and Juno-60 — none mention the SH-101,
and SoundOnSound reports he singles out the Pro-One and System 100M as
favourites [S15][S19][S20]. **Do not present Vince Clarke as a signature SH-101
user.**

## 4. Acid, techno & sound-design idioms

### 4.1 The instrument's second life

The SH-101 (1982-1986) was a commercial underperformer at launch that became,
via the cheap late-1980s/1990s second-hand market, a defining instrument of
acid house, Detroit/bleep techno, electro and drum & bass [S15][S14][S16][S21].
Roland's own framing notes it "did not achieve significant commercial success"
at launch but "later became a staple of electronic music in the 1990s."

### 4.2 Filter lineage — the TB-303 correction

The SH-101's "acid" usefulness comes from its **self-oscillating resonant
low-pass filter**, built on the Roland **IR3109** filter IC [confirmed at chip
level]. The IR3109 is shared with the **Juno-6/60 and the Jupiter-4/6/8 / JX-3P
/ MKS series** (the Juno-106 uses the D80017A, an SMD descendant of the same
design family) [reverse-engineered: Electric Druid / AMSynths] [S22][S23].

**Correction (FROZEN):** The TB-303 does **NOT** use the IR3109. It uses a
discrete 4-pole diode/transistor-ladder filter (2SC536F transistors as diodes)
— a categorically different topology [S24][S25]. Earlier framing that grouped
"TB-303 / MC-202 / Juno" as one filter family is wrong. There is a *sonic*
adjacency — Vintage Synth Explorer places the 101 "somewhere between the TB-303
and a Juno bass sound" [S26] — but that is timbre, not circuit lineage. The
**MC-202's** IR3109 use is genuinely **disputed**: AMSynths' chip history omits
it and calls the SH-101 the IR3109's only monosynth appearance, while some
retailer listings disagree [disputed] [S23]. Recommended wording for downstream
docs: *"Self-oscillating resonant VCF built on the Roland IR3109 chip, shared
with the Juno-6/60 and Jupiter family; sonically adjacent to (but
circuit-distinct from) the TB-303, whose filter is a discrete diode ladder."*

### 4.3 Squelchy resonant acid bass/lead — the signature idiom

The signature SH-101 idiom is the squelchy resonant acid bass/lead: high
resonance plus a fast-decay, zero-sustain envelope modulating the filter, often
with portamento [confirmed idiom] [S27][S21]. The canonical acid-house recipe
(MusicRadar) uses sawtooth at max, sub-osc ~halfway one octave down, cutoff
~11 o'clock, resonance ~2 o'clock, env amount ~4 o'clock, fast filter-envelope
decay with zero sustain and short release for clipped notes, auto portamento,
plus overdrive for grit [S27]. Roland's "Acid Lead" recipe corroborates the
qualitative ADSR shape: Attack 0 / fast Decay / Sustain 0 / short Release, high
resonance, portamento on for "techno slides" [S21]. **The numeric values are
software-emulation figures — see section 6.**

### 4.4 Sub-heavy basslines

Sub-heavy basslines are a core idiom, driven by the **independent-level
sub-oscillator** selectable to -1 Oct Square, -2 Oct Square or -2 Oct Pulse
[confirmed] [S15][S23][S26]. RBMA notes "sine waves on the SH-101 were the
deepest thing" [S14]. The sub-oscillator's *independent mixer level* is
confirmed [S26]. This underpins a dedicated Sub Bass preset category voiced to
sit beneath a 303-style line.

### 4.5 PWM leads (string/pad framing labelled as theory)

PWM — a square/pulse wave whose width is swept by an LFO — is a defining lead
timbre [confirmed for *leads*] [S21][S15][S26]. Roland's "PWM Lead" recipe uses
a pulse width sweep with a triangle LFO. **However**, the common "quasi-string /
pad" framing is **not documented** for the SH-101 and is general analog-synth
theory: the SH-101 is monophonic with one VCO, so true polyphonic pads are
impossible [theory/inference; honest label]. Any "Strings" preset is a mono PWM-
and-chorus *stylization*, and must be labelled as such.

### 4.6 Bright leads and percussive blips

Bright leads using saw/square with resonance at higher registers are documented
in Roland's lead recipes [S21]. The percussive "blip" idiom (very short
attack/decay on a resonant filter) is consistent with the 101's snappy envelope
but is **not** a source-documented SH-101-specific idiom — it is general
subtractive-synthesis practice [theory/general-practice, honest label] [S21].

### 4.7 The onboard sequencer and arpeggiator — the defining performance idiom

The onboard **~100-step sequencer** (real-time entry) and **up/down/up-down
arpeggiator** are the SH-101's signature performance idiom and a primary reason
for its dance-music adoption — its main differentiator versus the Korg MS-20 and
Moog Prodigy [S26][S15][S14]. **Honest gap:** the ~100-step count and real-time
entry are well confirmed across three independent sources, but the **REST and
LEGATO/slur (tie) functions** for 303-style slides could **not** be confirmed
from authoritative sources (Wikipedia, Vintage Synth Explorer, RBMA describe
only the step count and real-time entry); this feature likely derives from a
forum/blog and must be verified against the owner's/service manual before being
stated as fact [unverified — open gap] [S15][S26][S14].

### 4.8 The TB-303 / TR-808 production pairing

The SH-101 is canonically paired with the TB-303 and TR-808 [S21][S28]. The
best-documented example is A Guy Called Gerald's "Voodoo Ray" (1988), the first
British acid house single: a primary Sound On Sound interview confirms **two
SH-101s** built the bassline and melodic sequences via their internal
sequencers, **trigger-synced from a TR-808**, with a counter-riff from a TB-303
[primary interview, high] [S28]. Simpson said the SH-101 "had a lot more range
than my TB-303" [S14]. This is the canonical sequencer/sub-bass idiom in
practice and should inform preset design (e.g. sub-bass presets meant to sit
under a 303 line).

### 4.9 Detroit techno

Detroit techno pioneers used the SH-101 prominently. Per RBMA, Juan Atkins used
it on "Off To Battle," "Interference" and "O.K. Coral," within an early-'80s
gear-sharing circle with Derrick May, Kevin Saunderson, Eddie Fowlkes and James
Pennington [journalism, medium-high] [S14][S16]. The named-track attributions
come specifically from RBMA; the general Detroit/SH-101 association is
well-cited. Note: do **not** attribute a "string" preset to Derrick May's
"Strings of Life" as SH-101 fact — the specific synth on that record is not
confirmed in sources reviewed [open question].

### 4.10 Chiptune is a modern homage, not history

There is **no** documented evidence the original hardware SH-101 was used in
NES/Famicom-era chiptune or Japanese game music; those composers used dedicated
PSG/FM sound chips [confirmed negative] [S21][S15][S29]. The "Chiptune Lead" is
a preset name in Roland's *modern software* materials — an aesthetic homage,
not historical usage [software-emulation artifact, honest label]. Treat any
"game music" preset category as homage only.

## 5. Key parameters

All figures below are cultural/idiom parameters. Numeric panel/GUI values are
software-emulation conventions, **not** calibrated hardware values — see
section 6 before using any of them in the DSP model.

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| Aphex Twin core synth trio (era) | SH-101 + Korg MS-20 + Yamaha DX7 | instruments | high | [S1][S2] |
| Aphex Twin MS-20 count (only unmodified synth) | 3 | units | high | [S1] |
| Aphex primary source | Future Music, Issue 6, April 1993 ("The Aphex Effect") | publication | high | [S1] |
| Squarepusher SH-101 acquisition | 1992 (April, reportedly Future Music shop, Chelmsford) | year | medium | [S8][S9] |
| Documented Squarepusher tracks (debut) | Dimotane CO (intro, noise→VCO pitch mod); Theme from Ernest Borgnine (main melody); Windscale 2 (sawtooth time-stretch source) | tracks | high | [S7] |
| Voodoo Ray SH-101 units (documented) | 2 (trigger-synced from TR-808) | synths | high | [S28] |
| Sub-oscillator octave options | -1 Oct Square, -2 Oct Square, -2 Oct Pulse | octaves below VCO | high | [S15][S23] |
| Sequencer length (idiom) | up to ~100 | steps/notes | high (count); REST/slur unverified | [S15][S26][S14] |
| Arpeggiator modes | up / down / up-down | modes | high | [S15] |
| Filter lineage (acid relevance) | Self-oscillating IR3109 VCF; shared with Juno-6/60 & Jupiter family; NOT TB-303 | circuit family | high (Juno share); TB-303 corrected; MC-202 disputed | [S22][S23][S24] |
| Acid filter envelope (idiom) | Attack 0, fast Decay, Sustain 0, short Release | ADSR shape | medium | [S27][S21] |
| Acid filter resonance (emulation scale) | ~2 o'clock / Acid Lead 175-200 of 255 | panel/GUI position | low (mixed scales) | [S27][S21] |
| Acid/lead VCO register (SOFTWARE only) | 32' bass / 64'/16'/8' leads | foot register | low (software emulation; hardware = 16'/8'/4'/2') | [S21][S30][S31] |
| PWM lead pulse width (emulation scale) | 180-230 (example 219) | GUI 0-255 fader | low | [S21] |
| PWM LFO rate (emulation scale) | 70-110, triangle | GUI LFO rate | low | [S21] |
| Sub-bass source mix (idiom, software) | sub max, square max, saw ~70%, noise ~30% | relative mixer level | low | [S31] |
| LFO waveforms (HARDWARE) | Triangle, Square, Random, Noise (no Sine) | waveforms | high (corrected) | [S15][S26][S16] |

## 6. Confidence, disputes & honest labels

This section surfaces every disputed, low-confidence, software-artifact or
residual-risk item for this dimension, stated plainly. **None of the items below
may be presented as settled fact in downstream phases.**

### 6.1 Corrected / dropped claims

- **TB-303 filter lineage — CORRECTED (FROZEN).** The TB-303 does not share the
  SH-101's filter. The SH-101 uses the IR3109 OTA-based filter, shared with the
  Juno-6/60 and Jupiter family; the TB-303 uses a discrete diode ladder
  [S22][S23][S24][S25]. Any "same filter as the 303" claim is false.
- **MC-202 filter chip — DISPUTED.** AMSynths omits the MC-202 from IR3109 use
  and calls the SH-101 the chip's only monosynth appearance; retailer listings
  disagree. Do not assert SH-101/MC-202 filter identity without the MC-202
  service manual [disputed] [S23].
- **Vince Clarke — DROPPED.** Not a canonical SH-101 user; the claim rests on a
  single Wikipedia list entry and is contradicted by artist-specific sources
  [S15][S19][S20].
- **LFO waveforms — CORRECTED.** Original 1982 hardware LFO is **Triangle /
  Square / Random / Noise (no Sine)**. A Sine LFO appears **only** in the
  Roland Cloud software emulation. Earlier "sine / square / triangle" framing
  is wrong, was contradicted by its own cited source, and wrongly omitted
  Random/Noise [software-emulation artifact] [S15][S26][S16].

### 6.2 Software-emulation artifacts (not original hardware)

- **VCO foot registers.** Original hardware RANGE = **16'/8'/4'/2' only**. The
  **32' and 64'** registers exist **only** in the Roland Cloud plugin / SH-01A
  reissue. Any "32' acid bass / 64' lead" guidance must be scoped to the
  software emulation [S30][S31][S21].
- **All numeric patch values** (resonance 175-200/255, pulse width 180-230/219,
  LFO rate 70-110, decay ~200, portamento 5-20) are GUI fader values from the
  Roland Cloud software, **not** hardware panel calibrations. Use only as
  relative idiom recipes; pull real DSP ranges from the service-manual/technical
  dimension [S21][S27][S31].
- **The resonance figure mixes scales.** "~2 o'clock" (MusicRadar knob position)
  and "175-200/255" (Roland software fader) come from different sources on
  different scales; present separately, not as one merged spec [S27][S21].
- **"Chiptune Lead"** is a modern software preset name, not historical SH-101
  usage [S21].

### 6.3 Hedged / weak attributions

- **Aphex Twin track-level claims** are hedged: SH-101 ownership and modding are
  primary-sourced, but "Digeridoo" is likely DIY-box-based and "Polynomial-C"
  rests on speculative forum analysis. No primary source ties the 101 to a named
  Aphex track [S1][S2][S4][S5].
- **Autechre / "Nine"** — the SH-101 attribution is from a curated list, not an
  Autechre interview; their documented sound-defining gear is other equipment
  [S12].
- **Squarepusher acquisition detail** (April 1992, Chelmsford shop) rests on a
  single corroborated journalistic chain and is ~25-year-old artist
  recollection — medium confidence [S8][S9].
- **"Quasi-string/pad" PWM and percussive "blip"** idioms are general
  subtractive-synthesis theory, not documented SH-101-specific usage; the
  monophonic SH-101 cannot play true pads/chords [theory/general-practice]
  [S21].

### 6.4 Open validation gaps

- **Sequencer REST / LEGATO(slur/tie)** functions are unverified by
  authoritative sources; the ~100-step count and real-time entry are solid. Must
  be confirmed against the owner's/service manual before being stated as fact
  [open gap] [S15][S26][S14].
- **No machine-readable original manual** was parsed (the synthfool PDF is
  image-only; Roland's spec page returns HTTP 403). Hardware-spec confirmations
  here rely on secondary sources [open gap].
- **Aphex's "Roland 100M"** model is most plausibly the System 100M but is not
  unambiguously identified; avoid asserting which 100-series unit [provenance
  ambiguous] [S2][S6].
- **"Strings of Life" / Derrick May** — the synth on that record is not
  confirmed as the SH-101; do not attribute a "string" preset to it as fact
  [open question].
- **No physical-unit measurements** exist for this project. Anything requiring
  bench data is an open validation gap, not a delivered fact.

## 7. Design implications for mwAudio101

The cultural research defines the **factory-preset taxonomy** and voicing
targets, and confirms which engine features carry the cultural identity. It does
**not** supply DSP calibration — those numbers come from the technical/
service-manual dimensions.

### 7.1 Preset taxonomy (~64 IDM/acid-leaning presets)

1. **Acid Bass / Lead** (most important): self-oscillating resonant LP filter,
   high resonance, fast-decay zero-sustain filter envelope, portamento/glide;
   designed to be sequencer/arp-driven and overdrive-friendly [S27][S21]. Include
   a "filter-as-sine-oscillator" variant for theremin leads and resonant kicks
   [S16].
2. **Sub Bass**: independent-level sub-oscillator at -1/-2 oct, high mixer level
   under a clipped VCA, square+saw blend, optional noise for movement; voiced to
   sit beneath a TB-303-style line [S14][S28][S31].
3. **Lead**: bright saw/square with resonance for expressiveness, vibrato/PWM
   motion [S21].
4. **PWM / "Strings"**: square+sub with LFO sweeping pulse width plus chorus —
   explicitly a **mono PWM stylization**, labelled as such (true polyphonic pads
   are impossible on one VCO) [S21][honest label].
5. **Blips / FX**: very short A/D envelopes on a resonant filter plus
   noise-source effects — framed as general-practice character, not a sourced
   idiom [S21][theory label].
6. **Sequencer / Arp riffs**: presets authored together with stored sequencer
   patterns/arp settings — much of the 101's cultural identity is the *riff*,
   not just the timbre (Voodoo Ray is the canonical example) [S28][S14].

### 7.2 Engine requirements implied by the idioms

The engine must expose: an independent-level sub-oscillator with -1/-2 oct
switch; a blendable saw/square/sub/noise source mixer; PWM with LFO and manual
control; a self-oscillating resonant LP filter that screams under high
resonance; snappy envelopes capable of zero-sustain acid shapes; auto/normal
portamento; an LFO offering **Triangle / Square / Random / Noise** (hardware
spec — **not** Sine, which is software-only); and a faithful ~100-step sequencer
plus up/down/up-down arpeggiator with external/clock sync [S15][S26][S28].

### 7.3 Naming and marketing discipline

- Mark per-artist track claims as *inspired-by* / disputed, never "as used on
  track X" — Squarepusher's debut is the one documented exception that may be
  cited with care [S7].
- Offer an Aphex-style "modded character" option (extra slider re-routings /
  extreme mod depth) explicitly framed as *inspired-by*, since his real unit was
  heavily customised and his stock-101 track use is undocumented [S1][S2].
- Do **not** name Vince Clarke as an SH-101 user [S19][S20]; do **not** ship a
  "TB-303 filter" descriptor [S24]; scope any 32'/64' or sine-LFO feature to
  "software-style" extensions, not the vintage hardware [S30][S31].
- Use the online emulation patch numbers only to set relative slider character
  and preset *feel*; derive absolute parameter mappings from the
  service-manual/technical dimension [S21][S31].

## 8. References

- [S1] Future Music, Issue 6, April 1993, "The Aphex Effect" (Lanner Chronicle
  reprint): <https://lannerchronicle.wordpress.com/2020/08/30/the-aphex-effect-future-music-magazine-april-1993/>
- [S2] MusicRadar, Aphex Twin interview (Selected Ambient Works reprint of the
  Future Music interview): <https://www.musicradar.com/news/aphex-twin-interview-selected-ambient-works>
- [S3] Reverb Machine, "Aphex Twin — Selected Ambient Works 85-92":
  <https://reverbmachine.com/blog/aphex-twin-selected-ambient-works-85-92/>
- [S4] Reverb Machine (SAW reconstruction, as fetched/summarised):
  <https://reverbmachine.com/blog/aphex-twin-selected-ambient-works-85-92/>
- [S5] Vintage Synth Explorer forum thread (speculative Polynomial-C analysis):
  <https://forum.vintagesynth.com/viewtopic.php?t=57352>
- [S6] Lanner Chronicle, "Phonic Boy On Dope — Under Construction":
  <https://lannerchronicle.wordpress.com/2020/07/29/phonic-boy-on-dope-under-construction/>
- [S7] The Wire, "Feed me wired things: the gear that made Squarepusher's
  debut": <https://www.thewire.co.uk/in-writing/essays/feed-me-wired-things-the-gear-that-made-squarepusher-s-debut>
- [S8] DJ Mag longread, "The return of Squarepusher":
  <https://djmag.com/longreads/return-squarepusher>
- [S9] XLR8R, "Squarepusher: Back to the Future":
  <https://xlr8r.com/features/squarepusher-back-to-the-future/>
- [S10] Sound on Sound, Squarepusher (May 2011):
  <https://www.soundonsound.com/people/squarepusher>
- [S11] (Disambiguation note — Future Music magazine vs Future Music shop; see
  [S1] and [S8].)
- [S12] FACT Magazine, "7 pieces of gear that helped define Autechre's sound":
  <https://www.factmag.com/2017/02/25/autechre-gear-synths-samplers-drum-machines-effects/>
- [S13] Gearnews (DE), "Autechre und ihre Synthesizer":
  <https://www.gearnews.de/autechre-und-ihre-synthesizer-typische-hardware/>
- [S14] Red Bull Music Academy, "Roland SH-101: Instrumental Instruments":
  <https://daily.redbullmusicacademy.com/2017/09/roland-sh101-instrumental-instruments/>
- [S15] Wikipedia, "Roland SH-101":
  <https://en.wikipedia.org/wiki/Roland_SH-101>
- [S16] AOS-Pro, "Roland SH-101: The analog synthesizer that defined techno and
  acid house": <https://www.aos-pro.com/articles-1/roland-sh-101-the-analog-synthesizer-that-defined-techno-and-acid-house.html>
- [S17] Vintage Synth Explorer, "Roland SH-101":
  <https://www.vintagesynth.com/roland/sh-101>
- [S18] Acid Box Blues, "Setting up the Roland SH-101 to jack like…":
  <https://www.acidboxblues.com/2007/06/setting-up-roland-sh101-to-jack-like.html>
- [S19] Gearnews, "How to sound like Vince Clarke":
  <https://www.gearnews.com/how-to-sound-like-vince-clarke-of-depeche-mode-yazoo-and-erasure/>
- [S20] Sound on Sound, Vince Clarke electronic music pioneer:
  <https://www.soundonsound.com/people/vince-clarke-electronic-music-pioneer>
- [S21] Roland Articles, "Sound design: legendary leads with the SH-101":
  <https://articles.roland.com/sound-design-legendary-leads-with-the-sh-101/>
- [S22] Electric Druid, "Roland filter designs with the IR3109 or AS3109":
  <https://electricdruid.net/roland-filter-designs-with-the-ir3109-or-as3109/>
- [S23] AMSynths, "All about the IR3109 chip":
  <https://amsynths.co.uk/2022/04/06/all-about-the-ir3109-chip/>
- [S24] Tim Stinchcombe, "Diode-ladder filter (TB-303 topology)":
  <https://www.timstinchcombe.co.uk/index.php?pge=diode2>
- [S25] (TB-303 diode-ladder topology — see [S24].)
- [S26] Vintage Synth Explorer, "Roland SH-101":
  <https://www.vintagesynth.com/roland/sh-101>
- [S27] MusicRadar, "Recreate a classic acid house synth riff (virtual SH-101)":
  <https://www.musicradar.com/how-to/recreate-classic-acid-house-synth-riff-virtual-roland-sh-101>
- [S28] Sound on Sound, "Classic Tracks: A Guy Called Gerald — Voodoo Ray":
  <https://www.soundonsound.com/techniques/classic-tracks-guy-called-gerald-voodoo-ray>
- [S29] Gearspace, "80s video game sounds — synths":
  <https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1277510-80s-video-game-sounds-synths.html>
- [S30] Sweetwater, "Roland SH-101 technical specifications":
  <https://www.sweetwater.com/sweetcare/articles/roland-sh-101-technical-specifications/>
- [S31] MusicRadar, "How to create a PWM synth bass patch on a virtual SH-101":
  <https://www.musicradar.com/how-to/how-to-create-pwm-synth-bass-patch-on-a-virtual-roland-sh-101>
