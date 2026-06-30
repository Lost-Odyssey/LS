# test_generic_freefn.cmake — generic free-function in generic body correctness + memcheck driver.
#
# Exercises Set(T) end to end: a sample that `import std.core.set` and uses
# Set(int) / Set(Str) (POD + has_drop elements), list literals, for-in
# iteration, and set algebra. Asserts:
#   1. JIT run            — value correctness ("GFN PASS", no "FAIL")
#   2. AOT compile + run  — value correctness on the native path
#   3. JIT --memcheck     — 0 leak / 0 double-free / 0 invalid free
#      (the probe for generic struct + Map(T, bool) field auto-drop/clone)
#
# Required cache variables (passed by add_test):
#   LS_EXE    — path to the ls binary
#   SAMPLE    — absolute path to the .ls sample
#   WORK_DIR  — build directory (for the AOT binary)
#   STDLIB    — repo root, set as LS_HOME so `import std.core.set` resolves
cmake_minimum_required(VERSION 3.20)

if(NOT LS_EXE OR NOT SAMPLE)
    message(FATAL_ERROR "test_generic_freefn.cmake requires LS_EXE and SAMPLE")
endif()
if(STDLIB)
    set(ENV{LS_HOME} "${STDLIB}")
endif()
if(NOT TEST_NAME)
    set(TEST_NAME "gfn")
endif()

# ---- 1. JIT run: correctness ----
execute_process(
    COMMAND "${LS_EXE}" run "${SAMPLE}"
    OUTPUT_VARIABLE _jit_out  ERROR_VARIABLE _jit_err  RESULT_VARIABLE _jit_rc
)
if(NOT _jit_rc EQUAL 0)
    message(FATAL_ERROR "${TEST_NAME} JIT run failed (rc=${_jit_rc})\nstderr:\n${_jit_err}")
endif()
if(_jit_out MATCHES "FAIL")
    message(FATAL_ERROR "${TEST_NAME} JIT reported a FAIL:\n${_jit_out}")
endif()
if(NOT _jit_out MATCHES "GFN PASS")
    message(FATAL_ERROR "${TEST_NAME} JIT missing 'GFN PASS':\n${_jit_out}")
endif()

# ---- 2. AOT compile + run: correctness on the native path ----
set(_aot_bin "${WORK_DIR}/${TEST_NAME}_aot")
if(WIN32)
    set(_aot_bin "${_aot_bin}.exe")
endif()
execute_process(
    COMMAND "${LS_EXE}" compile "${SAMPLE}" -o "${_aot_bin}"
    RESULT_VARIABLE _aot_rc  ERROR_VARIABLE _aot_err
)
if(NOT _aot_rc EQUAL 0)
    message(FATAL_ERROR "${TEST_NAME} AOT compile failed:\n${_aot_err}")
endif()
execute_process(
    COMMAND "${_aot_bin}"
    OUTPUT_VARIABLE _aot_out  RESULT_VARIABLE _aot_run_rc
)
file(REMOVE "${_aot_bin}")
if(NOT _aot_run_rc EQUAL 0)
    message(FATAL_ERROR "${TEST_NAME} AOT run failed (rc=${_aot_run_rc})\nstdout:\n${_aot_out}")
endif()
if(_aot_out MATCHES "FAIL")
    message(FATAL_ERROR "${TEST_NAME} AOT reported a FAIL:\n${_aot_out}")
endif()
if(NOT _aot_out MATCHES "GFN PASS")
    message(FATAL_ERROR "${TEST_NAME} AOT missing 'GFN PASS':\n${_aot_out}")
endif()

# ---- 3. JIT memcheck: memory safety ----
execute_process(
    COMMAND "${LS_EXE}" run --memcheck "${SAMPLE}"
    OUTPUT_VARIABLE _mc_out  ERROR_VARIABLE _mc_err  RESULT_VARIABLE _mc_rc
)
if(NOT _mc_rc EQUAL 0)
    message(FATAL_ERROR "${TEST_NAME} memcheck run failed (rc=${_mc_rc})\nstderr:\n${_mc_err}")
endif()
if(NOT _mc_err MATCHES "SUMMARY: 0 leak\\(s\\) \\(0 bytes\\), 0 double-free, 0 invalid free")
    message(FATAL_ERROR "${TEST_NAME} memcheck SUMMARY mismatch\nstderr:\n${_mc_err}")
endif()

message(STATUS "${TEST_NAME}: JIT + AOT correctness + memcheck PASS")
