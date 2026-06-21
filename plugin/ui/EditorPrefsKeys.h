// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// plugin/ui/EditorPrefsKeys.h — the <extras> UI-preference attribute key(s) the editor
// persists through the processor's state I/O (task 115).
//
// The reduce-motion / low-CPU toggle is a UI PREFERENCE, not a host parameter, so it
// persists in the canonical MW101_STATE <extras> subtree exactly like the advisory
// editor size (task 114) — NOT on the audio-thread Extras POD [docs/design/10-ui.md §10;
// docs/design/06 §5.4; ADR-008 §4/§5 C8]. The key lives in this NEW dedicated header
// (rather than the shared core/state/StateTree.h) so the parallel fleet does not
// serialize on that aggregate; the processor reads/writes the property directly on the
// canonical tree's <extras> node in get/setStateInformation, mirroring how it threads
// the editor size through the canonical serializer.
//
// Message-thread only: this key is touched solely from get/setStateInformation and the
// editor's reduce-motion setter — never from the audio thread.

#pragma once

namespace mw::plugin::ui::prefs {

// bool: reduce-motion / low-CPU toggle state, persisted in <extras> [§10; ADR-015 C8].
inline constexpr const char* kExtrasReduceMotion = "reduceMotion";

} // namespace mw::plugin::ui::prefs
