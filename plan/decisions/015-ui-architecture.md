<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Matt Woolly
-->

# ADR 015: GUI architecture — resizable vector signal-flow UI, APVTS-bound, audio-decoupled

Status: accepted
Date: 2026-06-17

## Context

mwAudio101 needs a UI architecture. The question spans: the JUCE Component tree; a
modern, signal-flow-inspired vector look that is deliberately NOT an SH-101 faceplate
replica (maximum trademark distance); resizable + hi-DPI rendering; APVTS
attachment/binding for every control; a preset browser; how the UI stays decoupled
from the audio engine; and the asset strategy (vector vs raster).

Forces:

- The look is a legal posture, not only an aesthetic choice. The trademark/trade-dress
  research is explicit: do **not** pixel-clone Roland's faceplate; Roland registers
  and litigates the visual look (panel layout + color sequence), GPLv3 grants zero
  trademark cover, and every successful clone differentiates the look with its own
  palette, typography, and re-laid-out controls
  (`docs/research/12-market-legal-landscape.md` §5.2, §5.3, §5.4, §7.2).
- Function is not protectable; distinctiveness must live in styling, not workflow
  (`docs/research/12-market-legal-landscape.md` §7.2). The slider-per-parameter
  ergonomics that make the SH-101 instantly editable can — and should — be kept.
- The cross-platform mandate (macOS arm64 reference/bless, Linux x64 hard gate,
  Windows x64 goal) means the render path must behave consistently across JUCE's
  platform backends, including software rendering on Linux/X11/Wayland and
  Windows per-monitor-v2 DPI, without shipping an `@1x/@2x/@3x` raster matrix.
- The owner-locked real-time rule (no heap allocation, no locks on the audio thread)
  is as much a GUI-architecture constraint as a DSP one: if the editor ever reaches
  into engine state under a lock, or the engine allocates to hand the GUI data, the
  lock is violated.
- Scope item: a preset browser over ~64 factory presets, full automation, and host
  state recall are modern essentials the entire commercial field treats as table
  stakes.

This ADR touches several owner-locked decisions and **re-affirms** them; it reverses
none:

- **Modern reimagined UI, maximum trademark distance** — honored by construction (a
  signal-flow flow-diagram look in a non-Roland palette is a different visual genre
  from the faceplate, not a recolored clone).
- **Real-time safe: no heap allocation / no locks on the audio thread** — enforced by
  a one-directional UI/engine boundary (editor writes only via APVTS atomics; reads
  only via a pre-allocated lock-free SPSC telemetry path).
- **JUCE / C++20 / CMake; formats VST3, AU(macOS), CLAP, Standalone; LV2 = goal-tier**
  — the editor is a single format-agnostic `AudioProcessorEditor` shared by every
  wrapper (consistent with ADR 011's one-engine / thin-shell model).
- **GPL-3.0-or-later** — vector-in-code plus a few SVG glyphs avoids large bundled
  raster art and its licensing ambiguity, and keeps the binary small.
- **Feature scope** (poly/unison, host-synced arp + 100-step seq, ~64 presets, full
  automation, MPE-lite) — the layout and binding model are built to host all of it.

## Options considered

Two personas were on the panel. They converged on the same fundamental architecture —
a single resizable JUCE Component tree, rendered as resolution-independent vector
graphics in a signal-flow layout, every control APVTS-bound, the audio engine decoupled
via APVTS attachments (write) plus a lock-free SPSC snapshot (read), vector-first assets
with SVG only for static art. The panel did **not** split on the decision. It split only
on detail — the scaling mechanism, repaint discipline, and where the preset store lives
— and those details were merged.

### Persona: UX-modern (clean modern resizable vector UI, preset browser, usability)

Approach: an editor root owning an APVTS reference plus a top-level `LayoutManager`
driving a constraint/grid `resized()` with no fixed pixel coordinates; the panel
decomposed into Component "modules" mirroring the documented signal chain
(MODULATOR -> VCO -> SOURCE MIXER -> VCF -> VCA) plus a CONTROLLER strip and a
transport/mode bar, connected by drawn signal-flow "patch lines" so the UI teaches the
architecture. A custom `LookAndFeel` draws every control with `juce::Path`/`Graphics`
primitives parameterized by a single design-token table (palette, stroke weights,
corner radii, font) in a deliberately non-Roland palette. Resizable via
`setResizable(true)` + a constrainer with a fixed aspect ratio and stored size in APVTS
state. Every control binds through APVTS attachments. A processor-owned `PresetManager`
holds ~64 factory presets as APVTS state trees; the editor's `PresetBrowser` is a thin
view that lists/filters/loads by calling the manager and listening to a change
broadcaster. Engine->UI telemetry (meters/scope) flows through a lock-free atomic FIFO
drained by a ~30-60 Hz Timer.

- Pros: maximum trademark distance by construction (different visual genre, swappable
  non-Roland palette); hi-DPI and resize solved once with vector paths; total
  UI/engine decoupling honoring the no-alloc/no-lock rule; free full automation, undo,
  and host state recall via APVTS; the preset browser is the single biggest
  modern-essential usability win and is cheap because APVTS already serializes state;
  a single design-token table makes reskinning/theming/accessibility trivial;
  self-documenting patch lines reinforce the transparency/hackability differentiation
  lever (`docs/research/12-market-legal-landscape.md` §7.3).
- Cons: a custom vector `LookAndFeel` + signal-flow layout is meaningfully more upfront
  design/engineering than dropping raster knob filmstrips and can look amateur if
  undercooked; vector rendering of many simultaneously-animated controls (sliders,
  meters, scope, 100-step grid) can be CPU-heavy on the message thread; a flow-diagram
  layout is less instantly familiar to SH-101 veterans and needs careful information
  hierarchy; aspect-locked resizable layout needs disciplined constraint code; the
  lock-free FIFO + repaint Timer are extra moving parts.

Critiques adopted: the processor-owned `PresetManager` with a thin `PresetBrowser`
view (the preset store must NOT live in the editor); the single design-token table as
the one reskin/theme/contrast knob; the explicit signal-flow module decomposition
matching the documented chain; stored window size persisted in APVTS state; the
information-hierarchy caution recorded as a design obligation so the modern look does
not sacrifice the SH-101's at-a-glance editability.

### Persona: performance-gfx (efficient repaint, hi-DPI, no audio-thread coupling)

Approach: a single resizable Component tree driven by `AffineTransform` scaling over
ONE logical design coordinate space (e.g. 1000x640 design units), explicitly NOT
JUCE's deprecated `setScaleFactor`/per-pixel-density hacks. The signal-flow look drawn
almost entirely in vector code (`juce::Path`/`Graphics`/`Drawable` + `DropShadow`, with
`Slider`/`Button` subclasses under a custom `LookAndFeel`). SVG only for static
decorative/logo art via `Drawable::createFromSVG`; zero raster faceplate bitmaps.
Hi-DPI is automatic because vectors re-rasterize per physical-pixel transform; expose a
resize range with a fixed aspect ratio and snap-to-scale presets (75/100/150/200%). The
editor owns nothing the engine touches: parameters bind via APVTS attachments;
audio-derived visuals flow one-way through a lock-free SPSC ring/atomic snapshot the
engine fills, read by a 30-60 Hz Timer. Dirty-rect / per-sub-component repaint
discipline: each control repaints only its own bounds; expensive static layers
(background flow-graph, labels, patch lines) cached into a `juce::Image` regenerated
only on resize; an optional `juce::OpenGLContext` attachable but OFF by default (the
software path must hit frame budget first).

- Pros: resolution-independent from one design space, no raster asset matrix; maximum
  trademark distance for free (coded vector look is provably original); audio thread
  fully decoupled and provably RT-safe; small binary and clean GPL compliance;
  cross-platform parity via pure software rendering with OpenGL kept optional (not a
  Linux-driver liability); cached static layers + per-control dirty repaint keep CPU
  near zero when idle and bounded under automation.
- Cons: vector path rendering is more CPU-intensive per repaint than blitting a cached
  bitmap; a naive full-window 60 Hz repaint of a dense flow-graph can spike the message
  thread; designing the look entirely in code is more effort and harder to iterate than
  a designed faceplate image; software-only rendering on weak Linux GPUs / very high-res
  displays may miss a 60 Hz budget for animated elements without the optional OpenGL
  path; software-path antialiasing/gradient quality varies across platforms — GUI
  rendering is NOT pixel-identical across macOS/Linux/Windows (only the audio is
  bit-exact); requires disciplined repaint hygiene or a contributor can silently
  regress frame cost by calling `repaint()` on the whole editor.

Critiques adopted: the single design-coordinate-space + `AffineTransform` scaling as
the canonical hi-DPI/resize mechanism (explicitly rejecting the deprecated
`setScaleFactor` route); strict per-control dirty-rect repaint plus a cached static-layer
`juce::Image` regenerated only on resize; the single coalescing 30-60 Hz telemetry
Timer rather than per-sample callbacks; `juce::OpenGLContext` present as an opt-in
escape hatch but **OFF by default** so the software path must meet budget first and
OpenGL never becomes a Linux-driver dependency for the hard gate; the explicit honest
caveat that GUI rendering is not pixel-identical across platforms (the bless covers
audio, not pixels); a "reduce motion / low-CPU" toggle gating meters/scope for the worst
case.

### Resolution

The two positions are one architecture seen through a usability lens and a
rendering-performance lens. The decision merges them: UX-modern's processor-owned
`PresetManager`/thin `PresetBrowser`, design-token table, and explicit signal-flow
module decomposition; plus performance-gfx's single design-coordinate-space +
`AffineTransform` scaling, dirty-rect/cached-static-layer repaint discipline, coalescing
telemetry Timer, and OpenGL-optional-OFF discipline. No raster-faceplate or
fixed-pixel-layout alternative was credible: a faceplate replica is the exact
trade-dress posture the research warns against (§7.2) and is also a hi-DPI/resize dead
end.

## Decision

Build the editor as a **single, format-agnostic `juce::AudioProcessorEditor`** holding a
JUCE Component tree, laid out over **one logical design coordinate space** (nominal
1000x640 design units; final aspect ratio fixed during the design pass) and scaled to
any window size / display DPI by a single `AffineTransform` — **not** the deprecated
`setScaleFactor`/per-pixel-density path. There are **zero hard-coded pixel coordinates**;
layout is constraint/grid-driven in `resized()`.

The look is a **modern signal-flow diagram, NOT an SH-101 faceplate replica**, which is
the maximum-trademark-distance posture mandated by the owner-lock and validated by
`docs/research/12-market-legal-landscape.md` §5.2 (Roland registers and litigates panel
layout + color sequence), §5.4 (GPLv3 grants no trademark cover), and §7.2 (give the
product its own palette, its own typography, and a re-laid-out control arrangement;
function/workflow is not protectable, so distinctiveness lives in styling). The panel is
decomposed into Component "modules" mirroring the documented signal chain — MODULATOR ->
VCO -> SOURCE MIXER -> VCF -> VCA — plus a CONTROLLER strip and a transport/mode bar
hosting the host-synced arp and 100-step sequencer, with the modules connected by drawn
vector "patch lines" so the UI teaches the modeled architecture (reinforcing the
transparency/hackability differentiation lever, §7.3). The functional
slider-per-parameter ergonomics are retained deliberately (§7.2: function is not
protectable; keep the workflow, redraw the look). The palette is deliberately **NOT**
Roland grey/red/blue.

**Rendering and assets are vector-first.** Every control is drawn in code via a custom
`juce::LookAndFeel` over `juce::Path`/`Graphics`/`Drawable`, parameterized by a single
**design-token table** (palette, stroke weights, corner radii, typography) so one table
swap reskins the entire plugin and serves theming/accessibility (contrast, scale). The
**only** raster/SVG assets are static decorative art and the logo, loaded as SVG via
`juce::Drawable::createFromSVG` (compiled through `BinaryData`); there are **zero raster
faceplate bitmaps and no `@2x/@3x` raster matrix**. Because vector paths re-rasterize at
the physical-pixel transform, hi-DPI is automatic and identical across JUCE's
macOS/Linux/Windows backends. (Honest caveat: GUI rendering is not guaranteed
pixel-identical across platforms — software-path antialiasing/gradients vary; only the
audio is bit-exact per ADR 011.)

**Resizable + hi-DPI:** `setResizable(true)` with a constrainer enforcing a fixed aspect
ratio, snap-to-scale presets (75/100/150/200%), and the last window size persisted in
APVTS state so it round-trips with the session.

**Binding:** every control binds through APVTS attachments
(`SliderAttachment`/`ButtonAttachment`/`ComboBoxAttachment`). The editor holds **zero
audio-domain state** and never calls the processor's DSP directly. This gives full
automation, host undo, and DAW state save/restore with no bespoke glue and satisfies the
"full automation" scope item.

**Preset browser:** a `PresetManager` lives in the **processor** (not the editor) and
owns the ~64 factory presets as APVTS state trees. The editor's `PresetBrowser` is a
thin view that lists/filters/loads by calling the manager and listening to a change
broadcaster. The manager builds and swaps state trees on the **message thread only** and
hands the audio thread parameter *values* via APVTS atomics — never a tree pointer — so
a preset load never allocates or frees on the audio thread.

**UI<->engine decoupling is one-directional and RT-safe by construction** (re-affirming
the owner-locked no-heap-alloc / no-locks-on-audio-thread rule):

- The editor **writes** only via APVTS attachments (lock-free atomic parameter
  transport on the message thread); it never takes a lock the audio thread holds and
  never allocates on the audio thread because it never runs there.
- The editor **reads** audio-derived visuals (meters, scope, mod-source indicators)
  only from a **pre-allocated, fixed-capacity, lock-free single-producer/single-consumer
  snapshot/FIFO** that the audio thread *writes to only* (no blocking, no allocation),
  drained by a single coalescing `juce::Timer` at **30-60 Hz** on the message thread.
- The audio callback never locks, never allocates for the GUI, and holds no reference to
  any GUI object. A GUI frame overrun can therefore cause visual jank only — never an
  xrun.

**Repaint discipline** (the real cost is message-thread, not audio-thread): static
layers — panel chrome, patch lines, labels — are cached into a `juce::Image` regenerated
**only on resize**; only moving control overlays repaint, and each control repaints only
its own bounds (strict dirty-rect, never a whole-editor `repaint()`); parameter-change
repaints are coalesced; the telemetry Timer is capped at 30-60 Hz. A "reduce motion /
low-CPU" toggle gates meters/scope for the worst case. A `juce::OpenGLContext` is an
**opt-in escape hatch that is OFF by default** — the software path must meet the frame
budget first, so OpenGL never becomes a driver dependency for the Linux x64 hard gate.

## Consequences

This commits us to:

- A from-scratch vector design system: a custom `LookAndFeel`, a design-token table, and
  a real visual-design pass for the signal-flow layout and patch-line aesthetic. This is
  more upfront effort than raster knob filmstrips and must clear a quality bar (an
  undercooked vector UI looks amateur).
- Disciplined constraint-based layout over one design coordinate space with
  `AffineTransform` scaling and a fixed aspect ratio; ad-hoc pixel math is prohibited and
  will rot on the first corner-drag.
- A repaint-hygiene regime (cached static layers, per-control dirty-rect, coalesced
  repaints, capped Timer) that contributors must uphold; a stray full-editor `repaint()`
  is a frame-cost regression to guard in review.
- A pre-allocated lock-free SPSC telemetry path (correct sizing and overrun handling) and
  a processor-owned `PresetManager` that mutates trees on the message thread only.
- The editor as a single format-agnostic surface shared by every wrapper (VST3, AU, CLAP,
  Standalone, goal-tier LV2), inheriting ADR 011's one-engine/thin-shell model so the
  bless reference and host-facing semantics are not forked per format.

This forecloses / makes harder:

- **No raster faceplate, ever** — and GUI rendering is not pixel-identical across
  platforms (software-path AA/gradients vary). Any future "skin matches the hardware
  exactly" request is out of scope by design and by legal posture; the bless covers
  audio, not pixels.
- **OpenGL stays off by default** — animation-heavy views on weak Linux GPUs / very
  high-res displays must be made to meet budget on the software path first; turning
  OpenGL on globally would re-introduce a driver-portability liability the hard gate
  must not depend on.
- A flow-diagram layout is less instantly familiar to SH-101 veterans than a literal
  slider strip, so information hierarchy must be designed carefully to preserve the
  at-a-glance editability that is the instrument's appeal.

Owner ratification item: confirm that the UI is a **signal-flow reimagining in a
non-Roland palette (no faceplate replica, no hardware-skin option)** — this is the direct
implementation of the owner-locked "max trademark distance" rule and the §7.2 trade-dress
guidance, but it sets a user-facing expectation (no nostalgic faceplate skin) that should
be signed off explicitly so it is not relitigated as a feature request. The disclaimer /
naming posture itself (product not named "SH-101", no Roland logo, nominative reference
only) is owned by the naming/legal track per §5.5 and §7.1 and is not redecided here.

## Contract

The backlog implements the following normative cases verbatim.

| # | Case | Required behavior |
| --- | --- | --- |
| C1 | Window resized or moved to a different-DPI display | UI re-renders crisp at the new size/DPI via a single `AffineTransform` over the design coordinate space; no raster asset swap; aspect ratio stays fixed by the constrainer. |
| C2 | User picks a scale preset (75/100/150/200%) | Window snaps to that logical scale; the new size persists in APVTS state and round-trips on session reload. |
| C3 | Any control moved by the user | The control's APVTS attachment writes the parameter (atomic, message thread); the engine reads it lock-free; no editor->DSP direct call. |
| C4 | Host automates / recalls a parameter | The APVTS attachment moves the control to match; the editor reads parameter changes only via APVTS listeners, never by polling the engine. |
| C5 | Engine produces meter/scope/mod telemetry | Audio thread writes a fixed-size, pre-allocated, lock-free SPSC snapshot only (no alloc, no lock); the message-thread Timer drains it at 30-60 Hz and triggers targeted repaints. |
| C6 | Preset loaded from the browser | `PresetManager` (in the processor) builds/swaps the state tree on the message thread and hands the audio thread parameter values via APVTS atomics; no alloc/free and no tree-pointer crosses to the audio thread. |
| C7 | A control needs repaint | Only that control's bounds invalidate (dirty-rect); static layers (chrome, patch lines, labels) are served from a cached `juce::Image` regenerated only on resize; whole-editor `repaint()` is disallowed. |
| C8 | "Reduce motion / low-CPU" toggle enabled | Meters/scope animation is suppressed or downsampled; control bindings and automation remain fully functional. |
| C9 | OpenGL availability | `juce::OpenGLContext` is opt-in and OFF by default; the software render path must meet the frame budget without it; the Linux x64 hard gate must not require OpenGL. |
| C10 | Reskin / theme change | Swapping the single design-token table (palette/stroke/radius/font) restyles every control without touching layout or binding code; the palette is never Roland grey/red/blue. |
| C11 | Asset load | Only static decorative art and the logo are SVG (via `Drawable::createFromSVG` / `BinaryData`); no raster faceplate bitmaps and no `@2x/@3x` raster matrix exist in the build. |
| C12 | Audio-thread inspection under any UI activity | The audio callback holds no GUI reference, takes no lock, and performs no heap allocation for the GUI; a GUI frame overrun degrades visuals only and can never cause an xrun (asserted in debug/CI where feasible). |
