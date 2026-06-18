// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/calibration/TelemetryConstants.h — (PI) sizing constants for the UI
// telemetry SPSC path (task 107).
//
// Per docs/design/10-ui.md §8.3, the scope-point count, FIFO ring depth, and the
// per-frame scope decimation factor are (PI) pragmatic inventions — NOT measured
// SH-101 specs — and must centralize in calibration rather than be inlined at the
// telemetry call site [docs/design/10-ui.md §8.3; ADR-015 C5/C12]. They live in
// this NEW dedicated header (not Calibration.h, which the orchestrator wires the
// include into later) so the parallel fleet does not serialize on the shared
// aggregate table.
//
// kScopePoints  : decimated waveform sample count carried per Snapshot.
// kFifoCapacity : pre-allocated lock-free SPSC ring depth (overrun overwrites
//                 oldest, never blocks the audio thread).
// kScopeDecimation : per-frame source-sample decimation factor used by the
//                 producer when filling the scope array (display-only).

#pragma once

namespace mw::cal::telemetry {

// (PI) — decimated scope waveform length per telemetry frame [docs/design/10 §8.3].
inline constexpr int kScopePoints = 256;

// (PI) — pre-allocated lock-free SPSC ring depth [docs/design/10 §8.3; ADR-015 C5].
inline constexpr int kFifoCapacity = 4;

// (PI) — per-frame scope decimation factor (source samples per scope point)
// [docs/design/10 §8.3]. Display-only; never gates the audio thread.
inline constexpr int kScopeDecimation = 8;

} // namespace mw::cal::telemetry
