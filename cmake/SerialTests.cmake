# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Matt Woolly
#
# cmake/SerialTests.cmake — mark the memory-pressure- and timing-sensitive tests
# RUN_SERIAL (task 184).
#
# WHY: the CI testPresets now run ctest with parallelism (CMakePresets.json
# `execution.jobs`) so the long audio-rendering suite finishes on the slow shared
# Linux runner. A small subset of tests is NOT safe to run CONCURRENTLY with the
# rest, even though every test is single-threaded and correct on its own:
#
#   * Alloc/lock-guard tests (the AudioThreadGuard sentinel, [rt]/no-alloc cases):
#     they arm a thread-local no-malloc sentinel and assert ZERO heap allocations
#     in an audio-rate window. Under parallel memory pressure a first-touch page
#     fault / lazy page commit inside the armed window can take a heap path and
#     trip the guard — a false RT-discipline failure caused only by co-scheduled
#     load, not by the code under test.
#   * CPU-budget wall-time tests (the §13.5 / ADR-013 C21 budget golden): they
#     measure a per-block wall-time MEDIAN against a committed ceiling. Concurrent
#     CPU contention inflates the median past the ceiling — a false RT-budget
#     regression.
#   * Cross-run determinism / steady-state tests called out as flaky: they compare
#     two in-process runs (or a sustained steady-state loop) and must not race
#     themselves under co-scheduled load.
#
# RUN_SERIAL is a ctest EXECUTION property only — it changes WHEN a test runs (alone,
# never concurrent), not WHAT it asserts. No production code, no test source, and no
# label/tag is touched; the ctest-labels.snapshot is unaffected (RUN_SERIAL is a
# property, not a label). [docs/design/11 §13.1/§13.5; ADR-013 C19/C21; ADR-001 C3/C4]
#
# HOW: catch_discover_tests() registers tests at BUILD time, so their names do not
# exist at configure time. CMake's documented mechanism for fine-grained per-test
# properties on discovered tests is the directory TEST_INCLUDE_FILES property + the
# `<target>_TESTS` variable Catch2 populates. tests/CMakeLists.txt appends this file
# to TEST_INCLUDE_FILES; it runs at ctest time (after the generated discovery file
# has populated mw101_tests_TESTS / mw101_plugin_tests_TESTS) and applies RUN_SERIAL
# to the curated set below via set_tests_properties — exactly the named mechanism.

# --- The curated RUN_SERIAL set (task 184). Names are the Catch2 TEST_CASE names
# WITHOUT the discovery prefix; both the mw101. (core) and mw101_plugin. (JUCE-linked,
# built only when MW_BUILD_PLUGIN=ON) targets are scanned, so a name resolves under
# whichever target actually registered it. Derived from: every TEST_CASE that arms the
# AudioThreadGuard (alloc/lock sentinel), the cpu-budget wall-time golden, and the
# cross-run determinism / steady-state cases identified as flaky under -j. ----------
set(MW_SERIAL_TEST_NAMES
  [==[arp: advanceOnEdge does no heap allocation under the sentinel]==]
  [==[calibration: selectConstantSet allocates nothing and locks nothing]==]
  [==[capability_matrix: publishing the resolved rungs allocates and locks nowhere]==]
  [==[capability_matrix: the Free-run to transport transition allocates and locks nowhere]==]
  [==[cc_ingress: the controller ingress and dispatch is allocation and lock free under the guard]==]
  [==[clock: renderEdges writes only into the pre-sized span and does no heap alloc under the sentinel]==]
  [==[controlcore VINTAGE jitter-off tick stream is deterministic across runs]==]
  [==[controlcore advance is allocation-free under the audio-thread guard]==]
  [==[dispatch_character: dispatch and render are allocation and lock free under the guard]==]
  [==[dispatch_complete: the full dispatch and render path is allocation and lock free under the guard]==]
  [==[dispatch_fx: FX decode and render are allocation and lock free under the guard]==]
  [==[dispatch_gap: the pwm_depth and vcf lfo_mod path is allocation and lock free under the guard]==]
  [==[dispatch_lfo: the LFO and modulation dispatch is allocation and lock free under the guard]==]
  [==[dispatch_seqarp: the seq/arp dispatch path performs zero allocations and zero locks]==]
  [==[dispatch_vcf: VCF Env VCA dispatch is allocation and lock free under the guard]==]
  [==[dispatch_vco: dispatch and render are allocation and lock free under the guard]==]
  [==[e2e_smoke: streamed render over varied block sizes stays clean under the guard]==]
  [==[engine_assembly: process performs no heap allocation under the audio guard]==]
  [==[engine_reset: Engine and ControlCore reset perform zero allocations and zero locks]==]
  [==[engine_rtsafe: a max-voices process performs zero heap allocations and zero locks]==]
  [==[engine_rtsafe: allocation happens in prepare and never on the process or reset hot path]==]
  [==[engine_rtsafe: the FX-on post-voice chain process performs zero allocations and zero locks]==]
  [==[engine_s7: setGateTrigMode plus process is alloc-free and lock-free under the guard]==]
  [==[engine_seq: a sequencer-driven process performs zero allocations and zero locks]==]
  [==[env_trig: trigger entry points allocate and lock nothing under the sentinel]==]
  [==[envlfovca_rtsafe: a full attack-to-idle envelope lifecycle under the guard allocates nothing]==]
  [==[envlfovca_rtsafe: a representative env/lfo/vca/modrouting process pass allocates and locks nothing]==]
  [==[envlfovca_rtsafe: the LFO value and cycle edge are reproducible tick-for-tick across runs]==]
  [==[evttranslate is noexcept and allocation-free by construction]==]
  [==[fracdelay: after prepare(), write/read/processBlock perform no heap allocation]==]
  [==[fxchain: prepare/reset/process/setParams/getLatencySamples perform no heap allocation]==]
  [==[fxchorus: prepare/reset/process/setParams perform no heap allocation]==]
  [==[fxdelay: prepare reset setParams process perform no heap allocation and no locks]==]
  [==[fxdrive: prepare/reset/process/setParams perform no heap allocation]==]
  [==[fxos: prepare is the allocator (positive control trips the guard)]==]
  [==[fxos: process and latency query allocate no heap and take no locks]==]
  [==[golden: cpu budget a render that exceeds the ceiling is caught as a regression]==]
  [==[golden: cpu budget ceiling engine and oversample factor are read from MANIFEST]==]
  [==[golden: cpu budget measureWorstCaseBlockMicros returns a stable median of N runs]==]
  [==[golden: cpu budget worst-case median per-block micros stays under the committed ceiling]==]
  [==[gui_datapaths: the telemetry-publishing processBlock is steady-state stable]==]
  [==[hostevent: push beyond capacity drops, returns false, never allocates]==]
  [==[lifecycle_fuzz: random valid blocks and events never allocate on the hot path]==]
  [==[lifecycle_fuzz: reset is alloc-free and re-init returns a clean known start]==]
  [==[minblep: scheduleStep and next() perform no heap allocation]==]
  [==[modrouter: resolve performs no heap allocation under the alloc sentinel]==]
  [==[noise: renderSample is noexcept and allocates/locks nothing (RT guard)]==]
  [==[note_ingress: the corrected note ingress and render are allocation and lock free under the guard]==]
  [==[os-fir: prepare is the allocator (positive control trips the guard)]==]
  [==[os-fir: up/down kernels and reset allocate no heap (RT discipline)]==]
  [==[os-iir: prepare is the allocator (positive control trips the guard)]==]
  [==[os-iir: upsample/downsample and factor change allocate no heap (RT)]==]
  [==[os-zone: factor change and wrapped process allocate no heap on the audio thread]==]
  [==[os-zone: prepare is the allocator (positive control trips the guard)]==]
  [==[oscsection: renderSample performs no heap allocation and takes no locks]==]
  [==[pdc_invariant: FX process and reset are alloc-free and never recompute latency]==]
  [==[polyalloc: allocate and release are alloc-free and lock-free]==]
  [==[polyalloc: the steal scan is deterministic across identical runs]==]
  [==[polywire: POLY note handling and render are alloc-free and lock-free]==]
  [==[polywire: POLY steal is deterministic across identical runs]==]
  [==[processor processBlock is allocation-free and lock-free over a steady-state block]==]
  [==[processor_wire processBlock is allocation-free over a ProgramChange-bearing block]==]
  [==[renderversion_e2e: frozen constant-set selection is a prepare-time bind, not audio-rate]==]
  [==[renderversion_e2e: the engine does no audio-rate selection and never allocates in process]==]
  [==[rt: a clean armed scope reports no violation (negative control)]==]
  [==[rt: an allocation inside an armed scope trips a violation (positive)]==]
  [==[rt: the one-time warm-up carve-out excuses exactly one allocation]==]
  [==[seq_runstate: the run-on processBlock loop is steady-state stable]==]
  [==[seqengine: a live-snapshot swap during processBlock does no heap allocation]==]
  [==[seqengine: processBlock does no heap allocation under the RT sentinel]==]
  [==[stepseq: advanceOnEdge and record do no heap alloc under the sentinel]==]
  [==[sub: renderSample performs no heap allocation and takes no locks]==]
  [==[trigsource: resolve and observe are noexcept and allocate nothing under sentinel (sec 4.4)]==]
  [==[ui_telemetry: push() performs no heap allocation (instrumented allocator)]==]
  [==[vca_taper: process and processBlock are noexcept and allocate/lock nothing (RT guard)]==]
  [==[vca_thump: tickControl and processBlock are noexcept and allocate/lock nothing]==]
  [==[vcf-core: reset/setters/processSample/processBlock allocate nothing at audio rate]==]
  [==[vcf-reso: setResonance and the resonant processSample allocate nothing at audio rate]==]
  [==[vcf-tables: lookups allocate nothing and build is the only allocator]==]
  [==[vco: renderSample and phase and frequencyHz are noexcept and allocate or lock nothing]==]
  [==[vcoshape: the band-limited renderSample is noexcept and allocates or locks nothing]==]
  [==[velocity_ingress: the velocity ingress and dispatch is allocation and lock free under the guard]==]
  [==[vintage_model Re-roll consumption performs no allocation and no lock]==]
  [==[vintage_model allocation happens in prepare and never on the hot path]==]
  [==[vintage_model processBlock and noteOn perform zero heap allocations and zero locks]==]
  [==[voice: an Idle render performs no heap allocation]==]
  [==[voice: render performs no heap allocation]==]
  [==[voicemanager: control-tick and note-event handling are alloc-free]==]
  [==[voicemanager: render is deterministic across identical runs]==]
  [==[voicemanager: render performs no heap allocation]==]
)

# Apply RUN_SERIAL to every discovered test whose (de-prefixed) name is in the set.
# Both discovery targets are scanned; each is empty/undefined when its target was not
# built (e.g. mw101_plugin_tests only exists when MW_BUILD_PLUGIN=ON), so the loop is
# a no-op for the missing one. RUN_SERIAL is an EXECUTION property only — it never
# changes a test's assertions, its label, or the ctest-labels.snapshot.
set(_mw_serial_applied 0)
foreach(_var mw101_tests_TESTS mw101_plugin_tests_TESTS)
  foreach(_full IN LISTS ${_var})
    foreach(_prefix "mw101." "mw101_plugin.")
      string(LENGTH "${_prefix}" _plen)
      string(LENGTH "${_full}" _flen)
      if(_flen GREATER _plen)
        string(SUBSTRING "${_full}" 0 ${_plen} _head)
        if(_head STREQUAL "${_prefix}")
          string(SUBSTRING "${_full}" ${_plen} -1 _bare)
          # Use list(FIND) rather than the if(... IN_LIST ...) operator: this file is
          # included via TEST_INCLUDE_FILES at ctest time (the CTestTestfile.cmake context),
          # which does NOT inherit the project's policy stack, so CMP0057 defaults to OLD and
          # IN_LIST errors as "Unknown arguments" on CMake configs that honor that default
          # (it tripped linux-x64 CI while macOS happened to treat CMP0057 as NEW). list(FIND)
          # is policy-independent and behaves identically everywhere (task 184b).
          list(FIND MW_SERIAL_TEST_NAMES "${_bare}" _mw_serial_idx)
          if(NOT _mw_serial_idx EQUAL -1)
            set_tests_properties("${_full}" PROPERTIES RUN_SERIAL ON)
            math(EXPR _mw_serial_applied "${_mw_serial_applied} + 1")
          endif()
        endif()
      endif()
    endforeach()
  endforeach()
endforeach()

message(STATUS
  "mwAudio101: RUN_SERIAL applied to ${_mw_serial_applied} memory/timing-sensitive test(s) (task 184).")
