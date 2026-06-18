# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/CompilerFlags.cmake — the single mw_fp_discipline INTERFACE target.
#
# Realizes docs/design/11 §11 and ADR-014 C4 / ADR-001 C12. ONE INTERFACE target
# carries the FROZEN floating-point-discipline flags, linked by mwcore, the
# DSP-bearing plugin TUs, and the golden tests so golden-compare math matches the
# shipped core exactly. The flags are FROZEN by the design spec §5 — this file
# ENFORCES them, never relaxes them. Forbidden flags (-ffast-math / -Ofast /
# /fp:fast / -ffp-contract=fast) are NEVER added here, and the fp_discipline_guard
# ctest (tests/) mechanically greps compile_commands.json to prove it.

add_library(mw_fp_discipline INTERFACE)
add_library(mw::fp_discipline ALIAS mw_fp_discipline)

if(MSVC)
  # MSVC: precise FP, no contraction; never /fp:fast [docs/design/11 §11].
  target_compile_options(mw_fp_discipline INTERFACE
    /fp:precise
    /fp:contract-)
else()
  # GCC / Clang / AppleClang [docs/design/11 §11; ADR-014 C4].
  # -fdenormal-fp-math=ieee: runtime FTZ/DAZ flush is set in process() per ADR-001
  #  C11, never left to a build flag that could silently change golden output.
  target_compile_options(mw_fp_discipline INTERFACE
    -fno-fast-math
    -ffp-contract=off
    -fno-finite-math-only
    -fno-associative-math
    -fno-reciprocal-math
    -fexcess-precision=standard)

  # -fexcess-precision=standard and -fdenormal-fp-math=ieee are not accepted by
  # every front-end (Apple Clang accepts the former; the latter is a Clang/LLVM
  # flag). Add each only where the active compiler accepts it, so the FROZEN set
  # is honored without breaking the configure on a front-end that lacks one.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-fdenormal-fp-math=ieee" MW_HAS_DENORMAL_FP_MATH)
  if(MW_HAS_DENORMAL_FP_MATH)
    target_compile_options(mw_fp_discipline INTERFACE -fdenormal-fp-math=ieee)
  endif()
endif()
