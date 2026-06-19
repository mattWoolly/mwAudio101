// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/ControlDriverConstants.h — the ControlCore advance()-driver
// (PI) tunable-constant block (task 071).
//
// This header is part of THE single cross-module (PI) constants table whose root is
// core/calibration/Calibration.h; it #includes that root and APPENDS into a new
// `mw::cal::control` namespace (a sibling of mw::cal::pitch, which task 070 reserved
// for the VINTAGE pitch law), so the headers compose additively without editing the
// shared Calibration.h [docs/design/06 §3.10; docs/design/00 §8.3; ADR-020 S13]. No
// ControlCore call site inlines any literal below.
//
// PROVENANCE: the control-loop *period envelope* (1.5-3.5 ms firmware loop, ~2 ms
// nominal) is documented circuit behavior [research/07 §2.3; docs/design/04 §7.4],
// so kVintageTickSeconds and the jitter envelope carry a research trace. The MODERN
// clean sub-block tick length and the macro-crossfade time constant are NOT in
// research — the hardware does not dictate them [ADR-005 §Consequences "we now own
// (and must justify) the clean control-tick rate"; docs/design/04 §7.5] — so those
// are tagged (PI) pragmatic inventions.

#pragma once

#include "Calibration.h"

namespace mw::cal {

// ---------------------------------------------------------------------------
// §7.4 / §7.5 — ControlCore advance() driver constants.
// ---------------------------------------------------------------------------
namespace control {

    // §7.4 VINTAGE fixed control tick. The firmware polling loop runs every
    // 1.5-3.5 ms with a ~2 ms nominal; the fixed-tick, jitter-OFF configuration is
    // the bit-exact macOS arm64 reference / Linux co-gate variant
    // [docs/design/04 §7.4; research/07 §2.3; ADR-005 §Decision item 3, C1].
    inline constexpr double kVintageTickSeconds = 0.002;   // ~2 ms nominal [research/07 §2.3]

    // §7.4 loop-time jitter envelope: the documented 1.5-3.5 ms firmware-loop span.
    // OFF by default; the jitter magnitude is an OPEN VALIDATION GAP, so this is a
    // labeled flavor toggle, not asserted SH-101 fidelity
    // [docs/design/04 §7.4; research/07 §2.3, §8.5; ADR-005 C2].
    inline constexpr double kVintageJitterMinSeconds = 0.0015;  // 1.5 ms [research/07 §2.3]
    inline constexpr double kVintageJitterMaxSeconds = 0.0035;  // 3.5 ms [research/07 §2.3]

    // §7.5 MODERN clean fixed sub-block tick length, in SAMPLES. (PI) 16-32 smp;
    // the hardware does not dictate it [docs/design/04 §7.5; ADR-005 C3, §Consequences].
    // 24 sits in the middle of the documented 16-32 design range.
    inline constexpr int kModernTickSamples = 24;   // (PI) 16..32 smp [ADR-005 C3]
    inline constexpr int kModernTickMinSamples = 16; // (PI) design-range floor [ADR-005 C3]
    inline constexpr int kModernTickMaxSamples = 32; // (PI) design-range ceiling [ADR-005 C3]

    // §7.7 macro-automation VINTAGE<->MODERN crossfade time constant (seconds). When
    // the Vintage Control macro is automated, both CV branches are precomputed and the
    // blend coefficient slews toward the target pole over this constant so there is no
    // zipper. (PI) — no hardware analog [docs/design/04 §7.7; ADR-005 C7, §Consequences].
    inline constexpr double kMacroCrossfadeSeconds = 0.010;  // (PI) ~10 ms click-free blend

    // §7.7 crossfade snap epsilon: when |blend - target| falls below this the blend
    // snaps to the target so the integer "is-crossfading" state is deterministic and
    // the steady-state branch is bit-exact (mirrors cal::smoothing::kSnapThreshold).
    // (PI) [docs/design/04 §7.7; ADR-020 S10].
    inline constexpr float kMacroCrossfadeSnapEpsilon = 1.0e-6f;  // (PI)

} // namespace control

} // namespace mw::cal
