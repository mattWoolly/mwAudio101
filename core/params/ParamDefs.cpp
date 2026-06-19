// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Matt Woolly
//
// core/params/ParamDefs.cpp — anchor translation unit for the declarative parameter
// registry (task 019). The registry itself is header-only constexpr data; this TU
// exists so the table and its §3.1 compile-time static_assert invariants are compiled
// into mwcore (a header with no consumer is never instantiated). It contributes no
// runtime symbols beyond a single ODR-safe touch of kParamDefs.size().

#include "ParamDefs.h"

namespace mw::params {

// One ODR-safe reference so the table is materialized in this TU and the
// static_assert block in the header is evaluated at library build time.
const std::size_t kParamDefsCount = kParamDefs.size();

} // namespace mw::params
