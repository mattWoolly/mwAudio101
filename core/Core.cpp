// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/Core.cpp — translation unit anchoring the mwcore static library.
//
// The foundation core is otherwise header-only; this TU gives mwcore a real
// object file (so it is a genuine STATIC lib that links mw_fp_discipline and that
// the no-JUCE-in-core guard and fp_discipline_guard can scan), and force-includes
// the foundation headers so they are compiled (not merely parsed by tests).

#include "BlockContext.h"
#include "calibration/Calibration.h"
#include "params/ParamIDs.h"
#include "params/Smoother.h"
#include "params/SmoothingClass.h"
#include "state/Extras.h"
#include "state/StateTree.h"
#include "util/Prng.h"
#include "version/EngineVersion.h"

namespace mw {

// A non-trivial symbol so the archive is non-empty on every toolchain.
int mwCoreFoundationAbiTag() noexcept {
    return mw101::version::kCurrentRenderVersion;
}

} // namespace mw
