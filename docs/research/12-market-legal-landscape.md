<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# Competitive Landscape & Trademark/Trade-Dress

## 1. Scope and summary

This document maps the SH-101 emulation market and the trademark / trade-dress
constraints that bind an open-source, GPLv3, circuit-accurate clone (mwAudio101).
It is a citable source of truth: the architecture and backlog phases reference it
by section number.

The SH-101 emulation market is crowded but has one clear, unoccupied niche: there
is no prominent, maintained open-source/GPL software SH-101 plugin
[confirmed-with-caveat, see Section 8] [R1][R2]. Commercial software spans four
tiers — faithful/simple emulations (TAL-BassLine-101), the official circuit-modeled
version (Roland Cloud SH-101), feature-extended single emulations (Softube Model 82,
Air Music Tech Iona), and polyphonic "super-sized" takes (D16 LuSH-101 / Lush-2)
[R3][R4][R5][R6][R7]. Hardware is dominated by Behringer's MS-1, with boutique
Eurorack from AMSynths (SN-101 system) [R8][R9].

On the legal side: "SH-101" is a registered Roland trademark
(US Reg. 5505499, Serial 79216816), and **every** successful clone avoids the
"SH-101" name [R10][R14]. This validates a trademark-distance strategy: a non-
"SH-101" product name, no Roland wordmark/logo, "SH-101" used only as a nominative/
descriptive reference, and a visually distinct panel rather than a pixel-clone of
the Roland faceplate.

Confidence labels used in this document:

- **[service-manual, high]** — authoritative primary documentation.
- **[primary-source, high]** — USPTO TSDR, vendor pages, court records.
- **[secondary, medium]** — trade press / retailer reporting.
- **[absence-of-evidence]** — a "we did not find X" conclusion, not proof X
  does not exist.
- **[legal/inference, not-counsel]** — a reading of legal doctrine that is **not**
  a substitute for an IP attorney's opinion.

## 2. The open-source niche (the differentiation lever)

There is **no prominent, maintained open-source/GPL software SH-101 emulation
plugin**; the niche is effectively unoccupied
[confirmed-with-caveat / absence-of-evidence, medium] [R1][R11][R2].

- Targeted searches across the general web, GitHub, and LV2/VST keywords surfaced
  only commercial emulations (TAL-BassLine-101, Softube Model 82, D16 LuSH-101,
  Cherry Audio SH-MAX, AIR Iona, Roland Cloud) plus an early free-but-closed
  TAL-BassLine demo [R11][R2].
- The single open-source candidate found — the SourceForge project "sh101" — is
  **dormant**: last updated 2016-07-25, 0 downloads/week, **no stated license**
  (GPL unconfirmed), and no evidence it is a functioning or distributed VST
  [primary-source, high] [R1].

> **Honest label.** This is an *absence-of-evidence* finding. It cannot prove that
> no low-profile or newly published GitHub/LV2 project exists outside search
> visibility. The niche is *effectively* unoccupied as of June 2026, which is
> enough to justify the positioning but is not a proof of total non-existence.

This is the primary differentiation lever for mwAudio101: a libre, source-available,
circuit-accurate SH-101 clone with no commercial equivalent.

## 3. Commercial software competitors

### 3.1 Roland Cloud SH-101 — the authenticity benchmark

Roland's own, officially-licensed emulation uses Analog Circuit Behavior (ACB)
modeling [secondary, high] [R3][R4][R12].

- List ~$99–$100; frequently discounted to $49 [primary/secondary, high] [R12][R13].
- Extends the original: dedicated filter envelope, extended VCO octave range, extra
  LFO shapes, onboard effects (reverb / delay / crusher), tempo-synced arpeggiator,
  color skins, and PLUG-OUT support for Roland hardware (SYSTEM-8, SH-01A) [R3][R4].
- Formats: VST / AU / AAX. Sold via Roland Cloud subscription or perpetual license.

> Several of these features are **software-only extensions**, not properties of the
> 1982 hardware (no sine LFO, no extended octave range, no onboard FX on the original
> instrument). Where a "feature" is a software-emulation artifact, mwAudio101
> documentation must say so rather than attribute it to the SH-101 itself.

This is the **authenticity benchmark** mwAudio101 measures circuit fidelity against.

### 3.2 TAL-BassLine-101 — the community sonic reference

Widely rated as closer to a calibrated vintage unit than Roland's own plugin
[secondary, high] [R5][R15][R16].

- 24 dB low-pass filter calibrated to avoid digital artifacts, real-time waveform
  generation, an added 12 dB filter mode, Filter-FM (oscillator modulating cutoff),
  distortion, built-in sequencer/arpeggiator, and MIDI/MPE control of cutoff and
  volume [R5][R15].
- Price ~$69 USD (Plugin Boutique regular price; street ~$60 on sale)
  [primary-source, high — corrected, see Section 8] [R17][R5]. The free demo injects
  rising white noise each minute.
- Reputation: simple, faithful, no subscription.

This is the **community sonic reference** — the fidelity bar mwAudio101 must clear.

### 3.3 Softube Model 82 — feature-extended + modular

A feature-extended SH-101 emulation that also ships a modular (split-module) version
[secondary, high] [R6][R18][R19].

- Signal path: Source Mixer (saw, square/pulse, sub-osc, white noise) into a
  24 dB/oct resonant LPF; step sequencer with DAW sync (1/4–1/32) [R6][R18].
- Adds stereo doubling, drive, and velocity/aftertouch control of VCF/VCA.
- The "Model 82 for Modular" bundle splits into VCO, LFO, VCF-VCA, FX, ENV modules.
- Formats: VST / VST3 / AU / AAX.
- List ~$99 (USD/EUR); heavily discounted (seen $19–$29)
  [primary-source, high] [R20][R19][R21].

### 3.4 Air Music Tech Iona — modern feature-creep (2025)

A recent SH-101 emulation with heavy modern feature extensions and MPC/Force
standalone support [secondary, high] [R7].

- List ~$149 (intro $59).
- Five skins (grey/red/blue/white/yellow), nine-waveform LFO, graphical step
  sequencer up to 128 steps with transpose/shift, multi-FX (Lo-Fi, delay, reverb,
  3-band EQ), 100+ presets, BPM-syncable modulation.
- Formats: VST / VST3 / AU / AAX plus Akai MPC / Force hardware.
- Named "Iona" (not SH-101), marketed as "inspired by".

### 3.5 D16 Group LuSH-101 / Lush-2 — the polyphonic "super-sized" axis

Takes the SH-101 polyphonic and multitimbral — a "super-sized" interpretation
[secondary, high] [R22][R23][R24].

- Up to 32 voices, 8 independent layers, ~1,600 presets, multimode self-oscillating
  filter, supersaw and hard-sync oscillators, sub-osc, modulation matrix, arpeggiator,
  effects.
- Formats: VST2 / VST3 / AU / AAX; macOS (Apple Silicon + Intel) and Windows.
- **Original list ~$199** [primary-source, high]. **Context:** LuSH-101 is a
  legacy/discontinued product now superseded by **D16 Lush-2** (current ~$169);
  current street prices for LuSH-101 run $75–$101. The $199 figure is the original
  MSRP, not a current active street price [corrected framing, see Section 8] [R23][R25].
- Demonstrates a "beyond-mono" differentiation axis at the cost of authenticity.

### 3.6 Cherry Audio SH-MAX — adjacent, not a direct competitor (2026)

Cherry Audio targets the SH **family** but **not** the SH-101 specifically
[secondary, high] [R26].

- SH-MAX (2026) combines SH-5 (1976), SH-7 (1978), and SH-3A (1974) into one $59
  plugin with 16-voice polyphony, poly aftertouch, a Model-104-inspired
  4-lane/64-step sequencer, and a 20-module multi-FX.
- Named under Cherry Audio's own branding, positioned as emulation, not endorsement.
- Confirms there is **no direct Cherry Audio SH-101 competitor**, leaving the SH-101
  software space to others. (Older SH names appear less aggressively enforced or
  marketed than "SH-101".)

## 4. Hardware competitors

### 4.1 Behringer MS-1 — the dominant affordable hardware clone

The dominant affordable hardware SH-101 clone [secondary, high] [R8][R27][R28].

- Single oscillator with mixable saw/triangle/square-PWM + octave sub-oscillator +
  noise; resonant VCF; arpeggiator; 32-step sequencer; 32-key semi-weighted keyboard;
  mountable handgrip; USB + MIDI.
- **MkII** uses Coolaudio V662A (a clone of Roland's BA662), improving filter/VCA,
  and adds dual glide [secondary, high] [R8].

> The BA662/BA662A internals are **reverse-engineered** (Open Music Labs; no public
> datasheet). Any reference to V662A behavior is therefore
> **[reverse-engineered, clone-derived]**, not a manufacturer specification.

- Pricing: launch $299 (as "MS-101"); MkI later cut to ~$189–$199; MkII ~279 EUR
  [primary/secondary, high] [R28][R27][R8].
- **Naming:** originally launched as "**MS-101**", renamed "**MS-1**" under Roland
  trademark pressure — a direct, real-world example of the trademark-distance
  pattern [secondary, high] [R14][R28].

### 4.2 AMSynths SN-101 — primary-source confirmation of the naming decision

AMSynths sells a Eurorack replica of the SH-101 (the SN-101 system) and explicitly
renamed it due to Roland's trademark [secondary, high] [R9][R29][R30].

- Direct quote from AMSynths: *"Roland have trade marked SH-101 and therefore it can
  not be used."* This is a **primary-source confirmation** of the trademark-distance
  naming decision [R9].
- The SN-101 system is four 10HP modules: AM8110 VCO, AM8101 VCF (24 dB LPF), AM8112
  VCA & ADSR, AM8114 Modulator (LFO). Hand-built, ±12V (from the original ±9V),
  Roland N-127-style knobs.

> The AM8101 VCF uses the **Alfa AS3109** — an SMD **clone** of the original IR3109.
> Any electrical figures attributed to it (drive currents, ~20 Vpp self-oscillation)
> are **[clone-derived: Alfa AS3109 / AMSynths-module, presumed-equal]**, not
> original-instrument measurements. The original SH-101 VCF is the **IR3109 (IC14)**;
> the CEM3340 VCO is **IC13** [FROZEN schematic facts; service-manual reconciliation].

## 5. Trademark and trade-dress analysis

### 5.1 The "SH-101" wordmark is registered

"SH-101" is a registered Roland trademark [primary-source, high] [R10][R31].

- US Reg. 5505499, Serial 79216816; **filed 2017-05-31, registered 2018-07-03**.
- Owner: Roland Corporation.
- The mark is the **stylized dotted wording** "SH-101" (letters/numbers formed by
  dots) — a design/stylized wordmark, not a plain standard-character mark. Status
  reported as Registered [R10].
- **Goods/services class: International Class 009** (downloadable music-production
  software and related software/media) — **confirmed via USPTO TSDR**
  [primary-source, high — corrected, see Section 8] [R31][R10]. It is **NOT** Class 15
  (musical instruments). The application originally included additional Class 9
  hardware (computer mice, blank USB drives, electron tubes) that were **deleted**
  from the final registration.

**Core legal consequence:** mwAudio101 must NOT name itself "SH-101" and must NOT
reproduce the dotted-stylized logo.

### 5.2 Roland protects the visual LOOK, not just names

Roland's IP protection extends beyond names to the visual look (panel layout + color
sequence) as design marks / trade dress [confirmed, medium] [R32][R33][R34][R35][R36].

- Roland filed (US 2018; Germany 2019) to trademark the appearance of the
  TR-808/TB-303/TR-909: "the signature layout of the keyboard and knobs of the 303
  and the sequence of colored buttons on the 808" [R32][R33][R35].
- Coverage in the filings extends to synthesizers, software, t-shirts, phone cases,
  and mousepads [R32].
- Behringer responded by **reversing** the RD-8 button colors (white-yellow-orange-red
  instead of the TR-808 red-orange-yellow-white) to differentiate [R34][R32].

> **Honest label / residual risk.** The documented design-mark/trade-dress
> registrations are for the **TB-303, TR-808, and TR-909** (primarily German filings
> plus some US filings), **NOT** specifically demonstrated for the **SH-101** panel.
> This finding establishes Roland's *general IP strategy* (it does register visual
> look), but no confirmed **SH-101-specific** design-mark/trade-dress registration
> was located in this research. See Section 8.

### 5.3 Roland has a litigation history over look-and-feel

Roland has a documented history of enforcing trade dress/trademark against clone
makers [primary/secondary, high] [R37][R38][R39].

- Roland sued Behringer in 2005 over trade dress, trademark, and other IP in BOSS
  guitar pedals (case, knobs, font), alleging Behringer falsely told retailers the
  clones were Roland-endorsed [R37][R39].
- Settled confidentially in 2006 after Behringer changed the pedal designs; Roland
  stated the new line was "sufficiently different from the trade dress of the famous
  BOSS brand" [R37][R38].

This shows Roland will litigate look-and-feel, not just names.

### 5.4 GPLv3 grants no trademark cover

GPLv3 conveys only a copyright license — it conveys **no** trademark rights;
trademark/trade-dress must be managed separately
[legal/inference, not-counsel; high support] [R40][R41][R42].

- Open-source licenses (GPLv2 is silent on trademarks; GPLv3 grants only an express
  copyright license) do not authorize use of a third party's marks.
- Schematics/PCB/panels can be **redrawn** to avoid copyright, but valid trademarks
  (names/logos) cannot be "gone around" — they must be **avoided** [R40][R42].

**Consequence:** mwAudio101's GPLv3 status does **not** shield it from Roland's SH-101
wordmark or trade dress.

### 5.5 Nominative fair use permits descriptive reference

Nominative fair use permits referencing "Roland SH-101" descriptively, under a
three-part test, provided no endorsement is implied
[legal/inference, not-counsel; high support] [R43][R44][R45].

The US nominative fair use test (*New Kids on the Block*) has three prongs:

1. the product cannot be readily identified without the mark;
2. only so much of the mark as reasonably necessary is used;
3. nothing suggests sponsorship/endorsement.

It applies to compatibility/comparison statements (e.g., "an emulation of the Roland
SH-101") and fails if there is a likelihood of confusion about source/sponsorship.
mwAudio101 may say it emulates the SH-101 but must **not** use Roland's stylized logo,
must **not** name the product "SH-101", and **must** include a clear "not affiliated
with / not endorsed by Roland" disclaimer.

### 5.6 The industry-standard trademark-distance pattern

Every competing clone uses a non-"SH-101" product name, confirming the industry-
standard pattern [secondary, high] [R14][R9][R26].

| Vendor | Product name | Note |
| --- | --- | --- |
| Behringer | MS-1 | renamed from "MS-101" under Roland pressure [R14][R28] |
| AMSynths | SN-101 | explicitly renamed because "Roland have trade marked SH-101" [R9] |
| Softube | Model 82 | no "SH" string at all [R6] |
| Air Music Tech | Iona | marketed as "inspired by" [R7] |
| D16 Group | LuSH-101 | a pun that alters the string [R22] |
| Cherry Audio | SH-MAX | covers SH-5/SH-7/SH-3A, not SH-101 [R26] |

All reference "SH-101" only descriptively in marketing copy. This validates choosing
a distinct mark like "mwAudio101" and using "SH-101" only as a descriptive reference.

## 6. Key parameters

| Name | Value | Unit | Confidence | Source |
| --- | --- | --- | --- | --- |
| Roland Cloud SH-101 list price | 99–100 | USD | high | [R12] |
| Roland Cloud SH-101 typical sale price | 49 | USD | high | [R13] |
| TAL-BassLine-101 price (corrected) | ~69 (street ~60) | USD | high | [R17] |
| Softube Model 82 list price | 99 | USD/EUR | high | [R20] |
| Softube Model 82 deep-discount price | 19–29 | USD | high | [R19] |
| Air Music Tech Iona list / intro price | 149 / 59 | USD | high | [R7] |
| D16 LuSH-101 original list price | ~199 | USD | high | [R23] |
| D16 LuSH-101 current street price | 75–101 | USD | medium | [R25] |
| Cherry Audio SH-MAX price | 59 | USD | high | [R26] |
| Behringer MS-1 MkII price | ~279 | EUR | high | [R8] |
| Behringer MS-1 launch price (as MS-101) | 299 | USD | high | [R28] |
| SH-101 trademark US Registration Number | 5505499 | USPTO reg. no. | high | [R10] |
| SH-101 trademark US Serial Number | 79216816 | USPTO serial no. | high | [R10] |
| SH-101 trademark filing date | 2017-05-31 | date | high | [R10] |
| SH-101 trademark registration date | 2018-07-03 | date | high | [R10] |
| SH-101 trademark goods/services class (corrected) | 009 | Nice class | high | [R31] |
| Nominative fair use test prongs | 3 | legal test | high | [R43] |

## 7. Design implications for mwAudio101

### 7.1 Naming

Adopt a distinct product mark (e.g., "mwAudio101") and **never** name the product
"SH-101" or reproduce Roland's stylized dotted "SH-101" logo. This matches every
successful clone (Behringer MS-1, AMSynths SN-101, Softube Model 82, Air Iona, D16
LuSH-101) [R14][R9]. Use "SH-101" only as a descriptive/nominative reference ("an
open-source emulation of the Roland SH-101"), satisfying the 3-prong nominative-fair-
use test [R43], and ship a prominent non-affiliation/non-endorsement disclaimer.
GPLv3 covers the code/schematics but grants **zero** trademark cover, so this is
mandatory [R40].

### 7.2 Trade dress / panel

Do **NOT** pixel-clone Roland's faceplate. Roland actively registers and litigates
the look (panel layout + color sequence), and Behringer pre-emptively reversed its
RD-8 button colors [R32][R34][R37]. Give mwAudio101 a visually distinct UI: its own
color palette (avoid Roland's grey/red/blue scheme), its own typography, and a
re-laid-out (not 1:1) control arrangement. Redraw any schematics/PCB/panel art
originally derived from the SH-101 to stay clear of copyright [R40]. The functional
slider-per-parameter ergonomics can be retained (function is not protectable) —
distinctiveness should live in styling, not workflow.

### 7.3 Product differentiation

- **The standout gap** is the absence of any libre/open-source SH-101 plugin — lead
  with "the first open-source, circuit-accurate SH-101 emulation" [R1].
- **Second gap — Linux/format breadth:** commercial vendors ship VST/AU/AAX
  (Windows/macOS); shipping **CLAP + LV2 + VST3** with native Linux builds is a
  defensible, low-cost differentiator that the entire commercial field ignores
  [R3][R5][R6][R7][R22].
- **Third — transparency/hackability:** documented DSP, reproducible builds, and
  user-editable models.
- **Sonic bar to clear:** TAL-BassLine-101 (the community reference); Roland Cloud is
  the authenticity benchmark [R5][R3].
- **Scope decision:** a faithful, free, hackable **mono** SH-101 (TAL-like fidelity,
  no paywall) is the cleanest positioning. Optional extended modes (extra LFO shapes,
  filter envelope, poly layering à la LuSH/Iona/Model 82) can be opt-in "beyond-101"
  features without compromising the authentic core — and must be **labeled as
  extensions**, not as SH-101 hardware behavior.
- **Price differentiation is total:** free/donationware undercuts the entire
  $49–$199 commercial field.

## 8. Confidence, disputes & honest labels

This section surfaces every disputed/low-confidence item, every verification
correction, and every residual risk for this dimension, stated plainly. **REQUIRED.**

### 8.1 Verification corrections applied

- **CORRECTION 1 — trademark class (material).** The original research carried the
  goods/services class as "unconfirmed (likely Class 15 / Class 9)" at **low**
  confidence. This is now **CONFIRMED via primary source (USPTO TSDR)**:
  **International Class 009** (downloadable music-production software and related
  software/media). It is **NOT** Class 15 (musical instruments). Confidence is now
  **high**. The application originally included additional Class 9 hardware (computer
  mice, blank USB drives, electron tubes) that were **deleted** from the final
  registration [R31][R10].
- **CORRECTION 2 — TAL-BassLine-101 price (minor).** The best-supported list/retail
  price is **$69 USD** (Plugin Boutique), not the originally-claimed ~$60. Restated
  as "~$69 USD (street ~$60 on sale)" throughout [R17][R5].
- **CORRECTION 3 — D16 LuSH-101 price (context).** $199 is correct as **original
  MSRP/full price**, but LuSH-101 is a **legacy/discontinued** product now replaced by
  D16 Lush-2 (current ~$169); current LuSH-101 street prices run $75–$101. The $199
  figure is flagged as "original list price" to avoid implying it is a current active
  price [R23][R25].
- **CORRECTION 4 — open-source niche framing (minor).** Claim 1 is best stated as
  **confirmed-with-caveat**. One open-source candidate (SourceForge "sh101") exists
  but is abandoned (last update 2016, 0 downloads, license unverified), so "effectively
  unoccupied" holds, but it is an **absence-of-evidence** conclusion rather than proof
  of total non-existence [R1].

### 8.2 Disputed / low-confidence items (now resolved)

- **Trademark goods/services class.** Originally *disputed/low* ("could not be
  confirmed from a primary source"). **Refuted and resolved** by USPTO TSDR: it is
  confirmable and is **Class 009** [R31]. No longer disputed.

### 8.3 Residual risks (live)

- **Justia page blocked.** The Justia trademark page returned HTTP 403 and could not
  be directly fetched; class/goods confirmation rests on USPTO TSDR plus search-snippet
  corroboration. The TSDR fetch was via a small summarizing model — **recommend a human
  spot-check of the live TSDR record** for the exact verbatim goods/services wording
  before any legal reliance [R31][R10].
- **Open-source absence-of-evidence.** The SourceForge "sh101" project's license could
  not be confirmed and it is unclear whether it ever produced a functioning VST. A
  low-profile or newly-published GitHub/LV2 project could exist outside search
  visibility [R1].
- **No SH-101-specific trade-dress filing located.** Roland's documented design-
  mark/trade-dress registrations are for the **TB-303, TR-808, and TR-909** — **not**
  specifically demonstrated for the SH-101 panel. The "Roland protects visual look"
  conclusion is supported by the 303/808 pattern, not by an SH-101-specific record
  [R32][R33].
- **Prices are point-in-time (June 2026)** and currency/region-dependent (USD vs EUR
  list prices differ; VAT-inclusive EU prices differ from US). The TAL list price was
  taken from a third-party retailer (Plugin Boutique) because the vendor page shows no
  price; official Softube/D16 vendor list prices were not fetched directly [R17].
- **Retailer pages blocked.** Sweetwater and Plugin Boutique product pages repeatedly
  returned HTTP 403, forcing reliance on search snippets and third-party trackers
  (musicsoftwaredeals) for some price points. Tracker MSRP figures are generally
  reliable but are not the manufacturer's primary listing [R25].

### 8.4 Open validation gaps and unanswered questions

- Confirm the **verbatim** goods/services description and **current LIVE status**
  (Section 8/15 maintenance filings) of US Serial 79216816 / Reg. 5505499 directly on
  USPTO TSDR before finalizing legal posture.
- Determine whether Roland holds any trademark or design/trade-dress registration
  **specific to the SH-101 panel** (grey/red/blue faceplate, slider layout, hand-grip),
  as opposed to only the TR-808/TB-303/TR-909 marks. Not located here.
- Check for **EU (EUIPO), UK, Japan** "SH-101" wordmark registrations and their goods
  classes in markets where mwAudio101 would distribute; Roland's design filings
  differed between the US and Germany.
- Assess whether "**mwAudio101**" itself risks confusing-similarity with "SH-101"
  (shared "101" + synth context). Worth a trademark-clearance opinion; note many
  "...101" clones coexist (the former "MS-101", LuSH-101, SN-101), suggesting the bare
  numeral carries limited source-identifying weight.
- Confirm sufficient **disclaimer language** for safe nominative use (e.g.,
  "mwAudio101 is an independent open-source emulation inspired by the Roland SH-101.
  Roland and SH-101 are trademarks of Roland Corporation. mwAudio101 is not affiliated
  with, authorized, or endorsed by Roland.") — **confirm with IP counsel before
  launch.**
- Confirm the SH-101's underlying **circuit patents are expired** (1982 product →
  almost certainly yes), which would leave only trademark/trade-dress as the live risk.
  Behringer's public stance relies on patent expiry; verify for the SH-101 specifically.

> **Project-decision gap.** mwAudio101 has **no physical-unit measurements**. Nothing
> in this market/legal dimension depends on bench data, but any cross-references from
> sister documents to measured behavior (Bode plots, ADSR curves, harmonic spectra)
> remain **open validation gaps**, not delivered facts. None of the above legal
> conclusions are a substitute for advice from a qualified IP attorney.

## 9. References

- [R1] SourceForge — "sh101" project (dormant; license unconfirmed):
  <https://sourceforge.net/projects/sh101/>
- [R2] Gearnews — Roland SH-101 plugins roundup:
  <https://www.gearnews.com/roland-sh-101-plugins-synth/>
- [R3] Roland — Roland Cloud SH-101 product page:
  <https://www.roland.com/global/products/rc_sh-101/>
- [R4] Roland Cloud — SH-101 catalog page:
  <https://www.rolandcloud.com/catalog/legendary/sh-101>
- [R5] TAL Software — TAL-BassLine-101:
  <https://tal-software.com/products/tal-bassline-101>
- [R6] Rekkerd — Softube Model 82 announcement:
  <https://rekkerd.org/softube-model-82-sequencing-mono-synth-emulation-of-roland-sh-101/>
- [R7] Synth Anatomy — Air Music Tech Iona:
  <https://synthanatomy.com/2025/10/air-music-tech-iona-a-roland-sh-101.html>
- [R8] Synth Anatomy — Behringer MS-1 MkII (V662A chip):
  <https://synthanatomy.com/2025/07/behringer-ms-1-mk2-new-roland-sh-101-clone-revision-with-v662a-chip.html>
- [R9] AMSynths — AM8101 SH-101 filter (trademark quote):
  <https://amsynths.co.uk/home/products/filters/am8101-sh-101-filter/>
- [R10] Justia Trademarks — SH-101 (Serial 79216816):
  <https://trademarks.justia.com/792/16/sh-79216816.html>
- [R11] plugins4free — TAL-BassLine listing:
  <https://plugins4free.com/plugin/688>
- [R12] Plugin Librarian — Roland SH-101:
  <https://www.pluginlibrarian.com/2025/10/roland-sh-101.html>
- [R13] Roland Cloud — SH-101 50% off:
  <https://www.rolandcloud.com/news/purchase-the-sh-101-software-synth-for-50-off>
- [R14] Synth Anatomy — Behringer's reaction to Roland trademarks:
  <https://synthanatomy.com/2019/07/behringer-s-reaction-roland-trademarks.html>
- [R15] Synth Anatomy — TAL-BassLine-101 update 3.8.1:
  <https://synthanatomy.com/2023/09/tal-bassline-101-new-update-3-8-1-refines-the-engine-with-new-filter-and-distortion-fx.html>
- [R16] Syntorial — TAL-BassLine-101 highlight:
  <https://www.syntorial.com/highlights/tal-bassline-101/>
- [R17] Plugin Boutique — TAL-BassLine-101 (price):
  <https://www.pluginboutique.com/product/4-Synth/716-TAL-BassLine-101>
- [R18] MusicRadar — Softube Model 82:
  <https://www.musicradar.com/news/softube-model-82-synth-plugin-sh-101>
- [R19] Bedroom Producers Blog — Softube Model 82 deal:
  <https://bedroomproducersblog.com/2026/04/14/softube-model-82-deal-pb/>
- [R20] Plugin Librarian — Softube Model 82 ($99 list):
  <https://www.pluginlibrarian.com/2025/11/softube-model-82-sequencing-mono-synth.html>
- [R21] Rekkerd — Softube Model 82 sale:
  <https://rekkerd.org/softube-model-82-synthesizer-instrument-sale/>
- [R22] Rekkerd — D16 LuSH-101 review:
  <https://rekkerd.org/review-d16-group-lush-101-multi-timbral-polyphonic-synthesizer-plugin/>
- [R23] Sweetwater — D16 LuSH-101:
  <https://www.sweetwater.com/store/detail/LuSH101--d16-group-lush-101-analog-synthesizer-plug-in>
- [R24] Synth Anatomy — D16 Lush-2 deal:
  <https://synthanatomy.com/2024/09/deal-d16-group-lush-2-roland-sh-101-inspired-multi-layer-synth-plugin-74-off.html>
- [R25] musicsoftwaredeals — LuSH-101 price history:
  <https://musicsoftwaredeals.com/price-history/lush-101-by-d16-group/>
- [R26] Synth Anatomy — Cherry Audio SH-MAX review:
  <https://synthanatomy.com/2026/02/cherry-audio-sh-max-review-three-classic-roland-sh-synths-reborn-as-a-plugin.html>
- [R27] Synthtopia — Behringer MS-1 price cut to $199:
  <https://www.synthtopia.com/content/2024/01/23/behringer-ms-1-synthesizer-price-cut-to-199-if-you-can-find-it/>
- [R28] Synthtopia — Behringer MS-101 (as announced, $299):
  <https://www.synthtopia.com/content/2019/01/25/behringer-ms-101-synth-roland-sh-101-clone-coming-in-march-for-299/>
- [R29] AMSynths — SN-101 modular synth:
  <https://amsynths.co.uk/home/synthesizers/sn-101-modular-synth/>
- [R30] Synth Anatomy — AMSynths SN-101:
  <https://synthanatomy.com/2022/03/amsynths-sn-101-the-analog-sh-101-filter-in-a-eurorack-module.html>
- [R31] USPTO TSDR — SH-101 status (Serial 79216816; Class 009):
  <https://tsdr.uspto.gov/statusview/sn79216816>
- [R32] Gearnews — Roland registers the look of the TR-808/TB-303:
  <https://www.gearnews.com/roland-registers-the-look-of-the-tr-808-and-tb-303-as-trademarks/>
- [R33] Synth Anatomy — Roland registers TB-303/TR-808 as trademarks:
  <https://synthanatomy.com/2019/02/roland-registered-tb-303-tr-808-as-trademarks-clone-wars-news.html>
- [R34] Synth Anatomy — Behringer's reaction (RD-8 colors):
  <https://synthanatomy.com/2019/07/behringer-s-reaction-roland-trademarks.html>
- [R35] CDM — Roland 303/808 trademarks:
  <https://cdm.link/roland-303-808-trademarks/>
- [R36] The Quietus — Roland trademark 303/808 designs:
  <https://thequietus.com/articles/26010-roland-trademark-303-808-designs>
- [R37] Synthtopia — Behringer/Roland legal battle settled:
  <https://www.synthtopia.com/content/2006/04/11/behringerroland-legal-battle-settled/>
- [R38] Mix Online — Behringer and Roland settle lawsuit:
  <https://www.mixonline.com/technology/behringer-and-roland-settle-lawsuit-381972>
- [R39] Wikipedia — Behringer (litigation history):
  <https://en.wikipedia.org/wiki/Behringer>
- [R40] Lexology — open-source licenses and trademarks:
  <https://www.lexology.com/library/detail.aspx?g=9d96e1bf-bced-48f7-b5b4-ee561e7a9348>
- [R41] TWiki Blog — GPL and trademarks:
  <https://twiki.org/cgi-bin/view/Blog/BlogEntry201207x1>
- [R42] Modwiggler forum — clone IP discussion:
  <https://modwiggler.com/forum/viewtopic.php?t=86083>
- [R43] Wikipedia — Nominative use:
  <https://en.wikipedia.org/wiki/Nominative_use>
- [R44] INTA — Fair use of trademarks (non-legal audience):
  <https://www.inta.org/fact-sheets/fair-use-of-trademarks-intended-for-a-non-legal-audience/>
- [R45] Harrigan IP — Nominative fair use in trademark law:
  <https://harriganip.com/blog/nominative-fair-use-trademark-law/>
