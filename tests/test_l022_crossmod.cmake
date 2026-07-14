# test_l022_crossmod.cmake — L-022 half 1: cross-module inherent methods
# (JIT + AOT + memcheck).
#
# Fixture: tests/samples/l022_crossmod/{main,l022_owner,l022_methods}.lls
#   * l022_owner   declares `struct Widget` + inherent `methods Widget { base }`
#     (same module — the always-worked case).
#   * l022_methods hosts an inherent `methods Widget { doubled }` block in a
#     DIFFERENT module than the Widget declaration (the L-022 case). Pre-fix this
#     emitted "l022_methods__Widget.doubled" while dispatch required
#     "l022_owner__Widget.doubled" -> link failure.
#   * main imports both and asserts w.base()==21, w.doubled()==42 -> "L022 21 42".
cmake_minimum_required(VERSION 3.20)

set(MAIN "${SAMPLE_DIR}/l022_crossmod/main.lls")
set(_expected "L022 21 42")

# ---- 1. JIT ----
execute_process(
    COMMAND "${LS_EXE}" run "${MAIN}"
    OUTPUT_VARIABLE jit_out  ERROR_VARIABLE jit_err  RESULT_VARIABLE jit_rc
)
if(NOT jit_rc EQUAL 0)
    message(FATAL_ERROR "l022_crossmod JIT FAILED (rc=${jit_rc})\nstderr:\n${jit_err}")
endif()
if(NOT "${jit_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "l022_crossmod JIT missing '${_expected}'\nstdout:\n${jit_out}")
endif()
message(STATUS "l022_crossmod JIT: OK")

# ---- 2. AOT ----
set(aot_bin "${WORK_DIR}/l022_crossmod_aot")
if(WIN32)
    set(aot_bin "${aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${MAIN}" -o "${aot_bin}"
    RESULT_VARIABLE aot_rc  ERROR_VARIABLE aot_err
)
if(NOT aot_rc EQUAL 0)
    message(FATAL_ERROR "l022_crossmod AOT compile FAILED:\n${aot_err}")
endif()
execute_process(
    COMMAND "${aot_bin}"
    OUTPUT_VARIABLE aot_out  RESULT_VARIABLE aot_run_rc  ERROR_VARIABLE aot_run_err
)
if(NOT aot_run_rc EQUAL 0)
    message(FATAL_ERROR "l022_crossmod AOT run FAILED (rc=${aot_run_rc})\nstderr:\n${aot_run_err}")
endif()
if(NOT "${aot_out}" MATCHES "${_expected}")
    message(FATAL_ERROR "l022_crossmod AOT missing '${_expected}'\nstdout:\n${aot_out}")
endif()
message(STATUS "l022_crossmod AOT: OK")
file(REMOVE "${aot_bin}")

# ---- 3. memcheck (0 leak / 0 double-free / 0 invalid free) ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${MAIN}"
    OUTPUT_VARIABLE mc_out  ERROR_VARIABLE mc_err  RESULT_VARIABLE mc_rc
)
if(NOT mc_rc EQUAL 0)
    message(FATAL_ERROR "l022_crossmod memcheck run FAILED (rc=${mc_rc})\nstderr:\n${mc_err}")
endif()
if(NOT "${mc_err}" MATCHES "OK clean")
    message(FATAL_ERROR "l022_crossmod --memcheck FAILED (leaks/dfree detected)\nstderr:\n${mc_err}")
endif()
message(STATUS "l022_crossmod memcheck: OK clean")

message(STATUS "test_l022_crossmod: ALL PASSED")
