// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/state/StateTree.h — canonical root-tree identifiers + attribute keys (task 017).
// Realizes docs/design/06 §5.1. These are the string keys for the ONE canonical
// ValueTree; (de)serialization itself is owned by the param-state stream.

#pragma once

namespace mw::state {

// Root element [docs/design/06 §5.1].
inline constexpr const char* kRootId = "MW101_STATE";

// Root attribute keys.
inline constexpr const char* kAttrSchemaVersion = "schemaVersion";  // int; state SHAPE [ADR-008 C9-C10]
inline constexpr const char* kAttrPluginVersion = "pluginVersion";  // string; marketing
inline constexpr const char* kAttrEngineVersion = "engineVersion";  // string; informational [ADR-023 V1-V2]
inline constexpr const char* kAttrRenderVersion = "renderVersion";  // int; rendered AUDIO [ADR-023 V3-V4]

// Child element ids.
inline constexpr const char* kParamsId  = "PARAMS";   // APVTS state ValueTree
inline constexpr const char* kExtrasId  = "extras";   // §5.4 non-parameter state
inline constexpr const char* kSeqId     = "seq";      // 100-step pattern (§5.5)
inline constexpr const char* kCcLearnId = "ccLearn";  // CC# -> param index bindings
inline constexpr const char* kStepId    = "step";     // a single <seq> step element

// <extras> attribute keys [docs/design/06 §5.4].
inline constexpr const char* kExtrasArpLatch    = "arpLatch";    // bool
inline constexpr const char* kExtrasDriftSeed   = "driftSeed";   // int64
inline constexpr const char* kExtrasSeedLocked  = "seedLocked";  // bool
inline constexpr const char* kExtrasUiWidth     = "uiWidth";     // int (advisory)
inline constexpr const char* kExtrasUiHeight    = "uiHeight";    // int (advisory)
inline constexpr const char* kExtrasRenderOptIn = "renderOptIn"; // bool (sticky, §9)
inline constexpr const char* kExtrasRawNewerBlob = "rawNewerBlob"; // binary (optional, §8 L6)

} // namespace mw::state
