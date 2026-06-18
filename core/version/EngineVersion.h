// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/version/EngineVersion.h — engine/render/schema/plugin version constants (task 016).
// Realizes docs/design/06 §9.1/§9.2 and ADR-023 V1-V4.

#pragma once

namespace mw101::version {

// Versions the state SHAPE; drives the migration chain [docs/design/06 §9.1; ADR-008 C9-C10].
inline constexpr int kCurrentSchemaVersion = 1;

// Versions rendered AUDIO ("these parameters render these samples"). It is
// MONOTONICALLY INCREASING and bumps ONLY when a bless changes a blessed artifact
// hash (CLASS-EXACT) or moves a CLASS-FP artifact outside its tolerance band
// [ADR-023 V3, V5]. It is ORTHOGONAL to schemaVersion: a pure DSP re-tune bumps
// renderVersion only; a parameter-shape change bumps schemaVersion only; a
// DSP-only change MUST NOT add a no-op migration step [ADR-023 V4].
inline constexpr int kCurrentRenderVersion = 1;

// Human-facing MAJOR.MINOR.PATCH string. INFORMATIONAL ONLY — it MUST NOT trigger
// state migration [ADR-023 V1-V2]. MAJOR = intentional sonic redesign; MINOR =
// audio-altering change that bumps renderVersion; PATCH = proven not to alter any
// blessed artifact.
inline constexpr const char* kEngineVersion = "1.0.0";

// Marketing version, distinct from engineVersion [docs/design/06 §9.1; ADR-008 C9].
inline constexpr const char* kPluginVersion = "1.0.0";

} // namespace mw101::version
